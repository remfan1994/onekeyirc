#!/usr/bin/env python3
"""Guided native setup in private PTYs; any IRC peers are loopback-only."""
import argparse
import configparser
import re
import stat

from test_idle_terminal import IdleSession, BEGIN, END, FINISH, assert_no_persistent_hints
from test_integration import IRCFixture

UP=b"\x1b[A"
EMPTY="[connection]\nserver=\nnick=tester\nusername=tester\nautoconnect=false\n"


def start_log(s):
    s.wait(lambda:b"Not vegetarian?" in s.raw,"setup welcome missing")
    s.type("x")
    s.wait(lambda:not s.screen.alternate and b"-- S1 " in s.raw,"setup fixture did not reach log")


def setup_visible(s, question=None):
    title="".join(s.screen.cells[0])
    return (s.screen.alternate and s.screen.cursor_visible and
            (" - Quickstart" in title or " - Settings panel" in title) and
            (question is None or "".join(s.screen.cells[2]).rstrip()==question) and
            "".join(s.screen.cells[-6]).strip() in ("Answer:","Choice:") and
            s.screen.rows-5<=s.screen.row<s.screen.rows-3)


def wait_question(s, question):
    s.wait(lambda:setup_visible(s,question),"setup did not show question: "+question)
    assert_no_persistent_hints(s)
    assert not "".join(s.screen.cells[-1]).strip(),"setup retained a generic footer"


def current_answer(s):
    return "".join("".join(row) for row in s.screen.cells[-5:-3]).rstrip()


def open_setup(s, name="quickstart", full=False):
    s.type("/"+name)
    s.wait(lambda:s.editor("/"+name),"setup command did not open editor")
    s.type(b"\r")
    wait_question(s,"Server" if name=="quickstart" else "Choose a panel")
    title="".join(s.screen.cells[0]).rstrip()
    assert title.startswith(("vipirc" if full else "onekeyirc")+" - "),title
    assert ("onekeyirc" if full else "vipirc") not in title.lower(),"setup header names another product"


def clear_answer(s):
    previous=current_answer(s)
    if previous:
        s.type(FINISH+b"\x7f"*len(previous))
        s.wait(lambda:setup_visible(s) and not current_answer(s),"could not clear a rejected setup answer")


def enter_answer(s, value="", *, next_question=None, error=None, repeat=1, secret=False):
    assert setup_visible(s),s.screen.text()
    clear_answer(s)
    if value:
        s.type(value)
        visible="*"*len(value) if secret else value
        s.wait(lambda:setup_visible(s) and current_answer(s)==visible,"setup answer was not visible or safely masked")
    # Each displayed question deliberately needs a fresh Enter after the
    # application's 250 ms key-repeat guard. This waits for that real boundary.
    s.quiet(.30)
    s.type(b"\r"*repeat)
    if error:
        s.wait(lambda:setup_visible(s) and error.lower() in "".join(s.screen.cells[-3]).lower(),
               "setup validation error missing: "+error)
    elif next_question:
        wait_question(s,next_question)
    else:
        s.wait(lambda:not s.screen.alternate,"setup finish did not return to log")
        assert s.proc.poll() is None,"finishing setup exited the program"


def cancel_setup(s):
    marker=len(s.raw)
    s.type(b"\x1b" * 64)
    s.wait(lambda:not s.screen.alternate and b"Setup cancelled." in s.raw[marker:],"Escape did not cancel setup to log")
    assert s.proc.poll() is None


def quick_questions(s, host, port, nick):
    enter_answer(s,host,next_question="Use TLS")
    enter_answer(s,"no",next_question="Port")
    enter_answer(s,str(port),next_question="Nickname")
    enter_answer(s,nick,next_question="Finish",repeat=128)


def quickstart_cancel_validation_and_repeat(full=False):
    with IdleSession(no_connect=True,full=full,cols=100) as s:
        start_log(s);open_setup(s,full=full)
        s.type(b"\x04"*64)
        s.quiet(.08)
        assert setup_visible(s,"Server") and not current_answer(s)
        s.type(BEGIN+b"new.invalid\r3\rquit"+END+b"\r"*64)
        s.wait(lambda:setup_visible(s,"Server") and "new.invalid" in current_answer(s),"paste did not remain a literal setup answer")
        assert s.config.read_bytes()==s.before_config
        cancel_setup(s)
        open_setup(s,full=full)
        enter_answer(s,"new.invalid",next_question="Use TLS",repeat=128)
        enter_answer(s,"maybe",error="yes")
        assert setup_visible(s,"Use TLS")
        enter_answer(s,"no",next_question="Port")
        assert "Current: 6667" in s.screen.text(),"TLS choice did not update the standard port default"
        enter_answer(s,"70000",error="1 to 65535")
        enter_answer(s,"6667",next_question="Nickname")
        enter_answer(s,"#bad",error="nick")
        s.type(UP);s.quiet(.08)
        assert setup_visible(s,"Nickname"), "Up unexpectedly left the input field"
        s.type(b"\x1b");wait_question(s,"Port")
        assert "Current: 6667" in s.screen.text()
        enter_answer(s,"",next_question="Nickname")
        enter_answer(s,"NewNick",next_question="Finish",repeat=128)
        assert "Current: 3" in s.screen.text() and "Save and connect" in s.screen.text()
        assert s.config.read_bytes()==s.before_config,"typing setup values saved before explicit Finish"
        cancel_setup(s)
        s.command("/settings","Server: offline.invalid",keep_reader=True)
        assert "Nickname: tester" in s.screen.text(),"cancelled nickname became effective"
        s.close_reader()
        open_setup(s,full=full)
        s.resize(8,20)
        s.wait(lambda:s.screen.alternate and "Terminal too small" in s.screen.text(),
               "small setup did not show its size diagnostic")
        s.type(b"\r"*128);s.quiet(.08)
        s.resize(18,100);wait_question(s,"Server")
        cancel_setup(s)
        open_setup(s,full=full)
        s.type(b"\x03")
        s.wait(lambda:s.proc.poll() is not None,"Ctrl-C did not leave setup")
        assert s.proc.returncode==130
    print("native quickstart: exactly four fields, defaults/back/validation, pasted and repeated Enter isolation, cancel, resize, inert Ctrl-D and Ctrl-C restoration passed",flush=True)


def finish_choices_and_failed_save(full=False):
    for choice in (3,1,2,4):
        with IdleSession(live=True,no_connect=True,config_override=EMPTY,full=full,cols=120) as s:
            start_log(s);open_setup(s,full=full)
            quick_questions(s,"127.0.0.1",s.peer.port,"WizardNick")
            assert not s.peer.lines and s.peer.socket is None,"setup connected before Finish"
            assert s.config.read_bytes()==s.before_config
            enter_answer(s,str(choice))
            if choice in (1,2):
                s.wait(lambda:b"nick=WizardNick\n" in s.config.read_bytes(),"explicit Save did not persist setup")
                s.before_config=s.config.read_bytes()
                assert stat.S_IMODE(s.config.stat().st_mode)==0o600
                assert (b"[interface]" in s.before_config)==full
            if choice in (1,3):
                s.wait(lambda:s.peer.registered,"explicit Connect did not register with the private peer")
                assert s.peer.nick=="WizardNick"
                s.peer.send("PING :setup-connect-choice")
                s.wait(lambda:any(line==b"PONG :setup-connect-choice" for _,line in s.peer.lines),"setup connection lost keepalive")
            else:
                s.command("/settings","Nickname: WizardNick")
                assert not s.peer.lines and s.peer.socket is None,"save/apply-only unexpectedly connected"
            assert not s.peer.messages()
    with IdleSession(live=True,no_connect=True,config_override=EMPTY,full=full,cols=120) as s:
        start_log(s);open_setup(s,full=full)
        quick_questions(s,"127.0.0.1",s.peer.port,"RetryNick")
        s.config.unlink();s.config.mkdir(mode=0o700)
        try:
            enter_answer(s,"2",error="config")
            assert setup_visible(s,"Finish") and not s.peer.lines
            s.config.rmdir();s.config.write_bytes(s.before_config);s.config.chmod(0o600)
            # Fixing the destination is not consent to retry a failed Save.
            assert setup_visible(s,"Finish") and s.config.read_bytes()==s.before_config
            enter_answer(s,"2")
            s.wait(lambda:b"nick=RetryNick\n" in s.config.read_bytes(),"fresh Save confirmation did not retry")
            s.before_config=s.config.read_bytes()
            assert not s.peer.lines
        finally:
            if s.config.is_dir():
                s.config.rmdir();s.config.write_bytes(s.before_config);s.config.chmod(0o600)
    print("native Finish: explicit Connect once, Save-and-connect, Save-only and Apply-only; private files, no premature connection, atomic failure and fresh retry passed",flush=True)


def panel_credentials_and_live_backlog(full=False):
    # Offline simulation permits Apply but no writes or credential reads.
    config="[connection]\nserver=offline.invalid\ntls=true\nnick=tester\nusername=tester\nautoconnect=false\n"
    secret="fixture-secret-not-history"
    with IdleSession(full=full,no_connect=True,config_override=config,cols=120) as s:
        start_log(s);marker=len(s.raw);open_setup(s,"panel",full)
        assert "0: Application preferences" in s.screen.text() and "Current: 0" in s.screen.text()
        enter_answer(s,"1",next_question="Server")
        assert "Settings panel | Server 1" in "".join(s.screen.cells[0])
        seen=[]
        for _ in range(64):
            question="".join(s.screen.cells[2]).rstrip();seen.append(question)
            if question=="Finish":break
            value={"Auto-authentication":"sasl","Authentication account":"fixture-account",
                   "Authentication password source":"plain","Authentication password":secret}.get(question,"")
            clear_answer(s)
            if value:
                s.type(value)
                expected="*"*len(value) if question=="Authentication password" else value
                s.wait(lambda:setup_visible(s) and current_answer(s)==expected,"panel value did not render safely")
            s.quiet(.30);s.type(b"\r")
            s.wait(lambda:setup_visible(s) and "".join(s.screen.cells[2]).rstrip()!=question,"panel failed to advance: "+question)
        else:raise AssertionError("panel exceeded its bounded field count")
        assert seen[0]=="Server" and "Auto-connect at startup" in seen and "Auto-join channels" in seen
        assert not {"History lines","Send mode","Output format"}.intersection(seen),"server panel included application preferences"
        assert "Authentication password" in seen
        assert secret.encode() not in s.raw[marker:],"plaintext answer appeared on the terminal"
        enter_answer(s,"4")
        s.command("/settings","Output format: compact",keep_reader=True)
        assert secret.encode() not in s.raw[marker:],"settings disclosed the entered password"
        s.close_reader()
        s.type(b"/\x1b[A")
        s.wait(lambda:s.editor("/settings"),"setup answers displaced command history")
        s.type(b"\x1b[A")
        s.wait(lambda:s.editor("/panel"),"setup answers entered command history")
        assert secret.encode() not in s.raw[marker:]
        s.cancel()
        assert s.config.read_bytes()==s.before_config
    with IdleSession(live=True,full=full,cols=120) as s:
        s.start();open_setup(s,"panel",full)
        marker=len(s.raw)
        s.peer.send(":alice!u@h PRIVMSG #test :ARRIVED_DURING_PANEL")
        s.peer.send("PING :panel-alive")
        s.wait(lambda:any(line==b"PONG :panel-alive" for _,line in s.peer.lines),"panel blocked network service")
        assert b"ARRIVED_DURING_PANEL" not in s.raw[marker:],"chat overwrote a setup question"
        cancel_setup(s)
        s.wait(lambda:b"ARRIVED_DURING_PANEL" in s.raw[marker:],"panel cancellation lost held chat")
        assert bytes(s.raw[marker:]).count(b"ARRIVED_DURING_PANEL")==1
        assert not s.peer.messages()
    print("native server panel: one-based server choice, separate startup/auth fields, omitted application preferences, masked plaintext, answer-free command history, Apply-only and held live traffic/PING passed",flush=True)


def stage_application_preferences(s, full, history="160", send="review", output="full"):
    open_setup(s,"panel",full)
    assert "Current: 0" in s.screen.text() and "0: Application preferences" in s.screen.text()
    enter_answer(s,"",next_question="History lines")
    assert "Settings panel | Application preferences" in "".join(s.screen.cells[0])
    enter_answer(s,history,next_question="Send mode")
    enter_answer(s,send,next_question="Composer mode")
    enter_answer(s,"",next_question="Output format")
    enter_answer(s,output,next_question="Queue notices")
    enter_answer(s,"no",next_question="Program messages")
    enter_answer(s,"yes",next_question="Persistent logging")
    enter_answer(s,"no",next_question="Single-session mode")
    enter_answer(s,"yes",next_question="Command suggestions")
    if full:
        enter_answer(s,"",next_question="Default interface")
    enter_answer(s,"",next_question="Finish")
    assert "Current: 1" in s.screen.text(),"application preferences did not default to Save"
    assert "1 Save" in s.screen.text() and "2 Apply without saving" in s.screen.text()
    assert "Save and connect" not in s.screen.text(),"application preferences offered a connection action"


def check_application_settings(s, history, send, output):
    s.command("/settings","Output format: "+output,keep_reader=True)
    text=s.screen.text()
    assert "Application preferences:" in text and "History lines: %d"%history in text,text
    assert "send mode: "+send in text and "Output format: "+output in text,text
    assert_no_persistent_hints(s)
    s.close_reader()


def application_preferences_without_server(full=False):
    config=EMPTY+"[history]\nlines=50\n[input]\nsend_mode=direct\n[output]\nformat=compact\n"
    for finish in ("save","apply","cancel"):
        # A private listener verifies that none of these app-level operations
        # opens a connection, including Save when no server is configured.
        with IdleSession(live=True,no_connect=True,config_override=config,full=full,cols=120) as s:
            start_log(s)
            original=s.config.read_bytes()
            stage_application_preferences(s,full)
            assert s.config.read_bytes()==original and s.peer.socket is None
            if finish=="cancel":cancel_setup(s)
            else:enter_answer(s,"" if finish=="save" else "2")
            assert s.peer.socket is None and not s.peer.lines,"application preferences connected a server"
            assert not s.catalog.exists(),"application preferences created a server catalogue"
            if finish=="save":
                saved=configparser.ConfigParser();saved.read(s.config)
                assert saved.get("connection","server")=="" and saved.get("connection","nick")=="tester"
                assert not saved.getboolean("connection","autoconnect")
                assert saved.getint("interface" if full else "history","history_lines" if full else "lines")==160
                assert saved.get("input","send_mode")=="review" and saved.get("output","format")=="full"
                assert stat.S_IMODE(s.config.stat().st_mode)==0o600
                s.before_config=s.config.read_bytes()
            else:
                assert s.config.read_bytes()==original,"Apply or Cancel wrote the configuration"
            check_application_settings(s,50 if finish=="cancel" else 160,
                                       "direct" if finish=="cancel" else "review",
                                       "compact" if finish=="cancel" else "full")
            s.type("/settings")
            s.wait(lambda:s.editor("/settings") and "Command | S1 " in "".join(s.screen.cells[0]),
                   "application preferences changed the primary server selection")
            s.cancel()
    print("native app preferences (%s): panel0 has History/Send/Composer/Output/Queue/Program/Logging/Single-session preferences, default Save without a configured server, private persistence, Apply-only and Cancel isolation, no network or catalogue changes passed"%
          ("vipirc -i" if full else "onekeyirc"),flush=True)


def application_preferences_preserve_live_selection(full=False):
    second=IRCFixture()
    try:
        with IdleSession(live=True,review=False,full=full,cols=120) as s:
            s.start()
            s.command("/connect -plain -nick PanelSecond 127.0.0.1 %d"%second.port)
            s.wait(lambda:second.registered,"second panel fixture did not register")
            s.command("/join #other")
            s.wait(lambda:b"joined #other" in s.raw,"second panel fixture did not join")
            probe="PANEL_SELECTION_PROBE"
            s.type(probe)
            s.wait(lambda:s.editor(probe,len(probe)),"selected-channel probe did not open")
            context="".join(s.screen.cells[0]).strip().split(" | ")
            assert len(context)==5 and context[:3]==["Compose","S2 127.0.0.1","#other"],context
            assert context[3] in ("line","fullscreen"),context
            assert re.fullmatch(r"%d/[1-9][0-9]* bytes"%len(probe),context[4]),context
            s.cancel()
            first_nicks=sum(line.startswith(b"NICK ") for _,line in s.peer.lines)
            second_nicks=sum(line.startswith(b"NICK ") for _,line in second.lines)
            stage_application_preferences(s,full,history="180",send="review",output="full")
            marker=len(s.raw)
            second.send(":alice!u@fixture PRIVMSG #other :APP_PANEL_BACKLOG")
            for peer,token in ((s.peer,"first-app-panel"),(second,"second-app-panel")):
                peer.send("PING :"+token)
                s.wait(lambda peer=peer,token=token:any(line==("PONG :"+token).encode() for _,line in peer.lines),
                       "application panel blocked a connected server")
            assert b"APP_PANEL_BACKLOG" not in s.raw[marker:],"chat overwrote application settings"
            enter_answer(s,"")
            s.wait(lambda:b"APP_PANEL_BACKLOG" in s.raw[marker:],"application Save lost queued chat")
            assert bytes(s.raw[marker:]).count(b"APP_PANEL_BACKLOG")==1
            saved=configparser.ConfigParser();saved.read(s.config)
            assert saved.get("connection","nick")=="tester" and saved.getint("connection","port")==s.peer.port
            assert saved.get("connection","server")=="127.0.0.1" and saved.get("connection","channel")=="#test"
            assert saved.get("input","send_mode")=="review" and saved.get("output","format")=="full"
            assert not s.catalog.exists(),"application Save implicitly persisted the unsaved second server"
            s.before_config=s.config.read_bytes()
            s.type(probe)
            s.wait(lambda:s.editor(probe,len(probe)),"composer did not reopen after application Save")
            after="".join(s.screen.cells[0]).strip().split(" | ")
            assert after[:3]==context[:3],"application Save switched server or channel"
            # Global preferences also apply the primary profile's composer
            # mode to this server; its destination and draft meter stay put.
            assert after[3:]==[saved.get("input","composer_mode"),context[4]],after
            s.cancel()
            check_application_settings(s,180,"review","full")
            open_setup(s,"panel",full)
            assert "Current: 0" in s.screen.text(),"panel default depends on selected server"
            enter_answer(s,"2",next_question="Server")
            assert "Settings panel | Server 2" in "".join(s.screen.cells[0]),"panel number did not select server2"
            cancel_setup(s)
            assert sum(line.startswith(b"NICK ") for _,line in s.peer.lines)==first_nicks
            assert sum(line.startswith(b"NICK ") for _,line in second.lines)==second_nicks
            assert not s.peer.messages() and not second.messages("#other"),"settings or context probes sent chat"
    finally:second.close()
    print("native app preferences (%s): global Save from server2 retains both connections, selected channel, queued chat and server identities; shared settings and one-based panel selection passed"%
          ("vipirc -i" if full else "onekeyirc"),flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full",action="store_true",help="run the same native setup through vipirc -i")
    args=parser.parse_args()
    quickstart_cancel_validation_and_repeat(args.full)
    finish_choices_and_failed_save(args.full)
    panel_credentials_and_live_backlog(args.full)
    application_preferences_without_server(args.full)
    application_preferences_preserve_live_selection(args.full)
    print("all native setup PTY checks passed (%s)"%("vipirc -i" if args.full else "onekeyirc"),flush=True)


if __name__=="__main__":main()
