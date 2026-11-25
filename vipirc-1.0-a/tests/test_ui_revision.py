#!/usr/bin/env python3
"""Local PTY regressions for the reviewed app links and progressive hierarchy."""
import json
from pathlib import Path
import re

from test_integration import IRCFixture, Session
from test_offline import OfflineTerminal
from test_input_limits import BEGIN_PASTE, END_PASTE, editor_info, focused_label, mouse_click, wait_bytes
from test_settings import edit_field, select_field, toggle, action, open_settings
from test_servers import choose_action, focused_action, SERVER_ACTIONS, CHANNEL_ACTIONS, QUERY_ACTIONS
from test_status_navigation import resize

UP, DOWN, LEFT, RIGHT = b"\x1bOA", b"\x1bOB", b"\x1bOD", b"\x1bOC"
HOME, END = b"\x1bOH", b"\x1bOF"
ROOT=Path(__file__).resolve().parents[1]


def app_ready(s):
    return (s.panel_title().startswith("vipirc |") and
            any("[" in "".join(row) for row in s.screen.cells[3:-2]) and
            "Back" in "".join(s.screen.cells[-2]) and
            (s.screen.rows < 15 or s.screen.cols < 80 or
             ("[Background]" in s.screen.text() and "[About]" in s.screen.text() and "[Insha-Allah license agreement]" in s.screen.text())) and
            not s.screen.cursor_visible)


def start_empty(s):
    s.wait(lambda:b"Not vegetarian?" in s.raw,"welcome missing")
    s.type("x")
    s.wait(s.main_frame_ready,"empty workspace did not open")
    assert "".join(s.screen.cells[0]).strip()=="F1 vipirc"


def return_app(s):
    s.type(b"\x1b")
    s.wait(lambda:app_ready(s),"Settings/license Escape did not return to vipirc")
    assert not s.is_main()


def progressive_setup_without_restart():
    peer=IRCFixture()
    try:
        with OfflineTerminal(configured=False,test_mode=False,cols=132,rows=26) as s:
            start_empty(s)
            pid=s.proc.pid
            s.type(s.category_key(1))
            s.wait(lambda:app_ready(s),"F1 did not open visible setup links")
            assert "[Nick:" in s.screen.text()
            s.type(LEFT*8+RIGHT*8)
            s.quiet(.05)
            assert app_ready(s) and s.header_focus() is None,"body arrows switched the top category"
            s.type(DOWN+b"\r")
            s.wait(lambda:"Settings | Server 0" in s.panel_title() and focused_label(s,7,"Nickname"),
                   "Nick link did not select the current server nickname field")
            return_app(s)
            s.type(b"g\r")
            s.wait(lambda:"Settings | Server 0" in s.panel_title() and focused_label(s,4,"Server"),
                   "Server link did not open the current profile")
            edit_field(s,0,"127.0.0.1")
            edit_field(s,1,str(peer.port))
            edit_field(s,3,"tester")
            edit_field(s,4,"tester")
            toggle(s,2)
            action(s,1)
            s.wait(lambda:"Preferences applied in memory" in s.screen.text(),"initial setup Apply failed")
            return_app(s)
            s.wait(lambda:"F2 Servers (1)" in "".join(s.screen.cells[0]) and "127.0.0.1" in s.screen.text(),
                   "valid staged server did not reveal the next category")
            assert "Channels" not in "".join(s.screen.cells[0])
            s.type(s.category_key(2))
            s.wait(lambda:"Servers" in s.panel_title() and "127.0.0.1" in s.screen.text(),"F2 server entry missing")
            choose_action(s,"Connect",SERVER_ACTIONS)
            s.wait(lambda:peer.registered,"Connect did not use initial staged settings without restart")
            assert s.proc.pid==pid and any(line==b"NICK tester" for _,line in peer.lines)
            choose_action(s,"Open",SERVER_ACTIONS)
            s.wait(lambda:s.main_frame_ready() and "Channel 0: Server" in s.panel_title(),
                   "server Open did not lead directly to its message/MOTD view")
            s.wait(lambda:"Local fixture MOTD" in s.screen.text(),"opened server message view omitted its MOTD")
            s.command("/cl")
            s.wait(lambda:"Channels" in s.panel_title() and "0: server" in s.screen.text(),"channel command omitted the server message view")
            choose_action(s,"Join",CHANNEL_ACTIONS)
            s.wait(lambda:s.in_command() and s.command_text()=="/join","Join did not open a channel prompt")
            s.type("#test\r")
            s.wait(lambda:any(line==b"JOIN #test" for _,line in peer.lines) and "F3 Channels (1)" in "".join(s.screen.cells[0]),
                   "joining did not reveal F3")
            s.type(s.category_key(3))
            s.wait(lambda:"Channels" in s.panel_title() and "#test [joined]" in s.screen.text(),"F3 joined list missing")
            s.type(b"g"+DOWN+b"\r")
            s.wait(lambda:s.main_frame_ready() and "Channel 1: #test" in s.panel_title(),"channel selection did not open chat")
            s.command("/msg ReviewPeer hierarchy-message")
            s.wait(lambda:peer.messages("ReviewPeer") and "F4 Queries (1)" in "".join(s.screen.cells[0]),"/msg did not reveal queries")
            s.type(s.category_key(4))
            s.wait(lambda:"Query list" in s.panel_title() and "ReviewPeer" in s.screen.text(),"F4 queries missing")
            s.type(b"g\r")
            s.wait(lambda:s.main_frame_ready() and "Query: ReviewPeer" in s.panel_title(),"query list did not lead to chat")
            draft="QUERY_DRAFT_STAYS_LOCAL"
            s.type(draft);wait_bytes(s,len(draft))
            s.type(LEFT*4)
            s.wait(lambda:editor_info(s).col==len(draft)-3,"query draft caret did not move")
            s.type(b"\x1b")
            s.wait(lambda:"Query list" in s.panel_title() and focused_action(s,QUERY_ACTIONS)=="Open",
                   "query Escape did not return to its query list")
            assert "ReviewPeer" in s.screen.text() and "[Open]" in "".join(s.screen.cells[3])
            assert "[Open]" not in "".join(s.screen.cells[-2]),"query action bar remains detached at bottom"
            s.type(b"\r")
            s.wait(lambda:s.main_frame_ready() and "Query: ReviewPeer" in s.panel_title(),"query list Open did not restore query")
            assert draft in s.input_text() and editor_info(s).col==len(draft)-3,"query return changed draft/caret"
            s.type(b"\x1b")
            s.wait(lambda:"Query list" in s.panel_title(),"second query Escape skipped its list")
            s.type(b"\x1b")
            s.wait(s.main_frame_ready,"query-list Escape did not return to query chat")
            assert draft in s.input_text() and editor_info(s).col==len(draft)-3
            assert peer.messages("ReviewPeer")[0][1]==b"hierarchy-message"
            assert len(peer.messages("ReviewPeer"))==1,"query/list navigation submitted the draft"
            assert s.proc.pid==pid and not peer.messages()
    finally:
        peer.close()
    print("review hierarchy: vanilla F1 setup/Apply -> F2 Connect/Open message view without restart -> channel Join/F3 -> /msg/F4; query Escape returns to list with draft/caret preserved and no unintended send passed",flush=True)


def app_links_license_popup_and_caret():
    embedded=[]
    for line in (ROOT/"src/license.h").read_text().splitlines():
        if line.startswith("    \""): embedded.append(json.loads(line.strip().rstrip(",")))
    assert embedded==(ROOT/"LICENSE.md").read_text().splitlines(),"embedded agreement differs from LICENSE.md"
    for rows,cols,term,locale in ((26,132,"xterm-256color","C.UTF-8"),(9,20,"screen","C")):
        with OfflineTerminal(rows=rows,cols=cols,term=term,locale=locale) as s:
            s.start();s.command("/disconnect")
            draft="abcdefghijklmnop"
            s.type(draft);wait_bytes(s,len(draft))
            s.type(s.category_key(1))
            s.wait(lambda:s.panel_title().startswith("vipirc |") and "[Server" in s.screen.text(),"app setup links missing")
            if cols >= 80:
                s.wait(lambda:"[Background]" in s.screen.text() and "[Insha-Allah license agreement]" in s.screen.text(),
                       "vipirc actions did not finish drawing")
                background = next(i for i,cells in enumerate(s.screen.cells) if "[Background]" in "".join(cells))
                about = next(i for i,cells in enumerate(s.screen.cells) if "[About]" in "".join(cells))
                agreement = next(i for i,cells in enumerate(s.screen.cells) if "[Insha-Allah license agreement]" in "".join(cells))
                assert about == background + 2 and agreement == about + 1, "About/license block is not beside each other below the empty separator"
                assert not "".join(s.screen.cells[background + 1]).strip(), "About/license separator line is not empty"
                warning=next(i for i,cells in enumerate(s.screen.cells) if "Not vegetarian?  www.bloodguiltcurse.net will change your mind" in "".join(cells))
                server=next(i for i,cells in enumerate(s.screen.cells) if "[Server" in "".join(cells))
                assert 2 < warning < server,"welcome warning is not between app title and Server"
                assert "www.thatiam.vip" not in s.screen.text() and "remfan1994" not in s.screen.text(),"duplicate attribution remained on the app screen"
                connection=next(i for i,cells in enumerate(s.screen.cells) if "Connection:" in "".join(cells))
                assert not "".join(s.screen.cells[connection+1]).strip(),"removed attribution row was not left clear"
            s.type(DOWN+b"\r")
            s.wait(lambda:"Settings" in s.panel_title() and "Nickname" in s.screen.text(),"keyboard Nick link failed")
            assert any("[Nickname:" in "".join(row) for row in s.screen.cells),s.screen.text()
            return_app(s)
            s.type(b"G\r")
            s.wait(lambda:s.panel_title().startswith("Insha-Allah") and "Insha-Allah" in s.screen.text(),"keyboard IALA reader missing")
            s.type(b"G")
            s.wait(lambda:"Hamduillah." in s.screen.text(),"full license final clause is inaccessible")
            s.type(b"g")
            s.wait(lambda:"Effective" in s.screen.text() or "Insha-Allah License" in "".join(s.screen.cells[3]),"license Home failed")
            s.type(BEGIN_PASTE+b"/quit\r\x1b"+END_PASTE)
            s.quiet(.04)
            assert s.panel_title().startswith("Insha-Allah") and s.proc.poll() is None,"license interpreted pasted controls"
            return_app(s)
            s.type(b"g"+DOWN*6+b"\r")
            s.wait(lambda:s.panel_title().startswith("About"),"keyboard About reader missing")
            if cols>=80:
                version=re.search(r'^#define VIPIRC_VERSION "([^"]+)"', (ROOT/"src/app.h").read_text(),re.M)[1]
                s.wait(lambda:version in s.screen.text() and "www.thatiam.vip" in s.screen.text() and "remfan1994" in s.screen.text(),
                       "About omitted version, application URL or designer")
                assert "ThatIAm.vip" in s.screen.text() and "IALA" in s.screen.text()
            s.type(b"Gg"+BEGIN_PASTE+b"/quit\r\x1b"+END_PASTE)
            s.quiet(.04)
            assert s.panel_title().startswith("About") and s.proc.poll() is None,"About interpreted pasted controls"
            return_app(s)
            if cols>=80:
                row=next(i for i,cells in enumerate(s.screen.cells) if "[About]" in "".join(cells))
                mouse_click(s,row,3)
                s.wait(lambda:s.panel_title().startswith("About"),"clicked About link did not open its reader")
                return_app(s)
                row=next(i for i,cells in enumerate(s.screen.cells) if "[Insha-Allah license agreement]" in "".join(cells))
                mouse_click(s,row,3)
                s.wait(lambda:s.panel_title().startswith("Insha-Allah"),"clicked IALA link did not open agreement")
                return_app(s)
                nick_row = next(i for i,cells in enumerate(s.screen.cells) if "[Nick:" in "".join(cells))
                mouse_click(s,nick_row,3)
                s.wait(lambda:"Settings | Server 0" in s.panel_title() and focused_label(s,7,"Nickname"),"clicked Nick did not focus nickname")
                return_app(s)
            s.leave_app()
            s.wait(s.main_frame_ready,"app Back did not restore draft")
            assert draft in s.input_text()
            for key,column in ((LEFT,16),(LEFT,15),(RIGHT,16),(HOME,1),(UP,1),(END,17)):
                s.type(s.home_key if key==HOME else b"\x1b[4~" if key==END and term=="screen" else key)
                s.wait(lambda:s.main_frame_ready() and s.screen.col==column and
                       editor_info(s).col in (None,column),"chat arrow/Home/End hid or misplaced caret")
                assert s.screen.cursor_visible and s.screen.reverse_cells[s.screen.row][s.screen.col]
            open_settings(s)
            select_field(s,8);s.type(b"\r")
            s.wait(lambda:"(*)" in s.screen.text() and "Enter: choose" in s.screen.text(),"bounded auth chooser missing")
            selected=next(i for i,cells in enumerate(s.screen.cells) if "(*)" in "".join(cells))
            border=next(i for i,cells in enumerate(s.screen.cells) if "+" in "".join(cells) and "Auto-auth" in "".join(cells))
            line="".join(s.screen.cells[border]);left=line.index("+");right=line.rindex("+")
            assert 0<=left<right<cols and border>=2 and border+5<rows
            assert s.screen.reverse_cells[border+3][left+1],"chooser backdrop is not distinct from form"
            assert not s.screen.reverse_cells[selected][left+2],"selected auth choice is indistinguishable from backdrop"
            s.type(UP+b"\r")
            s.wait(lambda:"Choice staged" in s.screen.text() and "(*)" not in s.screen.text(),"auth choice did not stage")
            return_app(s)
            s.leave_app()
            s.wait(s.main_frame_ready,"auth chooser return lost chat")
            assert draft in s.input_text() and s.config.read_bytes()==s.before_config
    print("review app: inline warning, clean attribution/blank row, adjacent About/license with keyboard/mouse access, Settings/reader parent return, bounded auth popup and visible arrow caret at132/20 columns passed",flush=True)


def manual_parent_search_and_caret():
    for rows,cols,term,locale in ((26,132,"xterm-256color","C.UTF-8"),(9,20,"screen","C")):
        with OfflineTerminal(rows=rows,cols=cols,term=term,locale=locale) as s:
            s.start();s.command("/disconnect");s.command("/pm NickServ")
            def query_ready():
                return s.main_frame_ready() and "Query:" in s.panel_title() and (
                    s.screen.cols<30 or "NickServ" in s.panel_title())
            s.wait(query_ready,"manual fixture query missing")
            draft="MANUAL_QUERY_DRAFT"
            s.type(draft);wait_bytes(s,len(draft));s.type(LEFT*4)
            s.wait(lambda:s.main_frame_ready() and s.screen.col==len(draft)-3 and
                   editor_info(s).col in (None,len(draft)-3),"manual fixture draft caret did not move")
            s.open_help()
            assert "Return to: vipirc" in "".join(s.screen.cells[4])
            assert not any(marker in "\n".join("".join(row) for row in s.screen.cells[3:5])
                           for marker in ("Connection:","Server:","Nick:")),"manual retains the unrelated diagnostic block"
            assert "Back" in "".join(s.screen.cells[3]),"manual Back is not beside Search"
            assert "[Back]" not in "".join(s.screen.cells[-2]),"manual has a duplicate bottom Back control"
            s.type(b"/autoath"+LEFT*2+b"u\x7fU")
            s.wait(lambda:"autoaUth" in "".join(s.screen.cells[-2]) and s.screen.cursor_visible and
                   (s.screen.row,s.screen.col)==(rows-2,13),"manual search insertion/backspace hid or misplaced its caret")
            assert s.screen.reverse_cells[s.screen.row][s.screen.col],"manual search lacks a visible edit-position cell"
            s.type(b"\r")
            s.wait(lambda:re.search(r"Match 1 of \d+\.",s.screen.text()) is not None and not s.screen.cursor_visible,
                   "case-insensitive manual search did not find autoauth")
            matches=int(re.search(r"Match 1 of (\d+)\.",s.screen.text())[1])
            assert any(any(row) for row in s.screen.reverse_cells[5:-2]),"matching manual text is not highlighted"
            s.type("n")
            next_number=2 if matches>1 else 1
            s.wait(lambda:f"Match {next_number} of {matches}." in s.screen.text(),"manual Next did not move or wrap")
            s.type("N")
            s.wait(lambda:f"Match 1 of {matches}." in s.screen.text(),"manual Previous did not restore the match")
            body=tuple("".join(row) for row in s.screen.cells[5:-2])
            s.type("/")
            s.wait(lambda:"autoaUth" in "".join(s.screen.cells[-2]) and s.screen.cursor_visible,"Search did not restore the committed term")
            s.type("cancel")
            s.type(BEGIN_PASTE+b"/quit\r\n\x1b"+END_PASTE)
            s.quiet(.04)
            assert "Help / Manual" in s.panel_title() and s.proc.poll() is None,"manual search interpreted pasted controls"
            s.type(b"\x1b")
            s.wait(lambda:"Help / Manual" in s.panel_title() and not s.screen.cursor_visible,"search Escape left the reader")
            assert tuple("".join(row) for row in s.screen.cells[5:-2])==body,"cancelled search changed the committed match position"

            # A short missing term fits even the minimum-width search field.
            s.type("/");s.type(s.home_key+b"\x1b[3~"*32+b"zzzznonezz\r")
            s.wait(lambda:"No matches." in s.screen.text(),"missing manual search did not explain the result")
            s.type("/");s.type(s.home_key+b"\x1b[3~"*32+b"\r")
            s.wait(lambda:"Search cleared." in s.screen.text(),"empty manual search did not clear the match")

            if cols>=80:
                toolbar="".join(s.screen.cells[3]);mouse_click(s,3,toolbar.index("Search")+1)
            else:
                s.type("/")
            s.wait(lambda:"Find: [" in "".join(s.screen.cells[-2]) and s.screen.cursor_visible,"Search toolbar did not open its editor")
            s.type("X"*300)
            s.wait(lambda:"Search is limited" in s.screen.text() and s.screen.cursor_visible and
                   s.screen.row==s.screen.rows-2 and 7<=s.screen.col<s.screen.cols-1,
                   "manual search did not enforce its 256-byte limit with a visible, bounded caret")
            assert s.screen.cursor_visible and s.screen.row==s.screen.rows-2 and 7<=s.screen.col<s.screen.cols-1
            # Resizing during an edit retains the editor and a bounded cursor.
            target=(9,20) if cols>=80 else (26,132)
            resize(s,*target)
            s.wait(lambda:"Help / Manual" in s.panel_title() and "Find: [" in "".join(s.screen.cells[-2]) and
                   s.screen.cursor_visible and s.screen.row==s.screen.rows-2 and 7<=s.screen.col<s.screen.cols-1,
                   "manual resize lost the search editor or its caret")
            s.type(LEFT+b"\x7fZ")
            s.wait(lambda:s.screen.cursor_visible and s.screen.row==s.screen.rows-2,"manual edit after resize lost its caret")
            s.type(b"\x1b")
            s.wait(lambda:"Help / Manual" in s.panel_title() and not s.screen.cursor_visible,"search cancel after resize left the reader")
            s.type(LEFT)
            s.wait(lambda:"[Back]" in "".join(s.screen.cells[3]),"manual Back cannot be selected beside Search")
            # TERM=screen advertises legacy mouse reports, not this helper's SGR protocol.
            if term=="screen": s.type(b"\r")
            else: mouse_click(s,3,3)
            s.wait(lambda:app_ready(s),"manual toolbar Back did not return to vipirc")
            s.leave_app()
            s.wait(query_ready,"vipirc Back did not restore the underlying query")
            assert draft in s.input_text() and editor_info(s).byte_count==len(draft),"manual/search navigation changed the query draft"
            resize(s,rows,cols)
            s.wait(lambda:s.main_frame_ready() and s.screen.col==len(draft)-3 and
                   editor_info(s).col in (None,len(draft)-3),"manual/search resize changed the retained query caret")

            s.command("/manual",from_chat=True)
            s.wait(lambda:"Help / Manual" in s.panel_title() and
                   "Return to: Query: NickServ"[:s.screen.cols] in "".join(s.screen.cells[4]),
                   "direct /manual did not record its chat parent")
            s.type(b"\x1b")
            s.wait(query_ready,"direct /manual Escape skipped its chat parent")
            assert draft in s.input_text() and s.screen.col==1 and editor_info(s).col in (None,1)
            assert s.config.read_bytes()==s.before_config
    print("review manual: F1/direct-command parents, single nearby Back/Search toolbar, case-insensitive match/highlight/next/previous, cancel/miss/clear, visible editing caret and 256-byte bound, keyboard/mouse/resize/paste isolation at132/20 columns, retained query draft/caret passed",flush=True)


def grouped_activity_sources():
    with Session() as s:
        s.start();s.resize(30,132)
        draft="ACTIVITY_SOURCE_DRAFT"
        s.type(draft);wait_bytes(s,len(draft))
        s.type(s.category_key(1))
        s.wait(lambda:app_ready(s),"app panel did not cover chat")
        s.server.send(":alice!u@fixture PRIVMSG #test :tester MENTION_CHANNEL_BODY")
        s.server.send(":PrivatePeer!u@fixture PRIVMSG tester :MENTION_QUERY_BODY")
        s.server.send("PING :activity-complete")
        s.wait(lambda:any(line==b"PONG :activity-complete" for _,line in s.server.lines) and "F5 Mentions/unread" in "".join(s.screen.cells[0]),"combined activity category missing")
        s.type(s.category_key(5))
        s.wait(lambda:"Mentions and unread" in s.panel_title() and "MENTION_CHANNEL_BODY" in s.screen.text() and "MENTION_QUERY_BODY" in s.screen.text() and
               sum(bool(re.match(r"(?:Unread conversations|Mentions) \(","".join(cells))) or "  Server 0:" in "".join(cells)
                   for cells in s.screen.cells) >= 4,
               "activity did not finish its mention lines and section/server headings")
        headers=[i for i,cells in enumerate(s.screen.cells) if re.match(r"(?:Unread conversations|Mentions) \(","".join(cells)) or "  Server 0:" in "".join(cells)]
        assert len(headers)>=4,s.screen.text()
        mention_heading = next(i for i,cells in enumerate(s.screen.cells) if re.match(r"Mentions \(", "".join(cells)))
        unread_heading = next(i for i,cells in enumerate(s.screen.cells) if re.match(r"Unread conversations \(", "".join(cells)))
        assert mention_heading < unread_heading, "Mentions are not prioritized above unread conversations"
        mention_rows = [i for i,cells in enumerate(s.screen.cells) if "MENTION_" in "".join(cells)]
        assert all(mention_heading < row < unread_heading for row in mention_rows), "a mention appears below the unread section"
        for row in headers:
            assert not any(s.screen.reverse_cells[row]),"section/server header is selectable"
            mouse_click(s,row,3);s.quiet(.025)
            assert "Mentions and unread" in s.panel_title(),"heading activated a conversation"
        row=next(i for i,cells in enumerate(s.screen.cells) if "MENTION_CHANNEL_BODY" in "".join(cells))
        assert re.search(r"\[\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\]", "".join(s.screen.cells[row])),"mention timestamp missing"
        mouse_click(s,row,6)
        s.wait(lambda:s.main_frame_ready() and "Channel 1: #test" in s.panel_title(),"mention did not open its channel source")
        assert draft in s.input_text()
        s.type(s.category_key(5))
        s.wait(lambda:"MENTION_QUERY_BODY" in s.screen.text(),"private mention disappeared after channel selection")
        row=next(i for i,cells in enumerate(s.screen.cells) if "MENTION_QUERY_BODY" in "".join(cells))
        mouse_click(s,row,6)
        s.wait(lambda:s.main_frame_ready() and "Query: PrivatePeer" in s.panel_title(),"mention did not open its query source")
        assert not s.server.messages() and not s.server.messages("PrivatePeer")
    print("review activity: separate inert Unread/Mentions/server headers, full timestamped lines and exact channel/query source activation passed",flush=True)


def delete_and_quit_controls():
    def quit_question(session):
        text=session.screen.text()
        return "Really quit?" in text and "[Y/N]" in text
    delete=b"\x1b[3~"
    with OfflineTerminal(cols=132,rows=26) as s:
        s.start();s.command("/disconnect");s.command("/query DeletePeer")
        draft="DELETE_REQUIRES_DRAFT_CONSENT";s.type(draft);wait_bytes(s,len(draft))
        s.type(s.category_key(4))
        s.wait(lambda:"Query list" in s.panel_title(),"Delete query fixture list missing")
        s.type(delete)
        s.wait(lambda:"[y/N]" in s.screen.text(),"Delete bypassed query draft confirmation")
        s.quiet(.13);s.type(b"\r")
        s.wait(lambda:"[y/N]" not in s.screen.text() and "DeletePeer" in s.screen.text(),"default-No Delete discarded query")
        s.type(b"\r");s.wait(s.main_frame_ready,"query Open did not restore retained Delete draft")
        assert draft in s.input_text()
        s.type(s.category_key(4));s.wait(lambda:"Query list" in s.panel_title(),"query list did not reopen")
        s.type(delete);s.wait(lambda:"[y/N]" in s.screen.text(),"second Delete skipped draft confirmation")
        s.quiet(.13);s.type("y")
        s.wait(lambda:"Query list" in s.panel_title() and
               not any("DeletePeer" in "".join(row) for row in s.screen.cells[4:-2]),"approved Delete did not close query")
        s.type(s.category_key(3));s.wait(lambda:"Channels" in s.panel_title(),"channel Delete list missing")
        s.type(b"g"+DOWN+delete)
        s.wait(lambda:not any("#offline-a" in "".join(row) for row in s.screen.cells[4:-2]) and
               "#offline-b" in s.screen.text() and "0: server" in s.screen.text(),
               "Delete did not close only its selected retained channel")
        s.type(s.category_key(2));s.wait(lambda:"Servers" in s.panel_title(),"primary Delete list missing")
        s.type(delete)
        s.wait(lambda:"primary" in s.screen.text().lower(),"primary server Delete did not explain its protection")
        assert "Servers (1)" in "".join(s.screen.cells[0]) and "[y/N]" not in s.screen.text()
    for rows,cols in ((26,132),(9,20)):
        with OfflineTerminal(rows=rows,cols=cols) as s:
            s.start();s.type("QUIT_DRAFT");wait_bytes(s,10)
            s.type(s.category_key(1));s.wait(lambda:app_ready(s),"F1 quit fixture did not open")
            for key in (b"n",b"N",b"x",b"\r",b"\t",b"\x1b",b"\x04",LEFT,s.category_key(2)):
                s.type(b"\x1b");s.wait(lambda:quit_question(s),"F1 Escape did not ask before quit")
                s.type(key)
                s.wait(lambda:app_ready(s) and "Really quit?" not in s.screen.text(),f"non-Y key {key!r} did not cancel quit to F1")
                assert s.proc.poll() is None
            s.type(b"\x1b");s.wait(lambda:quit_question(s),"quit question did not reopen")
            resize(s,12 if rows==9 else 9,40 if cols==20 else 20)
            s.wait(lambda:quit_question(s),"resize lost the pending quit question")
            s.type(BEGIN_PASTE+b"Yy\r\n\x1b"+END_PASTE);s.quiet(.13)
            assert quit_question(s) and s.proc.poll() is None,"paste approved or canceled quit"
            s.type("n");s.wait(lambda:app_ready(s) and "Really quit?" not in s.screen.text(),"quit cancel after paste failed")
            s.leave_app();assert "QUIT_DRAFT" in s.input_text()
            s.type(s.category_key(1));s.wait(lambda:app_ready(s),"F1 did not reopen after Back")
            s.type(b"\x1b");s.wait(lambda:quit_question(s),"final quit confirmation missing")
            s.quiet(.13);s.type("Y" if rows==9 else "y")
            s.wait(lambda:s.proc.poll() is not None,"fresh explicit Y did not terminate vipirc")
    print("review controls: Delete closes selected channel/query with draft default-No and protectedprimary; F1 Escape onlyfreshY quits, otherkeys cancel, resize/paste do not decide, Back retains draft and terminal flags restore at132/20columns passed",flush=True)


def main():
    progressive_setup_without_restart()
    app_links_license_popup_and_caret()
    manual_parent_search_and_caret()
    grouped_activity_sources()
    delete_and_quit_controls()
    print("all reviewed ncurses UI checks passed",flush=True)


if __name__=="__main__":main()
