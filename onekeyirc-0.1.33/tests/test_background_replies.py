#!/usr/bin/env python3
"""Resume-waiter reply ordering through owned PTYs and private local peers.

The peer receives the complete terminal request before stopping our child.
It queues replies and closes its receive side before delivering HUP/CONT, so
the waiter must preserve queued replies after its control write fails.
No IRC server, curses screen, background registry, or user config is used.
"""
import argparse
import array
import errno
import fcntl
import os
from pathlib import Path
import pty
import signal
import socket
import subprocess
import tempfile
import termios
import time

ROOT = Path(__file__).resolve().parents[1]


def controlling_terminal():
    os.setsid()
    fcntl.ioctl(0, termios.TIOCSCTTY, 0)


def reply_case(binary, label, replies, expected, keep_write_open=False):
    with tempfile.TemporaryDirectory(prefix="vipirc-waiter-") as directory:
        endpoint = Path(directory) / "session"
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
            listener.bind(str(endpoint))
            endpoint.chmod(0o600)
            listener.listen(1)
            listener.settimeout(5)
            master, slave = pty.openpty()
            before = termios.tcgetattr(slave)
            env = dict(os.environ, TERM="xterm-256color", LANG="C", LC_ALL="C")
            proc = subprocess.Popen([str(binary), "--resume", str(endpoint)],
                                    stdin=slave, stdout=slave, stderr=slave,
                                    cwd=ROOT, env=env, preexec_fn=controlling_terminal)
            os.close(slave)
            stopped = False
            try:
                peer, _ = listener.accept()
                with peer:
                    peer.settimeout(5)
                    request, ancillary, flags, _ = peer.recvmsg(
                        288, socket.CMSG_SPACE(3 * array.array("i").itemsize))
                    descriptors = array.array("i")
                    try:
                        for level, kind, data in ancillary:
                            assert (level, kind) == (socket.SOL_SOCKET, socket.SCM_RIGHTS)
                            descriptors.frombytes(data)
                        assert len(descriptors) == 3 and not flags
                        assert all(os.isatty(fd) for fd in descriptors)
                        while len(request) < 288:
                            more = peer.recv(288 - len(request))
                            assert more, "incomplete attachment request"
                            request += more
                        assert request[:8] == b"VIPIRCB4" and len(request) == 288
                        assert request[280] in (0, 1, 2), request[280]
                        # Receipt of this request proves the waiter installed
                        # its handlers. This is our Popen child, never a PID
                        # inferred from a job number or an external file.
                        os.kill(proc.pid, signal.SIGSTOP)
                        pid, status = os.waitpid(proc.pid, os.WUNTRACED)
                        assert pid == proc.pid and os.WIFSTOPPED(status)
                        stopped = True
                        peer.sendall(replies)
                    finally:
                        for descriptor in descriptors:
                            os.close(descriptor)
                    if keep_write_open:
                        # Failed writes do not imply peer EOF. A queued S
                        # must not suspend a waiter that cannot send C back;
                        # an absent terminal acknowledgment must time out.
                        peer.shutdown(socket.SHUT_RD)
                    else:
                        peer.close()
                    started = time.monotonic()
                    os.kill(proc.pid, signal.SIGHUP)
                    os.kill(proc.pid, signal.SIGCONT)
                    stopped = False
                    proc.wait(timeout=7)
                    elapsed = time.monotonic() - started
                    assert elapsed < 7, (label, "reply drain was not bounded", elapsed)
                after = termios.tcgetattr(master)
                mask = termios.ICANON | termios.ECHO | termios.ISIG
                assert after[3] & mask == before[3] & mask, (label, "terminal mode changed")
                os.set_blocking(master, False)
                output = bytearray()
                while True:
                    try:
                        chunk = os.read(master, 4096)
                    except BlockingIOError:
                        break
                    except OSError as error:
                        if error.errno == errno.EIO:
                            break
                        raise
                    if not chunk:
                        break
                    output.extend(chunk)
                    assert len(output) <= 65536, (label, "unexpectedly large waiter output")
                assert b"Sanitizer" not in output and b"runtime error:" not in output, (
                    label, "sanitizer diagnostic", bytes(output))
                print("background reply %s: expected=%d actual=%d elapsed=%.3fs" %
                      (label, expected, proc.returncode, elapsed), flush=True)
                if proc.returncode != expected:
                    print("waiter output:", bytes(output), flush=True)
                return proc.returncode == expected
            finally:
                if stopped and proc.poll() is None:
                    os.kill(proc.pid, signal.SIGCONT)
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=5)
                os.close(master)


def legacy_fallback_case(binary):
    with tempfile.TemporaryDirectory(prefix="vipirc-legacy-waiter-") as directory:
        endpoint = Path(directory) / "session"
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
            listener.bind(str(endpoint)); endpoint.chmod(0o600); listener.listen(2); listener.settimeout(5)
            master, slave = pty.openpty()
            env = dict(os.environ, TERM="xterm-256color", LANG="C", LC_ALL="C")
            proc = subprocess.Popen([str(binary), "--resume", str(endpoint)],
                                    stdin=slave, stdout=slave, stderr=slave,
                                    cwd=ROOT, env=env, preexec_fn=controlling_terminal)
            os.close(slave)
            try:
                for attempt, (size, magic) in enumerate(((288, b"VIPIRCB4"), (264, b"VIPIRCB1"))):
                    peer, _ = listener.accept()
                    with peer:
                        peer.settimeout(5)
                        request, ancillary, flags, _ = peer.recvmsg(
                            size, socket.CMSG_SPACE(3 * array.array("i").itemsize))
                        descriptors = array.array("i")
                        try:
                            for level, kind, data in ancillary:
                                assert (level, kind) == (socket.SOL_SOCKET, socket.SCM_RIGHTS)
                                descriptors.frombytes(data)
                            assert len(descriptors) == 3 and not flags
                            while len(request) < size:
                                more = peer.recv(size - len(request)); assert more
                                request += more
                            assert request[:8] == magic and len(request) == size, (attempt, request[:8], len(request))
                            if not attempt:
                                peer.sendall(b"E\x02")
                            else:
                                peer.sendall(b"A\0D\0")
                        finally:
                            for descriptor in descriptors: os.close(descriptor)
                proc.wait(timeout=7)
                os.set_blocking(master, False)
                output = bytearray()
                while True:
                    try: chunk = os.read(master, 4096)
                    except BlockingIOError: break
                    except OSError as error:
                        if error.errno == errno.EIO: break
                        raise
                    if not chunk: break
                    output.extend(chunk)
                assert proc.returncode == 0, (proc.returncode, bytes(output))
                assert b"older resume protocol" in output, bytes(output)
                print("background legacy fallback: new frontend request rejected, established attach protocol recovered session passed", flush=True)
            finally:
                if proc.poll() is None:
                    proc.kill(); proc.wait(timeout=5)
                os.close(master)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path,
                        default=Path(os.environ.get("VIPIRC_BINARY", str(ROOT / "vipirc"))))
    args = parser.parse_args()
    binary = args.binary.resolve()
    assert binary.is_file() and os.access(binary, os.X_OK)
    cases = [
        ("queued-detach", b"A\0D\0", 0, False),
        ("queued-quit-status", b"A\0Q\x82", 130, False),
        ("eof-without-detach", b"A\0", 1, False),
        ("partial-detach", b"A\0D", 1, False),
        ("malformed-reply", b"A\0X\0", 1, False),
        ("detach-before-attach", b"D\0", 1, False),
        ("quit-before-attach", b"Q\0", 1, False),
        ("queued-suspend-timeout", b"A\0S\0", 1, True),
    ]
    failures = [label for label, replies, expected, keep in cases
                if not reply_case(binary, label, replies, expected, keep)]
    legacy_fallback_case(binary)
    assert not failures, ("reply-order regressions", failures)
    print("all background reply-order checks passed", flush=True)


if __name__ == "__main__":
    main()
