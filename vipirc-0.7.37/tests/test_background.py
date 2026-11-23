#!/usr/bin/env python3
"""Real-terminal detach/resume checks. Loopback peers only; no public IRC."""
import argparse
import array
import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import shlex
import shutil
import signal
import socket
import stat
import struct
import subprocess
import termios
import time

from test_integration import BINARY, ROOT, ModeControls, Screen, Session
from test_offline import OfflineTerminal
from test_input_limits import editor_info, wait_bytes


def until(predicate, label, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise AssertionError(label)


class ResumeTerminal(ModeControls):
    type = OfflineTerminal.type
    pump = OfflineTerminal.pump
    quiet = OfflineTerminal.quiet
    wait = OfflineTerminal.wait

    def __init__(self, endpoint, *, rows=28, cols=120, term="xterm-256color", locale="C.UTF-8"):
        self.endpoint = str(endpoint)
        self.master, slave = pty.openpty()
        self.term_before = termios.tcgetattr(slave)
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.screen = Screen(rows, cols)
        self.raw = bytearray()
        env = dict(os.environ, TERM=term, LANG=locale, LC_ALL=locale)

        def terminal():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        self.proc = subprocess.Popen([str(BINARY), "--resume", self.endpoint],
                                     stdin=slave, stdout=slave, stderr=slave,
                                     cwd=ROOT, env=env, preexec_fn=terminal)
        os.close(slave)
        os.set_blocking(self.master, False)

    def start(self):
        self.wait(lambda: self.is_main(), "resume did not restore the chat screen")
        self.wait(lambda: self.screen.cursor_visible, "resume hid the editor caret")

    def resize(self, rows, cols):
        fcntl.ioctl(self.master, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.screen.resize(rows, cols)
        # The kernel sends SIGWINCH to this foreground waiter. The worker has
        # no controlling terminal and must learn the new size via its socket.
        self.quiet(0.1)

    def close(self):
        if self.proc.poll() is None:
            if self.master is None:
                self.proc.terminate()
            else:
                self.type(b"\x03")
            try:
                self.proc.wait(timeout=4)
            except subprocess.TimeoutExpired:
                self.proc.terminate()
                self.proc.wait(timeout=4)
                raise AssertionError("resumed Ctrl-C did not close the worker promptly")
        if self.master is None:
            return
        self.quiet(0.03)
        current = termios.tcgetattr(self.master)
        mask = termios.ICANON | termios.ECHO | termios.ISIG
        assert current[3] & mask == self.term_before[3] & mask, "resume did not restore terminal attributes"
        os.close(self.master)

    def hangup(self):
        os.close(self.master)
        self.master = None
        self.proc.wait(timeout=5)
        assert self.proc.returncode == 0, ("terminal hangup did not detach the waiter cleanly",
                                           self.proc.returncode)

    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        self.close()


class ResumeShell(ResumeTerminal):
    """A real shell job, so VSUSP has a non-orphaned foreground group."""
    prompt = b"VIPIRC_BACKGROUND_SHELL> "

    def __init__(self, endpoint):
        self.endpoint = str(endpoint)
        self.stopped = False
        self.master, slave = pty.openpty()
        self.term_before = termios.tcgetattr(slave)
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 28, 120, 0, 0))
        self.screen = Screen(28, 120)
        self.raw = bytearray()
        env = dict(os.environ, TERM="xterm-256color", LANG="C.UTF-8", LC_ALL="C.UTF-8",
                   PS1=self.prompt.decode(), PS2="MORE> ", ENV="", BASH_ENV="", INPUTRC="/dev/null")
        for name in ("IGNOREEOF", "SHELLOPTS", "BASHOPTS"):
            env.pop(name, None)

        def terminal():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        bash = shutil.which("bash")
        command = [bash, "--noprofile", "--norc", "-i"] if bash else ["/bin/sh", "-i"]
        self.proc = subprocess.Popen(command, stdin=slave, stdout=slave, stderr=slave,
                                     cwd=ROOT, env=env, preexec_fn=terminal)
        os.close(slave)
        os.set_blocking(self.master, False)
        self.wait(lambda: self.prompt in self.raw, "resume parent shell did not start")
        self.shell_attrs = termios.tcgetattr(self.master)
        self.type("set -m\n")
        self.quiet(0.03)
        self.type(" ".join(shlex.quote(arg) for arg in (str(BINARY), "--resume", self.endpoint)) + "\n")

    def suspend(self):
        assert os.tcgetpgrp(self.master) != self.proc.pid
        begin = len(self.raw)
        self.type(b"\x1a")
        self.wait(lambda: os.tcgetpgrp(self.master) == self.proc.pid and self.prompt in self.raw[begin:],
                  "resumed Ctrl-Z did not return a restored terminal to the shell")
        self.stopped = True
        attrs = termios.tcgetattr(self.master)
        mask = termios.ICANON | termios.ECHO | termios.ISIG
        # Interactive shells such as bash run their prompt editor in their
        # own noncanonical mode. Compare its real baseline, not a fresh PTY.
        assert attrs[3] & mask == self.shell_attrs[3] & mask, "suspension changed the shell's own terminal mode"

    def foreground(self):
        begin = len(self.raw)
        self.type("fg\n")
        self.wait(lambda: os.tcgetpgrp(self.master) != self.proc.pid and self.is_main() and
                  b"\x1b[?2004h" in self.raw[begin:],
                  "fg did not restore the retained vipirc screen")
        self.wait(lambda: self.screen.cursor_visible, "fg hid the chat caret")
        self.stopped = False

    def close(self):
        if self.proc.poll() is None:
            if self.stopped:
                self.foreground()
            if os.tcgetpgrp(self.master) != self.proc.pid:
                begin = len(self.raw)
                self.type(b"\x03")
                self.wait(lambda: os.tcgetpgrp(self.master) == self.proc.pid and self.prompt in self.raw[begin:],
                          "resumed Ctrl-C did not return to the parent shell")
            self.type("exit\n")
            self.proc.wait(timeout=4)
        self.quiet(0.03)
        attrs = termios.tcgetattr(self.master)
        mask = termios.ICANON | termios.ECHO | termios.ISIG
        assert attrs[3] & mask == self.term_before[3] & mask
        os.close(self.master)


def detach(session):
    before = len(session.raw)
    session.open_command()
    session.type("background\r")
    deadline = time.monotonic() + 5
    while session.proc.poll() is None and time.monotonic() < deadline:
        session.pump()
    session.quiet(0.08)
    assert session.proc.poll() == 0, ("Background did not return to the shell", session.screen.text())
    match = re.search(rb"vipirc job ([1-9][0-9]*) is running", session.raw[before:])
    if match:
        number = int(match[1])
        assert (b"vipirc --resume " + str(number).encode()) in session.raw[before:]
        endpoint = Path("/tmp").resolve() / ("vipirc-jobs-%d" % os.getuid()) / ("job-%d" % number) / "session"
    else:
        # Explicit legacy-path attachments keep their exact path on detach.
        match = re.search(rb"vipirc --resume (/[^\s]+)", session.raw[before:])
        assert match, ("Background did not print a usable resume command", bytes(session.raw[before:]))
        endpoint = Path(match[1].decode("ascii"))
    assert stat.S_ISSOCK(endpoint.lstat().st_mode), endpoint
    assert stat.S_IMODE(endpoint.lstat().st_mode) == 0o600
    assert stat.S_IMODE(endpoint.parent.lstat().st_mode) == 0o700
    assert endpoint.stat().st_uid == os.getuid() and endpoint.parent.stat().st_uid == os.getuid()
    after = termios.tcgetattr(session.master)
    mask = termios.ICANON | termios.ECHO | termios.ISIG
    assert after[3] & mask == session.term_before[3] & mask, "Background left the shell terminal in raw mode"
    return endpoint


def cleanup(endpoint):
    if endpoint is None or not endpoint.exists():
        return
    with ResumeTerminal(endpoint) as resumed:
        resumed.start()
    until(lambda: not endpoint.exists(), "worker did not remove its endpoint on exit")


def check_rejected_terminals(endpoint):
    # A malformed or nonterminal attachment cannot change the worker's
    # existing terminal, and must not retain the received descriptor copies.
    with socket.socket(socket.AF_UNIX) as peer:
        peer.connect(str(endpoint))
        if hasattr(socket, "SO_PEERCRED"):
            worker, uid, unused_gid = struct.unpack("3i", peer.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            assert uid == os.getuid()
        else:
            worker = None
        peer.settimeout(5)
        tty = os.open("/dev/null", os.O_RDWR)
        try:
            request = b"VIPIRCB1" + b"xterm-256color".ljust(128, b"\0") + b"C.UTF-8".ljust(128, b"\0")
            peer.sendmsg([request], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", [tty] * 3))])
            assert peer.recv(2) == b"E\x02", "nonterminal descriptors were accepted"
        finally:
            os.close(tty)
    return worker


def offline_roundtrip():
    endpoint = None
    try:
        with OfflineTerminal() as original:
            original.start()
            text = "detached cafe\u00e9 draft"
            original.type(text)
            before = wait_bytes(original, len(text.encode()))
            original.type(b"\x1bOD" * 3)
            original.quiet(0.04)
            caret = editor_info(original).col
            endpoint = detach(original)
            assert original.config.read_bytes() == original.before_config
            worker = check_rejected_terminals(endpoint)
            descriptor_count = (len(list(Path("/proc/%d/fd" % worker).iterdir()))
                                if worker and Path("/proc/%d/fd" % worker).is_dir() else None)
            for unused in range(8):
                check_rejected_terminals(endpoint)
            if descriptor_count is not None:
                until(lambda: len(list(Path("/proc/%d/fd" % worker).iterdir())) == descriptor_count,
                      "rejected attachments leaked descriptors")
            with ResumeTerminal(endpoint, term="vipirc-unknown-terminfo-test") as unknown:
                until(lambda: unknown.proc.poll() is not None, "an unknown TERM stranded the attachment")
                assert unknown.proc.returncode == 0 and endpoint.exists(), "unknown TERM destroyed the retained session"

            with ResumeTerminal(endpoint, term="screen", locale="C", cols=100) as resumed:
                resumed.start()
                after = wait_bytes(resumed, before.byte_count)
                assert "detached cafe? draft" in resumed.input_text(), resumed.screen.text()
                assert after.col == caret, ("resume changed caret", caret, after)
                with ResumeTerminal(endpoint) as busy:
                    until(lambda: busy.proc.poll() is not None, "a second attachment blocked indefinitely")
                    busy.quiet(0.04)
                    assert busy.proc.returncode == 1 and b"already has a terminal" in busy.raw, (
                        "second attachment was not rejected with its expected diagnostic",
                        busy.proc.returncode, bytes(busy.raw[-8192:]))
                resumed.resize(30, 60)
                wait_bytes(resumed, before.byte_count)
                assert resumed.screen.cursor_visible
                endpoint = detach(resumed)

            with ResumeTerminal(endpoint, term="xterm-256color", locale="C.UTF-8", cols=120) as resumed:
                resumed.start()
                wait_bytes(resumed, before.byte_count)
                assert text in resumed.input_text(), "ASCII resume damaged stored UTF-8 text"
                resumed.type(b"\x04" * 64)
                resumed.quiet(0.05)
                assert resumed.proc.poll() is None, "Ctrl-D exited resumed session"
                wait_bytes(resumed, before.byte_count)
            until(lambda: not endpoint.exists(), "normal worker exit retained its resume endpoint")
            assert not endpoint.parent.exists(), "normal worker exit retained its private directory"
            endpoint = None
    finally:
        cleanup(endpoint)
    print("background offline: private endpoint, shell terminal restoration, malformed descriptor rejection, eight retry leak checks, unknown-TERM recovery, exclusive attach, C/screen and UTF-8/xterm reattach, resize, draft/caret, held Ctrl-D and cleanup passed")


def ping(server, label):
    sent = time.monotonic()
    server.send("PING :" + label)
    until(lambda: any(line == ("PONG :" + label).encode() for unused, line in server.lines),
          "background server PING was not answered: " + label, 3)
    assert time.monotonic() - sent < 2.5, "background polling stalled"


def live_roundtrip():
    from test_transfers import (IncomingPeer, action, offer_payload,
                                open_transfers, prepare_receive)
    endpoint = None
    transfer_peer = None
    try:
        with Session() as original:
            original.start()
            original.resize(28, 120)
            text = "LIVE_DRAFT_SURVIVES_DETACH"
            original.type(text)
            wait_bytes(original, len(text))
            transfer_peer = IncomingPeer()
            payload = bytes(range(256)) * 512
            receive_directory = Path(original.temp.name) / "background-receive"
            offer_payload(original.server, "alice", "background.bin", transfer_peer.port, len(payload))
            open_transfers(original)
            prepare_receive(original, "background.bin", receive_directory, "background.bin")
            action(original, "Accept")
            transfer_peer.accept(original)
            original.type(b"\x1b")
            original.wait(lambda: original.is_main() and original.mode() == "INPUT",
                          "transfer review did not return to chat before detach")
            for message in ("PACER_START", "QUEUED_BEFORE_FORK_A", "QUEUED_BEFORE_FORK_B"):
                original.command("/msg QueuePeer " + message)
            assert len(original.server.messages("QueuePeer")) < 3, "fixture did not leave a message queued before detach"
            endpoint = detach(original)
            for i in range(5):
                ping(original.server, "detached-%d" % i)
            until(lambda: len(original.server.messages("QueuePeer")) == 3,
                  "retained paced messages did not finish while detached", 5)
            sent_messages = original.server.messages("QueuePeer")
            assert [body for unused, body in sent_messages] == [b"PACER_START", b"QUEUED_BEFORE_FORK_A", b"QUEUED_BEFORE_FORK_B"]
            assert all(sent_messages[i + 1][0] - sent_messages[i][0] >= 1.15 for i in (0, 1)), "detach reset send pacing"
            for begin in range(0, len(payload), 8192):
                transfer_peer.send(payload[begin:begin + 8192])
                transfer_peer.wait_ack(original, min(begin + 8192, len(payload)))
            destination = receive_directory / "background.bin"
            until(destination.exists, "accepted DCC did not finish while detached")
            assert destination.read_bytes() == payload, "detach damaged accepted file-transfer bytes"
            original.server.send(":alice!u@localhost PRIVMSG #test :RECEIVED_WHILE_DETACHED")
            # A client that never completes its handshake must not monopolize
            # the IRC event loop. The receiver expires it within four seconds.
            with socket.socket(socket.AF_UNIX) as incomplete:
                incomplete.connect(str(endpoint))
                incomplete.sendall(b"VIP")
                ping(original.server, "partial-attach")
                incomplete.settimeout(5)
                assert incomplete.recv(2) == b"E\x02"
            with ResumeTerminal(endpoint) as resumed:
                resumed.start()
                wait_bytes(resumed, len(text))
                assert text in resumed.input_text()
                resumed.wait(lambda: "RECEIVED_WHILE_DETACHED" in resumed.screen.text(),
                             "detached incoming history was lost")
                assert not original.server.messages(), "detaching sent the retained draft"
                ping(original.server, "attached")
                resumed.type(b"\r")
                until(lambda: len(original.server.messages()) == 1 and original.server.messages()[0][1] == text.encode(),
                      "resumed draft did not send exactly once")
                endpoint = detach(resumed)
            ping(original.server, "second-detach")
            with ResumeTerminal(endpoint) as lost_terminal:
                lost_terminal.start()
                lost_terminal.hangup()
            ping(original.server, "terminal-hangup")
            with ResumeShell(endpoint) as resumed:
                resumed.start()
                resumed.suspend()
                ping(original.server, "shell-job-stopped")
                resumed.foreground()
                ping(original.server, "shell-job-foreground")
            until(lambda: not endpoint.exists(), "shell Ctrl-C did not remove the resume endpoint")
            endpoint = None
    finally:
        cleanup(endpoint)
        if transfer_peer is not None:
            transfer_peer.close()
    print("background live: PING while detached, during partial handshake and stopped shell job; preserved paced queue with no duplicates; 128 KiB reviewed DCC reception while detached; retained history/draft; exactly one draft send; repeated detach; Ctrl-Z/fg/Ctrl-C terminal restoration and clean exit passed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--live", action="store_true")
    args = parser.parse_args()
    if args.offline or not args.live:
        offline_roundtrip()
    if args.live or not args.offline:
        live_roundtrip()


if __name__ == "__main__":
    main()
