#!/usr/bin/env python3
"""No-terminal startup and resumable worker lifecycle, with owned local fixtures.

No public IRC server or real credential is used. Every stopped job was created
by this test, and signals target our Popen resume waiter rather than a guessed
worker PID. The existing terminal test model checks full mode restoration.
"""
import argparse
import fcntl
import os
from pathlib import Path
import re
import signal
import socket
import stat
import subprocess
import tempfile
import time

import test_idle_terminal as terminal
from test_integration import ModeControls


class GuiBackgroundSession(terminal.IdleSession, ModeControls):
    """Use the shared PTY/cleanup fixture with the actual curses controls."""
    def command(self, text, expected=None):
        assert text.startswith("/")
        marker = len(self.raw)
        self.open_command(from_chat=True)
        self.type(text[1:])
        self.wait(lambda: self.command_text() == text.rstrip(),
                  "full-screen command was not visibly composed: " + text)
        self.type(b"\r")
        if text in ("/background", "/quit"):
            self.wait(lambda: self.proc.poll() is not None,
                      "full-screen command did not return to its caller: " + text)
        else:
            self.wait(lambda: not self.in_command() and self.mode() in ("INPUT", "PANEL"),
                      "full-screen command did not complete: " + text)
        if expected:
            self.wait(lambda: expected in self.screen.text() or expected.encode() in self.raw[marker:],
                      "full-screen command response missing: " + expected)


def resume_session(path, gui=False):
    session_class = GuiBackgroundSession if gui else terminal.IdleSession
    session = session_class(args=["--resume", str(path)],
                            locale="C.UTF-8" if gui else "C", rows=28, cols=120)
    try:
        session.wait(lambda: b"\x1b[?2004h" in session.raw,
                     "background startup did not create a resumable terminal")
        if gui:
            session.wait(lambda: session.screen.alternate and
                         "vipirc" in "".join(session.screen.cells[0]) and
                         session.mode() in ("INPUT", "PANEL"),
                         "full-mode background startup did not resume the curses interface")
        else:
            session.wait(lambda: not session.screen.alternate,
                         "native background startup unexpectedly resumed a full-screen interface")
        assert b"Press a key to start" not in session.raw
        return session
    except BaseException:
        session.close()
        raise


def wait_registered(peer, channel, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if peer.registered and any(line == ("JOIN " + channel).encode() for _, line in peer.lines):
            return
        assert not peer.errors, peer.errors
        peer.stop.wait(.02)
    raise AssertionError(("detached client did not register and join", channel, peer.lines, peer.errors))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path,
                        default=Path(os.environ.get("ONEKEY_BINARY", terminal.BINARY)))
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--idle", action="store_true",
                       help="test the bundled vipirc -i entry point")
    modes.add_argument("--gui", action="store_true",
                       help="test full-screen vipirc (also the default for a binary named vipirc)")
    args = parser.parse_args()
    binary = args.binary.resolve()
    full = args.idle or args.gui or binary.name == "vipirc"
    gui = args.gui or (full and not args.idle)
    brand = "vipirc" if full else "onekeyirc"
    prefix = ["-i"] if args.idle else []
    mode = "vipirc curses" if gui else "vipirc -i" if full else "onekeyirc"
    terminal.BINARY = binary
    registry = Path("/tmp").resolve() / (brand + "-jobs-%d" % os.getuid())
    cases = 0

    with tempfile.TemporaryDirectory(prefix="onekey-boot-") as temporary:
        directory = Path(temporary)
        config = directory / "account.ini"
        saved = ("[connection]\nserver=offline.invalid\nport=6667\ntls=false\n"
                 "nick=boottest\nusername=boottest\nchannel=#boot\nautoconnect=true\n"
                 "auth=none\n[history]\nlines=50\n").encode()
        config.write_bytes(saved)
        config.chmod(0o600)
        env = dict(os.environ, HOME=str(directory), XDG_CONFIG_HOME=str(directory / "none"),
                   LANG="C", LC_ALL="C")
        env.pop("TERM", None)

        def run(*options, expected=0, idle=True, timeout=7):
            result = subprocess.run([str(binary), *(prefix if idle else []), *map(str, options)],
                                    input=b"", capture_output=True, cwd=directory,
                                    env=env, timeout=timeout, start_new_session=True)
            assert result.returncode == expected, (options, result.returncode, result.stdout, result.stderr)
            assert b"Sanitizer" not in result.stderr and b"runtime error:" not in result.stderr, result
            return result

        def wait_absent(path):
            end = time.monotonic() + 5
            while path.exists() and time.monotonic() < end:
                time.sleep(.02)
            assert not path.exists(), ("worker did not remove its endpoint", path)

        def attach(path):
            return resume_session(path, gui)

        def cleanup(path):
            if path and path.exists():
                session = attach(path)
                try:
                    session.proc.send_signal(signal.SIGTERM)
                    session.wait(lambda: session.proc.poll() is not None, "cleanup signal failed")
                finally:
                    session.close()
                wait_absent(path)

        def start(*extra, simulate=True, use_config=True):
            options = ["--background"]
            if simulate:
                options.append("--test-mode")
            if use_config:
                options.extend(["--config", str(config)])
            options.extend(extra)
            result = run(*options)
            assert result.stderr.count(b"www.bloodguiltcurse.net") == 1, result
            assert b"\x1b" not in result.stdout + result.stderr
            assert b"Press a key to start" not in result.stdout + result.stderr
            match = re.search(brand.encode() + rb" job ([1-9][0-9]*) is running", result.stdout)
            assert match, result
            number = int(match[1])
            path = registry / ("job-%d" % number) / "session"
            assert path.exists() and stat.S_ISSOCK(path.lstat().st_mode)
            assert stat.S_IMODE(path.lstat().st_mode) == 0o600
            assert stat.S_IMODE(path.parent.lstat().st_mode) == 0o700
            assert path.lstat().st_uid == os.getuid()
            assert config.read_bytes() == saved
            return number, path

        # Runtime flags never turn config-only operations into daemon launches.
        for options in (("--background", "--init-config"),
                        ("--background", "--check-config"),
                        ("--reconnect", "--init-config"),
                        ("--reconnect", "--check-config"),
                        ("--background", "--sessions"),
                        ("--reconnect", "--resume", "1")):
            run(*options, expected=2)
            cases += 1
        assert b"requires an existing configuration" in run("--background", expected=2).stderr
        cases += 1
        assert b"requires a terminal" in run("--test-mode", "--config", config, expected=2).stderr
        assert b"requires a terminal" in run("--reconnect", "--config", config, expected=2).stderr
        cases += 2
        assert b"--background" in run("--background", "--help").stdout
        cases += 1
        broken = directory / "broken.ini"
        broken.write_text("[connection]\nport=invalid\n", encoding="ascii")
        broken.chmod(0o600)
        run("--background", "--config", broken, expected=2)
        cases += 1

        # A changed CLI host must not inherit private automatic commands.
        # A nonempty enabled list requires TLS. Therefore switching only the
        # host plus transport validates only if the old-host list was cleared;
        # retaining the same host must still reject the insecure transport.
        original_host = directory / "original-host.ini"
        private_settings = (saved.replace(b"tls=false", b"tls=true") +
                            b"[autocommands]\nenabled=true\ndelay_seconds=0\n"
                            b"interval_seconds=2\ncommand=/msg NickServ fixture-secret\n")
        original_host.write_bytes(private_settings)
        original_host.chmod(0o600)
        checked = run("--config", original_host, "--server", "replacement.invalid",
                      "--plain", "--check-config")
        assert b"fixture-secret" not in checked.stdout + checked.stderr
        run("--config", original_host, "--server", "offline.invalid", "--plain",
            "--check-config", expected=2)
        assert original_host.read_bytes() == private_settings
        cases += 1

        catalog = Path(str(config) + ".servers")
        catalog.write_text("[servers]\nprofile=../escape\n", encoding="ascii")
        catalog.chmod(0o600)
        assert b"saved-server catalog" in run("--background", "--test-mode", "--config", config, expected=2).stderr
        catalog.unlink()
        cases += 1

        # A fresh detached process can attach, detach again, and quit normally.
        number, path = start("--reconnect")
        try:
            listed = run("--sessions", idle=False).stdout
            assert re.search(rb"^\s+" + str(number).encode() + rb"\s+detached\s", listed, re.M), listed
            with attach(path) as session:
                if gui:
                    session.command("/channel #boot")
                    session.wait(lambda: "#boot" in session.panel_title(), "headless simulator never joined")
                    session.type("UNSENT_COLD_START_DRAFT")
                    session.wait(lambda: "UNSENT_COLD_START_DRAFT" in session.input_text(),
                                 "cold-start full-screen draft was not entered")
                else:
                    session.wait(lambda: b"joined #boot" in session.raw, "headless simulator never joined")
                session.command("/background")
                session.wait(lambda: session.proc.poll() is not None, "resume waiter did not detach")
                assert session.proc.returncode == 0
            assert path.exists()
            with attach(path) as session:
                if gui:
                    session.input_mode()
                    session.wait(lambda: "UNSENT_COLD_START_DRAFT" in session.input_text(),
                                 "full-screen detach/reattach lost the draft")
                session.command("/quit")
                session.wait(lambda: session.proc.poll() is not None, "quit did not finish background job")
                assert session.proc.returncode == 0
            wait_absent(path)
            assert config.read_bytes() == saved
            cases += 1
        finally:
            cleanup(path)

        # Signals travel through this test's resume child to the owned worker.
        for signum in (signal.SIGTERM, signal.SIGINT):
            number, path = start()
            try:
                with attach(path) as session:
                    session.proc.send_signal(signum)
                    session.wait(lambda: session.proc.poll() is not None, "stop signal was not forwarded")
                    assert session.proc.returncode == 128 + signum, session.proc.returncode
                wait_absent(path)
                cases += 1
            finally:
                cleanup(path)

        if gui:
            for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
                with GuiBackgroundSession(args=["--test-mode", "--config", str(config)],
                                          locale="C.UTF-8", rows=28, cols=120) as session:
                    session.wait(lambda: b"Not vegetarian?" in session.raw,
                                 "foreground full-screen welcome was missing")
                    session.type("x")
                    session.wait(session.main_frame_ready, "foreground curses frame did not start")
                    session.proc.send_signal(signum)
                    session.wait(lambda: session.proc.poll() is not None,
                                 "foreground full-screen signal did not stop the client")
                    assert session.proc.returncode == 128 + signum, session.proc.returncode
                assert config.read_bytes() == saved
                cases += 1

        # A disconnected SSH-side waiter must not take its worker with it.
        number, path = start()
        try:
            with attach(path) as session:
                session.proc.send_signal(signal.SIGHUP)
                session.wait(lambda: session.proc.poll() is not None, "HUP did not detach waiter")
                assert session.proc.returncode == 0
            assert path.exists()
            with attach(path) as session:
                session.command("/quit")
                session.wait(lambda: session.proc.poll() is not None, "reattached HUP job did not quit")
                assert session.proc.returncode == 0
            wait_absent(path)
            cases += 1
        finally:
            cleanup(path)

        # No-connect takes precedence over both startup and retry flags. The
        # listener gives an observable failure if a connection is attempted.
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(.3)
            local_saved = saved
            saved = saved.replace(b"offline.invalid", b"127.0.0.1").replace(
                b"port=6667", ("port=%d" % listener.getsockname()[1]).encode())
            config.write_bytes(saved)
            number, path = start("--no-connect", "--reconnect", simulate=False)
            try:
                try:
                    peer, address = listener.accept()
                except socket.timeout:
                    pass
                else:
                    peer.close()
                    raise AssertionError("--no-connect opened an IRC connection")
                with attach(path) as session:
                    session.command("/quit")
                    session.wait(lambda: session.proc.poll() is not None, "no-connect worker did not quit")
                    assert session.proc.returncode == 0
                wait_absent(path)
                cases += 1
            finally:
                cleanup(path)
                saved = local_saved
                config.write_bytes(saved)

        # Nickname defaults and explicit overrides must work before the first
        # terminal is attached, with no interactive question or implicit save.
        local_saved = saved
        for requested in (None, "ColdOverrideNick"):
            peer = terminal.IRCFixture()
            path = None
            try:
                saved = local_saved.replace(b"offline.invalid", b"127.0.0.1").replace(
                    b"port=6667", ("port=%d" % peer.port).encode()).replace(b"nick=boottest", b"nick=")
                config.write_bytes(saved)
                options = [] if requested is None else ["--nick", requested]
                number, path = start(*options, simulate=False)
                wait_registered(peer, "#boot")
                assert peer.nick == (requested or terminal.login_nick()), peer.nick
                with attach(path) as session:
                    assert b"Set user nick" not in session.raw
                    session.command("/quit")
                    session.wait(lambda: session.proc.poll() is not None, "nickname-default worker did not quit")
                    assert session.proc.returncode == 0
                wait_absent(path)
                assert config.read_bytes() == saved
                cases += 1
            finally:
                cleanup(path)
                peer.close()
                saved = local_saved
                config.write_bytes(saved)

        # Non-host runtime overrides apply to the saved selected server. The
        # unused old ports make accidental primary/old-profile connections
        # observable without contacting any external address.
        local_saved = saved
        peer = terminal.IRCFixture()
        profile_dir = Path(str(config) + ".d")
        profile_dir.mkdir(mode=0o700)
        profile = profile_dir / "secondary.ini"
        path = None
        with socket.socket() as unused_primary, socket.socket() as unused_secondary:
            unused_primary.bind(("127.0.0.1", 0))
            unused_secondary.bind(("127.0.0.1", 0))
            try:
                saved = local_saved.replace(b"offline.invalid", b"127.0.0.1").replace(
                    b"port=6667", ("port=%d" % unused_primary.getsockname()[1]).encode()).replace(
                    b"autoconnect=true", b"autoconnect=false")
                config.write_bytes(saved)
                profile_saved = local_saved.replace(b"offline.invalid", b"127.0.0.1").replace(
                    b"port=6667", ("port=%d" % unused_secondary.getsockname()[1]).encode()).replace(
                    b"nick=boottest", b"nick=SavedSecondary").replace(b"channel=#boot", b"channel=#old-secondary")
                profile.write_bytes(profile_saved)
                profile.chmod(0o600)
                catalog_saved = b"[servers]\nprofile=secondary.ini\ndefault=secondary.ini\n"
                catalog.write_bytes(catalog_saved)
                catalog.chmod(0o600)
                number, path = start("--nick", "RuntimeSecondary", "--port", str(peer.port),
                                     "--channel", "#runtime-secondary", simulate=False)
                wait_registered(peer, "#runtime-secondary")
                assert peer.nick == "RuntimeSecondary", peer.nick
                assert not any(line == b"JOIN #old-secondary" for _, line in peer.lines)
                with attach(path) as session:
                    session.command("/join #selected-context")
                    session.wait(lambda: any(line == b"JOIN #selected-context" for _, line in peer.lines),
                                 "runtime overrides connected secondary but left commands on primary")
                    session.command("/quit")
                    session.wait(lambda: session.proc.poll() is not None, "secondary worker did not quit")
                    assert session.proc.returncode == 0
                wait_absent(path)
                assert config.read_bytes() == saved and profile.read_bytes() == profile_saved
                assert catalog.read_bytes() == catalog_saved
                cases += 1

                profile.write_bytes(profile_saved.replace(b"port=", b"port=invalid"))
                bad_profile = run("--background", "--config", config, expected=2)
                assert b"is running" not in bad_profile.stdout
                cases += 1
            finally:
                cleanup(path)
                peer.close()
                catalog.unlink(missing_ok=True)
                profile.unlink(missing_ok=True)
                saved = local_saved
                config.write_bytes(saved)

        # A busy private job registry yields a bounded foreground error before
        # any child can start networking; it must never report success.
        with (registry / "lock").open("r+b") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            failed = run("--background", "--test-mode", "--config", config, expected=1, timeout=3)
            assert b"registry is unavailable or busy" in failed.stderr
            assert b"is running" not in failed.stdout
            fcntl.flock(lock, fcntl.LOCK_UN)
        cases += 1

    print("background startup: %d checks passed for %s (%s)" % (cases, mode, binary), flush=True)


if __name__ == "__main__":
    main()
