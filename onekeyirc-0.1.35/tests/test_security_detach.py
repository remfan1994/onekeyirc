#!/usr/bin/env python3
"""Actual PTY detach-request boundary test; prepare is a rejecting C fixture.
No AF_UNIX endpoint is created and no background worker is forked.
"""
import argparse
import json
import os
from pathlib import Path
import tempfile
import time

import test_idle_terminal as terminal


def wait_without_pty(predicate, message, seconds=3):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    assert predicate(), message


def run(binary):
    terminal.BINARY = binary
    with tempfile.TemporaryDirectory(prefix="idle-boundary-") as directory:
        marker = Path(directory) / "prepare-reached.txt"
        previous = os.environ.get("VIPIRC_TEST_PREPARE_MARKER")
        os.environ["VIPIRC_TEST_PREPARE_MARKER"] = str(marker)
        try:
            with terminal.IdleSession(live=True, config_extra=
                    "[output]\npersistent_logging=false\nprogram_messages=false\n") as session:
                session.start()
                # A failed /background must still emit a queued exit from
                # the alternate screen and display its preparation error.
                session.command("/background", expected="Fixture refused background preparation.")
                assert marker.exists() and not session.screen.alternate
                marker.unlink()
                session.type("UNSENT-DRAFT-SAFETY")
                session.wait(lambda: session.editor("UNSENT-DRAFT-SAFETY"), "composer did not open")
                session.type(b"\x1b")
                session.wait(lambda: not session.screen.alternate, "editor Escape did not cancel")
                assert not marker.exists(), "editor Escape attempted to detach"
                session.type(terminal.BEGIN + b"/background\r\n\x1b" + terminal.END)
                session.wait(lambda: session.editor(), "framed paste did not stay in a literal editor")
                assert not marker.exists(), "framed paste attempted to detach"
                session.type(b"\x1b")
                session.wait(lambda: not session.screen.alternate, "pasted draft did not cancel")
                session.quiet(0.7)
                assert not marker.exists()

                for batch in range(40):
                    session.peer.send_bytes(b"".join(
                        b":Attacker!u@local PRIVMSG #test :flood-%04d " % (batch*50+n) +
                        b"x"*300 + b"\r\n" for n in range(50)))
                session.peer.send(":Attacker!u@local PRIVMSG #test :DETACH-HISTORY-MARKER")
                session.peer.send("PING :detach-pressure-prepared")
                wait_without_pty(lambda: any(line == b"PONG :detach-pressure-prepared"
                                             for _, line in session.peer.lines),
                                 "flood was not processed while terminal output was blocked")
                started = time.monotonic()
                session.type(b"\x1b")
                try:
                    wait_without_pty(marker.exists,
                                     "explicit detach never reached prepare behind terminal backlog")
                    latency = time.monotonic() - started
                    details = marker.read_text()
                    assert "marker=1" in details, ("canonical history lost before preparation", details)
                    assert session.proc.poll() is None
                    session.wait(lambda: b"Fixture refused background preparation." in session.raw[-80000:],
                                 "preparation error was not displayed after output recovered", timeout=5)
                    session.peer.send("PING :detach-failure-alive")
                    session.wait(lambda: any(line == b"PONG :detach-failure-alive"
                                              for _, line in session.peer.lines),
                                 "failed detach stopped IRC processing")
                    assert not session.screen.alternate
                    assert not session.peer.messages(), "draft or pasted command was sent"
                    return {"result": "PASS", "prepare_delay_ms": round(latency*1000, 3),
                            "pty_unread_until_prepare": True, "canonical_history_retained": True,
                            "prepare_error_displayed": True, "editor_and_paste_do_not_detach": True,
                            "af_unix_used": False, "background_worker_created": False}
                finally:
                    # Interrupt before draining on failure; the fixture never
                    # forks, and this also prevents delayed-request side effects.
                    if session.proc.poll() is None:
                        session.type(b"\x03")
                        session.wait(lambda: session.proc.poll() is not None, "fixture did not exit")
        finally:
            if previous is None:
                os.environ.pop("VIPIRC_TEST_PREPARE_MARKER", None)
            else:
                os.environ["VIPIRC_TEST_PREPARE_MARKER"] = previous


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True,
                        help="binary compiled with tests/test_idle_background_boundary.c and IDLE_BOUNDARY_FIXTURE")
    args = parser.parse_args()
    binary = args.fixture.resolve()
    assert binary.is_file() and os.access(binary, os.X_OK), binary
    print(json.dumps(run(binary), sort_keys=True), flush=True)


if __name__ == "__main__":
    main()
