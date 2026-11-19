#!/usr/bin/env python3
"""Numbered local jobs: private PTYs/sockets only; no public IRC or PID signals."""
import fcntl
import os
from pathlib import Path
import re
import socket
import stat
import struct
import subprocess
import tempfile
import threading
import time

from test_idle_terminal import BINARY, IdleSession

BINARY = BINARY.resolve()
REGISTRY = Path("/tmp").resolve() / ("onekeyirc-jobs-%d" % os.getuid())


def command(*args, expected=0, timeout=5):
    env = dict(os.environ, XDG_CONFIG_HOME="invalid-relative-home")
    result = subprocess.run([str(BINARY), *map(str, args)], input=b"", capture_output=True,
                            env=env, timeout=timeout)
    assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
    return result.stdout + result.stderr


def detach(session):
    marker = len(session.raw)
    session.command("/background")
    session.wait(lambda: session.proc.poll() is not None, "background parent did not exit")
    session.quiet(.04)
    assert session.proc.returncode == 0
    output = bytes(session.raw[marker:])
    match = re.search(rb"onekeyirc job ([1-9][0-9]*) is running", output)
    assert match, output
    number = int(match[1])
    assert b"onekeyirc --resume " + str(number).encode() in output and b"/tmp/" not in output
    path = REGISTRY / ("job-%d" % number) / "session"
    assert stat.S_ISSOCK(path.lstat().st_mode) and stat.S_IMODE(path.stat().st_mode) == 0o600
    assert stat.S_IMODE(path.parent.stat().st_mode) == 0o700
    assert stat.S_IMODE(REGISTRY.stat().st_mode) == 0o700
    assert path.stat().st_uid == path.parent.stat().st_uid == os.getuid()
    return number, path


def resume(args, quit_after=True):
    session = IdleSession(args=args)
    session.wait(lambda: b"\x1b[?2004h" in session.raw, "numbered resume did not restore frontend")
    if quit_after:
        session.command("/quit")
        session.wait(lambda: session.proc.poll() is not None, "quit did not release waiter")
        assert session.proc.returncode == 0
        session.close()
        return None
    return session


def cleanup(path):
    if path and path.exists():
        with IdleSession(args=["--resume", str(path)]) as session:
            session.wait(lambda: b"\x1b[?2004h" in session.raw, "cleanup attachment failed")
            session.command("/quit")
            session.wait(lambda: session.proc.poll() is not None, "cleanup quit failed")
            assert session.proc.returncode == 0


class LocalEntry:
    """Owned fixture inside a slot reserved under the production registry lock."""
    def __init__(self, *, held=True, wrong_brand=False):
        self.directory = None
        self.directory_fd = None
        self.listener = None
        self.lease = None
        self.thread = None
        self.stop = threading.Event()
        with (REGISTRY / "lock").open("r+b") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            for number in range(64, 0, -1):
                directory = REGISTRY / ("job-%d" % number)
                try:
                    directory.mkdir(mode=0o700)
                except FileExistsError:
                    continue
                self.number, self.directory = number, directory
                self.directory_fd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
                break
            assert self.directory is not None, "No free fixture job slot"
            self.lease = os.open(self.directory / "lease", os.O_RDWR | os.O_CREAT | os.O_EXCL, 0o600)
            if held:
                fcntl.flock(self.lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.listener = socket.socket(socket.AF_UNIX)
            self.listener.bind(str(self.directory / "session"))
            (self.directory / "session").chmod(0o600)
            self.listener.listen(2)
        if wrong_brand:
            self.listener.settimeout(.1)
            self.thread = threading.Thread(target=self.wrong_peer, daemon=True)
            self.thread.start()

    def wrong_peer(self):
        while not self.stop.is_set():
            try:
                peer, unused = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            with peer:
                peer.settimeout(.5)
                try:
                    request = peer.recv(8)
                    if request == b"VIPIRCQ1":
                        peer.sendall(struct.pack("=8s8sI16s192s", b"VIPIRCS1", b"vipirc\0\0",
                                                 self.number, b"x" * 16, b"wrong program"))
                except OSError:
                    pass

    def close(self):
        self.stop.set()
        if self.thread:
            self.thread.join(timeout=1)
        if self.listener:
            self.listener.close()
        if self.directory and self.directory_fd is not None:
            with (REGISTRY / "lock").open("r+b") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                before = os.fstat(self.directory_fd)
                try:
                    after = self.directory.lstat()
                except FileNotFoundError:
                    after = None
                if after and (after.st_dev, after.st_ino) == (before.st_dev, before.st_ino):
                    for name in ("session", "lease"):
                        try:
                            os.unlink(name, dir_fd=self.directory_fd)
                        except FileNotFoundError:
                            pass
                    self.directory.rmdir()
            os.close(self.directory_fd)
        if self.lease is not None:
            os.close(self.lease)


def numbered_jobs():
    output = command("--sessions")
    assert b"No background jobs." in output, (
        "This isolated regression requires no existing onekeyirc jobs; it never stops other sessions.", output)
    command("--resume", expected=1)
    invalid = [
        ("--resume", "--resume"), ("--resume=0",), ("--resume=65",),
        ("--resume=",), ("--resume", "-1"), ("--resume", "name"),
        ("--resume", "1", "extra"), ("--resume", "--sessions"),
        ("--sessions", "--config", "/missing"), ("--sessions", "operand"),
    ]
    for args in invalid:
        command(*args, expected=2)
    assert b"--sessions" in command("--sessions", "--help")

    paths = []
    fixtures = []
    attached = None
    try:
        with IdleSession() as first, IdleSession() as second:
            first.start()
            one, path_one = detach(first)
            paths.append(path_one)
            second.start()
            two, path_two = detach(second)
            paths.append(path_two)
            assert one != two
            output = command("--sessions")
            for number in (one, two):
                assert re.search(rb"^\s+" + str(number).encode() + rb"\s+detached\s", output, re.M), output
            assert b"offline.invalid" in output
            assert b"no unique verified detached job" in command("--resume", expected=1)
            assert path_one.exists() and path_two.exists()

            attached = resume(["--resume=" + str(one)], quit_after=False)
            output = command("--sessions")
            assert re.search(rb"^\s+" + str(one).encode() + rb"\s+attached/busy", output, re.M), output
            command("--resume", str(one), expected=1)
            attached.command("/quit")
            attached.wait(lambda: attached.proc.poll() is not None, "first numbered job did not quit")
            attached.close()
            attached = None
            assert not path_one.exists()
            output = command("--sessions")
            assert re.search(rb"^\s+" + str(two).encode() + rb"\s+detached", output, re.M), output

            # A peer with the right owner but another program's protocol label
            # is not a compatible detached job, and blocks automatic guessing.
            wrong = LocalEntry(wrong_brand=True)
            fixtures.append(wrong)
            output = command("--sessions")
            assert re.search(rb"^\s+" + str(wrong.number).encode() + rb"\s+unverified", output, re.M)
            command("--resume", expected=1)
            wrong.close()
            fixtures.remove(wrong)

            unsafe = LocalEntry()
            fixtures.append(unsafe)
            os.fchmod(unsafe.lease, 0o644)
            output = command("--sessions")
            assert re.search(rb"^\s+" + str(unsafe.number).encode() + rb"\s+unverified", output, re.M)
            assert unsafe.directory.exists(), "Unsafe entry was deleted"
            command("--resume", expected=1)
            (unsafe.directory / "lease").unlink()
            os.mkfifo(unsafe.directory / "lease", 0o600)
            output = command("--sessions", timeout=3)
            assert re.search(rb"^\s+" + str(unsafe.number).encode() + rb"\s+unverified", output, re.M)
            assert stat.S_ISFIFO((unsafe.directory / "lease").lstat().st_mode)
            unsafe.close()
            fixtures.remove(unsafe)

            stale = LocalEntry(held=False)
            fixtures.append(stale)
            stale.listener.close()
            stale.listener = None
            output = command("--sessions")
            assert b"stale local job" in output and not stale.directory.exists(), output
            stale.close()
            fixtures.remove(stale)

            # Every remaining slot can be unresponsive; discovery still has
            # one total deadline, not 63 independent handshake timeouts.
            for unused in range(63):
                fixtures.append(LocalEntry())
            start = time.monotonic()
            output = command("--sessions", timeout=5)
            elapsed = time.monotonic() - start
            assert elapsed < 4.0, ("Unresponsive jobs multiplied listing time", elapsed)
            assert output.count(b"unverified") == 63, output
            for fixture in fixtures:
                fixture.close()
            fixtures.clear()

            # The remaining job keeps its original number after the first
            # exits. With precisely one available job, bare --resume works.
            resume(["--resume"])
            assert not path_two.exists()
            assert b"No background jobs." in command("--sessions")
    finally:
        if attached:
            attached.close()
        for fixture in fixtures:
            fixture.close()
        for path in paths:
            cleanup(path)
    print("background jobs: optional resume grammar, private stable slots, ambiguous selection refusal, attached exclusion, compatible-program query, unsafe-lease/FIFO rejection without blocking, stale cleanup, all64 slots under one deadline, sole automatic attach and clean shutdown passed", flush=True)


if __name__ == "__main__":
    numbered_jobs()
    print("all numbered-session checks passed", flush=True)
