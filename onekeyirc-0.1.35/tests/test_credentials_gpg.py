#!/usr/bin/env python3
"""Real local GnuPG integration; fixture plaintext/passphrases stay in pipes.

Uses a temporary private GNUPGHOME and kills only its own agent. No IRC server,
network lookup, user keyring, or user's configuration is accessed.
"""
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
PROBE = Path(os.environ.get("VIPIRC_CREDENTIAL_TEST_BINARY", ROOT / "build/test_credentials")).resolve()
GPG = shutil.which("gpg")
GPGCONF = shutil.which("gpgconf")


def run(argv, env, data=b"", timeout=30):
    return subprocess.run(argv, input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          env=env, timeout=timeout, check=False)


def require(result, operation):
    if result.returncode:
        raise AssertionError(f"{operation} failed (status {result.returncode}); fixture output suppressed")
    return result.stdout


def main():
    if not GPG or not GPGCONF:
        print(json.dumps({"status": "skipped", "reason": "gpg and gpgconf are required for real GPG fixtures"}))
        return
    assert PROBE.is_file(), "Build test_credentials before running this test"
    version = require(run([GPG, "--version"], os.environ.copy()), "GPG version").splitlines()[0].decode()
    options = require(run([GPG, "--dump-options"], os.environ.copy()), "GPG options").splitlines()
    cases = []
    durations = []
    with tempfile.TemporaryDirectory(prefix="vipirc-real-gpg-") as temporary:
        home = Path(temporary) / "gnupg"
        home.mkdir(mode=0o700)
        env = dict(os.environ, GNUPGHOME=str(home))
        key_passphrase = os.urandom(24).hex().encode()
        plaintext = b"  vipirc fixture #;= " + os.urandom(16).hex().encode() + b"  "

        def probe(path, expected, success, name):
            start = time.monotonic()
            result = run([str(PROBE), "--probe", str(path)], env, expected, timeout=8)
            durations.append(time.monotonic() - start)
            if (result.returncode == 0) != success:
                # The production helper deliberately emits no raw GPG output.
                raise AssertionError(f"{name}: probe status {result.returncode}: " + result.stderr.decode(errors="replace"))
            assert plaintext not in result.stdout + result.stderr
            assert key_passphrase not in result.stdout + result.stderr
            cases.append(name)

        def encrypt(name, data, extra=()):
            path = Path(temporary) / name
            require(run([GPG, "--batch", "--yes", "--trust-model", "always", "--compress-algo", "none",
                         "--cipher-algo", "AES256", *extra, "--recipient", fingerprint,
                         "--output", str(path), "--encrypt"], env, data), "fixture encryption")
            path.chmod(0o600)
            return path

        try:
            generated = require(run([GPG, "--batch", "--pinentry-mode", "loopback", "--passphrase-fd", "0",
                                     "--status-fd", "1", "--quick-generate-key", "vipirc credential fixture",
                                     "rsa2048", "encr", "0"], env, key_passphrase + b"\n"), "fixture key generation")
            created = [line.split()[-1] for line in generated.splitlines() if line.startswith(b"[GNUPG:] KEY_CREATED ")]
            assert len(created) == 1
            fingerprint = created[0].decode("ascii")
            normal = encrypt("normal.gpg", plaintext + b"\r\n")

            # Exercise the production asynchronous writer, with an explicit
            # local fingerprint and the plaintext only on its stdin pipe.
            panel_created = Path(temporary) / "panel-created.gpg"
            creation = run([str(PROBE), "--create", str(panel_created), fingerprint], env, plaintext, timeout=8)
            require(creation, "panel credential encryption")
            assert plaintext not in creation.stdout + creation.stderr + panel_created.read_bytes()
            assert panel_created.stat().st_mode & 0o777 == 0o600
            saved_ciphertext = panel_created.read_bytes()
            repeated = run([str(PROBE), "--create", str(panel_created), fingerprint], env, b"must-not-replace", timeout=8)
            assert repeated.returncode and panel_created.read_bytes() == saved_ciphertext
            cases.extend(["panel-local-key-encryption", "panel-private-mode", "panel-no-clobber"])

            # The public launch directory reproduces the original late setup
            # failure. Read-only preflight now explains it before any secret;
            # the managed offer creates a separate private directory instead.
            public = Path(temporary) / "public"
            public.mkdir(mode=0o755)
            public.chmod(0o755)
            unsafe = public / "unsafe.gpg"
            checked = run([str(PROBE), "--check", str(unsafe)], env)
            assert checked.returncode and not unsafe.exists()
            assert str(public).encode() in checked.stderr and b"0755" in checked.stderr and b"0700" in checked.stderr
            failed = run([str(PROBE), "--create", str(unsafe), fingerprint], env, plaintext, timeout=8)
            assert failed.returncode and not unsafe.exists()
            assert plaintext not in failed.stdout + failed.stderr
            cases.append("unsafe-parent-preflight-no-publication")
            config = public / "app" / "config.ini"
            managed = config.parent / "credentials"
            offered = managed / "account-password.gpg"
            created_default = run([str(PROBE), "--create-default", str(config), fingerprint], env, plaintext, timeout=8)
            require(created_default, "managed default encryption")
            assert str(offered).encode() in created_default.stdout
            assert public.stat().st_mode & 0o777 == 0o755
            assert config.parent.stat().st_mode & 0o777 == 0o700
            assert managed.stat().st_mode & 0o777 == 0o700
            assert offered.stat().st_mode & 0o777 == 0o600
            assert not config.exists()
            original_default = offered.read_bytes()
            assert plaintext not in original_default + created_default.stdout + created_default.stderr
            second = managed / "account-password-2.gpg"
            repeated_default = run([str(PROBE), "--create-default", str(config), fingerprint], env, plaintext, timeout=8)
            require(repeated_default, "second managed default encryption")
            assert offered.read_bytes() == original_default and second.exists()
            assert str(second).encode() in repeated_default.stdout
            cases.extend(["managed-default-private-directory", "managed-default-explicit-path", "managed-default-collision-keeps-first"])
            missing = Path(temporary) / "missing-key.gpg"
            absent = "0" * 40
            assert absent != fingerprint
            rejected = run([str(PROBE), "--create", str(missing), absent], env, plaintext, timeout=8)
            assert rejected.returncode and not missing.exists()
            assert plaintext not in rejected.stdout + rejected.stderr
            cases.append("panel-missing-local-key-no-publication")

            require(run([GPGCONF, "--kill", "gpg-agent"], env), "clear fixture agent")
            require(run([GPGCONF, "--launch", "gpg-agent"], env), "launch fixture agent")
            probe(normal, plaintext, False, "locked-agent-fails-without-pinentry")
            require(run([GPG, "--batch", "--pinentry-mode", "loopback", "--passphrase-fd", "0",
                         "--output", "/dev/null", "--decrypt", str(normal)], env, key_passphrase + b"\n"), "unlock fixture key")
            probe(normal, plaintext, True, "cached-private-key-decrypts")
            probe(panel_created, plaintext, True, "panel-created-file-decrypts")
            probe(offered, plaintext, True, "managed-default-file-decrypts")
            probe(second, plaintext, True, "managed-second-file-decrypts")
            armored = encrypt("armored.asc", plaintext, ("--armor",))
            probe(armored, plaintext, True, "ASCII-armored-encryption")

            damaged = Path(temporary) / "damaged.gpg"
            data = bytearray(normal.read_bytes()); data[-12] ^= 0x40
            damaged.write_bytes(data); damaged.chmod(0o600)
            probe(damaged, plaintext, False, "modified-ciphertext-rejected")
            too_long = encrypt("oversize.gpg", b"p" * 1027)
            probe(too_long, plaintext, False, "decrypted-size-limit")
            multiple = encrypt("multiline.gpg", b"one\ntwo\n")
            probe(multiple, plaintext, False, "decrypted-extra-line-rejected")
            stored = Path(temporary) / "unencrypted.pgp"
            require(run([GPG, "--batch", "--output", str(stored), "--store"], env, plaintext), "literal packet")
            stored.chmod(0o600)
            probe(stored, plaintext, False, "unencrypted-literal-packet-rejected")
            legacy_options = ("--rfc2440", "--disable-mdc", "--cipher-algo", "CAST5")
            if b"--allow-old-cipher-algos" in options:
                legacy_options += ("--allow-old-cipher-algos",)
            legacy = encrypt("legacy-no-mdc.gpg", plaintext, legacy_options)
            probe(legacy, plaintext, False, "legacy-without-integrity-rejected")

            # A separate passphrase pipe leaves stdin available for plaintext.
            symmetric = Path(temporary) / "symmetric.gpg"
            read_fd, write_fd = os.pipe()
            try:
                os.write(write_fd, key_passphrase + b"\n"); os.close(write_fd); write_fd = -1
                result = subprocess.run([GPG, "--batch", "--pinentry-mode", "loopback", "--passphrase-fd", str(read_fd),
                                         "--cipher-algo", "AES256", "--output", str(symmetric), "--symmetric"],
                                        input=plaintext, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        env=env, timeout=30, pass_fds=(read_fd,), check=False)
                require(result, "symmetric fixture encryption")
            finally:
                os.close(read_fd)
                if write_fd >= 0: os.close(write_fd)
            symmetric.chmod(0o600)
            # Symmetric loopback decryption does not fill the normal agent
            # cache. Supply a normal external Pinentry conversation instead;
            # its passphrase travels only through a private local socket.
            # The fixture pinentry program/config contain no secret bytes.
            pinentry = Path(temporary) / "fixture-pinentry"
            address = str(Path(temporary) / "pinentry.sock")
            pinentry.write_text("#!/usr/bin/env python3\n"
                                "import socket,sys\n"
                                "print('OK fixture pinentry',flush=True)\n"
                                "for line in sys.stdin:\n"
                                " command=line.split(' ',1)[0].strip()\n"
                                " if command=='GETPIN':\n"
                                "  with socket.socket(socket.AF_UNIX) as peer:\n"
                                f"   peer.connect({address!r})\n"
                                "   value=peer.recv(128).decode('ascii')\n"
                                "  print('D '+value,flush=True)\n"
                                " print('OK',flush=True)\n"
                                " if command=='BYE': break\n")
            pinentry.chmod(0o700)
            (home / "gpg-agent.conf").write_text(f"pinentry-program {pinentry}\n")
            require(run([GPGCONF, "--kill", "gpg-agent"], env), "restart fixture agent")
            requests = []
            finished = threading.Event()
            listener = socket.socket(socket.AF_UNIX)
            listener.bind(address); os.chmod(address, 0o600); listener.listen(1); listener.settimeout(0.1)

            def supply_fixture_pin():
                while not finished.is_set():
                    try:
                        peer, _ = listener.accept()
                    except socket.timeout:
                        continue
                    with peer:
                        peer.sendall(key_passphrase)
                    requests.append(1)

            thread = threading.Thread(target=supply_fixture_pin, daemon=True)
            thread.start()
            try:
                require(run([GPG, "--no-tty", "--pinentry-mode", "ask", "--output", "/dev/null",
                             "--decrypt", str(symmetric)], env), "unlock symmetric fixture via pinentry")
            finally:
                finished.set(); thread.join(1); listener.close()
            assert requests, "The symmetric fixture must populate its ordinary agent cache"
            probe(symmetric, plaintext, True, "cached-symmetric-passphrase-decrypts")
        finally:
            run([GPGCONF, "--kill", "all"], env)
    print(json.dumps({"status": "passed", "gpg": version, "cases": cases,
                      "maximum_probe_seconds": round(max(durations), 3)}, sort_keys=True))


if __name__ == "__main__":
    main()
