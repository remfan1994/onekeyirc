#!/usr/bin/env python3
"""Native idle-loop terminal-loss regressions, without AF_UNIX handshakes.

The fixture uses real managed detach, PTYs, localhost IRC and TCP controller
closure. Initial managed state and descriptor reattachment are substituted;
passing this suite does not establish real session IPC/resume correctness.
"""
import argparse
import errno
import fcntl
import json
import os
from pathlib import Path
import pty
import select
import signal
import socket
import struct
import subprocess
import tempfile
import termios
import time

from test_idle_terminal import IdleSession, IdleScreen
from test_integration import IRCFixture


def tcp_pair():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        peer = socket.create_connection(listener.getsockname(), timeout=2)
        worker, unused = listener.accept()
    return peer, worker


class LossSession(IdleSession):
    """The worker has no controlling tty, matching a resumed managed worker."""
    def __init__(self, binary, managed=True, read_failure=None):
        self.temp = tempfile.TemporaryDirectory(prefix="idle-hangup-")
        self.directory = Path(self.temp.name)
        self.peer = IRCFixture()
        self.config = self.directory / "config.ini"
        self.config.write_text(
            "[connection]\nserver=127.0.0.1\nport=%d\ntls=false\nnick=tester\n"
            "username=tester\nchannel=#test\nautoconnect=true\nauth=none\n"
            "[input]\nsend_mode=review\ncomposer_mode=fullscreen\n"
            "[history]\nlines=50\n[output]\npersistent_logging=false\n"
            "program_messages=false\n" % self.peer.port)
        self.config.chmod(0o600)
        self.before_config = self.config.read_bytes()
        self.master, self.slave = pty.openpty()
        self.next_master, self.next_slave = pty.openpty()
        for fd in (self.slave, self.next_slave):
            fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 18, 80, 0, 0))
        self.before_tty = termios.tcgetattr(self.slave)
        self.before_flags = fcntl.fcntl(self.slave, fcntl.F_GETFL)
        self.screen, self.raw = IdleScreen(18, 80), bytearray()
        self.report, report_writer = os.pipe()
        request_reader, self.request = os.pipe()
        self.controller, worker_controller = tcp_pair()
        env = dict(os.environ, TERM="xterm-256color", LC_ALL="C.UTF-8",
                   HOME=str(self.directory), XDG_CONFIG_HOME=str(self.directory / "unused"),
                   VIPIRC_TEST_REPORT_FD=str(report_writer),
                   VIPIRC_TEST_REQUEST_FD=str(request_reader),
                   VIPIRC_TEST_CONTROLLER_FD=str(worker_controller.fileno()),
                   VIPIRC_TEST_REATTACH_FD=str(self.next_slave))
        if managed:
            env["VIPIRC_TEST_MANAGED"] = "1"
        else:
            env.pop("VIPIRC_TEST_MANAGED", None)
        if read_failure:
            env["VIPIRC_TEST_READ_FAILURE"] = read_failure
        else:
            env.pop("VIPIRC_TEST_READ_FAILURE", None)
        self.proc = subprocess.Popen(
            [str(binary), "--config", str(self.config)], stdin=self.slave,
            stdout=self.slave, stderr=self.slave, env=env, cwd=self.directory,
            start_new_session=True, close_fds=True,
            pass_fds=(report_writer, request_reader, worker_controller.fileno(), self.next_slave))
        os.close(report_writer)
        os.close(request_reader)
        worker_controller.close()
        self.events = bytearray()
        os.set_blocking(self.master, False)
        os.set_blocking(self.next_master, False)
        os.set_blocking(self.report, False)

    def pump(self, timeout=0.03):
        if self.master >= 0:
            super().pump(timeout)
        else:
            select.select([self.report], [], [], timeout)
        try:
            self.events.extend(os.read(self.report, 4096))
        except BlockingIOError:
            pass

    def wait_detached(self, controller_lost=False):
        self.wait(lambda: b"D" in self.events, "managed worker did not release terminal/controller")
        assert self.proc.poll() is None, "terminal loss terminated the managed worker"
        assert fcntl.fcntl(self.slave, fcntl.F_GETFL) == self.before_flags, (
            "terminal loss changed the invoking terminal's shared descriptor flags")
        self.controller.settimeout(2)
        reply = bytearray()
        while True:
            data = self.controller.recv(16)
            if not data:
                break
            reply.extend(data)
        assert bytes(reply) == (b"" if controller_lost else b"D\0"), (
            "controller was not closed with the expected detach reply", reply)

    def ping(self, marker):
        self.peer.send("PING :" + marker)
        self.wait(lambda: any(line == b"PONG :" + marker.encode() for _, line in self.peer.lines),
                  "IRC PING failed after terminal loss")

    def reattach(self):
        if self.master >= 0:
            os.close(self.master)
        os.close(self.slave)
        self.master, self.slave = self.next_master, self.next_slave
        self.next_master = self.next_slave = -1
        self.raw.clear()
        self.screen = IdleScreen(18, 80)
        assert os.write(self.request, b"A") == 1
        self.wait(lambda: b"A" in self.events and b"\x1b[?2004h" in self.raw,
                  "fixture reattachment did not restore the idle frontend")

    def close(self):
        forced = False
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                forced = True
                self.proc.kill()
                self.proc.wait(timeout=2)
        self.controller.close()
        self.peer.close()
        for fd in (self.master, self.slave, self.next_master, self.next_slave,
                   self.report, self.request):
            if fd >= 0:
                os.close(fd)
        try:
            assert not forced, "fixture needed forced cleanup"
            assert self.config.read_bytes() == self.before_config
            assert not list(self.directory.rglob("*.log")), "persistent logging was enabled"
        finally:
            self.temp.cleanup()


def loss_case(binary, mode):
    read_failure = mode if mode in ("eof", "eio") else None
    with LossSession(binary, read_failure=read_failure) as session:
        session.start()
        session.peer.send(":Attacker!u@local PRIVMSG #test :RETAINED-BEFORE-LOSS")
        session.wait(lambda: b"RETAINED-BEFORE-LOSS" in session.raw, "history marker missing")
        session.type("UNSENT-DRAFT-RETAINED")
        session.wait(lambda: session.editor("UNSENT-DRAFT-RETAINED"), "draft not composed")
        if mode == "backlog":
            # Leave an active presentation queue when the real PTY disappears.
            session.type(b"\x1b")
            session.wait(lambda: not session.screen.alternate, "draft did not cancel")
            for batch in range(32):
                session.peer.send_bytes(b"".join(
                    b":Attacker!u@local PRIVMSG #test :%04d " % (batch * 64 + n) +
                    b"x" * 300 + b"\r\n" for n in range(64)))
            session.peer.send("PING :backlog-queued")
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and not any(
                    line == b"PONG :backlog-queued" for _, line in session.peer.lines):
                time.sleep(.01)
            assert any(line == b"PONG :backlog-queued" for _, line in session.peer.lines)
        started = time.monotonic()
        if mode in ("master", "backlog"):
            os.close(session.master)
            session.master = -1
        elif mode == "hup":
            session.proc.send_signal(signal.SIGHUP)
        elif mode == "controller":
            session.controller.shutdown(socket.SHUT_WR)
        elif read_failure:
            session.type(bytes((29,)))
        else:
            raise AssertionError(mode)
        session.wait_detached(controller_lost=mode == "controller")
        elapsed = time.monotonic() - started
        assert elapsed < 3, ("terminal loss was not bounded", mode, elapsed)
        session.wait(lambda: any(line == b"AWAY :Detached from terminal" for _, line in session.peer.lines),
                     "managed terminal loss did not set automatic away status")
        away_index = len(session.peer.lines)
        session.ping("detached-" + mode)
        session.peer.send(":Attacker!u@local PRIVMSG #test :RETAINED-WHILE-DETACHED")
        session.ping("history-" + mode)
        session.reattach()
        session.wait(lambda: any(line == b"AWAY" for _, line in session.peer.lines[away_index:]),
                     "managed reattachment did not clear automatic away status")
        if mode != "backlog":
            session.wait(lambda: session.editor("UNSENT-DRAFT-RETAINED"),
                         "terminal loss discarded the unsent draft")
            session.type(b"\x1b")
            session.wait(lambda: not session.screen.alternate, "restored draft did not cancel")
        session.wait(lambda: b"RETAINED-WHILE-DETACHED" in session.raw,
                     "reattachment lost detached IRC history")
        if mode != "backlog":
            session.wait(lambda: b"RETAINED-BEFORE-LOSS" in session.raw,
                         "reattachment lost earlier IRC history")
        session.ping("reattached-" + mode)
        assert not session.peer.messages(), "retained draft was sent without approval"
        session.proc.send_signal(signal.SIGTERM)
        session.wait(lambda: session.proc.poll() is not None, "TERM did not stop the managed worker")
        assert session.proc.returncode == 128 + signal.SIGTERM
        return {"case": mode, "detach_ms": round(elapsed * 1000, 3)}


def unmanaged_hup(binary):
    with LossSession(binary, managed=False) as session:
        session.start()
        session.proc.send_signal(signal.SIGHUP)
        session.wait(lambda: session.proc.poll() is not None, "unmanaged HUP did not exit")
        assert session.proc.returncode == 128 + signal.SIGHUP
        assert b"D" not in session.events, "unmanaged session was silently converted to a worker"
    return {"case": "unmanaged-hup", "exit_status": 128 + signal.SIGHUP}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--case", choices=("master", "backlog", "hup", "controller", "eof", "eio", "unmanaged-hup"))
    args = parser.parse_args()
    binary = args.fixture.resolve()
    assert binary.is_file() and os.access(binary, os.X_OK), binary
    cases = [args.case] if args.case else ("master", "backlog", "hup", "controller", "eof", "eio", "unmanaged-hup")
    results = []
    for mode in cases:
        result = unmanaged_hup(binary) if mode == "unmanaged-hup" else loss_case(binary, mode)
        results.append(result)
        print(json.dumps(result, sort_keys=True), flush=True)
    print(json.dumps({"result": "PASS", "cases": len(results), "real_pty_closure": True,
                      "real_managed_detach": True, "af_unix_handshake_tested": False}, sort_keys=True))


if __name__ == "__main__":
    main()
