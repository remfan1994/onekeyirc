#!/usr/bin/env python3
"""Practical away, keyed-join and ignore workflows in isolated real PTYs."""
import re
import sys

from test_integration import Session
from test_offline import OfflineTerminal
from test_input_limits import wait_bytes


def header(session):
    return session.app_line("Connection:").lower()


def processed(session, token):
    session.server.send("PING :" + token)
    session.wait(lambda: any(line == ("PONG :" + token).encode("ascii")
                            for unused, line in session.server.lines),
                 "chat workflow stopped processing PING: " + token)


def offline_basics():
    with OfflineTerminal() as session:
        session.start()
        session.command("/away checking the offline interface")
        session.open_app_diagnostics()
        session.wait(lambda: "away" in header(session), "offline server did not acknowledge away")
        session.close_app_diagnostics()
        session.command("/back")
        session.open_app_diagnostics()
        session.wait(lambda: "away" not in header(session), "offline server did not clear away")
        session.close_app_diagnostics()
        session.command("/join #key-test offline-key")
        session.wait(lambda: "#key-test" in "".join(session.screen.cells[2]),
                     "keyed join did not work in simulation")
        wait_bytes(session, 0)
        assert "offline-key" not in session.screen.text(), "join key leaked into history/status"
        session.command("/ignore add Noisy")
        session.wait(lambda: "Ignore mask added" in session.screen.text(),
                     "offline ignore command did not report the rule")
        session.command("/ignore list")
        session.wait(lambda: "Noisy" in session.screen.text(), "offline ignore list omitted its rule")
        session.command("/unignore Noisy")
        session.quiet(0.08)
        wait_bytes(session, 0)
        session.command("/channel #key-test")
        session.wait(lambda: "#key-test" in "".join(session.screen.cells[2]), "keyed channel did not reopen")
        draft = "preserve this conversation draft"
        session.type(draft)
        wait_bytes(session, len(draft))
        from test_servers import open_servers, choose_action, SERVER_ACTIONS
        from test_settings import open_settings, select_field, WATCH_FIRST_FIELD
        open_servers(session)
        choose_action(session, "Away", SERVER_ACTIONS)
        session.wait(lambda: "away" in session.screen.text().lower(), "server Away did not take effect")
        choose_action(session, "Back", SERVER_ACTIONS)
        session.wait(lambda: session.is_main(), "server Back did not return")
        session.open_app_diagnostics()
        session.wait(lambda: "away" in header(session), "Away did not update the current server")
        session.close_app_diagnostics()
        wait_bytes(session, len(draft))
        assert "#key-test" in "".join(session.screen.cells[2])
        open_servers(session)
        choose_action(session, "Away", SERVER_ACTIONS)
        choose_action(session, "Back", SERVER_ACTIONS)
        session.wait(lambda: session.is_main(), "server Back did not return")
        session.open_app_diagnostics()
        session.wait(lambda: "away" not in header(session), "server Away did not return from away")
        session.close_app_diagnostics()
        wait_bytes(session, len(draft))
        open_settings(session)
        select_field(session, WATCH_FIRST_FIELD + 2)
        session.wait(lambda: "Add ignore mask" in session.screen.text(), "Settings omitted ignore-mask editing")
        session.type(b"\x1b")
        session.wait(lambda:session.panel_title().startswith("vipirc |"),"ignore Settings did not return to vipirc")
        session.leave_app()
        session.wait(lambda: "#key-test" in "".join(session.screen.cells[2]), "ignore Settings lost the current view")
        wait_bytes(session, len(draft))
        assert session.config.read_bytes() == session.before_config
    print("chat basics offline: away/back acknowledgments, keyed join, local ignore commands and nonwriting behavior passed")


def live_basics():
    with Session() as session:
        session.start()
        session.command("/away out for lunch")
        session.wait(lambda: any(line == b"AWAY :out for lunch" for unused, line in session.server.lines),
                     "away text did not reach the local server")
        session.open_app_diagnostics()
        assert "away" not in header(session), "away was shown before server acknowledgment"
        session.server.send(":srv 306 tester :You have been marked as being away")
        session.wait(lambda: "away" in header(session), "away acknowledgment did not update the interface")
        session.close_app_diagnostics()
        session.command("/back")
        session.open_app_diagnostics()
        session.wait(lambda: any(line == b"AWAY" for unused, line in session.server.lines),
                     "back did not clear away at the server")
        assert "away" in header(session), "away cleared before acknowledgment"
        session.server.send(":srv 305 tester :You are no longer marked as being away")
        session.wait(lambda: "away" not in header(session), "back acknowledgment did not update the interface")
        session.close_app_diagnostics()

        secret = "temporary-channel-key"
        session.command("/join #locked " + secret)
        session.wait(lambda: any(line == ("JOIN #locked " + secret).encode("ascii")
                                for unused, line in session.server.lines),
                     "keyed JOIN wire form was incorrect")
        session.wait(lambda: "#locked" in "".join(session.screen.cells[2]), "keyed channel did not open")
        wait_bytes(session, 0)
        assert secret not in session.screen.text(), "join key appeared in history/status after submission"
        session.command("/save")
        session.wait(lambda: "Saved server 0 (including application preferences)." in session.screen.text(),
                     "saving after keyed join did not confirm the selected primary profile")
        saved = session.config.read_text(encoding="utf-8")
        assert secret not in saved and "#locked" in saved, "key was retained or channel name lost"

        session.command("/channel 1")
        session.wait(lambda: "#test" in "".join(session.screen.cells[2]), "test channel did not reopen")
        session.command("/ignore add Noisy")
        session.wait(lambda: "Ignore mask added" in session.screen.text(),
                     "ignore command did not report the rule")
        wait_bytes(session, 0)
        session.server.send(":Noisy!u@bad PRIVMSG tester :BLOCKED_PRIVATE_PAYLOAD")
        session.server.send(":Noisy!u@bad PRIVMSG #test :tester BLOCKED_MENTION_PAYLOAD")
        session.server.send(":Noisy!u@bad NOTICE tester :BLOCKED_NOTICE_PAYLOAD")
        session.server.send(":Noisy!u@bad JOIN :#test")
        session.server.send(":srv.local NOTICE tester :SERVER_NOTICE_REMAINS")
        processed(session, "ignore-processed")
        assert b"BLOCKED_" not in session.raw, "an ignored message reached terminal output"
        session.command("/ql")
        session.wait(lambda: "Query list" in session.screen.text(), "query list did not open")
        assert "Noisy" not in session.screen.text(), "ignored private message created a query view"
        session.type(b"\x1b")
        session.wait(lambda: "#test" in "".join(session.screen.cells[2]), "query list failed to return")
        assert re.search(r"\bMentions/unread \([1-9]\d*/", "".join(session.screen.cells[0])) is None, \
            "ignored text created a mention alert"
        session.command("/users on")
        session.wait(lambda: "Users:" in session.screen.text(), "roster did not open")
        assert "Noisy" in session.screen.text(), "ignore damaged membership updates"
        session.command("/server")
        session.wait(lambda: "SERVER_NOTICE_REMAINS" in session.screen.text(), "ignore hid a bare server notice")

        session.command("/unignore Noisy")
        session.quiet(0.08)
        wait_bytes(session, 0)
        session.server.send(":Noisy!u@bad PRIVMSG tester :ALLOWED_PRIVATE_PAYLOAD")
        processed(session, "unignore-processed")
        session.command("/ql")
        session.wait(lambda: "Query list" in session.screen.text() and "Noisy" in session.screen.text(),
                     "unignored private conversation did not appear")
        session.type(b"\r")
        session.wait(lambda: "ALLOWED_PRIVATE_PAYLOAD" in session.screen.text(), "unignored text was not retained")
        assert not session.server.messages(), "chat workflow unexpectedly sent a message"
    print("chat basics live: acknowledged away state, keyed JOIN without persistence, ignore/query/mention boundaries, membership/server notices and unignore passed")


def main():
    if "--live-only" not in sys.argv:
        offline_basics()
    if "--offline-only" not in sys.argv:
        live_basics()
    print("all practical chat workflow tests passed")


if __name__ == "__main__":
    main()
