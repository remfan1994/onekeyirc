#!/usr/bin/env python3
"""Socket-free PTY checks for welcome control keys and real job control."""
import errno
import fcntl
import os
from pathlib import Path
import pty
import select
import shlex
import shutil
import signal
import struct
import subprocess
import tempfile
import termios
import time

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("VIPIRC_BINARY", str(ROOT / "vipirc")))
PROMPT = b"VIPIRC_WELCOME_TEST> "


class WelcomeTerminal:
    def __init__(self, *, interrupt=b"\x03", suspend=b"\x1a", shell=False):
        self.temp = tempfile.TemporaryDirectory(prefix="vipirc-welcome-")
        self.master, self.slave = pty.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 100, 0, 0))
        attrs = termios.tcgetattr(self.slave)
        attrs[3] |= termios.ISIG | termios.ICANON | termios.ECHO
        attrs[6][termios.VINTR] = interrupt
        attrs[6][termios.VSUSP] = suspend
        termios.tcsetattr(self.slave, termios.TCSANOW, attrs)
        self.before = termios.tcgetattr(self.slave)
        self.raw = bytearray()
        self.client_group = None
        env = dict(os.environ, TERM="xterm-256color", LANG="C.UTF-8", LC_ALL="C.UTF-8",
                   XDG_CONFIG_HOME=str(Path(self.temp.name) / "unused"),
                   PS1=PROMPT.decode("ascii"), PS2="MORE> ", ENV="", BASH_ENV="")
        self.shell = shell
        if shell:
            bash = shutil.which("bash")
            command = [bash, "--noprofile", "--norc", "-i"] if bash else ["/bin/sh", "-i"]
        else:
            command = [str(BINARY), "--test-mode"]

        def controlling_terminal():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        self.proc = subprocess.Popen(command, stdin=self.slave, stdout=self.slave, stderr=self.slave,
                                     cwd=self.temp.name, env=env, preexec_fn=controlling_terminal)
        os.set_blocking(self.master, False)
        if shell:
            self.wait(lambda: PROMPT in self.raw, "job-control shell did not start")
            self.type("set -m\n")
            self.quiet(0.1)
            self.type(shlex.quote(str(BINARY)) + " --test-mode\n")
        self.wait(lambda: b"Not vegetarian?" in self.raw, "welcome did not appear")
        if shell:
            self.client_group = os.tcgetpgrp(self.master)
            assert self.client_group != self.proc.pid, "client has no independent foreground process group"

    def type(self, value):
        if isinstance(value, str): value = value.encode("utf-8")
        while value:
            try:
                count = os.write(self.master, value)
                value = value[count:]
            except BlockingIOError:
                self.pump(0.02)

    def pump(self, timeout=0.02):
        if select.select([self.master], [], [], timeout)[0]:
            try: self.raw.extend(os.read(self.master, 65536))
            except OSError as error:
                if error.errno not in (errno.EIO, errno.EAGAIN): raise

    def quiet(self, duration=0.18):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            self.pump(min(0.02, max(0, deadline - time.monotonic())))

    def wait(self, predicate, label, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.pump()
            if predicate(): return
            if self.proc.poll() is not None: break
        raise AssertionError("%s; exit=%r; output=%r" % (label, self.proc.poll(), bytes(self.raw[-1800:])))

    def assert_welcome(self):
        self.quiet()
        assert self.proc.poll() is None
        assert b"[OFFLINE]" not in self.raw, "welcome input entered the application"
        current = termios.tcgetattr(self.slave)
        assert current[3] & termios.ISIG, "welcome disabled Unix signal processing"
        assert current[6][termios.VINTR] == self.before[6][termios.VINTR]
        assert current[6][termios.VSUSP] == self.before[6][termios.VSUSP]

    def terminated(self, status):
        self.proc.wait(timeout=4)
        self.quiet(0.04)
        assert self.proc.returncode == status, (self.proc.returncode, status, bytes(self.raw[-1000:]))
        assert termios.tcgetattr(self.slave) == self.before, "welcome did not restore the exact terminal attributes"
        assert b"[OFFLINE]" not in self.raw, "signal advanced past the welcome"
        assert b"\x1b[?2004l" in self.raw, "welcome left bracketed-paste mode enabled"
        assert not list(Path(self.temp.name).iterdir()), "welcome signal created configuration files"

    def close(self):
        if self.client_group:
            try: os.killpg(self.client_group, signal.SIGKILL)
            except ProcessLookupError: pass
        if self.proc.poll() is None:
            self.proc.terminate()
            try: self.proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=2)
        os.close(self.master)
        os.close(self.slave)
        self.temp.cleanup()

    def __enter__(self): return self
    def __exit__(self, kind, value, traceback): self.close()


def interruptions_and_other_controls():
    with WelcomeTerminal() as session:
        session.type(b"\x01\x02\x04\x08\x0c\x0e\x10\x15\x17\x18\x7f")
        session.assert_welcome()
        # A bracketed paste is not a welcome acknowledgment. Its printable
        # command-looking body must not leak into the main input state.
        session.type(b"\x1b[200~/quit\rmore text\x1b[201~")
        session.assert_welcome()
        session.type(b"\x03")
        session.terminated(130)
    with WelcomeTerminal(interrupt=b"\x18") as session:
        session.type(b"\x03")  # No longer VINTR: an ignored ordinary control byte.
        session.assert_welcome()
        session.type(b"\x18")
        session.terminated(130)
    with WelcomeTerminal() as session:
        session.type(b"\x1c")
        session.terminated(131)
    for signum in (signal.SIGTERM, signal.SIGHUP):
        with WelcomeTerminal() as session:
            os.kill(session.proc.pid, signum)
            session.terminated(128 + signum)
    print("welcome: control-byte/paste isolation, Ctrl-C, custom VINTR, quit/term/hangup statuses and exact terminal restoration passed")


def acknowledgment():
    for key in (b"x", b"\r", b"\t", b"\x1bOA"):
        with WelcomeTerminal() as session:
            session.type(key)
            session.wait(lambda: b"[OFFLINE]" in session.raw, "ordinary acknowledgment did not begin the app")
            session.type(b"\x03")
            session.proc.wait(timeout=3)
            assert session.proc.returncode == 130
            assert termios.tcgetattr(session.slave) == session.before
    print("welcome: printable, Enter, Tab and arrow acknowledgments still start the app")


def suspend_and_foreground():
    # The interactive parent shell keeps the client's process group
    # non-orphaned, so the kernel can perform normal SIGTSTP job control.
    for suspend in (b"\x1a", b"\x18"):
        with WelcomeTerminal(shell=True, suspend=suspend) as session:
            group = session.client_group
            begin = len(session.raw)
            session.type(suspend)
            session.wait(lambda: os.tcgetpgrp(session.master) == session.proc.pid and
                         b"Stopped" in session.raw[begin:], "suspend did not return to the job-control shell")
            os.kill(group, 0)  # The client is stopped, not gone.
            assert b"[OFFLINE]" not in session.raw
            resumed = len(session.raw)
            session.type("fg\n")
            session.wait(lambda: os.tcgetpgrp(session.master) == group and
                         b"Not vegetarian?" in session.raw[resumed:], "fg did not restore the welcome")
            session.assert_welcome()
            begin = len(session.raw)
            session.type(b"\x03")
            session.wait(lambda: os.tcgetpgrp(session.master) == session.proc.pid and PROMPT in session.raw[begin:],
                         "Ctrl-C after fg did not return to the shell")
            session.type("printf 'VIPIRC_STATUS=%d\\n' \"$?\"\n")
            session.wait(lambda: b"VIPIRC_STATUS=130" in session.raw, "shell did not observe interrupt status130")
            session.client_group = None
            session.type("exit\n")
            session.proc.wait(timeout=3)
    print("welcome: Ctrl-Z and custom VSUSP stop the real foreground job; fg repaints the welcome before Ctrl-C exits")


def main():
    interruptions_and_other_controls()
    acknowledgment()
    suspend_and_foreground()
    print("all welcome control/job tests passed")


if __name__ == "__main__":
    main()
