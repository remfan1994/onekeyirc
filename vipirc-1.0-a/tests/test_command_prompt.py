#!/usr/bin/env python3
"""Command-screen and bottom-button workflows through real terminal PTYs.

Offline cases use isolated simulation or an unconnected foreground shell job.
Live cases contact only private loopback fixtures. No user profile is read.
"""
import argparse
import configparser
import os
from pathlib import Path
import re

from test_integration import IRCFixture, Session
from test_offline import OfflineTerminal
from test_controls import PromptTerminal, PROMPT, repeat_ctrl_d
from test_input_limits import (BEGIN_PASTE, END_PASTE, WARNING, acknowledge,
                               editor_info, expect_warning, focused_label,
                               mouse_click, mouse_event, status_focus, wait_bytes)
from test_servers import MultiServerSession, HOME, DOWN, UP, RIGHT
from test_status_navigation import resize

DELETE = b"\x1b[3~"
PAGE_UP, PAGE_DOWN = b"\x1b[5~", b"\x1b[6~"


def start_empty(session):
    session.wait(lambda:b"Not vegetarian?" in session.raw,"empty-start welcome missing")
    session.type("x")
    session.wait(session.main_frame_ready,"empty-start chat frame missing")


def app_slash(session):
    session.open_destination("vipirc")
    session.type("/")
    wait_command(session,"/")


def command_page(session):
    caret_row,caret_col=session.screen.row,session.screen.col
    field_rows=3 if session.screen.rows>=14 else 1
    if not (session.screen.cursor_visible and 4<=caret_row<4+field_rows and
            1<=caret_col<session.screen.cols-1 and session.screen.reverse_cells[caret_row][caret_col]):
        return None
    for row,cells in enumerate(session.screen.cells):
        found=re.match(r"Commands (\d+)-(\d+) of (\d+)","".join(cells))
        if found:
            first,last,total=map(int,found.groups())
            names=[]
            for cells in session.screen.cells[row+1:-2]:
                name=re.match(r"/([a-z]+)(?:\s|$)","".join(cells))
                if name:names.append(name[1])
            if len(names)==last-first+1:
                return first,last,total,names
    return None


def full_command_discovery_and_app_slash():
    for rows,cols in ((12,40),(40,132)):
        with OfflineTerminal(configured=False,rows=rows,cols=cols) as session:
            start_empty(session)
            session.command("/set command_suggestions alphabetical", resume_input=False)
            session.open_destination("vipirc")
            session.type(BEGIN_PASTE+b"/quit\r\n"+END_PASTE)
            session.quiet(.03)
            assert session.panel_title().startswith("vipirc |") and session.proc.poll() is None
            session.type("/");wait_command(session,"/")
            observed=[];expected_first=1;pages=0
            while True:
                session.wait(lambda:command_page(session) is not None and command_page(session)[0]==expected_first,
                             "command catalogue page did not finish rendering")
                first,last,total,names=command_page(session)
                assert total==56 and session.command_text()=="/", "paging changed the command or omitted inventory"
                observed.extend(names);pages+=1
                if last==total:break
                expected_first=last+1;session.type(PAGE_DOWN)
            assert len(observed)==56 and observed==sorted(set(observed)),observed
            assert {"connect","join","msg","query","autoauth","autoconnect","autojoin","saveconf",
                    "panel","quickstart","help","server","set","whois"}.issubset(observed)
            session.type(PAGE_DOWN);session.quiet(.03)
            assert command_page(session)[:3]==(first,last,total),"PageDown wrapped past the last command"
            session.type(PAGE_UP*pages)
            session.wait(lambda:command_page(session) is not None and command_page(session)[0]==1,"PageUp did not reach the first commands")
            mouse_event(session,65,rows-3,2)
            session.wait(lambda:command_page(session) is not None and command_page(session)[0]>1,"wheel did not page command choices")
            mouse_event(session,64,rows-3,2)
            session.wait(lambda:command_page(session) is not None and command_page(session)[0]==1,"reverse wheel did not restore first commands")
            session.type(UP);wait_command(session,"/set command_suggestions alphabetical")
            session.type(DOWN);wait_command(session,"/")
            assert command_page(session)[0]==1,"history key unexpectedly paged command choices"
            session.type("conn");wait_command(session,"/conn")
            session.wait(lambda:command_page(session) is not None and command_page(session)[:3]==(1,1,1),
                         "typing did not reset and filter command pages")
            cancel_command(session)
    print("command discovery: F1 slash and parent cancellation, paste isolation, all56 commands through uncapped alphabetical pages at40/132columns, clamped PgUp/PgDn, mouse wheel and independent history/filter editing passed")


def offline_native_configuration_commands():
    with OfflineTerminal(configured=False,cols=132,rows=26) as session:
        start_empty(session);app_slash(session)
        session.type("connect native-fixture.invalid\r")
        # An empty offline profile deliberately gains demonstration channels.
        # Wait for those to settle rather than requiring the transient MOTD
        # frame before simulation auto-joins; the live case below asserts a
        # real channel-free connection stays on its server view.
        session.wait(lambda:session.main_frame_ready() and "native-fixture.invalid:6697" in session.panel_title() and
                     "Channels (2)" in "".join(session.screen.cells[0]),
                     "F1 slash /connect HOST did not open the selected simulator")
        session.command("/channel 0")
        session.wait(lambda:session.main_frame_ready() and "Channel 0: Server" in session.panel_title() and
                     "Offline simulator MOTD" in session.screen.text(),
                     "channel zero did not retain the simulator MOTD")
        session.open_app_diagnostics()
        server_line=session.app_line("Server ")
        assert "native-fixture.invalid:6697" in server_line
        selected_server=int(re.match(r"Server (\d+):",server_line)[1])
        server_count=int(re.search(r"Servers \((\d+)\)","".join(session.screen.cells[0]))[1])
        assert session.app_line("Nick:").split(":",1)[1].strip(),"default Unix/fallback nickname was empty"
        session.close_app_diagnostics()
        session.command("/join #native")
        session.wait(lambda:"#native" in session.panel_title(),"direct connection could not join normally")
        draft="NATIVE_COMMAND_DRAFT";session.type(draft);wait_bytes(session,len(draft))
        for command in ("/autoconnect on","/autojoin add #future","/set send_mode review","/set output_format full"):
            session.command(command)
            assert draft in session.input_text(),"configuration command replaced the conversation draft"
        source="/not-read/private-credential-fixture.gpg"
        session.command("/autoauth sasl fixture-account gpg "+source)
        session.command("/autoauth status")
        session.wait(lambda:session.panel_title()=="Authentication settings" and source in session.screen.text(),
                     "explicit authentication inquiry did not show the configured credential location")
        assert "Next SASL GPG file:" in session.screen.text()
        assert "credential: GPG file (redacted)" in session.screen.text()
        assert not session.screen.cursor_visible
        session.type(b"\x1b")
        session.wait(lambda:session.panel_title().startswith("vipirc |"),
                     "authentication inquiry did not return to the command's opening screen")
        session.leave_app()
        assert draft in session.input_text(),"authentication inquiry replaced the conversation draft"
        session.command("/autoauth off")
        session.open_command();session.type("autoauth");session.type(UP)
        wait_command(session,"/autoauth")
        assert "fixture-account" not in session.screen.text() and source not in session.screen.text()
        cancel_command(session)
        session.command("/help autoconnect")
        session.wait(lambda:"Help / Manual" in session.panel_title() and "Match " in session.screen.text(),
                     "help topic did not open its matching manual text")
        assert "Return to: vipirc" in session.screen.text();session.close_help()
        session.command("/quickstart")
        session.wait(lambda:f"Quickstart | Server {selected_server+1}" in session.panel_title() and
                     "".join(session.screen.cells[5]).strip()=="Server" and session.screen.cursor_visible,
                     "quickstart did not open the selected server's guided connection settings")
        session.input_mode()
        assert draft in session.input_text(),"quickstart cancellation lost the conversation draft"
        session.command("/panel")
        session.wait(lambda:session.panel_title().startswith("Settings panel") and
                     "Choose a panel" in session.screen.text() and "0: Application preferences" in session.screen.text(),
                     "panel did not offer separate application and server settings")
        session.input_mode()
        assert draft in session.input_text(),"panel cancellation lost the conversation draft"
        session.command("/ls channels")
        session.wait(lambda:"Channels" in session.panel_title() and "0: server" in session.screen.text(),"ls channels omitted channel zero")
        session.input_mode()
        session.command("/query NativePeer")
        session.wait(lambda:"Query: NativePeer" in session.panel_title(),"query alias did not open a private view")
        session.command("/channel #native");assert draft in session.input_text()
        session.command("/saveconf",error="does not write configuration");cancel_command(session)
        secret="REJECTED_CONNECT_CREDENTIAL"
        session.command("/connect extra.invalid 6697 "+secret,error="Extra connection arguments")
        assert secret not in "".join(session.screen.cells[-1]),"rejected credential was echoed into diagnostics"
        cancel_command(session)
        session.open_command();session.type("connect ");session.type(UP)
        wait_command(session,"/connect ")
        session.wait(lambda:"No matching command history." in session.screen.text(),
                     "rejected connection arguments entered this server's history")
        assert secret not in session.command_text();cancel_command(session)
        # The successful initial /connect belongs to its originating server.
        session.command("/server 0")
        session.open_command();session.type("connect ");session.type(UP)
        wait_command(session,"/connect native-fixture.invalid")
        assert secret not in session.command_text();cancel_command(session)
        session.command(f"/server {selected_server}");session.command("/channel #native")
        assert f"Servers ({server_count})" in "".join(session.screen.cells[0]) and draft in session.input_text()
    print("native curses commands offline: direct HOST/defaultnick, auto-connect/join/auth, redacted auth status/no recall, shared preferences, topic help/panel/quickstart/list/query aliases, blocked Save and invalid credential omission with unchanged draft/files passed")


def live_native_profile_and_current_server_close():
    with MultiServerSession() as session:
        session.start();session.type("PRIMARY_NATIVE_DRAFT");wait_bytes(session,len("PRIMARY_NATIVE_DRAFT"))
        second=IRCFixture();session.peers.append(second)
        app_slash(session)
        session.type(f"connect -plain -nick BridgeNick 127.0.0.1 {second.port}\r")
        session.wait(lambda:second.registered and session.main_frame_ready() and "Channel 0: Server" in session.panel_title(),
                     "native curses connect did not select/register the loopback server")
        session.wait(lambda:"Local fixture MOTD" in session.screen.text(),"native connect omitted the new server MOTD")
        assert second.nick=="BridgeNick" and not any(line.startswith(b"JOIN ") for _,line in second.lines),"new connection inherited a previous autojoin"
        session.command("/join #second")
        session.wait(lambda:"#second" in session.panel_title(),"new server channel did not open")
        session.type("SECOND_NATIVE_DRAFT");wait_bytes(session,len("SECOND_NATIVE_DRAFT"))
        session.command("/autoconnect on");session.command("/autojoin add #future");session.command("/saveconf")
        profile_dir=Path(str(session.config)+".d")
        profiles=list(profile_dir.glob("server-*.ini"));assert len(profiles)==1
        saved=configparser.ConfigParser();saved.read(profiles[0])
        assert saved.get("connection","nick")=="BridgeNick" and saved.getint("connection","port")==second.port
        assert saved.getboolean("connection","autoconnect") and not saved.getboolean("connection","tls")
        assert set(saved.get("connection","channel").split(","))=={"#second","#future"}
        assert saved.get("connection","auth")=="none"
        assert f"default={profiles[0].name}" in Path(str(session.config)+".servers").read_text()
        assert not any(line.startswith(b"JOIN #future") for _,line in second.lines),"autojoin staging joined immediately"
        session.command("/server 0");session.command("/channel #test")
        assert "PRIMARY_NATIVE_DRAFT" in session.input_text()
        session.command("/server 1");session.command("/channel #second")
        assert "SECOND_NATIVE_DRAFT" in session.input_text()
        session.command("/server close 1")
        session.wait(lambda:session.main_frame_ready() and "Servers (1)" in "".join(session.screen.cells[0]),
                     "closing the current server left a freed/stuck command UI")
        session.command("/channel #test");assert "PRIMARY_NATIVE_DRAFT" in session.input_text()
        session.server.send("PING :native-close-survivor")
        session.wait(lambda:any(line==b"PONG :native-close-survivor" for _,line in session.server.lines),
                     "current-server close stopped the surviving connection")
        session.command("/server close 0",error="primary entry");cancel_command(session)
        assert not session.server.messages() and not second.messages(),"native profile/navigation commands submitted a retained draft"
    print("native curses commands live: direct local connect/explicitnick/MOTD, independent autojoin/autoconnect/saveconf/default, per-server drafts, safe deferred current-server close, protected primary and surviving PING passed")


def cancel_command(session, mouse=False, from_app=True):
    assert session.in_command(), session.screen.text()
    if mouse:
        click_command_button(session, "Cancel")
    else:
        session.type(b"\x1b")
    session.finish_command_cancel(from_app)
    assert session.screen.cursor_visible, "cancel did not restore the chat caret"


def click_command_button(session, label):
    row = session.screen.rows - 2
    session.wait(lambda:label in "".join(session.screen.cells[row]), "command footer did not finish rendering: " + label)
    line = "".join(session.screen.cells[row])
    assert label in line, (label, session.screen.text())
    mouse_click(session, row, line.index(label) + len(label) // 2)


def run_prompt(session):
    assert session.in_command(), session.screen.text()
    session.type(b"\r")
    session.wait(lambda: session.is_main() and session.mode() == "INPUT",
                 "successful Run did not restore chat editing")
    assert session.screen.cursor_visible


def wait_command(session, text):
    def caret_ready():
        row,col=session.screen.row,session.screen.col
        rows=3 if session.screen.rows>=14 else 1
        return 4<=row<4+rows and 1<=col<=session.screen.cols-2 and session.screen.reverse_cells[row][col]
    session.wait(lambda: session.in_command() and session.command_text() == text.rstrip() and
                 "Run" in "".join(session.screen.cells[-2]) and "Cancel" in "".join(session.screen.cells[-2]) and caret_ready(),
                 "command editor differs from expected text: " + text)
    assert session.screen.cursor_visible, "command caret disappeared"


def command_boundaries_and_geometry():
    with OfflineTerminal() as session:
        session.start()
        info = wait_bytes(session, 0)
        footer = session.screen.rows - 1
        header = "".join(session.screen.cells[0])
        assert "Channel 1: #offline-a" in session.panel_title()
        assert all(label not in header for label in ("NAVIGATION", "INPUT", "must-not-resolve.invalid", "offline_tester", "TLS"))
        assert re.search(r"Clear\]?\s+\[?Send", "".join(session.screen.cells[footer]))
        assert "Settings" not in "".join(session.screen.cells[footer])
        assert "Help" not in "".join(session.screen.cells[footer])
        wanted = min(max(1, session.screen.rows - 9), info.budget // (session.screen.cols - 2) + 1)
        assert session.screen.rows - info.label_row - 3 == wanted
        assert session.screen.cursor_visible

        session.input_mode()
        draft = "retained channel draft"
        session.type(draft)
        wait_bytes(session, len(draft))
        session.type(session.home_key + RIGHT * 4)
        session.wait(lambda: editor_info(session).col == 5, "initial draft caret missing")
        session.open_command()
        assert "Clear" not in "".join(session.screen.cells[-1])
        assert "Run" in "".join(session.screen.cells[-2]) and "Cancel" in "".join(session.screen.cells[-2])
        session.type("watch add cancelled")
        wait_command(session, "/watch add cancelled")
        cancel_command(session)
        current = wait_bytes(session, len(draft))
        assert current.label_row == info.label_row and current.col == 5
        assert draft in session.input_text()

        # Typing slash at the start opens Command without inserting it into
        # an existing chat draft. A slash in the middle remains normal text.
        session.input_mode()
        session.type(session.home_key + b"/")
        wait_command(session, "/")
        cancel_command(session, mouse=True, from_app=False)
        assert wait_bytes(session, len(draft)).col == 1
        assert draft in session.input_text()
        session.clear_input()
        session.type("ab/cd")
        wait_bytes(session, 5)
        assert not session.in_command() and "ab/cd" in session.input_text()
        assert editor_info(session).label_row == info.label_row

        # A clipboard slash belongs to the chat draft and cannot open a
        # command screen or invoke a local operation.
        session.clear_input()
        session.type(BEGIN_PASTE + b"/quit" + END_PASTE)
        wait_bytes(session, 5)
        assert session.is_main() and session.mode() == "INPUT"
        assert "/quit" in session.input_text() and session.proc.poll() is None
        assert b"<offline_tester>" not in session.raw
    print("command boundaries: slash entry, literal middle/pasted slash, distinct editor, preserved draft/caret, hardware focus and fixed main geometry passed")


def completion_and_history():
    with OfflineTerminal() as session:
        session.start()
        session.input_mode()
        draft = "CHAT_DRAFT_UNTOUCHED"
        session.type(draft)
        wait_bytes(session, len(draft))
        session.open_command()
        session.type("jo")
        wait_command(session, "/jo")
        session.wait(lambda: "/join #CHANNEL" in session.screen.text(), "join usage suggestion missing")
        session.type(RIGHT)
        wait_command(session, "/join")
        assert session.proc.poll() is None, "completion executed a command"
        cancel_command(session)
        assert draft in session.input_text()

        session.open_command()
        session.type("m")
        wait_command(session, "/m")
        for name in ("/manual", "/me", "/mentions", "/mouse", "/msg"):
            assert name in session.screen.text(), ("ambiguous suggestion missing", name, session.screen.text())
        cancel_command(session)

        for text in ("/watch add caf\u00e9", "/watch add alpha", "/watch add beta"):
            session.command(text, resume_input=False)
            assert session.mode() == "INPUT" and draft in session.input_text()
        session.open_command()
        session.type("watch add ")
        wait_command(session, "/watch add")
        for expected in ("/watch add beta", "/watch add alpha", "/watch add caf\u00e9"):
            session.type(UP)
            wait_command(session, expected)
        for expected in ("/watch add alpha", "/watch add beta", "/watch add"):
            session.type(DOWN)
            wait_command(session, expected)
        cancel_command(session)

        for unused in range(2):
            session.command("/watch list", resume_input=False)
        session.open_command()
        session.type(UP)
        wait_command(session, "/watch list")
        session.type(UP)
        wait_command(session, "/watch add beta")
        cancel_command(session)

        session.command("/join #command-private transient-secret-key", resume_input=False)
        session.open_command()
        session.type("join ")
        session.type(UP)
        wait_command(session, "/join")
        assert "transient-secret-key" not in session.screen.text(), "keyed join leaked into command recall"
        cancel_command(session)
        session.command("/channel #offline-a", resume_input=False)
        assert draft in session.input_text()
        assert session.config.read_bytes() == session.before_config
    print("command completion/history: usage suggestions, unique completion, ambiguous names, UTF-8 arguments, filtered newest-first recall, original-line restoration, deduplication and keyed-join omission passed")


def exact_completion_before_ranked_prefixes():
    for mode in ("common", "contextual"):
        with OfflineTerminal(config_extra="\n[input]\ncommand_suggestions="+mode+"\n") as session:
            session.start()
            for exact, matches in (("cl", {"cl", "clear", "clearinput", "close"}),
                                   ("server", {"server", "servers"})):
                session.open_command()
                session.type(exact)
                wait_command(session, "/"+exact)
                session.type(RIGHT)
                session.wait(lambda: session.in_command() and session.command_text() == "/"+exact and
                             (session.screen.row, session.screen.col) == (4, len(exact)+3),
                             mode+" completion replaced an exact /"+exact+" command")
                seen = {exact}
                previous = exact
                for _ in range(len(matches)-1):
                    session.type(RIGHT)
                    session.wait(lambda: session.command_text().lstrip("/") in matches and
                                 session.command_text() != "/"+previous,
                                 "completion did not cycle to another matching command")
                    previous = session.command_text()[1:]
                    assert previous not in seen, "completion repeated a match before visiting the full prefix set"
                    seen.add(previous)
                assert seen == matches
                session.type(RIGHT)
                wait_command(session, "/"+exact)
                session.type("kept")
                wait_command(session, "/"+exact+" kept")
                session.type(RIGHT)
                session.quiet(.03)
                assert session.command_text() == "/"+exact+" kept", "completion replaced existing arguments"
                cancel_command(session)
    print("command completion: Common/Contextual keep exact /cl and /server, append one space, cycle every prefix match and preserve arguments")


def contextual_suggestions_and_insert_review():
    with OfflineTerminal(config_extra="\n[input]\ncommand_suggestions=contextual\n", cols=132) as session:
        session.start()
        session.open_command()
        session.wait(lambda: command_page(session) is not None, "main-context suggestions did not render")
        assert command_page(session)[3][0] == "summary", "main screen inherited the active channel's suggestions"
        session.type(DOWN)
        session.wait(lambda: "[/summary]" in session.screen.text() and not session.screen.cursor_visible,
                     "Down did not highlight the first suggestion")
        session.type("?")
        session.wait(lambda: "Help: /summary" in session.screen.text() and "About command screen" in session.screen.text(),
                     "selected suggestion help omitted its command or containing screen")
        session.type(b"\r")
        session.wait(lambda: "command screen" in session.screen.text().lower() and "Help: /summary" not in session.screen.text(),
                     "parent command-screen help link did not open")
        session.type(b"\x1b")
        session.wait(lambda: "Help: /summary" in session.screen.text(), "help Back did not restore selected-command help")
        session.type(b"\x1b")
        session.wait(lambda: session.in_command() and "[/summary]" in session.screen.text(),
                     "closing help did not preserve the command suggestion selection")
        session.type(b"\r\r")
        wait_command(session, "/summary")
        assert session.proc.poll() is None and session.in_command(), "queued Enter executed an inserted suggestion"
        cancel_command(session)

        session.open_command(from_chat=True)
        session.wait(lambda: command_page(session) is not None, "channel-context suggestions did not render")
        assert command_page(session)[3][:2] == ["me", "topic"]
        session.type("quit")
        wait_command(session, "/quit")
        assert "/quit" in session.screen.text(), "context filtering hid a matching global command"
        session.type(DOWN + b"\r\r\r")
        wait_command(session, "/quit")
        assert session.proc.poll() is None and "Exit all sessions?" not in session.screen.text()
        cancel_command(session, from_app=False)

        session.open_command(from_chat=True)
        session.type("me why?")
        wait_command(session, "/me why?")
        assert session.in_command(), "ordinary question mark opened help while editing text"
        cancel_command(session, from_app=False)

        session.command("/channel 0")
        session.open_command(from_chat=True)
        session.wait(lambda: command_page(session) is not None, "server-context suggestions did not render")
        assert command_page(session)[3][:2] == ["join", "list"]
        cancel_command(session, from_app=False)

        session.command("/query SuggestionPeer")
        session.open_command(from_chat=True)
        session.wait(lambda: command_page(session) is not None, "query-context suggestions did not render")
        assert command_page(session)[3][:2] == ["me", "whois"]
        cancel_command(session, from_app=False)
        assert session.config.read_bytes() == session.before_config
        assert session.tree() == session.before_tree, "offline suggestions wrote usage files"
    print("contextual suggestions: main/channel/server/query origins, complete prefix matching, literal question marks, Down selection, insertion-only Enter and queued-Enter guard passed")


def personal_suggestion_deletion():
    with OfflineTerminal(config_extra="\n[input]\ncommand_suggestions=history\n", cols=132) as session:
        session.start()
        session.command("/watch add unique-history-one")
        session.command("/watch add unique-history-two")
        session.open_command()
        session.type("watch add ")
        wait_command(session, "/watch add")
        session.type(DOWN)
        session.wait(lambda: "[/watch add unique-history-two]" in session.screen.text(), "recent history suggestion was not selectable")
        session.type(DELETE)
        session.wait(lambda: "unique-history-two" not in session.screen.text(), "Delete retained the selected recent entry")
        assert "unique-history-one" in session.screen.text()
        session.type(b"\r")
        wait_command(session, "/watch add unique-history-one")
        cancel_command(session)
        session.command("/set command_suggestions frequent")
        session.command("/whois CountingPeer")
        session.command("/whois CountingPeer")
        session.open_command()
        session.type("whois")
        wait_command(session, "/whois")
        session.type(DOWN + DELETE)
        session.wait(lambda: "Reset this command's usage count" in session.screen.text(), "Delete did not reset a personal usage count")
        assert "/whois NICK" in session.screen.text(), "reset removed a supported catalog command"
        session.type(UP)
        cancel_command(session)
        assert session.config.read_bytes() == session.before_config
        assert session.tree() == session.before_tree
    print("personal suggestions: recent deletion, exact history insertion, personal count reset and catalog preservation passed")


def local_list_alias_completion():
    with Session() as session:
        session.start()
        draft="LIST_ALIASES_KEEP_CHAT_DRAFT"
        session.type(draft);wait_bytes(session,len(draft))
        for name,title in (("cl","Channels"),("channels","Channels"),
                           ("ql","Query list"),("queries","Query list"),
                           ("sl","Servers"),("servers","Servers")):
            session.open_command()
            session.type(name)
            wait_command(session,"/"+name)
            session.type(RIGHT)
            session.wait(lambda:session.in_command() and session.command_text()=="/"+name and
                         (session.screen.row,session.screen.col)==(4,len(name)+3),
                         "list completion did not append its normal argument space: "+name)
            session.type(b"\r")
            session.wait(lambda:title in session.panel_title() and not session.in_command(),
                         "completed list alias did not execute: "+name)
            session.type(b"\x1b")
            session.wait(session.main_frame_ready,"list Escape failed after completed "+name)
            assert draft in session.input_text(),"local list completion discarded the chat draft"
        for name in ("cl","ql","queries","sl","servers"):
            session.open_command()
            session.type(name+" extra")
            wait_command(session,"/"+name+" extra")
            session.type(b"\r")
            session.wait(lambda:session.in_command() and "Usage:" in "".join(session.screen.cells[-1]),
                         "list alias accepted unexpected arguments: "+name)
            cancel_command(session)
        session.server.send("PING :alias-actions-complete")
        session.wait(lambda:any(line==b"PONG :alias-actions-complete" for _,line in session.server.lines),
                     "alias fixture did not reach IRC barrier")
        assert not session.server.messages(),"local list aliases sent the retained chat draft"
        assert not any(line.startswith(b"LIST") for _,line in session.server.lines),"local aliases sent a public-directory request"
    print("command list aliases: completed /cl-/channels, /ql-/queries and /sl-/servers accept their trailing space, reject extra arguments, retain drafts and send no IRC LIST/chat passed")


def command_editor_geometry():
    with OfflineTerminal() as session:
        session.start()
        session.input_mode()
        draft = "CHAT_GEOMETRY_DRAFT"
        session.type(draft)
        wait_bytes(session, len(draft))
        for rows, cols in ((9, 20), (12, 40), (24, 80), (40, 160)):
            resize(session, rows, cols)
            session.open_command()
            field_rows = 3 if rows >= 14 else 1
            info = wait_bytes(session, 1)
            assert info.label_row == 4 + field_rows
            assert session.screen.cells[3][0] == session.screen.cells[3][-1] == "+"
            assert session.screen.cells[info.label_row][0] == session.screen.cells[info.label_row][-1] == "+"
            for row in session.screen.cells[4:4 + field_rows]:
                assert row[0] == row[-1] == "|", "command text lacks a complete outline"
            text = "/watch add " + "A" * (cols - 2) + "\u4e2d"
            session.type(text[1:])
            count = len(text.encode("utf-8"))
            assert wait_bytes(session, count).label_row == info.label_row
            assert session.screen.cursor_visible and 1 <= session.screen.col <= cols - 2
            assert 4 <= session.screen.row < 4 + field_rows
            session.type(HOME)
            session.wait(lambda:(session.screen.row, session.screen.col)==(4,1), "command Home did not reach its first cell")
            session.type(b"\x1bOF\x7f")
            wait_bytes(session, count - 3)
            assert session.in_command(), "wrapped Backspace escaped the command field"
            resize(session, 8, 19)
            session.wait(lambda:"Resize; Esc returns" in session.screen.text(), "undersized Command has no recovery cue")
            resize(session, 24, 80)
            wait_bytes(session, count - 3)
            cancel_command(session)
            assert draft in session.input_text(), "command resize/editing changed the chat draft"
            assert wait_bytes(session, len(draft)).byte_count == len(draft)
        assert b"<offline_tester>" not in session.raw
    print("command editor: compact/full fixed borders, wrapped UTF-8 editing, visible caret, Home/End/Backspace and below-minimum resize recovery passed")


def command_global_selector():
    with OfflineTerminal() as session:
        session.start()
        draft="COMMAND_GLOBAL_SELECTOR_DRAFT"
        session.type(draft);wait_bytes(session,len(draft))
        session.open_command()
        session.type("jo")
        wait_command(session,"/jo")
        session.type(b"\t")
        session.quiet(0.04)
        assert session.header_focus() is None and session.in_command(),"Command Tab unexpectedly entered global navigation"
        assert session.command_text()=="/jo","Tab unexpectedly changed the command"
        session.type(b"\x1b")
        session.finish_command_cancel()
        session.open_destination("Servers")
        session.wait(lambda:"Servers" in session.panel_title() and not session.in_command(),"function-key shortcut did not open Servers")
        assert b"<offline_tester>" not in session.raw
        session.type(b"\x1b")
        session.wait(lambda:session.is_main(),"server Escape did not return to chat")
        assert draft in session.input_text()
    print("command selector: Tab leaves Command intact; function-key category navigation opens a chosen panel without running draft commands")


def errors_and_paste():
    with OfflineTerminal(no_connect=True) as session:
        session.start()
        session.input_mode()
        draft = "UNCONNECTED_CHAT_DRAFT"
        session.type(draft)
        wait_bytes(session, len(draft))
        for text, error in (("/not-a-command", "Unknown command"),
                            ("/manual extra", "Usage:"),
                            ("/join #requires-server", "registered")):
            session.command(text, error=error, resume_input=False)
            assert session.command_text() == text and session.mode() == "COMMAND"
            cancel_command(session)
            assert draft in session.input_text()

        session.open_command()
        session.type("watch add caf")
        session.type(b"\xc3")
        repeat_ctrl_d(session, "fragmented UTF-8 command")
        session.type(b"\xa9")
        wait_command(session, "/watch add caf\u00e9")
        session.type(b"\x18\x02\x01")
        session.quiet(0.04)
        wait_command(session, "/watch add caf\u00e9")
        cancel_command(session)
        assert draft in session.input_text()

        session.open_command()
        session.type(BEGIN_PASTE + b"watch add REVIEW_ONLY\r\n/quit\t\x1b" + END_PASTE)
        session.wait(lambda: session.in_command() and "REVIEW_ONLY" in session.command_text(),
                     "command paste did not remain available for review")
        session.quiet(0.08)
        assert session.proc.poll() is None and session.in_command(), "pasted controls executed or cancelled Command"
        assert "\n" not in session.command_text() and "\r" not in session.command_text()
        cancel_command(session, mouse=True)
        assert draft in session.input_text()
        session.command("/watch list", resume_input=False)
        assert "REVIEW_ONLY" not in session.screen.text(), "unconfirmed command paste changed watch terms"
        assert draft in session.input_text()
    print("command failures/paste: editable diagnostics, stopped-session return, fragmented UTF-8, ignored controls and review-only clipboard data passed")


def foreground_job_controls():
    with PromptTerminal() as session:
        session.type("x")
        session.wait(lambda: b"Set user nick:" in session.raw, "job-control nickname prompt missing")
        session.type("command_tester\r")
        session.wait(lambda: session.is_main(), "unconnected shell job did not enter main")
        session.open_command()
        session.type("watch add retained")
        wait_command(session, "/watch add retained")
        repeat_ctrl_d(session, "parented command prompt", session.assert_foreground)
        wait_command(session, "/watch add retained")
        group = session.client_group
        begin = len(session.raw)
        session.type(b"\x1a")
        session.wait(lambda: os.tcgetpgrp(session.master) == session.proc.pid and
                     b"Stopped" in session.raw[begin:], "Ctrl-Z did not suspend Command to the parent shell")
        os.kill(group, 0)
        session.type("fg\n")
        session.wait(lambda: os.tcgetpgrp(session.master) == group and session.in_command(),
                     "fg did not restore the command screen")
        wait_command(session, "/watch add retained")
        assert session.screen.cursor_visible
        session.finish()
        assert session.client_group is None, "explicit Ctrl-C did not return to a usable shell"
    print("command job controls: held Ctrl-D preserves parent shell, Ctrl-Z/fg restore prompt and text, explicit Ctrl-C exits cleanly passed")


def live_mouse_and_commands():
    with Session() as session:
        session.start()
        session.input_mode()
        draft = "PRIVATE_DRAFT_NOT_SENT"
        session.type(draft)
        wait_bytes(session, len(draft))
        session.open_command()
        session.type("who")
        wait_command(session, "/who")
        session.wait(lambda: any("/whois NICK" in "".join(row) for row in session.screen.cells[7:-2]),
                     "mouse completion candidate is missing")
        row = next(i for i in range(7, session.screen.rows - 2)
                   if "/whois NICK" in "".join(session.screen.cells[i]))
        line = "".join(session.screen.cells[row])
        before = len(session.server.lines)
        mouse_click(session, row, line.index("/whois") + 2)
        wait_command(session, "/whois")
        assert not any(line.startswith(b"WHOIS") for unused, line in session.server.lines[before:]), \
            "candidate selection executed WHOIS"
        session.type("MousePeer")
        wait_command(session, "/whois MousePeer")
        click_command_button(session, "Run")
        session.wait(lambda: session.is_main() and session.mode() == "INPUT", "mouse Run did not return")
        session.wait(lambda: any(line == b"WHOIS MousePeer" for unused, line in session.server.lines),
                     "mouse Run did not send the reviewed command")
        assert "Channel 0: Server" in "".join(session.screen.cells[2]), "WHOIS did not show its server reply view"
        assert not session.server.messages(), "WHOIS sent the retained chat draft"
        session.command("/channel 1", resume_input=False)
        assert draft in session.input_text() and not session.server.messages()

        session.open_command()
        session.type("whois RetainedArgument")
        wait_command(session, "/whois RetainedArgument")
        session.type(RIGHT)
        wait_command(session, "/whois RetainedArgument")
        candidate_row=next(i for i in range(7,session.screen.rows-2)
                           if "/whois NICK" in "".join(session.screen.cells[i]))
        candidate_line="".join(session.screen.cells[candidate_row])
        mouse_click(session,candidate_row,candidate_line.index("/whois")+2)
        wait_command(session, "/whois RetainedArgument")
        assert not any(line == b"WHOIS RetainedArgument" for unused,line in session.server.lines), \
            "completing a command with arguments executed it"
        cancel_command(session)
        assert draft in session.input_text()

        session.command("/msg MessagePeer caf\u00e9\u4e2d", resume_input=False)
        session.wait(lambda: session.server.messages("MessagePeer"), "explicit UTF-8 message command was not sent")
        assert session.server.messages("MessagePeer")[-1][1] == "caf\u00e9\u4e2d".encode("utf-8")
        assert draft in session.input_text() and not session.server.messages("#test")
        session.open_command()
        session.type(BEGIN_PASTE + b"msg MessagePeer REVIEW_ONLY\r\n/quit" + END_PASTE)
        session.server.send("PING :review-command-paste")
        session.wait(lambda: any(line == b"PONG :review-command-paste" for unused, line in session.server.lines),
                     "command screen stopped servicing the server")
        assert session.in_command() and session.proc.poll() is None
        assert len(session.server.messages("MessagePeer")) == 1
        cancel_command(session)
        assert draft in session.input_text()
    print("command live/mouse: candidate selection without execution, explicit Run, UTF-8 wire payload, untouched chat draft and PING during paste review passed")


def per_server_history():
    with MultiServerSession() as session:
        session.start()
        session.command("/watch add primary-memory", resume_input=False)
        slot, second = session.add_server(nick="history_second")
        assert slot == 1 and second.registered
        session.open_command()
        session.type("watch add ")
        session.type(UP)
        wait_command(session, "/watch add")
        assert "primary-memory" not in session.screen.text(), "command history crossed servers"
        cancel_command(session)
        session.command("/watch add secondary-memory", resume_input=False)
        session.select_server(0)
        session.open_command()
        session.type("watch add ")
        session.type(UP)
        wait_command(session, "/watch add primary-memory")
        assert "secondary-memory" not in session.screen.text()
        cancel_command(session)
        session.select_server(1)
        session.open_command()
        session.type("watch add ")
        session.type(UP)
        wait_command(session, "/watch add secondary-memory")
        cancel_command(session)
        assert not session.server.messages() and not second.messages()
    print("command history: independent server-session recall passed")


def bottom_send_confirmation():
    with Session() as session:
        session.start()
        session.input_mode()
        original = wait_bytes(session, 0)
        session.type("one reviewed line")
        wait_bytes(session, len("one reviewed line"))
        footer = session.screen.rows - 1
        footer_text = "".join(session.screen.cells[footer])
        mouse_click(session, footer, footer_text.index("Send") + 1)
        session.wait(lambda: len(session.server.messages()) == 1, "bottom Send did not send the current draft")
        assert session.server.messages()[0][1] == b"one reviewed line"
        assert wait_bytes(session, 0).label_row == original.label_row
        session.wait(lambda:"1 message queued; paced at one per 1.2 seconds." in
                     "".join(session.screen.cells[-1]),
                     "send pacing notice did not appear in the bottom status area")
        assert not any("queued; paced" in "".join(row) for row in session.screen.cells[:-1]), \
            "queue notice duplicated itself in the chat/header/input area"
        assert session.mode() == "INPUT", "mouse Send unexpectedly changed the editing mode"

        text = "B" * (original.budget + 12)
        session.type(BEGIN_PASTE + text.encode("ascii") + END_PASTE)
        expect_warning(session)
        acknowledge(session)
        assert editor_info(session).label_row == original.label_row
        for decision in (b"\r", b"y"):
            session.input_mode()
            session.type(b"\r")
            session.wait(lambda: "[y/N]" in session.screen.text(), "Send bypassed multi-line confirmation")
            assert len(session.server.messages()) == 1
            session.quiet(0.13)
            session.type(decision)
            session.wait(lambda: "[y/N]" not in session.screen.text(), "split-send decision was not applied")
            if decision == b"\r":
                wait_bytes(session, len(text))
                assert len(session.server.messages()) == 1, "default decision sent multiple lines"
                assert editor_info(session).label_row == original.label_row
        session.wait(lambda: len(session.server.messages()) == 3, "explicit split approval did not send two lines", timeout=7)
        assert b"".join(body for unused, body in session.server.messages()[1:]) == text.encode("ascii")
        assert wait_bytes(session, 0).label_row == original.label_row
    print("bottom Send: mouse Send and ordinary Enter, preserved input/height, default-No split decision and exact approved payload passed")


def command_split_confirmation():
    with Session() as session:
        session.start()
        session.input_mode()
        draft = "CHAT_DRAFT_AND_CARET_STAY"
        session.type(draft)
        baseline = wait_bytes(session, len(draft))
        session.type(HOME + RIGHT * 4)
        session.wait(lambda:editor_info(session).col == 5, "chat caret setup failed")
        text = "C" * (baseline.budget + 13)
        for accept in (False, True):
            session.open_command()
            session.type(BEGIN_PASTE + ("msg #test " + text).encode("ascii") + END_PASTE)
            expect_warning(session)
            acknowledge(session)
            wait_bytes(session, len(text))
            session.type(b"\r")
            session.wait(lambda:"[y/N]" in session.screen.text(), "command Run skipped split-send consent")
            assert not session.server.messages(), "command payload was sent before approval"
            session.quiet(0.13)
            session.type(b"y" if accept else b"\r")
            session.wait(lambda:"[y/N]" not in session.screen.text(), "command split decision did not finish")
            if not accept:
                assert session.in_command(), "default-No discarded the editable command"
                wait_bytes(session, len(text))
                assert not session.server.messages()
                cancel_command(session)
            else:
                session.wait(lambda:session.is_main() and session.mode()=="INPUT", "approved command did not return to chat editing")
                session.wait(lambda:len(session.server.messages())==2, "approved command did not send exactly two lines", timeout=7)
                assert b"".join(body for unused,body in session.server.messages()) == text.encode("ascii")
            restored=wait_bytes(session,len(draft))
            assert restored.col == 5 and restored.label_row == baseline.label_row
            assert draft in session.input_text(), "command confirmation cleared or changed the chat draft"
    print("command split consent: early warning, default-No retains the separate editor, explicit approval sends exact payload, original chat draft/caret remain intact")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--offline-only", action="store_true")
    modes.add_argument("--live-only", action="store_true")
    args = parser.parse_args()
    if not args.live_only:
        full_command_discovery_and_app_slash()
        offline_native_configuration_commands()
        command_boundaries_and_geometry()
        completion_and_history()
        exact_completion_before_ranked_prefixes()
        contextual_suggestions_and_insert_review()
        personal_suggestion_deletion()
        command_editor_geometry()
        command_global_selector()
        errors_and_paste()
        foreground_job_controls()
    if not args.offline_only:
        live_native_profile_and_current_server_close()
        local_list_alias_completion()
        live_mouse_and_commands()
        per_server_history()
        bottom_send_confirmation()
        command_split_confirmation()
    print("all selected command-prompt PTY tests passed")


if __name__ == "__main__":
    main()
