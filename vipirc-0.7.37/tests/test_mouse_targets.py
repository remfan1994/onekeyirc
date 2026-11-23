#!/usr/bin/env python3
"""Nickname and channel mouse targets against a private loopback IRC server.

These use real rendered rows and SGR mouse events, not application internals.
No public service, user profile, or remote transfer is involved.
"""
from test_integration import Session
from test_input_limits import editor_info, mouse_click, wait_bytes
from test_status_navigation import resize

DOWN = b"\x1bOB"
RIGHT = b"\x1bOC"
HOME = b"\x1bOH"


def wait_chat(session, title=None):
    session.wait(lambda:session.main_frame_ready() and
                 (title is None or title in session.panel_title()),
                 "mouse operation did not return to a complete chat screen: " + str(title))


def visible_text(session, text, *, roster=False):
    """Find only displayed chat history/sidebar cells, never header or input."""
    result = None
    def find():
        nonlocal result
        info = editor_info(session)
        if info is None or not session.main_frame_ready():
            return False
        first = session.screen.cols - 22 if roster else 0
        last = session.screen.cols if roster else session.screen.cols - 22
        for row in range(3, info.label_row):
            line = "".join(session.screen.cells[row][first:last])
            start = line.find(text)
            if start >= 0:
                result = (row, first + start)
                return True
        return False
    session.wait(find, "clickable text was not rendered: " + text)
    return result


def click_text(session, text, *, roster=False, offset=1):
    row, col = visible_text(session, text, roster=roster)
    mouse_click(session, row, col + min(offset, len(text)-1))


def wait_menu(session, nick):
    session.wait(lambda:session.panel_title() == "Nickname: " + nick and
                 all(name in "".join(session.screen.cells[row])
                     for row,name in ((4,"Query"),(5,"Whois"),(7,"Back"))),
                 "nickname click did not open the correct complete context menu: " + nick)


def choose_menu(session, action, *, mouse=False):
    index = {"Query":0, "Whois":1, "Ignore":2, "Unignore":2, "Back":3}[action]
    assert action in "".join(session.screen.cells[4 + index]), session.screen.text()
    if mouse:
        line = "".join(session.screen.cells[4 + index])
        mouse_click(session, 4 + index, line.index(action) + 1)
    else:
        # A freshly opened context menu starts on Query.
        session.type(DOWN * index + b"\r")
    wait_chat(session)


def keep_draft(session, text, column):
    info = wait_bytes(session, len(text.encode("utf-8")))
    assert text in session.input_text(), "mouse navigation changed the original chat draft"
    assert info.col == column, ("mouse navigation moved the original caret", info, column)


def nicknames_and_actions():
    with Session() as session:
        session.start()
        resize(session, 28, 132)
        session.command("/users on")
        draft = "LOCAL_DRAFT_STAYS_UNSENT"
        session.type(draft)
        wait_bytes(session, len(draft))
        session.type(HOME + RIGHT * 5)
        session.wait(lambda:editor_info(session).col == 6, "initial mouse-test caret missing")
        before = len(session.server.lines)
        click_text(session, "alice", roster=True)
        wait_menu(session, "alice")
        session.quiet(0.05)
        assert not any(line.startswith((b"WHOIS ",b"PRIVMSG ")) for _,line in session.server.lines[before:]), \
            "opening a nickname menu issued a command without choosing one"
        choose_menu(session, "Whois")
        session.wait(lambda:any(line == b"WHOIS alice" for _,line in session.server.lines[before:]),
                     "Whois action did not request the clicked nickname")
        wait_chat(session, "Channel 0: Server")
        session.command("/channel #test")
        keep_draft(session, draft, 6)

        click_text(session, "alice", roster=True)
        wait_menu(session, "alice")
        choose_menu(session, "Query", mouse=True)
        wait_chat(session, "Query: alice")
        wait_bytes(session, 0)
        session.command("/channel #test")
        keep_draft(session, draft, 6)

        # Ignore and Unignore are local, explicit actions on the same target.
        click_text(session, "alice", roster=True)
        wait_menu(session, "alice")
        choose_menu(session, "Ignore")
        session.server.send(":alice!u@localhost PRIVMSG #test :MOUSE_IGNORED_PAYLOAD")
        session.server.send("PING :mouse-ignore-fence")
        session.wait(lambda:any(line == b"PONG :mouse-ignore-fence" for _,line in session.server.lines),
                     "client stopped servicing IRC after Ignore")
        assert "MOUSE_IGNORED_PAYLOAD" not in session.screen.text()
        click_text(session, "alice", roster=True)
        wait_menu(session, "alice")
        choose_menu(session, "Unignore", mouse=True)
        session.server.send(":alice!u@localhost PRIVMSG #test :MOUSE_UNIGNORED_PAYLOAD")
        visible_text(session, "MOUSE_UNIGNORED_PAYLOAD")
        keep_draft(session, draft, 6)

        # Both ordinary and hyphenated NOTICE senders must be whole names.
        for nick, command, body in (("Chat-Peer","PRIVMSG","CHAT_SENDER_TARGET"),
                                    ("Notice-Peer","NOTICE","NOTICE_SENDER_TARGET")):
            session.server.send(":%s!u@localhost %s #test :%s" % (nick,command,body))
            visible_text(session, body)
            click_text(session, nick)
            wait_menu(session, nick)
            choose_menu(session, "Whois", mouse=True)
            session.wait(lambda:any(line == ("WHOIS " + nick).encode() for _,line in session.server.lines),
                         "chat sender menu truncated or changed the requested nick")
            wait_chat(session, "Channel 0: Server")
            session.command("/channel #test")
            keep_draft(session, draft, 6)
        assert not session.server.messages(), "a mouse operation sent the retained draft"
        assert session.config.read_bytes() == b"", "local nick menu silently saved configuration"
    print("mouse nicknames: roster and chat/NOTICE senders, reviewed Whois/Query, Ignore/Unignore, no accidental sends, and draft/caret preservation passed")


def channel_links_and_punctuation():
    with Session() as session:
        session.start()
        resize(session, 28, 132)
        session.command("/users on")
        draft = "CHANNEL_CLICK_DRAFT"
        session.type(draft)
        wait_bytes(session, len(draft))
        session.type(HOME + RIGHT * 3)
        session.wait(lambda:editor_info(session).col == 4, "channel-test caret missing")
        before = len(session.server.lines)
        session.server.send(":alice!u@localhost PRIVMSG #test :Visit #mouse-new, please.")
        visible_text(session, "Visit #mouse-new")
        click_text(session, "#mouse-new")
        session.wait(lambda:sum(line == b"JOIN #mouse-new" for _,line in session.server.lines[before:]) == 1,
                     "clicking an unjoined channel did not join the exact target once")
        wait_chat(session, "#mouse-new")
        wait_bytes(session, 0)
        session.server.send(":alice!u@localhost PRIVMSG #mouse-new :Return to #test.")
        visible_text(session, "Return to #test")
        after_join = len(session.server.lines)
        click_text(session, "#test")
        wait_chat(session, "#test")
        keep_draft(session, draft, 4)
        session.quiet(0.08)
        assert not any(line.startswith(b"JOIN ") for _,line in session.server.lines[after_join:]), \
            "clicking a joined channel issued a duplicate JOIN"

        # Retained channel names include legal punctuation. The longest known
        # match wins: #foo must never absorb a click on #foo.bar.
        for channel in ("#foo", "#foo.bar", "#room]"):
            session.command("/join " + channel)
            wait_chat(session, channel)
        session.command("/channel #test")
        for target in ("#foo.bar", "#room]", "#mouse-new"):
            marker = "EXACT_CHANNEL_" + target
            session.server.send(":alice!u@localhost PRIVMSG #test :%s (%s)." % (marker,target))
            visible_text(session, marker)
            before = len(session.server.lines)
            click_text(session, "(" + target + ")", offset=2)
            wait_chat(session, target)
            assert session.panel_title().split(" |",1)[0].endswith(target), session.panel_title()
            session.quiet(0.05)
            assert not any(line.startswith(b"JOIN ") for _,line in session.server.lines[before:]), \
                "retained channel punctuation changed the clicked identity or rejoined"
            session.command("/channel #test")
            keep_draft(session, draft, 4)
        assert sum(line == b"JOIN #mouse-new" for _,line in session.server.lines) == 1
        assert not session.server.messages(), "channel clicks sent an unsent draft"
    print("mouse channels: one exact JOIN for new targets, no duplicate JOIN for retained targets, longest dotted/bracketed names, return draft/caret preservation passed")


def main():
    nicknames_and_actions()
    channel_links_and_punctuation()
    print("all mouse target tests passed")


if __name__ == "__main__":
    main()
