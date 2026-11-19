#!/usr/bin/env python3
"""Real AF_UNIX/PTY/localhost-IRC SSH-loss and numbered-resume lifecycle.

Every job and foreground waiter affected is created by this test. On a host
that denies AF_UNIX, exit 77 reports unavailable coverage rather than a pass.
"""
import argparse
import errno
import json
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import tempfile
import time

import test_idle_terminal as terminal
from test_background_start import GuiBackgroundSession, wait_registered
from test_integration import IRCFixture


def ipc_available(directory):
    path = str(directory / "probe")
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
            probe.bind(path)
            probe.listen(1)
        os.unlink(path)
        return True
    except OSError as error:
        if error.errno not in (errno.EPERM, errno.EACCES, errno.EAFNOSUPPORT,
                              errno.EPROTONOSUPPORT):
            raise
        print(json.dumps({"result": "UNAVAILABLE", "reason": str(error),
                          "af_unix_handshake_tested": False}), flush=True)
        return False


def close_pty(session):
    """Cleanup after a real master hangup cannot query the dead tty settings."""
    if session.proc.poll() is None:
        session.proc.terminate()
        try:
            session.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            session.proc.kill()
            session.proc.wait(timeout=2)
    for name in ("master", "slave"):
        fd = getattr(session, name)
        if fd >= 0:
            os.close(fd)
            setattr(session, name, -1)
    session.temp.cleanup()


def run(binary, full):
    terminal.BINARY = binary
    frontend = ["-N"] if full else ["-i"]
    session_class = GuiBackgroundSession if full else terminal.IdleSession
    peer = IRCFixture()
    job = None
    attached = None
    results = []
    with tempfile.TemporaryDirectory(prefix="session-hangup-") as temporary:
        directory = Path(temporary)
        if not ipc_available(directory):
            peer.close()
            return 77
        config = directory / "config.ini"
        config.write_text(
            "[connection]\nserver=127.0.0.1\nport=%d\ntls=false\nnick=tester\n"
            "username=tester\nchannel=#test\nautoconnect=true\nauth=none\n"
            "[history]\nlines=50\n[output]\npersistent_logging=false\n"
            "[session]\nsingle_session=false\n" % peer.port)
        config.chmod(0o600)
        original = config.read_bytes()
        env = dict(os.environ, HOME=str(directory), XDG_CONFIG_HOME=str(directory / "unused"))

        def command(*options, expected=0):
            result = subprocess.run([str(binary), *map(str, options)], input=b"",
                                    capture_output=True, env=env, cwd=directory,
                                    start_new_session=True, timeout=10)
            assert result.returncode == expected, (options, result.returncode,
                                                    result.stdout, result.stderr)
            return result.stdout + result.stderr

        def state(wanted):
            output = command("--sessions")
            return bool(re.search(rb"^\s*" + str(job).encode() + rb"\s+" + wanted,
                                  output, re.M))

        def wait_state(wanted):
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if state(wanted):
                    return
                time.sleep(.03)
            raise AssertionError(("job did not reach expected state", job, wanted,
                                  command("--sessions")))

        def ping(marker):
            peer.send("PING :" + marker)
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                if any(line == b"PONG :" + marker.encode() for _, line in peer.lines):
                    return
                time.sleep(.01)
            raise AssertionError(("IRC connection did not survive terminal loss", marker, peer.lines[-8:]))

        def attach():
            session = session_class(args=[*frontend, "--resume", str(job)],
                                    rows=28, cols=120, locale="C.UTF-8")
            try:
                session.wait(lambda: b"\x1b[?2004h" in session.raw,
                             "numbered resume did not restore a terminal")
                if full:
                    session.wait(session.main_frame_ready, "full frontend did not redraw after resume")
                session.command("/channel #test")
                wait_state(rb"attached(?:/busy)?\b")
                return session
            except BaseException:
                close_pty(session)
                raise

        try:
            output = command(*frontend, "--background", "--config", config)
            match = re.search(rb"\b(?:onekeyirc|vipirc) job ([1-9][0-9]*) is running", output)
            assert match, ("startup did not return its owned job number", output)
            job = int(match[1])
            wait_registered(peer, "#test")
            wait_state(rb"detached\b")
            for mode in ("master-close", "waiter-hup", "waiter-exit"):
                attached = attach()
                before = "BEFORE-" + mode
                peer.send(":Attacker!u@local PRIVMSG #test :" + before)
                attached.wait(lambda: before in attached.screen.text() or before.encode() in attached.raw,
                              "pre-hangup message did not reach the attached terminal")
                started = time.monotonic()
                if mode == "master-close":
                    os.close(attached.master)
                    attached.master = -1
                elif mode == "waiter-hup":
                    attached.proc.send_signal(signal.SIGHUP)
                else:
                    # This PID belongs to our resume waiter, never a worker ID
                    # inferred from a session number or registry pathname.
                    attached.proc.kill()
                attached.proc.wait(timeout=5)
                wait_state(rb"detached\b")
                elapsed = time.monotonic() - started
                ping("after-" + mode)
                close_pty(attached)
                attached = None
                marker = "WHILE-DETACHED-" + mode
                peer.send(":Attacker!u@local PRIVMSG #test :" + marker)
                ping("retained-" + mode)
                attached = attach()
                attached.wait(lambda: marker in attached.screen.text() or marker.encode() in attached.raw,
                              "reattachment lost messages received while detached")
                # Release through the real controller without relying on UI
                # key sequences that differ between the two frontends.
                attached.proc.send_signal(signal.SIGHUP)
                attached.wait(lambda: attached.proc.poll() is not None,
                              "reattached waiter did not detach on HUP")
                wait_state(rb"detached\b")
                close_pty(attached)
                attached = None
                results.append({"case": mode, "detach_ms": round(elapsed * 1000, 3)})
            # A new shell can explicitly detach an attached waiter without
            # ending the worker or its IRC connections, then resume it again.
            attached = attach()
            peer.send(":Attacker!u@local PRIVMSG #test :BEFORE-EXTERNAL-DETACH")
            attached.wait(lambda: "BEFORE-EXTERNAL-DETACH" in attached.screen.text() or
                          b"BEFORE-EXTERNAL-DETACH" in attached.raw,
                          "pre-detach message did not reach attached terminal")
            command("--detach-session", job)
            attached.wait(lambda: attached.proc.poll() is not None,
                          "old resume waiter did not exit after --detach-session")
            wait_state(rb"detached\b")
            ping("after-external-detach")
            close_pty(attached); attached = None
            peer.send(":Attacker!u@local PRIVMSG #test :WHILE-EXTERNALLY-DETACHED")
            ping("retained-external-detach")
            attached = attach()
            attached.wait(lambda: "WHILE-EXTERNALLY-DETACHED" in attached.screen.text() or
                          b"WHILE-EXTERNALLY-DETACHED" in attached.raw,
                          "--detach-session lost retained IRC traffic")
            attached.proc.send_signal(signal.SIGHUP)
            attached.wait(lambda: attached.proc.poll() is not None,
                          "reattached waiter did not detach after external recovery")
            wait_state(rb"detached\b")
            close_pty(attached); attached = None
            results.append({"case": "external-detach", "detach_ms": 0})
            assert not peer.messages(), "test sent an unexpected chat message"
            assert config.read_bytes() == original
            assert not list(directory.rglob("*.log")), "persistent logging was enabled"
            command("--close-session", job)
            assert not re.search(rb"^\s*" + str(job).encode() + rb"\s+", command("--sessions"), re.M)
            job = None
        finally:
            # Close the exact job returned by this test, even if it is still
            # attached. Never enumerate-and-stop other users' or test jobs.
            try:
                if job is not None:
                    command("--close-session", job)
            finally:
                if attached is not None:
                    close_pty(attached)
                peer.close()
    print(json.dumps({"result": "PASS", "frontend": "full" if full else "idle",
                      "af_unix_handshake_tested": True, "cases": results}, sort_keys=True))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--full", action="store_true", help="use the full vipirc frontend")
    args = parser.parse_args()
    binary = args.binary.resolve()
    assert binary.is_file() and os.access(binary, os.X_OK), binary
    raise SystemExit(run(binary, args.full))


if __name__ == "__main__":
    main()
