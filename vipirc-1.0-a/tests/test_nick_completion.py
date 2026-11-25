#!/usr/bin/env python3
"""Tab nickname completion through real terminals; only offline/loopback peers."""
import argparse

from test_integration import Session
from test_offline import OfflineTerminal
from test_input_limits import (BEGIN_PASTE, END_PASTE, WARNING, acknowledge,
                               assert_no_send, expect_warning, wait_bytes)


def expect_text(session, text):
    session.wait(lambda:session.is_main() and text in session.input_text(),
                 "completed nickname text did not render: " + text)
    info = wait_bytes(session, len(text.encode("utf-8")))
    assert session.is_main() and session.header_focus() is None
    assert session.screen.cursor_visible, "nickname completion hid the caret"
    return info


def offline_completion():
    for term, locale in (("xterm-256color", "C.UTF-8"), ("screen", "C")):
        with OfflineTerminal(term=term, locale=locale) as session:
            session.start()
            session.type("as\t")
            expect_text(session, "Aster: ")
            session.type(b"\t" * 65 + b"\x1b[Z" * 64)
            session.quiet(0.04)
            expect_text(session, "Aster: ")
            assert_no_send(session)
            session.clear_input()
            session.type("hello bi\t")
            expect_text(session, "hello Birch ")
            session.type("says hello")
            expect_text(session, "hello Birch says hello")
            session.clear_input()
            session.type("hello CeTail world")
            wait_bytes(session, 18)
            session.type(session.home_key + b"\x1bOC" * 8 + b"\t")
            info = expect_text(session, "hello Cedar world")
            assert (info.row, info.col) == (1, 12), "mid-token completion moved the suffix or caret"
            session.clear_input()
            session.type("NobodyHere\t")
            expect_text(session, "NobodyHere")
            session.clear_input()
            session.type(BEGIN_PASTE + b"As\t" + END_PASTE)
            expect_text(session, "As ")
            session.clear_input()
            session.command("/mouse off")
            session.type("ce\t")
            expect_text(session, "Cedar: ")
            assert "Clear" not in "".join(session.screen.cells[-1])
            assert "Send" not in "".join(session.screen.cells[-1])
            assert_no_send(session)
    print("nickname completion offline: Tab/Shift-Tab remain input editing, held completion, middle-token caret, literal paste, no match, C/UTF-8 terminals and disabled mouse passed")


def barrier(session, token):
    session.server.send("PING :" + token)
    session.wait(lambda:any(line == ("PONG :" + token).encode("ascii")
                            for unused, line in session.server.lines),
                 "completion interrupted IRC service: " + token)


def live_candidates_and_budget():
    with Session() as session:
        session.start()
        session.server.send(":Alan!u@fixture JOIN :#test")
        session.server.send(":Alpine!u@fixture JOIN :#test")
        barrier(session, "completion-candidates")
        session.type("al\t")
        expect_text(session, "Alan: ")
        for text in ("alice: ", "Alpine: ", "Alan: "):
            session.type(b"\t")
            expect_text(session, text)
        session.type(b"\x1b[Z")
        expect_text(session, "Alpine: ")
        session.server.send(":Alan!u@fixture NICK :Zoe")
        barrier(session, "completion-nick-change")
        session.type(b"\x1b[Z")
        expect_text(session, "alice: ")
        session.type(b"\x1b[Z")
        expect_text(session, "Alpine: ")
        session.clear_input()
        session.command("/pm QueryPeer")
        session.type("qu\t")
        expect_text(session, "QueryPeer: ")
        session.clear_input()
        session.type("al\t")
        expect_text(session, "al")
        session.clear_input()
        session.command("/channel 1")
        budget=wait_bytes(session, 0).budget
        text="x" * (budget - 4) + " al"
        session.type(text)
        wait_bytes(session, len(text))
        assert WARNING not in session.screen.text()
        session.type(b"\t")
        session.wait(lambda:WARNING in session.screen.text(), "completion crossing the byte budget did not warn immediately")
        session.quiet(0.13)
        session.type(b"\x1b")
        session.wait(lambda:WARNING not in session.screen.text(), "completion budget warning could not be dismissed")
        expected="x" * (budget - 4) + " alice "
        wait_bytes(session, len(expected))
        assert not session.server.messages(), "nickname completion sent a message"
        assert not session.server.messages("QueryPeer"), "query completion sent a message"
        barrier(session, "completion-after-warning")
    print("nickname completion live: sorted bidirectional cycling, live NICK removal, query isolation, immediate byte warning and zero sends passed")


def main():
    parser=argparse.ArgumentParser()
    group=parser.add_mutually_exclusive_group()
    group.add_argument("--offline-only", action="store_true")
    group.add_argument("--live-only", action="store_true")
    args=parser.parse_args()
    if not args.live_only:
        offline_completion()
    if not args.offline_only:
        live_candidates_and_budget()


if __name__ == "__main__":
    main()
