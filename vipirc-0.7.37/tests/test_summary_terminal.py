#!/usr/bin/env python3
"""Local-loopback LIST and summary workflows through real terminal PTYs."""
import argparse
from pathlib import Path

import test_integration as gui
import test_idle_terminal as native


class DirectoryFixture(gui.IRCFixture):
    def handle(self, line):
        if line == b"LIST *offtopic":
            self.send(":srv 321 %s Channel :Users Name" % self.nick)
            self.send(":srv 322 %s #local-offtopic 23 :Directory fixture topic" % self.nick)
            self.send(":srv 323 %s :Directory fixture complete" % self.nick)
        else:
            super().handle(line)


def list_requests(peer):
    return [line for _, line in peer.lines if line == b"LIST" or line.startswith(b"LIST ")]


def gui_list_and_summary():
    gui.IRCFixture = DirectoryFixture
    with gui.Session(channel=False) as session:
        session.start()
        session.command("/list *offtopic")
        session.wait(lambda: list_requests(session.server) == [b"LIST *offtopic"],
                     "curses command screen did not send the exact LIST mask")
        session.wait(lambda: "Channel 0: Server" in session.panel_title() and
                     "#local-offtopic (23 users): Directory fixture topic" in session.screen.text(),
                     "curses LIST results were not rendered in channel 0")
        session.command("/query Alice")
        session.type("SUMMARY_KEEPS_THIS_DRAFT")
        session.wait(lambda: "SUMMARY_KEEPS_THIS_DRAFT" in session.input_text(), "draft was not entered")
        session.open_command()
        session.type("summary")
        session.wait(lambda: session.command_text() == "/summary", "summary command not composed")
        session.type(b"\r")
        session.wait(lambda: session.panel_title() == "Session summary" and
                     "Query 1: Alice" in session.screen.text(), "curses summary did not open")
        session.server.send("PING :summary-local-barrier")
        session.wait(lambda: any(line == b"PONG :summary-local-barrier" for _, line in session.server.lines),
                     "summary reader blocked the IRC connection")
        session.type("/")
        session.wait(lambda: session.in_command() and session.command_text() == "/",
                     "summary did not open its command prompt")
        session.type("join #never-sent")
        session.wait(lambda: session.command_text() == "/join #never-sent", "summary child command missing")
        session.type(b"\x1b")
        session.wait(lambda: session.panel_title() == "Session summary", "cancel did not restore summary")
        session.type("r")
        session.quiet(.05)
        assert session.panel_title() == "Session summary"
        session.type(b"\x1b")
        session.wait(lambda: session.panel_title().startswith("vipirc |"), "summary lost the originating main screen")
        session.leave_app()
        session.wait(lambda: "SUMMARY_KEEPS_THIS_DRAFT" in session.input_text(), "summary lost the chat draft")
        assert list_requests(session.server) == [b"LIST *offtopic"]
        assert not any(line.startswith((b"PRIVMSG ", b"JOIN #never-sent")) for _, line in session.server.lines)
    print("curses terminal: /list *offtopic from channel 0, exact mask and rendered replies, summary draft/parent retention, child-command cancellation and live PING handling passed", flush=True)


def native_list_and_summary(full):
    native.IRCFixture = DirectoryFixture
    with native.IdleSession(live=True, full=full, rows=26, cols=100) as session:
        session.start()
        session.command("/channel 0")
        marker = len(session.raw)
        session.command("/list *offtopic")
        session.wait(lambda: list_requests(session.peer) == [b"LIST *offtopic"],
                     "native command screen did not send the exact LIST mask")
        session.wait(lambda: b"#local-offtopic (23 users): Directory fixture topic" in session.raw[marker:],
                     "native LIST results were not printed in channel 0")
        session.command("/summary", "Session summary", keep_reader=True)
        session.wait(lambda: session.reader() and "Session summary" in session.screen.text(),
                     "native summary did not open its reader")
        session.peer.send("PING :native-summary-barrier")
        session.wait(lambda: any(line == b"PONG :native-summary-barrier" for _, line in session.peer.lines),
                     "native summary reader blocked IRC")
        session.type("/")
        session.wait(lambda: session.editor("/", 1), "native summary did not open child command input")
        session.type("join #never-sent")
        session.wait(lambda: session.editor("/join #never-sent"), "native child command was not composed")
        session.type(b"\x1b")
        session.wait(lambda: session.reader() and "Session summary" in session.screen.text(),
                     "native child-command cancel did not restore summary")
        session.type("r")
        session.quiet(.05)
        assert session.reader() and "Session summary" in session.screen.text()
        session.close_reader()
        assert list_requests(session.peer) == [b"LIST *offtopic"]
        assert not any(line.startswith((b"PRIVMSG ", b"JOIN #never-sent")) for _, line in session.peer.lines)
    print(("vipirc --idle" if full else "onekeyirc") +
          " terminal: channel-0 LIST mask/replies, summary reader, child-command cancellation, refresh and live PING handling passed", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--onekey", type=Path, help="also test this standalone onekeyirc binary")
    parser.add_argument("--gui-only", action="store_true")
    parser.add_argument("--native-only", action="store_true")
    args = parser.parse_args()
    if not args.native_only:
        gui_list_and_summary()
    if not args.gui_only:
        native_list_and_summary(True)
        if args.onekey:
            native.BINARY = args.onekey.resolve()
            native_list_and_summary(False)


if __name__ == "__main__":
    main()
