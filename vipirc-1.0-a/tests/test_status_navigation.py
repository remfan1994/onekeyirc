#!/usr/bin/env python3
"""Real-screen status navigation at minimum geometry and across local servers."""
import argparse
import fcntl
import re
import struct
import termios

from test_offline import OfflineTerminal
from test_integration import Session
from test_input_limits import (BEGIN_PASTE, END_PASTE, editor_info, focused_label,
                               mouse_click, status_focus, wait_bytes)
from test_servers import (MultiServerSession, HOME, DOWN, open_servers, select_server_row,
                          choose_action, SERVER_ACTIONS, CHANNEL_ACTIONS, QUERY_ACTIONS,
                          focused_action, server_rows)


def resize(session, rows, cols):
    # The controlling PTY delivers SIGWINCH for this real geometry change.
    # A second synthetic signal can queue a later resize after a visible frame;
    # mouse coordinates concurrent with that resize must remain fail-closed.
    if (session.screen.rows, session.screen.cols) == (rows, cols):
        return
    fcntl.ioctl(session.master, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    session.screen.resize(rows, cols)
    session.quiet(0.06)
    assert session.proc.poll() is None, "client exited during status layout resize"


def main_visible(session):
    return session.is_main() and session.mode() == "INPUT"


def empty_startup_and_keyboard():
    for no_connect in (False, True):
        with OfflineTerminal(configured=False, no_connect=no_connect,
                             test_mode=False, cols=132) as session:
            session.wait(lambda:b"Not vegetarian?" in session.raw,"empty-start welcome missing")
            session.type("x")
            session.wait(lambda:session.is_main(),"empty startup did not reach the disconnected editor")
            assert b"Set IRC server:" not in session.raw and b"Set user nick:" not in session.raw, \
                "empty startup unexpectedly blocked for connection details"
            header="".join(session.screen.cells[0]).strip()
            assert header=="F1 vipirc",("empty startup exposed empty categories",header)
            draft="NO_SERVER_DRAFT"
            session.type(draft);wait_bytes(session,len(draft))
            session.type(b"\x1b")
            session.wait(lambda:session.header_focus() is None and "vipirc" in session.panel_title(),
                         "Escape did not immediately open the sole available destination")
            session.type(b"\x1bOC"*64+b"\x1bOD"*64)
            session.quiet(0.08)
            assert session.header_focus() is None and "vipirc" in session.panel_title()
            assert all(label not in "".join(session.screen.cells[0]) for label in session.destinations[1:])
            session.leave_app()
            session.wait(lambda:session.is_main(),"single-tab cycle could not return to chat")
            info=wait_bytes(session,len(draft))
            assert draft in session.input_text() and (info.row,info.col)==(1,len(draft)+1)
            session.type(session.category_key(2))
            session.quiet(0.08)
            assert session.is_main() and draft in session.input_text(), "F2 created or opened an empty server category"
            assert "Servers" not in "".join(session.screen.cells[0])
            session.type(b"\x1b")
            session.quiet(0.01)
            session.type(b"OP")
            session.wait(lambda:session.panel_title().startswith("vipirc |"), "fragmented F1 did not open vipirc")
            session.leave_app()
            session.wait(lambda:session.is_main(), "F1 header did not return to chat")
            assert draft in session.input_text()
            session.command("/mouse off")
            assert "Clear" not in "".join(session.screen.cells[-1]) and "Send" not in "".join(session.screen.cells[-1])
            session.clear_input()
            wait_bytes(session,0)
            session.type(b"\x1bOA")
            session.wait(session.main_frame_ready,"Up from first row hid the editor")
            session.app_entry("Settings")
            session.wait(lambda:"Settings" in session.panel_title() and "New server" in session.screen.text(),
                         "empty-start Settings/New server route is inaccessible")
            session.type(b"\x1b")
            session.wait(lambda:session.panel_title().startswith("vipirc |"),"empty-start Settings did not return to vipirc")
            session.leave_app()
            session.wait(session.main_frame_ready,"vipirc did not restore empty chat")
    print("empty startup: plain/no-connect launches show only vipirc, repeated body arrows stay local, Settings/New server is reachable and disabled mouse hides buttons")


def check_panel(session,title,rows,cols,draft_bytes):
    session.wait(lambda:not session.is_main(),"destination did not leave chat")
    resize(session,24,132)
    session.wait(lambda:title in session.panel_title(),"destination opened wrong panel: "+title)
    if title in ("Servers","Channels","Query list"):
        labels=SERVER_ACTIONS if title=="Servers" else CHANNEL_ACTIONS if title=="Channels" else QUERY_ACTIONS
        session.wait(lambda:focused_action(session,labels)=="Open","list did not show its default Open action")
        session.type(b"\x1bOC")
        session.wait(lambda:focused_action(session,labels)==labels[1],"Right did not select the next list action")
        session.type(b"\x1bOD ")
        session.wait(lambda:focused_action(session,labels)=="Open","Left/Space did not preserve the selected action")
        assert title in session.panel_title(), "list arrows changed the category"
    session.type(b"\t")
    session.quiet(0.04)
    assert session.header_focus() is None,"panel Tab still enters global navigation"
    assert title in session.panel_title(),"panel Tab changed destination"
    session.type(BEGIN_PASTE+b"/quit\r\n\t\x1b"+END_PASTE)
    session.quiet(0.04)
    assert title in session.panel_title(),"pasted controls activated a panel destination"
    if title=="vipirc": session.leave_app()
    else: session.type(b"\x1b")
    if title in ("Settings", "Help / Manual"):
        session.wait(lambda:session.panel_title().startswith("vipirc |"),"Settings Escape did not return to vipirc")
        session.leave_app()
    session.wait(lambda:main_visible(session),"panel Escape did not return to chat editing")
    wait_bytes(session,draft_bytes)
    resize(session,rows,cols)
    wait_bytes(session,draft_bytes)


def narrow_status_controls():
    checks=0
    for style in ("reverse","brackets"):
        for rows,cols in ((9,20),(12,40),(24,80)):
            with OfflineTerminal(rows=rows,cols=cols,focus_style=style) as session:
                session.start()
                assert "[OFFLINE]" in session.screen.text() or "OFFLINE TEST MODE" in session.screen.text()
                session.command("/pm CompactPeer")
                session.command("/channel 1")
                session.type("safe");wait_bytes(session,4)
                for label,title in (("vipirc","vipirc"),("Servers","Servers"),("Channels","Channels"),
                                    ("Queries","Query list"),("File transfers","File transfers"),("Mentions/unread","Mentions and unread")):
                    if label=="File transfers" and rows<12:
                        session.command("/transfers")
                    else:
                        session.open_destination(label)
                    check_panel(session,title,rows,cols,4)
                    checks+=1
                session.app_entry("Settings")
                check_panel(session,"Settings",rows,cols,4)
                session.open_help()
                check_panel(session,"Help / Manual",rows,cols,4)
                footer=session.screen.rows-1
                line="".join(session.screen.cells[footer])
                mouse_click(session,footer,line.index("Clear")+1)
                wait_bytes(session,0)
                line="".join(session.screen.cells[footer])
                mouse_click(session,footer,line.index("Send")+1)
                session.quiet(0.04);wait_bytes(session,0)
                assert session.mode()=="INPUT" and session.screen.cursor_visible
                assert b"<offline_tester>" not in session.raw
                assert session.config.read_bytes()==session.before_config
                checks+=4
    print("global selector compact: %d panel/footer activations at 20/40/80 columns, scrolling categories, arrow/Esc routes and draft/paste isolation passed"%checks)

def divider_numbers_are_atomic():
    checked = 0
    for style in ("reverse", "brackets"):
        with OfflineTerminal(rows=19, cols=100, focus_style=style) as session:
            session.start()
            session.input_mode()
            session.type("abcdefghijkl")
            baseline = wait_bytes(session, 12)
            assert baseline.budget and baseline.budget > 200
            for cols in range(20, 81):
                resize(session, 19, cols)
                info = wait_bytes(session, 12)
                assert info.budget is None or info.budget == baseline.budget, (
                    "divider clipped its byte budget to a different number", style, cols, info, session.screen.text())
                assert info.row is None or info.row == 1, (
                    "divider clipped the caret row", style, cols, info, session.screen.text())
                assert info.col is None or info.col == 13, (
                    "divider clipped the caret column", style, cols, info, session.screen.text())
                assert session.screen.col == 13 and session.screen.reverse_cells[session.screen.row][13], (
                    "narrow divider change hid or moved the actual caret", style, cols, session.screen.text())
                checked += 1
            assert b"<offline_tester>" not in session.raw
    print("status divider: byte budget/caret numbers remain whole or omitted across %d width/style combinations; explicit hardware caret remains correct" % checked)


def server_only_actions_and_hidden_categories():
    with OfflineTerminal(no_connect=True, test_mode=False, cols=132) as session:
        session.wait(lambda:b"Not vegetarian?" in session.raw,"server-only welcome missing")
        session.type("x")
        session.wait(session.main_frame_ready,"server-only startup did not open chat")
        header = "".join(session.screen.cells[0]).strip()
        assert re.fullmatch(r"F1 vipirc\s+>\s+F2 Servers \(1\)", header), header
        draft = "NO_CHANNEL_NO_QUERY_DRAFT"
        session.type(draft); wait_bytes(session,len(draft))
        session.focus_destination("Servers")
        session.wait(lambda:focused_action(session,SERVER_ACTIONS)=="Open","server Open action missing")
        for key, expected in ((b"\x1bOC", "New"),(b"\x1bOD", "Open"),(b"h", "Back"),(b"l", "Open")):
            session.type(key)
            session.wait(lambda:focused_action(session,SERVER_ACTIONS)==expected,"server arrow did not select " + expected)
            assert "Servers" in session.panel_title() and session.header_focus() is None
        session.type(b"\x1bOC"*32+b"\x1bOD"*32)
        session.quiet(.08)
        assert "Servers" in session.panel_title() and focused_action(session,SERVER_ACTIONS)=="Open", \
            "repeated arrows escaped to an unestablished category"
        for number in (3,4,5,6):
            session.type(session.category_key(number)); session.quiet(.035)
            assert "Servers" in session.panel_title(), "function-key shortcut opened an empty category"
        assert re.fullmatch(r"F1 vipirc\s+>\s+F2 Servers \(1\)", "".join(session.screen.cells[0]).strip())
        session.type(b"\x1bOC\x1bOB\x1bOA ")
        session.wait(lambda:focused_action(session,SERVER_ACTIONS)=="New","Up/Down/Space changed the server action")
        assert focused_label(session,4,"0: must-not-resolve.invalid"), "chosen server lost its row highlight"
        session.type(b"\r")
        session.wait(lambda:"New server" in session.panel_title(),"Enter ignored the selected New action")
        assert session.config.read_bytes()==session.before_config
    print("server-only actions: parenthesized count, direct arrows/hjkl, simultaneous row/action focus, hidden F3..6 and Enter New passed")


def conditional_status_and_list_actions():
    with Session() as session:
        session.start()
        session.resize(26, 132)
        session.input_mode()
        draft = "LIST_ACTION_SERVER_CHANNEL_DRAFT"
        session.type(draft)
        original_caret = wait_bytes(session, len(draft))
        header = "".join(session.screen.cells[0])
        for label in ("F1 vipirc","F2 Servers (1)","F3 Channels (1)"):
            assert label in header,("nonempty destination or count missing",label,header)
        for label in ("Queries","File transfers","Mentions"):
            assert label not in header,("empty destination still visible",label,header)
        session.command("/cl")
        session.wait(lambda:"Channels" in session.panel_title() and "0: server" in session.screen.text(),
                     "/cl did not open the local channel list")
        assert not any(line.startswith(b"LIST") for _,line in session.server.lines), "/cl sent a server directory request"
        session.command("/sl")
        session.wait(lambda:"Servers" in session.panel_title(),"/sl did not open the local server list")
        session.type("l")
        session.wait(lambda: "Servers" in session.panel_title() and focused_action(session,SERVER_ACTIONS)=="New",
                     "server-row l did not select New locally")
        choose_action(session,"Open",SERVER_ACTIONS)
        session.wait(lambda:session.main_frame_ready() and "Channel 0: Server" in session.panel_title(),
                     "server Open did not enter its message view")
        session.command("/channel #test")
        session.wait(lambda:session.main_frame_ready() and "Channel 1: #test" in session.panel_title() and
                     draft in session.input_text(), "explicit channel selection did not restore its saved draft")
        restored_caret = wait_bytes(session, len(draft))
        assert (restored_caret.row,restored_caret.col)==(original_caret.row,original_caret.col), \
            "opening the server message view changed the retained channel caret"
        session.command("/cl")
        session.wait(lambda: "Channels" in session.panel_title() and "0: server" in session.screen.text(),
                     "explicit channel-list command did not open that server's list")
        session.type("l")
        session.wait(lambda: "Channels" in session.panel_title() and focused_action(session,CHANNEL_ACTIONS)=="Join",
                     "channel-row l did not select Join locally")
        session.type(session.category_key(4))
        session.quiet(.05)
        assert "Channels" in session.panel_title() and focused_action(session,CHANNEL_ACTIONS)=="Join", \
            "absent query category changed the channel screen or action"
        session.command("/queries")
        session.wait(lambda: "Query list" in session.panel_title() and "New query..." in session.screen.text(),
                     "explicit empty query list did not provide its new-query entry")
        assert focused_action(session, QUERY_ACTIONS)=="Open", "empty query list has no default Open action"
        session.type("lh")
        session.wait(lambda: focused_action(session,QUERY_ACTIONS)=="Open","query arrows did not cycle actions locally")
        assert "Query list" in session.panel_title()
        empty_query_title = session.panel_title()
        session.type(b"\r")
        session.wait(lambda: session.in_command() and session.command_text()=="/pm",
                     "empty new-query entry did not open the separate command editor")
        session.type(b"\x1b")
        session.wait(lambda: session.panel_title()==empty_query_title and
                     focused_action(session,QUERY_ACTIONS)=="Open" and
                     focused_label(session,4,"New query..."),
                     "new-query cancel did not restore its empty Query list, Open action and selected New query entry")
        assert "Queries (" not in "".join(session.screen.cells[0]), "cancel created a private query"
        session.type(b"\x1b")
        session.wait(lambda: session.main_frame_ready() and draft in session.input_text(),
                     "Escape from the restored Query list did not restore the chat draft")
        session.navigate("q")
        session.wait(lambda: "New query..." in session.screen.text(), "empty query entry did not remain accessible")
        row=next(i for i,cells in enumerate(session.screen.cells) if "New query..." in "".join(cells))
        line="".join(session.screen.cells[row])
        mouse_click(session,row,line.index("New query")+2)
        session.wait(lambda: session.in_command() and session.command_text()=="/pm", "new-query entry click did not open Command")
        session.type(b"\x1b")
        session.wait(lambda: session.panel_title()==empty_query_title and
                     focused_action(session,QUERY_ACTIONS)=="Open" and
                     focused_label(session,4,"New query..."),
                     "clicked new-query cancel did not restore its empty Query list, Open action and selected New query entry")
        assert "Queries (" not in "".join(session.screen.cells[0]), "clicked-entry cancel created a private query"
        session.type(b"\x1b")
        session.wait(lambda: session.main_frame_ready() and draft in session.input_text(),
                     "Escape from the restored clicked Query list did not restore the chat draft")
        session.navigate("c")
        session.wait(lambda: "Channels" in session.panel_title(), "Channels did not reopen after command cancellation")
        session.type("h")
        session.wait(lambda: focused_action(session,CHANNEL_ACTIONS)=="Back", "channel Left did not choose Back locally")
        assert "Channels" in session.panel_title()
        session.type("l"+"g")
        session.type(DOWN+b"\r")
        session.wait(lambda: session.main_frame_ready() and draft in session.input_text(),
                     "channel Enter did not use Open on the selected row")

        session.server.send(":StatusPeer!u@fixture PRIVMSG tester :tester status mention")
        session.server.send(":FilePeer!u@fixture PRIVMSG tester :\x01DCC SEND status.bin 2130706433 9 1\x01")
        session.wait(lambda: all(label in "".join(session.screen.cells[0])
                                for label in ("F4 Queries (1)", "F6 File transfers (1)", "F5 Mentions/unread (")),
                     "parenthesized nonzero status counters did not appear")
        header = "".join(session.screen.cells[0])
        assert header.index("Queries") < header.index("Mentions/unread") < header.index("File transfers")
        for category,title,actions in (("Queries","Query list",QUERY_ACTIONS),("Channels","Channels",CHANNEL_ACTIONS),("Servers","Servers",SERVER_ACTIONS)):
            session.focus_destination(category)
            session.type("lh")
            session.wait(lambda: title in session.panel_title() and focused_action(session,actions)=="Open",
                         "Left/Right changed an established category: "+category)
        session.type(b"\x1b")
        session.wait(lambda: session.main_frame_ready() and draft in session.input_text(),
                     "list Escape did not return to chat")
        for cols in (20, 40, 80):
            resize(session, 24, cols)
            status_focus(session, "Transfers", "outline")
            check_panel(session, "File transfers", 24, cols, len(draft))
            status_focus(session, "Queries", "outline")
            check_panel(session, "Query list", 24, cols, len(draft))
        resize(session, 26, 132)
        status_focus(session, "Mentions/unread", "outline")
        session.wait(lambda: "Mentions and unread" in session.panel_title() and "status mention" in session.screen.text(),
                     "mention status did not show its complete captured line")
        assert "F5 Mentions/unread (" in "".join(session.screen.cells[0]), "retained mention category disappeared"
        session.command("/channel 1", resume_input=False)
        session.command("/close StatusPeer", resume_input=False)
        assert "Queries (1)" not in "".join(session.screen.cells[0]), "closed last query retained its counter"
        assert draft in session.input_text() and not session.server.messages()
    print("status hierarchy: parenthesized counters, hidden empty entries, local action arrows, direct server message view and explicit channel-list access, compact access and draft isolation passed")


def fixed_query_identity_and_cycle():
    with Session() as session:
        session.start()
        session.resize(26,132)
        session.command("/pm ChanServ")
        session.command("/pm OtherPeer")
        session.command("/channel 1")
        session.open_destination("Queries")
        assert "ChanServ" in session.screen.text() and "OtherPeer" in session.screen.text()
        session.type(HOME+b"\r")
        session.wait(lambda:session.is_main() and "Query: ChanServ" in session.panel_title(),
                     "query-list selection did not open ChanServ")
        header="".join(session.screen.cells[0])
        assert "Queries (2)" in header,"opening ChanServ replaced or changed the Queries category"
        assert "ChanServ" not in header and "Query:" not in header,"query identity leaked into the fixed global header"
        draft="CHANSERV_DRAFT_STAYS_LOCAL"
        session.type(draft);wait_bytes(session,len(draft))
        labels=tuple(label for label in session.destinations if label in header)
        for label in labels+tuple(reversed(labels)):
            session.focus_destination(label)
            assert session.header_focus() is None,"function-key shortcut intercepted body focus"
            assert "Queries (2)" in "".join(session.screen.cells[0])
        session.leave_app()
        session.wait(lambda:session.main_frame_ready(),"vipirc Back did not return query editing")
        assert "Query: ChanServ" in session.panel_title() and draft in session.input_text()
        for number,label in enumerate(session.destinations,1):
            session.type(session.category_key(number))
            if label in labels:
                expected={"vipirc":"vipirc","Servers":"Servers","Channels":"Channels","Queries":"Query list",
                          "File transfers":"File transfers","Mentions/unread":"Mentions and unread"}[label]
                session.wait(lambda:session.header_focus() is None and expected.lower() in session.panel_title().lower(),
                             "F%d did not select its stable category" % number)
                if label=="vipirc":session.leave_app()
                else:session.type(b"\x1b")
                session.wait(session.main_frame_ready,"function-key category did not restore query draft")
            else:
                session.quiet(0.08)
                assert session.main_frame_ready() and session.header_focus() is None, \
                    "function-key shortcut created an empty category"
            assert "Query: ChanServ" in session.panel_title() and draft in session.input_text()
            assert wait_bytes(session,len(draft)).byte_count == len(draft)
        # Function-key bytes inside pasted text must not navigate or submit.
        session.type(BEGIN_PASTE+session.category_key(3)+session.category_key(4)+END_PASTE)
        session.quiet(0.08)
        assert session.main_frame_ready() and session.header_focus() is None
        assert "Query: ChanServ" in session.panel_title() and draft in session.input_text()
        assert not session.server.messages() and not session.server.messages("ChanServ"),"header traversal sent the query draft"
    print("query header: Queries (2) -> chooser -> ChanServ retains category identity; stable function keys skip empty entries and preserve draft passed")


def in_unread(session):
    return session.destination_visible("Mentions/unread")


def unread_rows(session):
    result = {}
    for row in range(4, session.screen.rows - 2):
        text = "".join(session.screen.cells[row])
        found = re.search(r"\| S(\d+) (Channel \d+: [^|]+|Query: [^|]+)\|", text)
        if found:
            result[(int(found[1]), found[2].strip())] = (row, text)
    return result


def activity_rows(session):
    """Visible first rows of selectable entries, excluding section/server headings."""
    return [row for row in range(4,session.screen.rows-2)
            if re.search(r"\| S\d+ (?:Channel|Query:)|\bS\d+ \[\d{4}-", "".join(session.screen.cells[row]))]


def barrier(session, fixture, token):
    fixture.send("PING :" + token)
    session.wait(lambda: any(line == ("PONG :" + token).encode("ascii") for unused, line in fixture.lines),
                 "background IRC service stopped: " + token)
    session.quiet(0.05)


def unread_routes_and_stable_selection():
    with MultiServerSession(rows=26, cols=132) as session:
        session.start()
        slot, second = session.add_server(nick="secondnick")
        assert slot == 1
        # Read startup messages and keep both current views on their server.
        for target in (1, 0):
            session.select_server(target)
            session.command("/channel #test")
            session.command("/server")
        session.input_mode()
        draft = "preserved primary server draft"
        session.type(draft)
        wait_bytes(session, len(draft))

        # These two typed rows belong to a background session. The first
        # session intentionally has no unread conversations yet.
        second.send(":UnreadPeer!u@fixture PRIVMSG #test :secondnick channel mention")
        second.send(":UnreadPeer!u@fixture PRIVMSG secondnick :secondnick private mention")
        barrier(session, second, "initial-unread")
        status_focus(session, "unread view", "reverse")
        session.wait(lambda: in_unread(session), "unread count did not open its picker")
        session.wait(lambda: (1, "Channel 1: #test") in unread_rows(session) and
                     (1, "Query: UnreadPeer") in unread_rows(session),
                     "unread picker omitted typed channel/query rows")
        rows = unread_rows(session)
        assert all(key[0] == 1 for key in rows), ("freshly read primary server reported unread", rows)
        assert all("!1" in value[1] for value in rows.values()), ("mention indicators missing", rows)
        query_position = activity_rows(session).index(rows[(1, "Query: UnreadPeer")][0])
        session.type(HOME + DOWN * query_position)
        query_row = unread_rows(session)[(1, "Query: UnreadPeer")][0]
        session.wait(lambda: focused_label(session, query_row, "S1 Query: UnreadPeer", "reverse"),
                     "query unread row did not receive visible focus")
        session.type(b"\t")
        session.quiet(0.04)
        assert session.header_focus() is None, "unread Tab entered global navigation"
        session.type(BEGIN_PASTE + b"\r\n/quit" + END_PASTE)
        session.quiet(0.04)
        assert in_unread(session), "paste chose an unread conversation"

        # A new row sorts ahead of the selected query. Keep focus on the
        # same (server, conversation), rather than its stale numeric index.
        session.server.send(":EarlierPeer!u@fixture PRIVMSG #test :tester earlier server activity")
        barrier(session, session.server, "insert-before-selected")
        session.wait(lambda: (0, "Channel 1: #test") in unread_rows(session), "new earlier-server row missing")
        query_row = unread_rows(session)[(1, "Query: UnreadPeer")][0]
        session.wait(lambda: focused_label(session, query_row, "S1 Query: UnreadPeer", "reverse"),
                     "new activity silently changed the selected unread conversation")
        session.type(b"\r")
        session.wait(lambda: main_visible(session) and "Query: UnreadPeer" in session.panel_title(),
                     "Enter did not route to the selected query")
        session.open_app_diagnostics()
        assert "Server 1:" in session.app_line("Server ")
        session.close_app_diagnostics()
        session.wait(lambda:session.main_frame_ready() and "private mention" in session.screen.text(),
                     "closing help did not redraw the selected query message")
        wait_bytes(session, 0)
        status_focus(session, "unread view", "reverse")
        session.wait(lambda: in_unread(session) and
                     (1, "Query: UnreadPeer") not in unread_rows(session) and
                     (0, "Channel 1: #test") in unread_rows(session) and
                     (1, "Channel 1: #test") in unread_rows(session), "unread picker did not finish reopening")
        assert (1, "Query: UnreadPeer") not in unread_rows(session), "opened query stayed unread"
        assert (0, "Channel 1: #test") in unread_rows(session) and (1, "Channel 1: #test") in unread_rows(session)
        session.type(b"\x1b")
        session.wait(lambda: main_visible(session), "unread Escape did not return to the originating query")
        assert "Query: UnreadPeer" in session.panel_title()
        session.select_server(0)
        wait_bytes(session, len(draft))
        assert draft in session.input_text(), "unread cross-server navigation lost the primary draft"
        assert not any(line.startswith(b"PRIVMSG ") for peer in session.peers for unused, line in peer.lines), \
            "unread navigation submitted a chat message"
    print("status unread: typed cross-server rows, counts/mention flags, PING service, stable selection after insertion, exact Enter routing, read updates and draft isolation passed")


def unseen_activity_below_minimum_size():
    with Session() as session:
        session.start()
        session.command("/server")
        session.command("/channel #test")
        session.input_mode()
        draft = "draft while terminal is too small"
        session.type(draft)
        wait_bytes(session, len(draft))
        session.command("/unread")
        resize(session, 5, 10)
        session.server.send(":SmallPeer!u@fixture PRIVMSG #test :SMALL_UNSEEN")
        barrier(session, session.server, "too-small-unread")
        # The empty unread screen was explicitly opened before shrinking;
        # widening must not reveal chat and mark the unseen message read.
        resize(session, 24, 80)
        session.wait(lambda: in_unread(session), "unread list did not survive an undersized terminal")
        assert (0, "Channel 1: #test") in unread_rows(session), (
            "unseen text was marked read while the terminal could not display chat", session.screen.text())
        row = list(unread_rows(session)).index((0, "Channel 1: #test"))
        session.type(HOME + DOWN * row + b"\r")
        session.wait(lambda: main_visible(session) and "SMALL_UNSEEN" in session.screen.text(),
                     "unseen message was lost when reopening its conversation")
        wait_bytes(session, len(draft))
        assert draft in session.input_text()
        assert not session.server.messages(), "terminal size changes submitted the draft"
    print("status visibility: messages arriving below minimum geometry remain unread until actually opened; PING and draft survive")


def selected_server_context_actions():
    with MultiServerSession(rows=26, cols=132) as session:
        session.start()
        slot, second = session.add_server(nick="secondnick")
        assert slot == 1
        drafts = ("PRIMARY_CONTEXT_DRAFT", "SECOND_CONTEXT_DRAFT")
        for index in (0, 1):
            session.select_server(index)
            session.input_mode()
            session.type(drafts[index])
            wait_bytes(session, len(drafts[index]))
        session.select_server(0)
        open_servers(session)
        select_server_row(session, 1)
        choose_action(session, "Away", SERVER_ACTIONS)
        session.wait(lambda: any(line == b"AWAY :Away" for unused, line in second.lines),
                     "Away did not target the selected background server")
        assert not any(line.startswith(b"AWAY") for unused, line in session.server.lines), \
            "background-server Away affected the current server"
        assert "(away)" not in server_rows(session)[1].lower(), "away state appeared before acknowledgment"
        second.send(":srv 306 secondnick :You have been marked as being away")
        barrier(session, second, "background-away")
        session.wait(lambda: "(away)" in server_rows(session)[1].lower(), "selected server row omitted away state")
        assert "(away)" not in server_rows(session)[0].lower(), "away state leaked to another server"
        choose_action(session, "Away", SERVER_ACTIONS)
        session.wait(lambda: any(line == b"AWAY" for unused, line in second.lines), "Away did not clear selected-server away state")
        second.send(":srv 305 secondnick :You are no longer marked as being away")
        barrier(session, second, "background-back")
        session.wait(lambda: "(away)" not in server_rows(session)[1].lower(), "return-from-away acknowledgment was lost")
        choose_action(session, "Open", SERVER_ACTIONS)
        session.wait(lambda:main_visible(session) and "Channel 0: Server" in session.panel_title(),
                     "server Open did not select its own channel zero")
        session.open_app_diagnostics()
        assert "Server 1:" in session.app_line("Server "), "context Open entered a different server"
        session.close_app_diagnostics()
        wait_bytes(session, len(drafts[1]))
        assert drafts[1] in session.input_text()
        session.select_server(0)
        wait_bytes(session, len(drafts[0]))
        assert drafts[0] in session.input_text()
        assert not any(line.startswith(b"PRIVMSG ") for peer in session.peers for unused, line in peer.lines)
    print("server context: Away/back acknowledgment and Open target the selected server's message view; other connections and both drafts remain intact")


def main():
    parser = argparse.ArgumentParser()
    scope = parser.add_mutually_exclusive_group()
    scope.add_argument("--offline-only", action="store_true")
    scope.add_argument("--live-only", action="store_true")
    args = parser.parse_args()
    if not args.live_only:
        empty_startup_and_keyboard()
        server_only_actions_and_hidden_categories()
        narrow_status_controls()
        divider_numbers_are_atomic()
    if not args.offline_only:
        conditional_status_and_list_actions()
        fixed_query_identity_and_cycle()
        unread_routes_and_stable_selection()
        unseen_activity_below_minimum_size()
        selected_server_context_actions()
    print("all selected status navigation tests passed")


if __name__ == "__main__":
    main()
