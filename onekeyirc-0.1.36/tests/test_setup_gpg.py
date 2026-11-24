#!/usr/bin/env python3
"""Real guided GPG creation, saved credentials and verified-TLS SASL/autojoin.

Every key, passphrase, configuration, certificate and IRC peer is disposable.
Passwords travel only through the hidden editor and pipes; no public network
or existing user keyring is used. The private key is unlocked externally.
"""
import argparse
import base64
import configparser
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
from unittest.mock import patch

from test_idle_terminal import IdleSession
from test_integration import IRCFixture
from test_setup_async import Frontend, SECRET
from test_setup_paths import find_on_pages


def cert_pair(directory):
    # The standalone archive intentionally omits the full transport-test
    # driver, so keep this small owned certificate fixture self-contained.
    certificate, key = directory / "localhost.pem", directory / "localhost.key"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                    "-keyout", str(key), "-out", str(certificate), "-days", "1",
                    "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return certificate, key


class AuthenticatedPeer(IRCFixture):
    def __init__(self, certificate, key, expected):
        self.expected = expected
        self.authenticated = False
        self.joins = []
        super().__init__(certificate, key, "success")

    def handle(self, line):
        if line.startswith(b"AUTHENTICATE ") and line != b"AUTHENTICATE PLAIN":
            part = line.split(b" ", 1)[1]
            if len(part) < 400 or part == b"+":
                encoded = bytes(self.sasl_data) + (b"" if part == b"+" else part)
                assert base64.b64decode(encoded, validate=True) == self.expected, "saved GPG credential did not match the fixture account"
                self.authenticated = True
        if line.startswith(b"JOIN "):
            assert self.authenticated and self.registered, "channel join preceded successful SASL registration"
            self.joins.extend(line.split(b" ")[1].split(b","))
        super().handle(line)


def run(argv, environment, data=b"", timeout=30):
    result = subprocess.run(argv, input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            env=environment, timeout=timeout, check=False)
    if result.returncode:
        raise AssertionError("owned GPG operation failed (status %d); fixture output suppressed" % result.returncode)
    return result.stdout


def command(session, mode, text):
    if mode == "gui":
        session.command(text, from_chat=True)
    else:
        session.command(text)


def panel_to_join(mode, environment, fingerprint, passphrase, gpg, gpgconf, certificate, key):
    # Encryption uses the public key while the private-key cache is empty.
    run([gpgconf, "--kill", "gpg-agent"], environment)
    run([gpgconf, "--launch", "gpg-agent"], environment)
    peer = AuthenticatedPeer(certificate, key, b"\0fixture-account\0" + SECRET.encode("utf-8"))
    try:
        with patch.dict(os.environ, environment):
            if mode == "gui":
                from test_servers import MultiServerSession
                session = MultiServerSession(cols=132)
            else:
                session = IdleSession(live=True, review=False, full=mode == "idle", cols=120)
        with session as s:
            before = s.config.read_bytes()
            ui = Frontend(s, mode)
            ui.begin()
            raw_start = len(s.raw)
            ui.enter("Choose a panel", "1")
            destination = s.directory / "credentials" / "account-password.gpg"
            values = {
                "Use TLS": "yes",
                "Port": str(peer.port),
                "Auto-join channels": "#nastycode,#vipirc",
                "Auto-authentication": "sasl",
                "Authentication account": "fixture-account",
                "Authentication password source": "gpg",
                "Authentication GPG file": "create",
                "Authentication GPG key fingerprint": fingerprint,
                "Authentication password to encrypt": SECRET,
                "Confirm authentication password": SECRET,
                "CA certificate file": str(certificate),
            }
            for _ in range(48):
                question = ui.question()
                if question == "Finish":
                    break
                if question == "Authentication password file":
                    find_on_pages(s, str(destination))
                    assert not destination.parent.exists()
                ui.enter(question, values.get(question, ""))
                assert not destination.exists(), "ciphertext was created before explicit Finish"
                assert s.config.read_bytes() == before, "setup saved before explicit Finish"
            else:
                raise AssertionError("real-GPG setup did not reach Finish")
            find_on_pages(s, str(destination))
            ui.ping("real-gpg-before-save")
            ui.wait("Finish")
            s.type("2")  # Save only; the original live connection keeps running.
            s.quiet(.30 if ui.native else .12)
            s.type(b"\r")
            s.wait(lambda: not ui.active(), "real-GPG Save did not finish", timeout=8)
            ui.ping("real-gpg-after-save")
            ciphertext = destination.read_bytes()
            assert ciphertext and SECRET.encode("utf-8") not in ciphertext
            assert stat.S_IMODE(destination.stat().st_mode) == 0o600
            assert stat.S_IMODE(destination.parent.stat().st_mode) == 0o700
            assert stat.S_IMODE(s.config.stat().st_mode) == 0o600
            saved_bytes = s.config.read_bytes()
            if ui.native:
                s.before_config = saved_bytes  # This explicit Save is the one expected disk change.
            assert SECRET.encode("utf-8") not in saved_bytes and passphrase not in saved_bytes
            saved = configparser.RawConfigParser()
            saved.read_string(saved_bytes.decode("utf-8"))
            connection = saved["connection"]
            assert connection["password_gpg_file"] == str(destination)
            assert connection["password"] == connection["password_file"] == ""
            assert connection["tls"] == "true" and connection["auth"] == "sasl"
            assert connection["auth_user"] == "fixture-account"
            assert connection["channel"] == "#nastycode,#vipirc"
            assert connection["ca_file"] == str(certificate)
            assert int(connection["port"]) == peer.port
            assert not peer.lines, "Save only connected before the explicit connection command"

            # A protected key needs external unlocking. Neither the client nor
            # its GPG helper is permitted to open a passphrase prompt.
            run([gpg, "--batch", "--pinentry-mode", "loopback", "--passphrase-fd", "0",
                 "--output", "/dev/null", "--decrypt", str(destination)], environment, passphrase + b"\n")
            command(s, mode, "/disconnect")
            command(s, mode, "/connect")
            s.wait(lambda: peer.authenticated and peer.registered and
                   peer.joins == [b"#nastycode", b"#vipirc"],
                   "saved GPG credential did not authenticate and join both configured channels", timeout=8)
            peer.send("PING :real-gpg-authenticated")
            s.wait(lambda: any(line == b"PONG :real-gpg-authenticated" for _, line in peer.lines),
                   "authenticated connection stopped serving PING")
            assert not peer.errors
            assert not any(line.startswith(b"PRIVMSG ") for _, line in peer.lines)
            assert SECRET.encode("utf-8") not in s.raw[raw_start:] and passphrase not in s.raw[raw_start:]
            assert bytes(peer.sasl_data) not in s.raw[raw_start:], "SASL payload leaked to terminal output"
            assert destination.read_bytes() == ciphertext and s.config.read_bytes() == saved_bytes
    finally:
        peer.close()
    print("real GPG setup: %s passed; default absolute path, Save, private ciphertext, external key unlock, verified-TLS SASL and JOIN #nastycode + #vipirc" % mode,
          flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("one", "idle", "gui", "all"), default="all")
    args = parser.parse_args()
    gpg, gpgconf = shutil.which("gpg"), shutil.which("gpgconf")
    if not gpg or not gpgconf:
        raise SystemExit("real GPG setup test requires gpg and gpgconf")
    with tempfile.TemporaryDirectory(prefix="irc-real-setup-") as temporary:
        directory = Path(temporary)
        keyring = directory / "keyring"
        keyring.mkdir(mode=0o700)
        environment = dict(os.environ, GNUPGHOME=str(keyring))
        passphrase = os.urandom(24).hex().encode("ascii")
        try:
            generated = run([gpg, "--batch", "--pinentry-mode", "loopback", "--passphrase-fd", "0",
                             "--status-fd", "1", "--quick-generate-key", "IRC guided setup fixture",
                             "rsa2048", "encr", "0"], environment, passphrase + b"\n")
            created = [line.split()[-1] for line in generated.splitlines() if line.startswith(b"[GNUPG:] KEY_CREATED ")]
            assert len(created) == 1
            fingerprint = created[0].decode("ascii")
            certificate, key = cert_pair(directory)
            for mode in (("one", "idle", "gui") if args.mode == "all" else (args.mode,)):
                panel_to_join(mode, environment, fingerprint, passphrase, gpg, gpgconf, certificate, key)
        finally:
            subprocess.run([gpgconf, "--kill", "all"], env=environment, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=5, check=False)


if __name__ == "__main__":
    main()
