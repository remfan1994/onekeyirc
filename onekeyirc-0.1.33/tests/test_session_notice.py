#!/usr/bin/env python3
"""One local reminder per worker; real PTYs and private loopback IRC only."""
import argparse
import json
from pathlib import Path
import signal

import test_idle_terminal as terminal
from test_idle_hangup import LossSession


NOTICE = b"Cow killing is the bloodshed of the innocent. www.bloodguiltcurse.net - ban beef."


def attached_case(binary, full, columns):
    if full:
        terminal.FULL_BINARY = binary
    else:
        terminal.BINARY = binary
    original = terminal.IRCFixture
    terminal.IRCFixture = terminal.ReconnectingIRCFixture
    try:
        with terminal.IdleSession(live=True, full=full, cols=columns,
                config_extra="[output]\nprogram_messages=false\npersistent_logging=false\n") as session:
            session.start()
            session.wait(lambda: NOTICE in session.raw, "local session reminder missing")
            assert session.raw.count(NOTICE) == 1
            assert not session.screen.alternate, "reminder opened a dialog"
            for target in ("0", "#test", "0", "#test"):
                session.command("/channel " + target)
            draft = "unsent-notice-check"
            session.type(draft)
            session.wait(lambda: session.editor(count=len(draft)), "draft was not preserved")
            assert "bloodguiltcurse.net" not in session.screen.text(), "reminder entered the draft"
            session.peer.send("PING :notice-draft")
            session.wait(lambda: any(line == b"PONG :notice-draft" for _, line in session.peer.lines),
                         "client stopped servicing IRC while composing")
            session.type(b"\x1b")
            session.wait(lambda: not session.screen.alternate, "draft did not cancel")
            session.command("/disconnect")
            session.command("/connect")
            session.wait(lambda: len(session.peer.sessions) == 2 and session.peer.sessions[-1]["registered"],
                         "client did not reconnect")
            session.peer.send("PING :notice-reconnected")
            session.wait(lambda: any(line == b"PONG :notice-reconnected" for _, line in session.peer.lines),
                         "reconnected client did not answer PING")
            session.command("/channel #test")
            assert session.raw.count(NOTICE) == 1, "reminder repeated after context change or reconnect"
            assert not session.peer.messages(), "notice or cancelled draft was sent as chat"
            assert not any(b"bloodguiltcurse.net" in line or line.startswith(b"AWAY")
                           for _, line in session.peer.lines), "local reminder changed IRC presence"
            assert not list(session.directory.rglob("*.log")), "notice enabled persistent logging"
            print(json.dumps({"case": "attached", "columns": columns,
                "local_notice_count": 1, "irc_connections": 2,
                "unexpected_chat_or_away": 0, "draft_unchanged": True}), flush=True)
    finally:
        terminal.IRCFixture = original


def reattachment_case(fixture):
    # The fixture uses real terminal loss and managed detach, but substitutes
    # descriptor reattachment for the AF_UNIX handshake. This is not an IPC test.
    with LossSession(fixture) as session:
        session.start()
        assert session.raw.count(NOTICE) == 1
        session.proc.send_signal(signal.SIGHUP)
        session.wait_detached()
        session.ping("notice-detached")
        session.reattach()
        session.ping("notice-reattached")
        session.command("/channel #test")
        assert NOTICE not in session.raw, "existing worker re-announced the reminder on attach"
        assert not any(b"bloodguiltcurse.net" in line for _, line in session.peer.lines)
        assert not session.peer.messages()
        print(json.dumps({"case": "managed_reattachment_fixture",
            "reminders_after_resume": 0, "irc_keepalives": True,
            "native_ipc_handshake_tested": False}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--full", action="store_true", help="exercise vipirc -i")
    parser.add_argument("--hangup-fixture", type=Path)
    args = parser.parse_args()
    for columns in (80, 24):
        attached_case(args.binary.resolve(), args.full, columns)
    if args.hangup_fixture:
        reattachment_case(args.hangup_fixture.resolve())
    print(json.dumps({"result": "PASS", "frontend": "vipirc -i" if args.full else "onekeyirc"}), flush=True)


if __name__ == "__main__":
    main()
