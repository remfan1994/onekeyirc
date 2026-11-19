#!/usr/bin/env python3
"""Real-PTY idle wrapping regressions with a private localhost IRC peer.

Checks exact transmitted text separately from the composer tail presentation.
No public server, persistent logging, or background session is used.
"""
import argparse
import json
import os
from pathlib import Path

import test_idle_terminal as terminal


def wrapped_tail(full, composer):
    with terminal.IdleSession(live=True, review=False, rows=18, cols=80,
                              full=full, composer=composer,
                              config_extra="[output]\npersistent_logging=false\n") as session:
        session.start()
        message = "FIRST_ROW_" + "abcdefghij " * 8 + "SECOND_ROW_END"
        session.type(message)
        session.wait(lambda: session.editor(count=len(message)), "sent-text draft was not drawn")
        session.type(b"\r")
        session.wait(lambda: len(session.peer.messages()) == 1 and not session.screen.alternate,
                     "message did not send and return to the watch log")
        assert session.peer.messages()[0][1] == message.encode(), "wire text was truncated"
        session.wait(lambda: "SECOND_ROW_END" in session.screen.text(),
                     "normal watch log lost the sent message ending")
        session.type("z")
        session.wait(lambda: session.editor(count=1), "next input field did not open")
        assert "SECOND_ROW_END" in session.screen.text(), (
            "opening input clipped the sent message continuation", composer, session.screen.text())

        # A record taller than the tail must retain its newest visual rows,
        # and a following record must remain below that partial older record.
        long_message = "OLD_RECORD_START_" + "r" * 380 + "_LONG_END"
        session.peer.send(":alice!u@local PRIVMSG #test :" + long_message)
        session.wait(lambda: "_LONG_END" in session.screen.text(), "long record ending was clipped")
        if composer == "fullscreen":
            assert "OLD_RECORD_START_" not in session.screen.text(), (
                "limited tail kept the beginning instead of the newest rows", session.screen.text())
        session.peer.send(":alice!u@local PRIVMSG #test :AFTER_LONG")
        session.wait(lambda: "AFTER_LONG" in session.screen.text(), "newest record was not shown")
        view = session.screen.text()
        assert "_LONG_END" in view and view.index("_LONG_END") < view.index("AFTER_LONG"), view

        # Byte count differs from cell width, and the narrower resize puts a
        # combining sequence at a row boundary. Count/paint must agree.
        unicode_message = "WIDE_" + "\u754c" * 10 + "e\u0301" * 8 + "_UNICODE_END"
        session.peer.send(":alice!u@local PRIVMSG #test :" + unicode_message)
        session.wait(lambda: "_UNICODE_END" in session.screen.text(), "Unicode tail ending was clipped")
        for columns in (50, 80):
            session.resize(18, columns)
            session.wait(lambda: "_UNICODE_END" in session.screen.text(), "resize lost wrapped Unicode text")
            view = session.screen.text()
            assert view.count("\u754c") == 10 and view.count("e\u0301") == 8, view
            assert session.screen.cursor_visible, "tail repaint hid the input caret"

        session.cancel()
        session.command("/channel #test")
        session.wait(lambda: "_UNICODE_END" in session.screen.text(),
                     "same-channel selection lost retained history")
        assert [text for _, text in session.peer.messages()] == [message.encode()], (
            "tail repaint sent the unfinished draft")
        assert not list(session.directory.rglob("*.log")), "test enabled persistent logging"
    return {"case": "wrapped_tail", "composer": composer, "wire_exact": True,
            "message_bytes": len(message), "unicode_resize": True}


def exact_margin_newline(full, composer):
    with terminal.IdleSession(live=True, review=False, rows=18, cols=80,
                              full=full, composer=composer,
                              config_extra="[output]\npersistent_logging=false\n") as session:
        session.start()
        # The shared startup helper checks the byte-count label, which is
        # intentionally clipped in a 40-column line-layout header.
        session.resize(18, 40)
        first, second = "A" * 39, "SECOND_LINE_END"
        message = first + "\n" + second
        session.type(terminal.BEGIN + message.encode() + terminal.END)
        expected_row = 17 if composer == "line" else 5
        session.wait(lambda: session.screen.alternate and
                     (session.screen.row, session.screen.col) == (expected_row, len(second)),
                     "exact-margin newline moved the caret to the wrong row")
        shown = "".join(session.screen.cells[expected_row]).rstrip()
        # Send first, so even a failing baseline separately proves that the
        # hidden row was retained and delivered as the second IRC message.
        marker = len(session.raw)
        session.type(b"\r")
        # Message review owns a temporary alternate screen until confirmation.
        session.wait(lambda: session.screen.alternate and
                     "Send these messages?" in session.screen.text() and
                     b"2 messages" in session.raw[marker:] and b"Send these messages?" in session.raw[marker:],
                     "two logical lines did not require their existing split-send review")
        assert not session.peer.messages(), "split draft sent before review confirmation"
        session.quiet(.3)
        session.type(b"\r")
        session.wait(lambda: len(session.peer.messages()) == 2, "both logical lines were not sent")
        assert [text for _, text in session.peer.messages()] == [first.encode(), second.encode()], (
            "logical-line transmission changed", session.peer.messages())
        assert shown == second, ("exact-margin newline hid or displaced the second input line",
                                 composer, shown, second)
        assert not list(session.directory.rglob("*.log")), "test enabled persistent logging"
    return {"case": "exact_margin_newline", "composer": composer, "wire_exact": True,
            "messages": 2, "caret_matches_text": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path,
                        help="binary to test; existing ONEKEY_BINARY/VIPIRC_IDLE_BINARY envs also work")
    parser.add_argument("--full", action="store_true", help="test vipirc -i, using its actual idle frontend")
    parser.add_argument("--case", choices=("tail", "newline"))
    args = parser.parse_args()
    if args.binary:
        binary = args.binary.resolve()
        assert binary.is_file() and os.access(binary, os.X_OK), binary
        if args.full:
            terminal.FULL_BINARY = binary
        else:
            terminal.BINARY = binary
    results = []
    for composer in ("line", "fullscreen"):
        if args.case in (None, "tail"):
            result = wrapped_tail(args.full, composer)
            results.append(result)
            print(json.dumps(result, sort_keys=True), flush=True)
        if args.case in (None, "newline"):
            result = exact_margin_newline(args.full, composer)
            results.append(result)
            print(json.dumps(result, sort_keys=True), flush=True)
    print(json.dumps({"result": "PASS", "frontend": "vipirc -i" if args.full else "onekeyirc",
                      "cases": len(results)}, sort_keys=True), flush=True)


if __name__ == "__main__":
    main()
