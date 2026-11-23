#!/usr/bin/env python3
"""Cold detached reconnect, rejoin, and cancellation using owned loopback only.

Run separately from test_sessions.py, which requires an empty job registry.
Only the exact private endpoint created here is resumed or stopped. No public
IRC service, real account, credentials, or guessed worker PID is used.
"""
import argparse
import os
from pathlib import Path
import re
import signal
import socket
import stat
import subprocess
import tempfile
import threading
import time

import test_idle_terminal as terminal
from test_background_start import resume_session


class ReturningIRC(terminal.IRCFixture):
    """Reuse the IRC protocol fixture, but permit delayed listen and reconnect."""
    def __init__(self):
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.settimeout(.05)
        self.port = self.listener.getsockname()[1]
        self.socket = None
        self.thread = None
        self.lines = []
        self.errors = []
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.certificate = self.key = self.sasl = None
        self.accepted = []
        self.registrations = []
        self.joins = []

    def start(self):
        self.listener.listen(4)
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def handle(self, line):
        registered = self.registered
        super().handle(line)
        if self.registered and not registered:
            self.registrations.append(len(self.accepted))
        if line == b"JOIN #reconnect":
            self.joins.append(len(self.accepted))

    def run(self):
        try:
            while not self.stop.is_set():
                try:
                    connection, unused = self.listener.accept()
                except socket.timeout:
                    continue
                self.accepted.append(time.monotonic())
                assert len(self.accepted) <= 4, "unexpected reconnect loop"
                connection.settimeout(.05)
                self.nick = "tester"
                self.sasl_data = bytearray()
                self.has_user = self.cap_end = self.registered = False
                with self.lock:
                    self.socket = connection
                try:
                    pending = b""
                    while not self.stop.is_set():
                        try:
                            data = connection.recv(32768)
                        except socket.timeout:
                            continue
                        if not data:
                            break
                        pending += data
                        assert len(pending) <= 65536
                        while b"\r\n" in pending:
                            line, pending = pending.split(b"\r\n", 1)
                            assert len(line) + 2 <= 512 and not any(c in line for c in (b"\0", b"\r", b"\n"))
                            self.lines.append((time.monotonic(), line))
                            assert len(self.lines) <= 2048
                            self.handle(line)
                except (ConnectionResetError, BrokenPipeError):
                    pass
                finally:
                    with self.lock:
                        if self.socket is connection:
                            self.socket = None
                    connection.close()
        except OSError as error:
            if not self.stop.is_set():
                self.errors.append(error)
        except BaseException as error:
            self.errors.append(error)

    def remote_close(self, error=False):
        with self.lock:
            assert self.socket is not None
            if error:
                self.socket.sendall(b"ERROR :Owned test server restarting\r\n")
            self.socket.shutdown(socket.SHUT_WR)

    def wait_join(self, number, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end and number not in self.joins and not self.errors:
            self.stop.wait(.02)
        assert not self.errors, self.errors
        assert number in self.registrations and number in self.joins, (
            "connection did not register and autojoin", number, self.accepted, self.lines)

    def close(self):
        self.stop.set()
        with self.lock:
            if self.socket is not None:
                try:
                    self.socket.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        self.listener.close()
        if self.thread is not None:
            self.thread.join(timeout=2)
            assert not self.thread.is_alive(), "owned fixture thread did not exit"
        assert not self.errors, self.errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path,
                        default=Path(os.environ.get("ONEKEY_BINARY", terminal.BINARY)))
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--idle", action="store_true", help="test vipirc -i")
    modes.add_argument("--gui", action="store_true", help="test full-screen vipirc")
    args = parser.parse_args()
    binary = args.binary.resolve()
    full = args.idle or args.gui or binary.name == "vipirc"
    gui = args.gui or (full and not args.idle)
    brand = "vipirc" if full else "onekeyirc"
    prefix = ["-i"] if args.idle else []
    mode = "vipirc curses" if gui else "vipirc -i" if full else "onekeyirc"
    terminal.BINARY = binary
    registry = Path("/tmp").resolve() / (brand + "-jobs-%d" % os.getuid())
    server = ReturningIRC()
    path = identity = None
    started = time.monotonic()

    def owned():
        if path is None:
            return False
        try:
            current = path.lstat()
        except FileNotFoundError:
            return False
        assert (current.st_dev, current.st_ino, current.st_ctime_ns) == identity, "owned endpoint was replaced"
        return True

    def attach():
        assert owned(), "owned background endpoint disappeared"
        session = resume_session(path, gui)
        try:
            if gui:
                session.command("/channel 0")
            return session
        except BaseException:
            session.close()
            raise

    def notice(session, text, marker=0):
        return (gui and text in session.screen.text()) or text.encode() in session.raw[marker:]

    def detach(session):
        session.command("/background")
        session.wait(lambda: session.proc.poll() is not None, "resume waiter did not detach")
        assert session.proc.returncode == 0
        assert owned()

    def wait_absent():
        end = time.monotonic() + 5
        while owned() and time.monotonic() < end:
            time.sleep(.02)
        assert not owned(), "worker did not remove its endpoint"

    try:
        with tempfile.TemporaryDirectory(prefix="onekey-reconnect-") as tmp:
            directory = Path(tmp)
            config = directory / "account.ini"
            saved = ("[connection]\nserver=127.0.0.1\nport=%d\ntls=false\n"
                     "nick=tester\nusername=tester\nchannel=#reconnect\nautoconnect=true\n"
                     "auth=none\n[history]\nlines=100\n" % server.port).encode("ascii")
            config.write_bytes(saved)
            config.chmod(0o600)
            environment = dict(os.environ, LANG="C", LC_ALL="C")
            environment.pop("TERM", None)
            result = subprocess.run([str(binary), *prefix, "--background", "--reconnect", "--config", str(config)],
                                    input=b"", capture_output=True, cwd=directory, env=environment,
                                    timeout=7, start_new_session=True)
            match = re.search(brand.encode() + rb" job ([1-9][0-9]*) is running", result.stdout)
            assert result.returncode == 0 and match, (result.returncode, result.stdout, result.stderr)
            path = registry / ("job-%d" % int(match[1])) / "session"
            info = path.lstat()
            identity = (info.st_dev, info.st_ino, info.st_ctime_ns)
            assert stat.S_ISSOCK(info.st_mode) and stat.S_IMODE(info.st_mode) == 0o600
            assert b"\x1b" not in result.stdout + result.stderr
            assert b"Press a key to start" not in result.stdout + result.stderr
            with attach() as session:
                session.wait(lambda: notice(session, "Automatic reconnect in 5 seconds"),
                             "cold connection refusal did not schedule a retry")
                detach(session)
            assert not server.accepted  # The reserved port has never listened.
            server.start()
            server.wait_join(1, 8)
            print("cold detached refusal recovered and autojoined", flush=True)

            with attach() as session:
                marker = len(session.raw)
                server.remote_close(error=True)
                session.wait(lambda: notice(session, "Automatic reconnect in 10 seconds", marker),
                             "registered remote ERROR did not schedule retry")
                detach(session)
            server.wait_join(2, 13)
            assert server.registrations == [1, 2] and server.joins == [1, 2]
            print("remote ERROR recovered and rejoined while detached", flush=True)

            with attach() as session:
                marker = len(session.raw)
                server.remote_close()
                session.wait(lambda: notice(session, "Automatic reconnect in 20 seconds", marker),
                             "remote EOF did not schedule retry")
                session.command("/disconnect", expected="disconnect requested." if gui else "Disconnected by user")
                cancelled = time.monotonic()
                detach(session)
            # Observe beyond the actual scheduled deadline, not just an empty
            # queue immediately after the cancellation command.
            while time.monotonic() < cancelled + 21:
                assert len(server.accepted) == 2 and not server.errors, (server.accepted, server.errors)
                assert owned()
                server.stop.wait(.05)
            assert len(server.accepted) == 2
            assert config.read_bytes() == saved
            print("manual disconnect cancelled the pending EOF retry past its deadline", flush=True)
            with attach() as session:
                session.command("/quit")
                session.wait(lambda: session.proc.poll() is not None, "quit did not stop owned worker")
                assert session.proc.returncode == 0
            wait_absent()
            assert config.read_bytes() == saved
            assert not any(line.startswith(b"PRIVMSG ") for unused, line in server.lines)
    finally:
        try:
            if owned():
                with attach() as session:
                    session.proc.send_signal(signal.SIGTERM)
                    session.wait(lambda: session.proc.poll() is not None, "owned cleanup signal was not forwarded")
                wait_absent()
        finally:
            server.close()
    elapsed = time.monotonic() - started
    assert elapsed < 45, ("reconnect integration exceeded its expected bound", elapsed)
    print("reconnect integration passed for %s (%s) in %.3fs" % (mode, binary, elapsed), flush=True)


if __name__ == "__main__":
    main()
