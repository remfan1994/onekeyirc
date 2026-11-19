#!/usr/bin/env python3
"""Curses-free terminal regressions. Offline simulation and private loopback only."""
import argparse
import copy
import errno
import fcntl
import os
from pathlib import Path
import pty
import pwd
import re
import select
import shlex
import signal
import socket
import struct
import subprocess
import tempfile
import termios
import time

from test_integration import IRCFixture, Screen

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("ONEKEY_BINARY", str(ROOT / "onekeyirc"))).resolve()
FULL_BINARY = Path(os.environ.get("VIPIRC_IDLE_BINARY", str(ROOT / "vipirc"))).resolve()
BEGIN, END = b"\x1b[200~", b"\x1b[201~"
LEFT, RIGHT, HOME, FINISH = b"\x1b[D", b"\x1b[C", b"\x1b[H", b"\x1b[F"


def assert_no_persistent_hints(session):
    """Help text may explain keys; persistent screen furniture must not."""
    footer="".join(session.screen.cells[-1]).strip()
    old_footers=("Esc back |", "Enter/Esc back |", "Enter submits; Esc cancels",
                 "Enter next |", "Enter finish |")
    assert not footer.startswith(old_footers),("persistent key hints remain",footer)
    for row in session.screen.cells:
        assert "(Enter keeps current):" not in "".join(row),"answer field retained generic key instructions"


class IdleScreen(Screen):
    """Add the normal/alternate screen separation to the shared VT test model."""
    def __init__(self, *args, **kwargs):
        self.alternate = False
        self.normal = None
        super().__init__(*args, **kwargs)

    def command(self, final):
        if self.sequence == "?1049" and final in "hl":
            fields = ("cells", "reverse_cells", "bold_cells", "row", "col", "top", "bottom")
            if final == "h" and not self.alternate:
                self.normal = {name: copy.deepcopy(getattr(self, name)) for name in fields}
                self.alternate = True
                self.resize(self.rows, self.cols)
            elif final == "l" and self.alternate:
                self.alternate = False
                if self.normal and len(self.normal["cells"]) == self.rows and len(self.normal["cells"][0]) == self.cols:
                    for name, value in self.normal.items(): setattr(self, name, value)
                else: self.resize(self.rows, self.cols)
                self.normal = None
            return
        super().command(final)


class IdleSession:
    def __init__(self, *, live=False, review=True, rows=18, cols=80, locale="C.UTF-8",
                 full=False, args=None, catalog=None, channel="#test", config_extra="", composer="fullscreen",
                 config_override=None, no_connect=False, config_home=None):
        self.temp = tempfile.TemporaryDirectory(prefix="onekeyirc-pty-")
        self.directory = Path(self.temp.name)
        self.peer = IRCFixture() if live else None
        self.config = self.directory / "config.ini"
        composer_setting = "" if composer is None else "composer_mode=%s\n" % composer
        self.config.write_text(
            "[connection]\nserver=%s\nport=%d\ntls=false\nnick=tester\nusername=tester\n"
            "channel=%s\nautoconnect=true\nauth=none\n[input]\nsend_mode=%s\n%s[history]\nlines=50\n" %
            ("127.0.0.1" if live else "offline.invalid", self.peer.port if live else 6667,
             channel, "review" if review else "direct", composer_setting) + config_extra)
        if config_override is not None:self.config.write_text(config_override)
        self.config.chmod(0o600)
        self.before_config = self.config.read_bytes()
        self.catalog = Path(str(self.config)+".servers")
        self.before_catalog = None
        if catalog is not None:
            self.catalog.write_text("[servers]\nprofile=%s\n" % ("../escape" if catalog=="bad" else "missing.ini"))
            self.catalog.chmod(0o600)
            self.before_catalog = self.catalog.read_bytes()
        executable = FULL_BINARY if full else BINARY
        assert executable.stat().st_size > 0 and os.access(executable, os.X_OK), executable
        self.master, self.slave = pty.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.before_tty = termios.tcgetattr(self.slave)
        self.screen = IdleScreen(rows, cols)
        self.raw = bytearray()
        env = dict(os.environ, TERM="xterm-256color", LC_ALL=locale, HOME=str(self.directory),
                   XDG_CONFIG_HOME=str(config_home if config_home is not None else self.directory / "unused-config"))
        command = [str(executable)]
        if args is not None: command += args
        else:
            if full: command.append("--idle")
            if not live: command.append("--test-mode")
            command += ["--config", str(self.config)]
            if no_connect:command.append("--no-connect")

        def terminal_child():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)
        self.proc = subprocess.Popen(command, stdin=self.slave, stdout=self.slave, stderr=self.slave,
                                     cwd=self.directory, env=env, preexec_fn=terminal_child, close_fds=True)
        os.set_blocking(self.master, False)

    def pump(self, timeout=0.03):
        ready, _, _ = select.select([self.master], [], [], timeout)
        if not ready: return
        try: data = os.read(self.master, 65536)
        except OSError as error:
            if error.errno in (errno.EIO, errno.EAGAIN): return
            raise
        self.raw.extend(data)
        self.screen.feed(data)

    def wait(self, predicate, message, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate(): return
            self.pump()
            if self.proc.poll() is not None:
                # Exit can race the last PTY write. Drain already-ready bytes
                # before testing the unchanged condition, without extending
                # the deadline or waiting for further output.
                for _ in range(16):
                    before = len(self.raw)
                    self.pump(0)
                    if len(self.raw) == before: break
                if not predicate(): break
        assert predicate(), "%s\nexit=%r\n%s\nraw tail=%r" % (
            message, self.proc.poll(), self.screen.text(), self.raw[-1000:])

    def quiet(self, seconds=0.1):
        end = time.monotonic() + seconds
        while time.monotonic() < end: self.pump(min(.03, end-time.monotonic()))

    def type(self, text):
        data = text.encode("utf-8") if isinstance(text, str) else text
        while data:
            try: n = os.write(self.master, data)
            except BlockingIOError:
                self.pump(); continue
            data = data[n:]

    def start(self):
        self.wait(lambda: b"Not vegetarian?" in self.raw, "welcome missing")
        self.type("x")
        if self.peer:
            self.wait(lambda: self.peer.registered and b"joined #test" in self.raw, "local connection did not join")
        else:
            self.wait(lambda: b"joined #test" in self.raw, "offline simulator did not join")
        self.command("/channel #test")

    def editor(self, text=None, count=None):
        screen = self.screen.text()
        count_visible = count is None or re.search(
            r"(?<![0-9])%d(?: UTF-8 bytes|/[0-9]+ bytes| bytes)" % count, screen) is not None
        return (self.screen.alternate and self.screen.cursor_visible and
                (text is None or text in screen) and count_visible)

    def reader(self):
        return (self.screen.alternate and not self.screen.cursor_visible and
                "".join(self.screen.cells[0]).strip() in ("Help", "Command response"))

    def close_reader(self):
        assert self.reader(), self.screen.text()
        self.type(b"\x1b")
        self.wait(lambda:not self.screen.alternate,"reader Escape did not restore log")
        assert self.proc.poll() is None,"reader Escape exited the program"

    def command(self, text, expected=None, *, keep_reader=False):
        assert text.startswith("/")
        marker = len(self.raw)
        self.type(text)
        self.wait(lambda: self.editor(text, len(text.encode())), "command was not visibly composed: "+text)
        self.type(b"\r")
        self.wait(lambda: self.reader() or (not self.screen.alternate and b"\x1b[?1049l" in self.raw[marker:]),
                  "command did not open its result or return to log: "+text)
        if expected: self.wait(lambda: expected.encode() in self.raw[marker:], "command response missing: "+expected)
        if self.reader() and not keep_reader:self.close_reader()


    def cancel(self):
        marker = len(self.raw)
        self.type(b"\x1b")
        self.wait(lambda: b"Draft cancelled" in self.raw[marker:] and not self.screen.alternate,
                  "Escape did not cancel to log")

    def resize(self, rows, cols):
        fcntl.ioctl(self.master, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.screen.resize(rows, cols)

    def close(self):
        forced = False
        if self.proc.poll() is None:
            self.type(b"\x03")
            try:
                self.wait(lambda: self.proc.poll() is not None, "Ctrl-C did not exit", timeout=3)
            except AssertionError:
                forced = True; self.proc.kill(); self.proc.wait(timeout=2)
        self.quiet(.03)
        after = termios.tcgetattr(self.slave)
        os.close(self.master); os.close(self.slave)
        try:
            if self.peer: self.peer.close()
            assert not forced, "forced terminal cleanup was necessary"
            assert after == self.before_tty, ("terminal attributes were not restored", self.before_tty, after)
            assert self.config.read_bytes() == self.before_config, "terminal workflow changed configuration"
            if self.before_catalog is not None:
                assert self.catalog.read_bytes()==self.before_catalog,"terminal workflow changed saved-server catalog"
        finally: self.temp.cleanup()

    def __enter__(self): return self
    def __exit__(self, kind, value, traceback): self.close()


def exited_process_output_is_drained():
    """A terminated child can still have its last PTY write awaiting a read."""
    class ExitedProcess:
        def poll(self): return 0

    session = IdleSession.__new__(IdleSession)
    session.raw = bytearray()
    session.proc = ExitedProcess()
    session.screen = IdleScreen(2, 40)
    chunks = [b"\x1b[?2004l\r\n", b"Resume: onekeyirc --resume 1\r\n"]
    waits = []

    def pump(timeout=0.03):
        waits.append(timeout)
        if chunks: session.raw.extend(chunks.pop(0))

    session.pump = pump
    session.wait(lambda: session.proc.poll() == 0 and b"--resume " in session.raw,
                 "exited child output was not drained")
    assert not chunks and waits[0] == 0.03 and all(value == 0 for value in waits[1:])
    session.raw.clear()
    chunks[:] = [b"\x1b[?2004l\r\n"]
    try:
        session.wait(lambda: b"--resume " in session.raw, "missing notice must still fail")
    except AssertionError:
        pass
    else:
        raise AssertionError("draining exit output accepted a missing notice")
    print("idle test helper: split exit output drains without waiting; missing notice still fails", flush=True)


class ReconnectingIRCFixture(IRCFixture):
    """Keep one private loopback endpoint across distinct client launches."""
    def __init__(self):
        self.sessions=[]
        super().__init__()

    def run(self):
        try:
            while not self.stop.is_set():
                try:connection,_=self.listener.accept()
                except socket.timeout:continue
                connection.settimeout(.05)
                self.socket=connection
                self.nick="tester"
                self.has_user=self.cap_end=self.registered=False
                session={"lines":[],"registered":False,"nick":None}
                self.sessions.append(session)
                pending=b""
                try:
                    while not self.stop.is_set():
                        try:data=connection.recv(32768)
                        except socket.timeout:continue
                        if not data:break
                        pending+=data
                        while b"\r\n" in pending:
                            line,pending=pending.split(b"\r\n",1)
                            assert len(line)+2<=512 and not any(c in line for c in (b"\0",b"\n",b"\r"))
                            self.lines.append((time.monotonic(),line))
                            session["lines"].append(line)
                            self.handle(line)
                            session.update(registered=self.registered,nick=self.nick)
                except (ConnectionResetError,BrokenPipeError):pass
                finally:
                    connection.close()
                    self.socket=None
        except OSError as error:
            if not self.stop.is_set():self.errors.append(error)
        except Exception as error:self.errors.append(error)
        finally:self.listener.close()


def offline_editor_and_commands(full=False):
    with IdleSession(cols=40, full=full) as s:
        s.start()
        s.type(b"\x04" * 100)
        s.quiet(.08)
        assert s.proc.poll() is None and not s.screen.alternate
        s.type(b"\r")
        s.wait(lambda: s.editor(count=0), "Enter from log did not open an empty composer")
        assert_no_persistent_hints(s)
        assert not "".join(s.screen.cells[-1]).strip(),"empty composer retained a generic footer"
        s.type(b"\r" * 40)
        s.quiet(.08)
        assert s.editor(count=0), "empty Enter submitted or left the composer"
        s.type("A" * 39)
        s.wait(lambda: s.editor(count=39) and (s.screen.row, s.screen.col)==(5,0), "exact-width caret did not wrap visibly")
        s.type(b"\x7f")
        s.wait(lambda: s.editor(count=38) and (s.screen.row, s.screen.col)==(4,38), "Backspace failed across visual row boundary")
        s.type(HOME + "中e\u0301".encode())
        s.wait(lambda: s.editor(count=44), "wide/combining text did not preserve byte count")
        s.type(LEFT + b"\x7f")
        s.wait(lambda: s.editor(count=41), "wide character deletion failed")
        s.resize(12,20)
        s.wait(lambda: s.editor(count=41), "narrow resize lost editor/counter")
        assert s.screen.cursor_visible and s.screen.row < s.screen.rows-2
        s.resize(18,80)
        s.wait(lambda: s.editor(count=41), "resize back lost draft")
        s.cancel()
        s.type(b"/he\t")
        s.wait(lambda: s.editor("/help "), "Tab did not complete supported command")
        s.wait(lambda:"".join(s.screen.cells[-1]).startswith("/help"),
               "minimal command editor lost its applicable command syntax")
        assert_no_persistent_hints(s)
        s.type(b"\x1b")
        s.wait(lambda: not s.screen.alternate, "command Escape did not restore log")
        s.command("/help ls", "bouncers")
        s.type(b"/\x1b[A")
        s.wait(lambda: s.editor("/help ls"), "Up did not recall matching accepted command")
        s.cancel()
        marker=len(s.raw)
        s.command("/help all",keep_reader=True)
        assert s.reader() and "".join(s.screen.cells[0]).strip()=="Help"
        first_page=s.screen.text()
        s.type(b"\x1b[6~")
        s.wait(lambda:s.reader() and s.screen.text()!=first_page,"manual PageDown did not advance")
        s.type(HOME)
        s.wait(lambda:s.reader() and s.screen.text()==first_page,"manual Home did not restore first page")
        s.type(FINISH)
        s.wait(lambda:s.reader() and "/znc Connect" in s.screen.text(),"complete help lost its final command")
        assert b"output exceeded" not in s.raw[marker:]
        s.close_reader()
        assert "Usage: /whois" not in s.screen.text(),"manual contents polluted the chat screen"
        s.command("/help keys",keep_reader=True)
        s.type(FINISH)
        s.wait(lambda:s.reader() and "Legacy unframed paste" in s.screen.text(),
               "key help did not retain its final paste guidance")
        s.type(b"\x1b[5~")
        s.wait(lambda:s.reader() and "Escape" in s.screen.text(),
               "key help lost its Escape documentation above the expanded signal policy")
        s.close_reader()
        s.type("/")
        s.wait(lambda:s.editor("/"),"slash did not open command")
        s.type("not-a-command\r")
        s.wait(lambda:s.editor() and "Unknown" in s.screen.text(),"command error was hidden behind editor")
        s.cancel()
    print("idle editor: real caret/wrap deletion, Unicode, resize, inert EOF, completion/history, complete help and visible errors passed",flush=True)


def paste_and_review(full=False):
    with IdleSession(live=True,review=True,cols=100,full=full) as s:
        s.start()
        s.type("draft-one")
        s.wait(lambda:s.editor("draft-one",9),"first text key was lost")
        marker=len(s.raw)
        s.peer.send(":alice!u@host PRIVMSG #test :ARRIVED_WHILE_TYPING")
        s.peer.send("PING :editor-alive")
        s.wait(lambda:any(b"editor-alive" in line and line.startswith(b"PONG") for _,line in s.peer.lines),
               "editor blocked network PING")
        s.wait(lambda:s.editor("draft-one",9) and "ARRIVED_WHILE_TYPING" in s.screen.text(),
               "live tail did not repaint incoming chat while composing")
        s.type(b"\r"*150)
        s.wait(lambda:s.screen.alternate and "Review S1 #test" in s.screen.text(),
               "first Enter did not open ephemeral review")
        s.wait(lambda:"Send this message?" in s.screen.text(),"review lost its explicit send question")
        assert b"Enter sends; Esc cancels" not in s.raw[marker:],"review retained generic key instructions"
        assert not s.peer.messages(),"first Enter or same input burst sent the draft"

        # Escape from review is an edit/back action, not a destructive cancel.
        marker=len(s.raw);s.quiet(.3);s.type(b"\x1b")
        s.wait(lambda:s.editor("draft-one",9),"Escape from review did not return to the composer")
        assert b"Draft cancelled" not in s.raw[marker:],"review Escape emitted a cancellation notice"

        # Re-editing and re-reviewing must replace the temporary review screen,
        # not append another preview/prompt to the normal transcript.
        s.type("+")
        s.wait(lambda:s.editor("draft-one+",10),"editing after review Escape lost the draft")
        s.type(b"\r")
        s.wait(lambda:s.screen.alternate and "Review S1 #test" in s.screen.text() and
                      "draft-one+" in s.screen.text(),"edited draft did not reopen review")
        before_send=len(s.raw)
        s.quiet(.3);s.type(b"\r")
        s.wait(lambda:any(text==b"draft-one+" for _,text in s.peer.messages()) and not s.screen.alternate,
               "fresh review approval did not send and restore the log")
        assert len(s.peer.messages())==1
        s.wait(lambda:"ARRIVED_WHILE_TYPING" in s.screen.text(),
               "traffic retained under review did not return with the chat log")
        assert "Review S1 #test" not in s.screen.text(),"review title leaked into the normal transcript"
        assert "Send this message?" not in s.screen.text(),"review prompt leaked into the normal transcript"
        s.quiet(1.35)
        assert b"queued; paced" not in s.raw[before_send:],"reviewed send retained a redundant queue notice"
        assert bytes(s.raw[before_send:]).count(b"<tester> draft-one+")==1,"reviewed send did not leave exactly one own-message chat entry"
        s.type(BEGIN+b"/quit\r\nnever-run"+END+b"\r")
        s.wait(lambda:s.editor(count=15),"paste did not become literal multiline composition")
        assert s.proc.poll() is None and len(s.peer.messages())==1
        s.cancel()
        s.type("route-pinned")
        s.wait(lambda:s.editor("route-pinned"),"pinned draft missing")
        s.type(b"\r")
        s.wait(lambda:s.screen.alternate and "Review S1 #test" in s.screen.text(),
               "pinned draft did not enter review")
        s.peer.send(":operator!u@host KICK #test tester :membership removed")
        s.quiet(.2)
        s.quiet(.3);s.type(b"\r")
        s.wait(lambda:s.screen.alternate and "channel was left" in s.screen.text(),
               "stale membership did not reject send inside review")
        assert len(s.peer.messages())==1,"review silently sent to a left channel"
        s.type(b"\x1b")
        s.wait(lambda:s.editor("route-pinned"),"Escape from rejected review did not restore the composer")
        s.cancel()
    print("idle review (%s): ephemeral preview/prompt, clean one-message transcript, responsive PING, edit/re-review, same-burst Enter isolation, literal paste and pinned membership passed" %
          ("vipirc -i" if full else "onekeyirc"),flush=True)


def review_screen_bounds(full=False):
    with IdleSession(live=True,review=True,rows=10,cols=80,full=full) as s:
        s.start()
        s.type("tiny-review")
        s.wait(lambda:s.editor("tiny-review"),"small-review fixture did not compose")
        s.resize(5,80);s.quiet(.12);s.type(b"\r")
        s.wait(lambda:s.screen.alternate and "Terminal too small for message review" in s.screen.text(),
               "undersized review did not retain the draft safely")
        s.quiet(.3);s.type(b"\r");s.quiet(.2)
        assert not s.peer.messages(),"undersized review allowed blind confirmation"
        s.resize(10,80)
        s.wait(lambda:s.screen.alternate and "Review S1 #test" in s.screen.text() and
                      "Send this message?" in s.screen.text(),"resized review did not redraw")
        s.quiet(.3);s.type(b"\r")
        s.wait(lambda:any(text==b"tiny-review" for _,text in s.peer.messages()) and not s.screen.alternate,
               "redrawn review did not confirm normally")

    with IdleSession(live=True,review=True,rows=8,cols=40,full=full) as s:
        s.start()
        draft="L"*220
        s.type(draft)
        s.wait(lambda:s.editor(count=len(draft)),"long-review fixture did not compose")
        s.type(b"\r")
        s.wait(lambda:s.screen.alternate and "Review rows" in s.screen.text(),
               "long review did not expose bounded navigation")
        first=s.screen.text()
        s.type(b"\x1b[6~");s.quiet(.12)
        assert s.screen.text()!=first,"review Page Down did not move through the draft"
        s.type(HOME);s.quiet(.12)
        assert s.screen.text()==first,"review Home did not return to the first page"
        s.type(b"\x1b")
        s.wait(lambda:s.editor(count=len(draft)),"review Escape did not restore long draft")
        s.cancel()
    print("idle review bounds (%s): blind confirmation blocked on undersized terminals; resize redraw and long-review paging passed" %
          ("vipirc -i" if full else "onekeyirc"),flush=True)


def line_mode_live_tail_and_toggle():
    # Omit composer_mode entirely to prove the new default is line mode.
    with IdleSession(live=True,review=False,cols=100,composer=None) as s:
        s.start()
        s.type("line-draft")
        s.wait(lambda:s.editor(count=10) and "| line |" in "".join(s.screen.cells[0]),
               "default composer mode was not line")
        assert s.screen.row==s.screen.rows-1,"line composer was not anchored to the bottom row"
        s.peer.send(":alice!u@h PRIVMSG #test :LINE_MODE_LIVE_FEED")
        s.peer.send("PING :line-mode-live")
        s.wait(lambda:any(line==b"PONG :line-mode-live" for _,line in s.peer.lines),
               "line composer blocked IRC keepalive")
        s.wait(lambda:"LINE_MODE_LIVE_FEED" in s.screen.text() and s.editor(count=10),
               "line composer did not retain a live incoming tail")
        assert s.screen.row==s.screen.rows-1,"live-tail repaint moved the line-mode caret"
        s.cancel()

        s.type("/")
        s.wait(lambda:s.editor("/",1) and "Suggestions:" in s.screen.text(),
               "line command mode did not display predictions")
        visible_predictions=[row.strip() for row in s.screen.text().splitlines()
                             if row.strip().startswith("/") and row.strip()!="/"]
        assert len(visible_predictions)<=3,("line mode used too much tail space for predictions",visible_predictions)
        assert "LINE_MODE_LIVE_FEED" in s.screen.text(),"command predictions displaced the live tail"
        assert s.screen.row==s.screen.rows-1,"line command input was not on the bottom row"
        s.cancel()

        s.type("X")
        s.wait(lambda:s.editor(count=1),"line-mode byte-budget fixture did not open")
        match=re.search(r"\| line \| 1/([0-9]+) bytes", "".join(s.screen.cells[0]))
        assert match,s.screen.text()
        budget=int(match.group(1));assert budget>200
        s.type("X"*(budget-1))
        s.wait(lambda:s.editor(count=budget) and "Message limit reached" in s.screen.text(),
               "line mode did not pre-empt at the IRC message limit")
        s.type("Z")
        s.wait(lambda:s.editor(count=budget),"line mode accepted text before limit confirmation")
        s.type(b"\r")
        s.wait(lambda:s.editor(count=budget) and "Message limit acknowledged" in s.screen.text(),
               "line mode did not require explicit continued-editing confirmation")
        s.cancel()

        s.command("/set composer_mode fullscreen","Composer mode changed for all servers")
        s.type("fullscreen-draft")
        s.wait(lambda:s.editor("fullscreen-draft",16) and "| fullscreen |" in "".join(s.screen.cells[0]),
               "runtime composer-mode switch did not enter fullscreen")
        assert s.screen.row < s.screen.rows-2,"fullscreen composer still behaved as a single bottom line"
        s.peer.send(":alice!u@h PRIVMSG #test :FULLSCREEN_LIVE_FEED")
        s.wait(lambda:"FULLSCREEN_LIVE_FEED" in s.screen.text() and s.editor(count=16),
               "fullscreen composer lost the live tail")
        s.cancel()
    print("idle composer modes: default line input, live tail, capped predictions, pre-emptive byte confirmation and runtime fullscreen toggle passed",flush=True)


def direct_limit_and_bounds():
    with IdleSession(live=True,review=False,cols=120) as s:
        s.start()
        s.type("direct-first")
        s.wait(lambda:s.editor(count=12),"direct composer missing")
        s.type(b"\r")
        s.wait(lambda:s.peer.messages() and not s.screen.alternate,"direct Enter did not send")
        s.type("X")
        s.wait(lambda:s.editor(count=1),"next composer missing")
        match=re.search(r"(\d+)/message",s.screen.text());assert match,s.screen.text()
        budget=int(match[1]);assert budget>200
        s.type("X"*(budget-1))
        s.wait(lambda:s.editor(count=budget) and "Message limit reached" in s.screen.text(),"threshold warning was late")
        s.type("Z")
        s.wait(lambda:s.editor(count=budget) and "Message limit reached" in s.screen.text(),
               "threshold warning allowed more text without confirmation")
        s.type(b"\r")
        s.wait(lambda:s.editor(count=budget) and "Message limit acknowledged" in s.screen.text(),
               "Enter did not acknowledge continued editing at the message limit")
        s.type("Z")
        s.wait(lambda:s.editor(count=budget+1) and "2 IRC messages" in s.screen.text(),
               "acknowledged editor did not allow the next byte or show split state")
        s.type(b"\r")
        s.wait(lambda:s.screen.alternate and "2 messages" in s.screen.text(),
               "direct mode bypassed mandatory split review")
        assert len(s.peer.messages())==1
        # REVIEW Escape restores the draft; cancelling it is a separate step.
        s.type(b"\x1b")
        s.wait(lambda:s.editor(count=budget+1) and "2 IRC messages" in s.screen.text(),
               "split-review Escape did not restore the complete oversized draft")
        assert [text for _,text in s.peer.messages()]==[b"direct-first"], \
               "leaving split review sent an unconfirmed draft"
        s.cancel()
        s.type("bounded-hidden")
        s.wait(lambda:s.editor(count=14),"history-bound composer missing")
        for i in range(150):s.peer.send(":alice!u@h PRIVMSG #test :hidden-record-%03d"%i)
        s.peer.send("PING :bounded-alive")
        s.wait(lambda:any(b"bounded-alive" in line and line.startswith(b"PONG") for _,line in s.peer.lines),"live-tail flood blocked PING")
        s.wait(lambda:"hidden-record-149" in s.screen.text(),"live tail did not retain the newest incoming record")
        marker=len(s.raw);s.cancel()
        s.wait(lambda:b"earlier records omitted" in s.raw[marker:] and b"hidden-record-149" in s.raw[marker:],
               "bounded hidden history failed to report omissions or retain tail")
        assert [text for _,text in s.peer.messages()]==[b"direct-first"], \
               "cancelling retained drafts sent an extra message"
    print("idle limits: direct sends, pre-emptive limit acknowledgement, mandatory split review, live tail and bounded-history omission passed",flush=True)


def ascii_and_signals():
    with IdleSession(locale="C",review=False) as s:
        s.start();s.type("ascii")
        s.wait(lambda:s.editor(count=5),"ASCII editor missing")
        s.type("中".encode()+b"\x04"*128)
        s.wait(lambda:s.editor(count=5) and "non-ASCII input rejected" in s.screen.text(),"ASCII rejection changed draft")
        s.cancel();s.type(b"\x03")
        s.wait(lambda:s.proc.poll() is not None,"Ctrl-C did not terminate")
        assert s.proc.returncode==130,s.proc.returncode
    with IdleSession() as s:
        s.wait(lambda:b"Not vegetarian?" in s.raw,"signal welcome missing")
        s.type(b"\x04"*128);s.quiet(.08)
        assert s.proc.poll() is None and b"Compose" not in s.raw
        s.type(b"\x03")
        s.wait(lambda:s.proc.poll() is not None,"welcome Ctrl-C was treated as any key")
        assert s.proc.returncode==130,s.proc.returncode
    print("idle terminal controls: ASCII rejection, held Ctrl-D, welcome/main Ctrl-C130 and exact tty restoration passed",flush=True)


def fragmented_terminal_keys():
    with IdleSession() as s:
        s.start()
        s.type(b"\x1b");s.quiet(.18)
        assert s.proc.poll() is None and not s.screen.alternate,"fragmented arrow ESC exited the log"
        s.type(b"[A")
        s.quiet(.08)
        assert not s.screen.alternate,"fragmented Up unexpectedly opened a composer/screen without older history"
        s.type("abc");s.wait(lambda:s.editor(count=3),"key-fragment fixture missing")
        s.type(b"\x1b[");s.quiet(.22)
        s.type(b"D")
        s.wait(lambda:s.editor(count=3) and s.screen.col==2,"delayed CSI continuation leaked text or lost Left")
        s.type("X")
        s.wait(lambda:s.editor("abXc",4),"delayed Left used wrong caret")
        s.type(b"\x1bO");s.quiet(.22);s.type(b"H")
        s.wait(lambda:s.editor(count=4) and s.screen.col==0,"delayed SS3 Home failed")
        s.cancel()
    print("idle SSH keys: delayed180ms bareESC prefix and220ms CSI/SS3 continuations retain navigation without quitting or inserting suffixes passed",flush=True)


def paste_scalar_boundary(full=False):
    with IdleSession(full=full) as s:
        s.start()
        for split in (False, True):
            s.type("kept")
            s.wait(lambda:s.editor("kept",4),"paste UTF-8 boundary fixture missing")
            s.type(BEGIN+b"-paste\xc3")
            s.wait(lambda:s.editor("kept-paste",10),"paste prefix was not retained")
            if split:
                for byte in END:
                    s.type(bytes((byte,)));s.quiet(.08)
            else:s.type(END)
            s.quiet(.08)
            s.type(b"\xa9")
            s.wait(lambda:s.editor("kept-paste",10) and "Invalid UTF-8 input ignored" in s.screen.text(),
                   "UTF-8 continuation crossed the closing paste marker")
            s.type("Z")
            s.wait(lambda:s.editor("kept-pasteZ",11),"input did not recover after a truncated pasted scalar")
            s.cancel()
    print("idle paste UTF-8: complete and byte-delayed closing markers discard an incomplete scalar; later continuation bytes cannot join it",flush=True)


def no_target_escape_paths(full=False):
    """Empty startup must remain escapable before any server/channel exists."""
    variants = (
        ("no config", {"args": (["--idle"] if full else []) + ["--no-connect"]}),
        ("saved empty config", {"config_override": "[connection]\nserver=\nnick=tester\nautoconnect=false\n", "no_connect": True}),
        ("configured but disconnected", {"no_connect": True}),
    )
    for label, options in variants:
        with IdleSession(full=full, **options) as s:
            s.wait(lambda:b"Not vegetarian?" in s.raw,label+" welcome missing")
            s.type("x")
            s.wait(lambda:b"-- S1 " in s.raw and not s.screen.alternate,label+" did not reach log")
            assert b"Set IRC server:" not in s.raw and b"Set user nick:" not in s.raw
            for draft, escapes, invalid in ((b"\r", b"\x1b", False),
                                             (b"/", b"\x1b\x1b", False),
                                             (b"/help", b"\x1b"*32, False),
                                             (b"orphan", b"\x1b\x1b", True)):
                s.type(draft)
                count=0 if draft==b"\r" else len(draft)
                s.wait(lambda:s.editor(count=count),label+" did not open empty/no-target editor")
                if invalid:
                    s.type(b"\r")
                    s.wait(lambda:s.editor("orphan",6) and
                           "Destination disconnected; draft retained" in s.screen.text(),label+" no-target send was not rejected")
                marker=len(s.raw)
                s.type(escapes)
                s.wait(lambda:not s.screen.alternate and b"Draft cancelled" in s.raw[marker:],
                       label+" Escape trapped the editor")
                assert s.proc.poll() is None,label+" Escape cancellation exited the application"
            # A held key can arrive in several terminal reads. Repeat past the
            # bare-Escape grace period, then allow the final prefix to expire.
            until=time.monotonic()+.65
            while time.monotonic()<until:
                s.type(b"\x1b\x1b")
                s.quiet(.08)
                assert s.proc.poll() is None,"held Escape cascaded from cancel into exit"
            s.quiet(.55)
            assert s.proc.poll() is None and not s.screen.alternate
            s.type("retained")
            s.wait(lambda:s.editor("retained",8),"first character after cancelling was swallowed")
            s.cancel()
            s.type(b"\x03")
            s.wait(lambda:s.proc.poll() is not None,"Ctrl-C stopped working after cancellation")
            assert s.proc.returncode==130
    print("idle no-target Escape: missing/saved-empty/disconnected profiles, empty Compose/Command, failed send, paired/held Escape, first-key retention and Ctrl-C passed (%s)"%
          ("vipirc -i" if full else "onekeyirc"),flush=True)


def help_reader_preserves_chat_and_command_context(full=False):
    with IdleSession(live=True,review=False,cols=100,full=full) as s:
        s.start()
        s.command("/help commands",keep_reader=True)
        assert s.reader() and "".join(s.screen.cells[0]).strip()=="Help"
        assert set("".join(s.screen.cells[1]).strip())=={"-"},"reader separator is not plain text"
        first=s.screen.text()
        assert_no_persistent_hints(s)
        s.type(b"\r")
        s.wait(lambda:s.reader() and s.screen.text()!=first,"Help Enter no longer scrolls its text")
        s.type(HOME)
        s.wait(lambda:s.reader() and s.screen.text()==first,"Help Home did not restore the page after Enter")
        s.type(b"\x1b[6~")
        s.wait(lambda:s.reader() and s.screen.text()!=first,"command help did not page")
        one_page=s.screen.text()
        s.type(HOME)
        s.wait(lambda:s.reader() and s.screen.text()==first,"command help Home failed")
        s.type(b"\x1b[6~"*2)
        s.wait(lambda:s.reader() and s.screen.text()!=first and s.screen.text()!=one_page,
               "two PageDown keys in one terminal read advanced only one page")
        page=s.screen.text()
        s.type("/")
        s.wait(lambda:s.editor("/",1),"slash from reader did not open Command")
        s.type(b"\x1b[A")
        s.wait(lambda:s.editor("/help commands"),"reader command lost accepted history")
        s.type(b"\x1b")
        s.wait(lambda:s.reader() and s.screen.text()==page,"Command Escape lost reader position")
        s.resize(9,20)
        s.wait(lambda:s.reader() and "".join(s.screen.cells[1]).strip()=="-"*19,
               "help reader did not adapt to a narrow SSH terminal")
        s.resize(18,100)
        s.wait(lambda:s.reader() and s.screen.text()==page,"help reader resize lost its response position")
        marker=len(s.raw)
        for i in range(3):s.peer.send(":alice!u@h PRIVMSG #test :HELP_BACKLOG_%d"%i)
        s.peer.send("PING :reader-traffic-held")
        s.wait(lambda:any(line==b"PONG :reader-traffic-held" for _,line in s.peer.lines),
               "help reader blocked IRC keepalive")
        assert b"HELP_BACKLOG_" not in s.raw[marker:],"chat overwrote the help reader"
        assert s.screen.text()==page
        s.close_reader()
        s.wait(lambda:b"HELP_BACKLOG_2" in s.raw[marker:],"reader close lost held messages")
        tail=bytes(s.raw[marker:])
        for i in range(3):assert tail.count(("HELP_BACKLOG_%d"%i).encode())==1,"held chat duplicated"
        assert "The command system" not in s.screen.text(),"help contents became chat output"
        for short,long in (("cl","channels"),("ql","queries"),("sl","servers")):
            s.command("/"+short,keep_reader=True)
            assert s.reader(),"local list did not open a readable response: "+short
            listed=s.screen.text()
            s.close_reader()
            s.command("/"+long,keep_reader=True)
            assert s.screen.text()==listed,"short/long local list aliases disagree: "+short
            s.close_reader()
            s.type("/"+short)
            s.wait(lambda:s.editor("/"+short),"alias completion prompt missing")
            s.type(b"\t")
            s.wait(lambda:s.editor(count=len(short)+2) and
                   any("".join(row).startswith("/"+short+" ") for row in s.screen.cells),
                   "short list alias not available to completion")
            s.cancel()
            s.command("/help /"+short,"Usage: /"+short)
        s.type("/cl extra\r")
        s.wait(lambda:s.reader() and "Usage: /cl" in s.screen.text(),"multi-line command error did not open reader")
        s.type(b"\x1b")
        s.wait(lambda:s.editor("/cl extra"),"error reader Escape did not restore editable command")
        s.cancel()
        assert not s.peer.messages()
        assert not any(line.startswith(b"LIST") for _,line in s.peer.lines),"local list command sent a server directory request"
    print("idle reader: plain heading/divider, paging, command history and return position, held live chat/PING, list alias completion/help/parity and editable errors passed (%s)"%
          ("vipirc -i" if full else "onekeyirc"),flush=True)


def output_format_and_persistence(full=False):
    with IdleSession(live=True,review=False,cols=132,full=full) as s:
        s.start()
        marker=len(s.raw)
        s.peer.send(":alice!u@h PRIVMSG #test :COMPACT_DEFAULT")
        s.wait(lambda:b"COMPACT_DEFAULT" in s.raw[marker:],"compact incoming message missing")
        line=bytes(s.raw[marker:])
        assert re.search(rb"(?:^|\r\n)\[\d{2}:\d{2}:\d{2}\] <alice> COMPACT_DEFAULT\r\n",line),line
        assert not re.search(rb"\[S[0-9]+",line) and b"[#test]" not in line,"compact chat retained redundant origin/date fields"
        marker=len(s.raw)
        s.type("COMPACT_SELF")
        s.wait(lambda:s.editor("COMPACT_SELF",12),"self echo draft missing")
        s.type(b"\r")
        s.wait(lambda:not s.screen.alternate and any(text==b"COMPACT_SELF" for _,text in s.peer.messages()),
               "direct composition did not send")
        s.wait(lambda:re.search(rb"\[\d{2}:\d{2}:\d{2}\] <tester> COMPACT_SELF\r\n",bytes(s.raw[marker:])),
               "compact self echo differs from incoming presentation")
        s.command("/set output_format full","Output format changed")
        marker=len(s.raw)
        s.peer.send(":alice!u@h PRIVMSG #test :FULL_FORMAT")
        s.wait(lambda:b"FULL_FORMAT" in s.raw[marker:],"full-format message missing")
        line=bytes(s.raw[marker:])
        assert re.search(rb"\[S1 127\.0\.0\.1 #test\] \[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\]",line),line
        assert b"[#test] <alice> FULL_FORMAT" in line,line
        s.command("/save output")
        s.wait(lambda:b"[output]\nformat=full\n" in s.config.read_bytes(),"output format was not saved")
        s.before_config=s.config.read_bytes()
        saved=s.before_config.decode()
        assert (b"[interface]" in s.before_config)==full,"standalone/native saved the wrong configuration flavor"
        if not full:assert b"hints=" not in s.before_config
        s.command("/set output_format compact","Output format changed")
        assert s.config.read_bytes()==s.before_config,"unsaved output preference rewrote configuration"
        marker=len(s.raw)
        s.peer.send(":alice!u@h PRIVMSG #test :tester MENTION_ORIGIN")
        s.wait(lambda:b"MENTION_ORIGIN" in s.raw[marker:],"mention source missing")
        marker=len(s.raw)
        s.command("/mentions")
        s.wait(lambda:b"MENTION_ORIGIN" in s.raw[marker:],"compact mentions lost source record")
        assert re.search(rb"\[\d{2}:\d{2}:\d{2}\] \[#test\] <alice> tester MENTION_ORIGIN",bytes(s.raw[marker:])), \
            "compact mentions removed necessary channel origin"
    with IdleSession(full=full,config_override=saved,no_connect=True) as reloaded:
        reloaded.wait(lambda:b"Not vegetarian?" in reloaded.raw,"saved-output welcome missing")
        reloaded.type("x")
        reloaded.wait(lambda:b"-- S1 " in reloaded.raw,"saved output did not reach disconnected log")
        reloaded.command("/settings","Output format: full",keep_reader=True)
        assert reloaded.reader()
        reloaded.close_reader()
    print("idle output: compact timestamp/nick incoming and self echo, explicit full origin/date, mention context, private Save and reload passed (%s)"%
          ("vipirc -i" if full else "onekeyirc"),flush=True)


def startup_profile_diagnostics():
    for problem,marker in (("bad",b"Saved-server catalog:"),("missing",b"Saved server 2:")):
        with IdleSession(catalog=problem) as s:
            s.start()
            s.wait(lambda:marker in s.raw,"saved-server startup error was hidden: "+problem)
            assert s.proc.poll() is None,"usable primary session was aborted by a secondary profile error"
            s.type("primary-still-usable")
            s.wait(lambda:s.editor(count=20),"startup diagnostic left primary unusable")
            s.cancel()
    print("idle startup: invalid catalog and missing saved profile produce visible diagnostics while primary stays usable passed",flush=True)


def maximum_visible_draft():
    with IdleSession(rows=200,cols=500) as s:
        s.start()
        s.type(BEGIN+b"W"*8192+END)
        expected_row=4+8192//499;expected_col=8192%499
        s.wait(lambda:s.editor(count=8192) and (s.screen.row,s.screen.col)==(expected_row,expected_col),
               "maximum visible draft lost its footer or caret",timeout=8)
        assert "Message limit reached" in s.screen.text()
        assert "Display capacity reached" not in s.screen.text()
        s.type(b"\x7f")
        s.wait(lambda:s.editor(count=8191) and s.screen.col==expected_col-1,
               "maximum visible draft could not delete at its real caret")
        s.resize(9,20)
        s.wait(lambda:s.editor(count=8191) and s.screen.cursor_visible and s.screen.row<7,
               "maximum draft lost its caret after compact resize")
        s.cancel()
    print("idle render bound:8192 visible characters at500x200 retain complete footer and caret; compact resize/backspace passed",flush=True)


def command_destination_is_pinned():
    with IdleSession(live=True,review=False) as s:
        s.start()
        s.peer.send(":operator!u@h KICK #test tester :prepare-delayed-join")
        s.wait(lambda:b"prepare-delayed-join" in s.raw,"initial KICK missing")
        s.command("/server")
        s.type("/part")
        s.wait(lambda:s.editor("/part",5) and "server" in "".join(s.screen.cells[0]),"command label did not pin server view")
        before=sum(line.startswith(b"PART ") for _,line in s.peer.lines)
        s.peer.send(":tester!u@localhost JOIN :#test")
        s.peer.send("PING :join-context-received")
        s.wait(lambda:any(b"join-context-received" in line and line.startswith(b"PONG") for _,line in s.peer.lines),
               "late JOIN was not processed during command editing")
        s.type(b"\r")
        s.wait(lambda:s.editor() and "part" in "".join(s.screen.cells[-1]).lower(),"pinned server /part was not rejected visibly")
        assert sum(line.startswith(b"PART ") for _,line in s.peer.lines)==before,"/part silently used newly joined channel context"
        s.cancel()
        assert not s.peer.messages()
    print("idle command routing: incoming first-channel JOIN cannot retarget a command labelled for server1 passed",flush=True)


def clear_keeps_other_view_history():
    with IdleSession(live=True,review=False) as s:
        s.start()
        def replay(command):
            s.type(command)
            s.wait(lambda:s.editor(command,len(command.encode())),"replay command was not visibly composed: "+command)
            # Mark after composition so the live-tail editor repaint is not
            # mistaken for output replayed by the command itself.
            marker=len(s.raw)
            s.type(b"\r")
            s.wait(lambda:s.reader() or not s.screen.alternate,"replay command did not return to log: "+command)
            if s.reader():s.close_reader()
            # This finite command response is written after the replay batch,
            # giving negative assertions an observable output barrier.
            s.command("/help about","Welcome warning:")
            return bytes(s.raw[marker:])
        s.peer.send(":alice!u@h PRIVMSG #test :tester FIRST_SHARED_MENTION")
        s.wait(lambda:b"FIRST_SHARED_MENTION" in s.raw,"source mention did not display")
        assert b"FIRST_SHARED_MENTION" in replay("/mentions")
        s.command("/clear","Retained stream records")
        assert b"FIRST_SHARED_MENTION" in replay("/channel #test"),"clearing Mentions erased source channel history"
        assert b"FIRST_SHARED_MENTION" not in replay("/mentions"),"cleared Mentions replayed old source flag"
        s.peer.send(":alice!u@h PRIVMSG #test :tester SECOND_SHARED_MENTION")
        s.wait(lambda:b"SECOND_SHARED_MENTION" in s.raw,"second mention missing")
        assert b"SECOND_SHARED_MENTION" in replay("/channel #test")
        s.command("/clear","Retained stream records")
        assert b"SECOND_SHARED_MENTION" in replay("/mentions"),"clearing source channel erased its collected mention"
        output=replay("/channel #test")
        assert b"FIRST_SHARED_MENTION" not in output and b"SECOND_SHARED_MENTION" not in output,"cleared source history replayed"
        replay("/mentions");s.command("/clear","Retained stream records")
        assert b"SECOND_SHARED_MENTION" not in replay("/channel #test")
        assert b"SECOND_SHARED_MENTION" not in replay("/mentions")
        s.peer.send(":alice!u@h PRIVMSG #test :tester AFTER_BOTH_CLEARED")
        s.wait(lambda:b"AFTER_BOTH_CLEARED" in s.raw,"clear disabled later mention capture")
        assert b"AFTER_BOTH_CLEARED" in replay("/channel #test")
        assert b"AFTER_BOTH_CLEARED" in replay("/mentions")
        assert not s.peer.messages()
    print("idle clear: source and Mentions retain independent visibility in both clear orders without duplicate message storage passed",flush=True)


def slow_terminal_keeps_network_alive():
    with IdleSession(live=True,review=False,cols=100) as s:
        s.start()
        for i in range(1500):
            s.peer.send(":alice!u@h PRIVMSG #test :slow-reader-%04d-"%i+"X"*260)
        s.peer.send("PING :slow-reader-ping")
        # Deliberately do not read the PTY. The kernel's output queue fills;
        # the application's network loop must still read and answer PING.
        deadline=time.monotonic()+5
        while time.monotonic()<deadline and not any(b"slow-reader-ping" in line and line.startswith(b"PONG")
                                                     for _,line in s.peer.lines):
            time.sleep(.01)
        assert any(b"slow-reader-ping" in line and line.startswith(b"PONG") for _,line in s.peer.lines), \
            "slow terminal output blocked IRC processing"
        assert s.proc.poll() is None
        s.wait(lambda:b"slow-reader-1499-" in s.raw,"stream did not recover when terminal resumed reading",timeout=5)
        assert b"earlier records omitted" in s.raw,"backpressure loss was not explicitly indicated"
        assert not s.peer.messages()
    print("idle backpressure: unconsumed terminal output during1500 messages remains bounded, reports omissions and services PING passed",flush=True)


def escape_closes_large_response_then_backgrounds(full=False):
    with IdleSession(live=True,review=False,cols=100,full=full) as s:
        s.start()
        s.type("ROSTER_CONTEXT_PROBE")
        s.wait(lambda:s.editor("ROSTER_CONTEXT_PROBE"),"initial roster context probe did not open")
        context="".join(s.screen.cells[0]).strip()
        assert context.startswith("Compose | S1 127.0.0.1 | #test | fullscreen |"),context
        s.cancel()
        s.peer.send(":alice!u@fixture PRIVMSG #test :CHAT_BEFORE_ROSTER")
        s.wait(lambda:not s.screen.alternate and "CHAT_BEFORE_ROSTER" in s.screen.text(),
               "chat before the roster was not visible")
        names=["Pending%04d"%i+"N"*45 for i in range(3000)]
        for start in range(0,len(names),6):
            s.peer.send(":srv 353 tester = #test :"+" ".join(names[start:start+6]))
        s.peer.send(":srv 366 tester #test :End of large NAMES")
        s.peer.send("PING :roster-prepared")
        s.wait(lambda:any(b"roster-prepared" in line and line.startswith(b"PONG") for _,line in s.peer.lines),
               "large roster did not finish before the output test")
        s.command("/users",keep_reader=True)
        assert s.reader() and "Users in #test: 3000" in s.screen.text()
        s.wait(lambda:names[s.screen.rows-4] in "".join(s.screen.cells[-1]),
               "roster did not use the former footer row for its content")
        assert_no_persistent_hints(s)
        first=s.screen.text()
        s.type(BEGIN+b"\r\n"+END);s.quiet(.05)
        assert s.reader() and s.screen.text()==first,"pasted Enter closed or moved the roster"
        marker=len(s.raw)
        s.peer.send(":alice!u@fixture PRIVMSG #test :CHAT_DURING_ROSTER")
        s.peer.send("PING :large-reader-alive")
        s.wait(lambda:any(b"large-reader-alive" in line and line.startswith(b"PONG") for _,line in s.peer.lines),
               "large command reader blocked IRC service")
        assert b"CHAT_DURING_ROSTER" not in s.raw[marker:],"chat overwrote the roster reader"
        s.type(b"\r"*8)
        s.wait(lambda:not s.screen.alternate and "CHAT_DURING_ROSTER" in s.screen.text(),
               "roster Enter did not restore the log and its queued chat")
        assert "CHAT_BEFORE_ROSTER" in s.screen.text(),"roster Enter lost previously visible chat"
        assert bytes(s.raw[marker:]).count(b"CHAT_DURING_ROSTER")==1,"roster Enter duplicated queued chat"
        assert "Pending0000" not in s.screen.text(),"roster response polluted the chat log"
        s.type("ROSTER_CONTEXT_PROBE")
        s.wait(lambda:s.editor("ROSTER_CONTEXT_PROBE"),"chat composer did not work after roster Enter")
        assert "".join(s.screen.cells[0]).strip()==context,"roster Enter changed the selected server or channel"
        s.cancel()

        s.command("/users #test",keep_reader=True)
        s.type(FINISH)
        s.wait(lambda:s.reader() and names[-1] in s.screen.text(),"large reader lost its final roster entry")
        s.type(b"\r")
        s.wait(lambda:not s.screen.alternate,"Enter from the final roster page scrolled instead of returning")
        assert "Pending2999" not in s.screen.text(),"final roster page became chat output"
        s.command("/users",keep_reader=True)
        s.close_reader()
        assert "Pending2999" not in s.screen.text(),"roster response polluted the chat log"

        # A complete empty NAMES reply produces only one response line. It
        # still needs the same explicit roster reader and Enter return.
        s.peer.send(":srv 353 tester = #test :")
        s.peer.send(":srv 366 tester #test :End of empty NAMES")
        s.peer.send("PING :empty-roster-prepared")
        s.wait(lambda:any(line==b"PONG :empty-roster-prepared" for _,line in s.peer.lines),
               "empty roster did not finish before its reader test")
        # Use LF consistently for this terminal; CR followed by LF is one
        # coalesced Enter even when its bytes arrive in separate reads.
        command="/USERS #test"
        s.type(command)
        s.wait(lambda:s.editor(command,len(command)),"LF roster command was not visibly composed")
        s.type(b"\n")
        s.wait(s.reader,"LF command Enter did not open the empty roster")
        assert s.reader() and "Users in #test: 0" in s.screen.text(),"empty roster skipped its reader"
        s.type(b"\n")
        s.wait(lambda:not s.screen.alternate,"empty roster Enter did not restore the log")
        assert s.proc.poll() is None

        # An invalid /users remains an editable error response, not a roster.
        s.command("/users #test extra",keep_reader=True)
        assert s.reader() and "Usage: /users" in s.screen.text(),"failed /users did not retain its diagnostic"
        assert_no_persistent_hints(s)
        error=s.screen.text();s.type(b"\r")
        s.wait(lambda:s.reader() and s.screen.text()!=error,"Enter changed failed /users return semantics")
        s.type(b"\x1b")
        s.wait(lambda:s.editor("/users #test extra"),"failed /users Escape lost its editable command")
        s.cancel()
        # Cancellation deliberately suppresses held Escape. This next press is
        # a separate background request after that documented key-repeat grace.
        s.quiet(.55)
        stop_at=len(s.raw);s.type(b"\x1b")
        program="vipirc" if full else "onekeyirc"
        s.wait(lambda:s.proc.poll() is not None and ("Resume: %s --resume " % program).encode() in s.raw[stop_at:],
               "Escape from restored log did not background the client",timeout=4)
        s.wait(lambda:b"\x1b[?2004l" in s.raw[stop_at:],"background did not restore terminal paste mode")
        assert s.proc.returncode==0 and not s.screen.alternate
        assert termios.tcgetattr(s.slave)==s.before_tty
        match=re.search((r"Resume: %s --resume ([0-9]+)" % program).encode(),s.raw[stop_at:])
        assert match,("Escape background notice missing session number",s.raw[stop_at:])
        number=int(match[1])
        path=Path('/tmp').resolve()/('%s-jobs-%d'%(program,os.getuid()))/('job-%d'%number)/'session'
        assert path.exists(),"Escape background worker has no resumable endpoint"
        s.peer.send("PING :escape-background-live")
        s.wait(lambda:any(b"escape-background-live" in line and line.startswith(b"PONG") for _,line in s.peer.lines),
               "Escape-backgrounded worker stopped networking")
        try:
            with IdleSession(args=["--resume",str(number)],full=full) as resumed:
                resumed.wait(lambda:b"\x1b[?2004h" in resumed.raw,"Escape-backgrounded worker did not resume")
                resumed.command("/quit")
                resumed.wait(lambda:resumed.proc.poll() is not None,"resumed /quit did not terminate worker")
                assert resumed.proc.returncode==0
            assert not path.exists(),"/quit did not remove Escape-backgrounded session"
        finally:
            if path.exists():
                with IdleSession(args=["--resume",str(path)],full=full) as cleanup:
                    cleanup.wait(lambda:b"\x1b[?2004h" in cleanup.raw,"cleanup resume failed")
                    cleanup.type(b"\x03")
                    cleanup.wait(lambda:cleanup.proc.poll() is not None,"cleanup interrupt failed")
        assert not s.peer.messages()
    print("idle roster (%s): Enter closes first/final/empty roster pages; preserved log, queued chat, server/channel, paste isolation, PING, error navigation, Escape return/background, resume/quit and tty restoration with no chat sent passed"%
          ("vipirc -i" if full else "onekeyirc"),flush=True)


def presence_departure_messages(full=False):
    with IdleSession(live=True,review=False,full=full) as s:
        s.start()
        s.command("/part #test Custom part reason")
        s.wait(lambda:any(line == b"PART #test :Custom part reason" for _,line in s.peer.lines),
               "custom PART reason was not transmitted")
        s.command("/quit Custom quit reason")
        s.wait(lambda:s.proc.poll() is not None,"custom QUIT did not exit")
        s.wait(lambda:any(line == b"QUIT :Custom quit reason" for _,line in s.peer.lines),
               "custom QUIT reason was not transmitted")
    print("idle departure reasons (%s): explicit PART and QUIT messages reached the IRC peer" %
          ("vipirc -i" if full else "onekeyirc"),flush=True)


def background_resume():
    with IdleSession(live=True,review=False,config_extra="\n[session]\nsingle_session=true\n") as s:
        s.start()
        s.peer.send(":alice!u@h PRIVMSG #test :BEFORE_BACKGROUND_REPLAY")
        s.wait(lambda:b"BEFORE_BACKGROUND_REPLAY" in s.raw,"pre-background history line was not rendered")
        s.command("/background")
        s.wait(lambda:s.proc.poll() is not None and b"--resume " in s.raw,"background did not restore terminal/return shell")
        assert s.proc.returncode==0,s.proc.returncode
        s.wait(lambda:any(line == b"AWAY :Detached from terminal" for _,line in s.peer.lines),
               "explicit background did not set automatic away status")
        away_index=len(s.peer.lines)
        match=re.search(rb"Resume: onekeyirc --resume ([0-9]+)",s.raw)
        assert match,("resume notice used the wrong program name",s.raw[-500:])
        number=int(match[1])
        path=Path('/tmp').resolve()/('onekeyirc-jobs-%d'%os.getuid())/('job-%d'%number)/'session'
        s.peer.send("PING :detached-live")
        s.wait(lambda:any(b"detached-live" in line and line.startswith(b"PONG") for _,line in s.peer.lines),
               "detached worker stopped networking")
        try:
            with IdleSession(args=["--resume"]) as resumed:
                resumed.wait(lambda:b"\x1b[?2004h" in resumed.raw,"resumed idle frontend did not initialize terminal")
                s.wait(lambda:any(line == b"AWAY" for _,line in s.peer.lines[away_index:]),
                       "resume did not clear client-generated background away status")
                resumed.wait(lambda:b"BEFORE_BACKGROUND_REPLAY" in resumed.raw,
                             "resume with no detached chat activity did not repaint retained history")
                s.peer.send(":alice!u@h PRIVMSG #test :RESUMED_STILL_CONNECTED")
                resumed.wait(lambda:b"RESUMED_STILL_CONNECTED" in resumed.raw,"resumed worker lost connection")
                resumed.command("/quit")
                resumed.wait(lambda:resumed.proc.poll() is not None,"resumed quit did not release waiter")
                assert resumed.proc.returncode==0,resumed.proc.returncode
            assert not path.exists(),"background endpoint survived orderly quit"
        finally:
            if path.exists():
                # Do not leave a detached fixture client behind after a failed assertion.
                with IdleSession(args=["--resume",str(path)]) as cleanup:
                    cleanup.wait(lambda:b"\x1b[?2004h" in cleanup.raw,"cleanup resume failed")
                    cleanup.type(b"\x03")
                    cleanup.wait(lambda:cleanup.proc.poll() is not None,"cleanup interrupt failed")
    print("idle background: automatic away/back, single-session bare resume, no-activity retained-history repaint, restored tty, detached PING, reattach and endpoint cleanup passed",flush=True)


def shell_job_control():
    with tempfile.TemporaryDirectory(prefix="onekeyirc-jobs-") as name:
        directory=Path(name);config=directory/"config.ini";pidfile=directory/"pid"
        config.write_text("[connection]\nserver=offline.invalid\nnick=tester\nchannel=#test\n[input]\ncomposer_mode=fullscreen\n")
        config.chmod(0o600)
        launcher=directory/"launch.sh"
        launcher.write_text('#!/bin/sh\nprintf "%s\\n" "$$" > "$1"\nshift\nexec "$@"\n')
        launcher.chmod(0o700)
        master,slave=pty.openpty()
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack("HHHH",18,100,0,0))
        before=termios.tcgetattr(slave)
        env=dict(os.environ,TERM="xterm-256color",LC_ALL="C.UTF-8",PS1="IDLE_SHELL> ",HOME=name)
        def child():
            os.setsid();fcntl.ioctl(0,termios.TIOCSCTTY,0)
        shell=subprocess.Popen(["bash","--noprofile","--norc","-i"],stdin=slave,stdout=slave,stderr=slave,
                               env=env,cwd=directory,preexec_fn=child,close_fds=True)
        os.set_blocking(master,False);raw=bytearray();client_pid=None
        def wait(needle,offset=0,timeout=4):
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                if needle in raw[offset:]:return
                ready,_,_=select.select([master],[],[],.03)
                if ready:
                    try:raw.extend(os.read(master,65536))
                    except OSError as error:
                        if error.errno!=errno.EIO:raise
            raise AssertionError(("job-control output missing",needle,bytes(raw[-2500:])))
        try:
            wait(b"IDLE_SHELL> ")
            command=" ".join(shlex.quote(str(arg)) for arg in (launcher,pidfile,BINARY,"--test-mode","--config",config))
            os.write(master,command.encode()+b"\n")
            wait(b"Not vegetarian?")
            client_pid=int(pidfile.read_text())
            mark=len(raw);os.write(master,b"\x1a")
            wait(b"Stopped",mark);wait(b"IDLE_SHELL> ",mark)
            mark=len(raw);os.write(master,b"fg\n")
            wait(b"Not vegetarian?",mark)
            os.write(master,b"x");wait(b"joined #test",mark)
            os.write(master,BEGIN+b"job-draft")
            wait(b"9 UTF-8 bytes",mark)
            mark=len(raw);os.write(master,b"\x1a")
            wait(b"Stopped",mark);wait(b"IDLE_SHELL> ",mark)
            mark=len(raw);os.write(master,b"fg\n")
            wait(b"9 UTF-8 bytes",mark)
            wait(b"Paste interrupted",mark)
            mark=len(raw);os.write(master,b"\x1b")
            wait(b"Draft cancelled",mark)
            mark=len(raw);os.write(master,b"/quickstart\r")
            wait(b"onekeyirc - Quickstart",mark)
            wait(b"Answer:",mark)
            os.write(master,b"staged.invalid")
            wait(b"staged.invalid",mark)
            mark=len(raw);os.write(master,b"\x1a")
            wait(b"Stopped",mark);wait(b"IDLE_SHELL> ",mark)
            mark=len(raw);os.write(master,b"fg\n")
            wait(b"onekeyirc - Quickstart",mark)
            wait(b"staged.invalid",mark)
            mark=len(raw);os.write(master,b"\x1b")
            wait(b"Setup cancelled.",mark)
            os.write(master,b"second-draft");wait(b"12 UTF-8 bytes",mark)
            mark=len(raw);os.write(master,b"\x03")
            wait(b"IDLE_SHELL> ",mark)
            os.write(master,b"printf 'CLIENT_EXIT:%s\\n' \"$?\"\n")
            wait(b"CLIENT_EXIT:130",mark)
            os.write(master,b"exit\n");shell.wait(timeout=3)
            assert termios.tcgetattr(slave)==before,"job-control path did not restore original terminal"
        finally:
            if client_pid:
                try:os.kill(client_pid,signal.SIGCONT);os.kill(client_pid,signal.SIGTERM)
                except ProcessLookupError:pass
            if shell.poll() is None:shell.kill();shell.wait(timeout=2)
            os.close(master);os.close(slave)
    print("idle job control: real shell Ctrl-Z/fg on welcome, composer and setup retains draft/caret and Ctrl-C exit130 passed",flush=True)


def login_nick():
    try:name=pwd.getpwuid(os.getuid()).pw_name
    except KeyError:name="user"
    return name if re.fullmatch(r"[A-Za-z\[\]\\`_^{|}][A-Za-z0-9\[\]\\`_^{|}-]{0,62}",name) else "user"


def bare_start(session):
    session.wait(lambda:b"Not vegetarian?" in session.raw,"fresh native welcome missing")
    session.type("x")
    session.wait(lambda:not session.screen.alternate and b"-- S1 " in session.raw,
                 "fresh native startup did not reach the log without prompting")
    assert b"Set user nick" not in session.raw,"native startup required an unnecessary nickname answer"


def finish_native(session):
    session.command("/quit")
    session.wait(lambda:session.proc.poll() is not None,"native /quit did not finish")
    assert session.proc.returncode==0


def direct_connect_save_and_reuse(full=False):
    first,second=ReconnectingIRCFixture(),ReconnectingIRCFixture()
    program="vipirc" if full else "onekeyirc"
    args=["-i"] if full else []
    try:
        # There is no command-line server, --config file, or pre-existing profile.
        with IdleSession(full=full,args=args,review=False,cols=120) as s:
            home=s.directory/"unused-config"
            saved=home/program/"config.ini"
            catalog=Path(str(saved)+".servers")
            bare_start(s)
            assert not saved.exists() and not catalog.exists()
            direct="/connect -plain 127.0.0.1 %d"%first.port
            s.command(direct)
            s.wait(lambda:len(first.sessions)==1 and first.sessions[0]["registered"],
                   "direct hostname connect did not register without adding a profile")
            assert first.sessions[0]["nick"]==login_nick(),first.sessions
            assert b"Set user nick" not in s.raw
            s.command("/join #test")
            s.wait(lambda:b"joined #test" in s.raw,"standard /join did not finish")
            s.type("ordinary channel text")
            s.wait(lambda:s.editor("ordinary channel text"),"/join did not provide a usable chat composer")
            s.type(b"\r")
            s.wait(lambda:any(text==b"ordinary channel text" for _,text in first.messages()),
                   "ordinary chat after /join was not routed to the joined channel")
            s.command("/query alice")
            s.type("ordinary query text")
            s.wait(lambda:s.editor("ordinary query text"),"/query did not select its composer")
            s.type(b"\r")
            s.wait(lambda:any(text==b"ordinary query text" for _,text in first.messages("alice")),
                   "ordinary chat after /query was not routed to the query")
            s.command(direct)
            s.quiet(.10)
            assert len(first.sessions)==1,"repeating /connect opened a duplicate connection"
            s.command("/server -plain -nick SecondNick 127.0.0.1 %d"%second.port)
            s.wait(lambda:len(second.sessions)==1 and second.sessions[0]["registered"],
                   "/server HOST did not connect and select the new endpoint")
            assert second.sessions[0]["nick"]=="SecondNick"
            first.send("PING :first-still-connected")
            s.wait(lambda:any(line.startswith(b"PONG") and b"first-still-connected" in line for _,line in first.lines),
                   "connecting a different endpoint disturbed the existing connection")
            s.command("/join #saved")
            s.wait(lambda:b"joined #saved" in s.raw,"second server join missing")
            s.command("/servers",keep_reader=True)
            s.wait(lambda:"1: 127.0.0.1:%d"%first.port in s.screen.text() and
                   "2: 127.0.0.1:%d"%second.port in s.screen.text(),"native server list was not one-based")
            assert not re.search(r"(?m)^\s*0:",s.screen.text()),"native server list exposed internal slot zero"
            s.close_reader()
            s.command("/server 1","Selected server 1:")
            s.type("NUMBERING_CONTEXT_PROBE")
            s.wait(lambda:s.editor("NUMBERING_CONTEXT_PROBE"),"numeric server selection did not restore editing")
            assert "Compose | S1 127.0.0.1 | server" in "".join(s.screen.cells[0]),"/server1 selected another connection"
            s.cancel()
            s.command("/connect 2")
            s.command("/channel #saved")
            s.type("NUMBERING_CONTEXT_PROBE")
            s.wait(lambda:s.editor("NUMBERING_CONTEXT_PROBE"),"numeric connect did not restore editing")
            assert "Compose | S2 127.0.0.1 | #saved" in "".join(s.screen.cells[0]),"/connect2 selected another connection"
            s.cancel()
            assert len(first.sessions)==1 and len(second.sessions)==1,"numeric selection duplicated a live connection"
            s.command("/server 0",keep_reader=True)
            s.wait(lambda:s.reader() and "Servers start at 1" in s.screen.text(),"native server zero was not rejected")
            s.type(b"\x1b")
            s.wait(lambda:s.editor("/server 0"),"invalid server zero lost the editable command")
            s.cancel()
            s.command("/set output_format full")
            s.command("/set send_mode review")
            assert not saved.exists() and not catalog.exists(),"ordinary IRC commands saved configuration implicitly"
            s.command("/saveconf")
            s.wait(catalog.is_file,"/saveconf did not persist the selected default server")
            text=catalog.read_bytes()
            match=re.search(rb"(?m)^default=([^\r\n]+)$",text)
            assert match and b"profile="+match[1]+b"\n" in text,text
            profile=Path(str(saved)+".d")/match[1].decode("ascii")
            content=profile.read_bytes()
            for wanted in (b"server=127.0.0.1\n",("port=%d\n"%second.port).encode(),
                           b"tls=false\n",b"nick=SecondNick\n",b"channel=#saved\n",b"autoconnect=false\n"):
                assert wanted in content,(wanted,content)
            primary_content=saved.read_bytes()
            for wanted in (b"server=127.0.0.1\n",("port=%d\n"%first.port).encode(),
                           b"channel=#test\n",b"send_mode=review\n",b"format=full\n"):
                assert wanted in primary_content,(wanted,primary_content)
            assert b"nick=SecondNick\n" not in primary_content,"saving the selected server overwrote the primary identity"
            assert profile.stat().st_mode&0o777==0o600 and catalog.stat().st_mode&0o777==0o600
            finish_native(s)

            # Use the same default configuration home with a completely new process.
            # No /server selection or connection alias is supplied on either launch.
            with IdleSession(full=full,args=args,config_home=home,cols=120) as resumed:
                resumed.wait(lambda:b"Not vegetarian?" in resumed.raw,"restart welcome missing")
                resumed.type("x")
                resumed.wait(lambda:not resumed.screen.alternate and b"-- S" in resumed.raw,
                             "restart did not reach its selected saved server")
                resumed.quiet(.10)
                assert len(second.sessions)==1,"/saveconf unexpectedly enabled autoconnect"
                resumed.command("/settings","send mode: review",keep_reader=True)
                assert "Output format: full" in resumed.screen.text(),"/saveconf lost application-wide preferences"
                resumed.close_reader()
                resumed.command("/connect")
                resumed.wait(lambda:len(second.sessions)==2 and second.sessions[1]["registered"],
                             "bare /connect after restart did not use the saved secondary server")
                assert second.sessions[1]["nick"]=="SecondNick"
                resumed.wait(lambda:b"JOIN #saved" in second.sessions[1]["lines"],
                             "saved channel was not restored on the remembered connection")
                assert len(first.sessions)==1,"bare /connect silently switched to the original server"
                assert profile.read_bytes()==content and catalog.read_bytes()==text and saved.read_bytes()==primary_content
                finish_native(resumed)

            # A new explicit command-line destination takes precedence over
            # the remembered secondary for both connection and chat selection.
            explicit=args+["--server","127.0.0.1","--port",str(first.port),"--plain","--nick","OverrideNick"]
            with IdleSession(full=full,args=explicit,config_home=home,cols=120) as override:
                override.wait(lambda:b"Not vegetarian?" in override.raw,"explicit-server restart welcome missing")
                override.type("x")
                override.wait(lambda:len(first.sessions)==2 and first.sessions[1]["registered"],
                              "explicit --server did not connect after a secondary had been remembered")
                assert first.sessions[1]["nick"]=="OverrideNick"
                override.command("/join #cli-selected")
                override.wait(lambda:b"JOIN #cli-selected" in first.sessions[1]["lines"],
                              "explicit --server connected a peer but left commands on the remembered server")
                assert len(second.sessions)==2,"explicit --server also connected the unrelated saved default"
                assert profile.read_bytes()==content and catalog.read_bytes()==text and saved.read_bytes()==primary_content
                finish_native(override)

            # An option without a new host updates the remembered connection
            # for this invocation, instead of silently editing primary.
            with IdleSession(full=full,args=args+["--nick","RevisedNick"],config_home=home,cols=120) as renamed:
                renamed.wait(lambda:b"Not vegetarian?" in renamed.raw,"saved-default nickname override welcome missing")
                renamed.type("x")
                renamed.wait(lambda:not renamed.screen.alternate and b"-- S" in renamed.raw,
                             "saved-default nickname override did not reach the log")
                renamed.command("/connect")
                renamed.wait(lambda:len(second.sessions)==3 and second.sessions[2]["registered"],
                             "nickname-only invocation did not retain the saved endpoint")
                assert second.sessions[2]["nick"]=="RevisedNick"
                assert len(first.sessions)==2,"nickname-only override connected or changed primary"
                assert profile.read_bytes()==content and catalog.read_bytes()==text and saved.read_bytes()==primary_content
                finish_native(renamed)

            # Primary can have a different security policy from the remembered
            # server. These artificial credentials must remain on unused primary;
            # --plain and --channel are validated against the selected server.
            protected_primary=primary_content.replace(b"tls=false\n",b"tls=true\n",1)
            protected_primary=protected_primary.replace(b"\nauth=none\n",b"\nauth=sasl\n",1)
            protected_primary=protected_primary.replace(b"\nauth_user=\n",b"\nauth_user=dummy-primary-account\n",1)
            protected_primary=protected_primary.replace(b"\npassword=\n",b"\npassword=DUMMY-PRIMARY-NEVER-SENT\n",1)
            saved.write_bytes(protected_primary)
            try:
                with IdleSession(full=full,args=args+["--plain","--channel","#nonhost-override"],config_home=home,cols=120) as nonhost:
                    nonhost.wait(lambda:b"Not vegetarian?" in nonhost.raw,"valid selected plaintext override was rejected using primary credentials")
                    nonhost.type("x")
                    nonhost.wait(lambda:not nonhost.screen.alternate and b"-- S" in nonhost.raw,
                                 "selected plaintext override did not reach the log")
                    nonhost.quiet(.10)
                    assert len(first.sessions)==2 and len(second.sessions)==3,"non-host overrides enabled autoconnect"
                    nonhost.command("/connect")
                    nonhost.wait(lambda:len(second.sessions)==4 and second.sessions[3]["registered"],
                                 "non-host overrides were applied to the wrong server profile")
                    nonhost.wait(lambda:b"JOIN #nonhost-override" in second.sessions[3]["lines"],
                                 "channel-only override did not follow the saved selected server")
                    assert second.sessions[3]["nick"]=="SecondNick" and len(first.sessions)==2
                    assert not any(line.startswith((b"PASS ",b"AUTHENTICATE ")) for line in second.sessions[3]["lines"])
                    assert b"DUMMY-PRIMARY-NEVER-SENT" not in nonhost.raw
                    assert saved.read_bytes()==protected_primary and profile.read_bytes()==content and catalog.read_bytes()==text
                    finish_native(nonhost)
            finally:saved.write_bytes(primary_content)

            # If the saved default cannot be loaded, non-host overrides must
            # fail visibly before any fallback connection or configuration write.
            unavailable=profile.with_name(profile.name+".unavailable")
            profile.rename(unavailable)
            try:
                with IdleSession(full=full,args=args+["--nick","DoNotRedirect"],config_home=home,cols=120) as invalid:
                    invalid.wait(lambda:invalid.proc.poll() is not None or b"Not vegetarian?" in invalid.raw,
                                 "unavailable-default invocation did not provide a diagnostic")
                    if invalid.proc.poll() is None:invalid.type("x")
                    invalid.wait(lambda:invalid.proc.poll() is not None,
                                 "unavailable default silently accepted a nickname-only override")
                    invalid.quiet(.05)
                    assert invalid.proc.returncode==2,invalid.raw
                    assert b"--server" in invalid.raw and b"default" in invalid.raw.lower(),invalid.raw
                    assert len(first.sessions)==2 and len(second.sessions)==4
                    assert not profile.exists() and catalog.read_bytes()==text and saved.read_bytes()==primary_content
            finally:unavailable.rename(profile)
    finally:
        first.close();second.close()
    print("native direct connection (%s): no-argument startup, login nickname, /connect HOST, /join/chat, /query/chat, existing-session reuse, /server HOST, independent endpoints, explicit private /saveconf, fresh-process bare /connect, explicit CLI precedence, selected-profile option/security validation and unavailable-default guard passed"%program,flush=True)


def supplied_server_does_not_prompt_for_nick(full=False):
    peer=ReconnectingIRCFixture()
    try:
        for requested in (None,"ExplicitNick"):
            args=(["-i"] if full else [])+["--server","127.0.0.1","--port",str(peer.port),"--plain"]
            if requested:args += ["--nick",requested]
            count=len(peer.sessions)
            with IdleSession(full=full,args=args,cols=100) as s:
                s.wait(lambda:b"Not vegetarian?" in s.raw,"supplied-server welcome missing")
                s.type("x")
                s.wait(lambda:len(peer.sessions)==count+1 and peer.sessions[-1]["registered"],
                       "native --server startup blocked on nickname input")
                assert peer.sessions[-1]["nick"]==(requested or login_nick())
                assert b"Set user nick" not in s.raw
                finish_native(s)
    finally:peer.close()
    print("native startup (%s): --server connects without a nick prompt, login default and explicit --nick preserved passed"%("vipirc" if full else "onekeyirc"),flush=True)



def main_chat_scrollback_keys(full=False):
    page_up=b"\x1b[5~"
    page_down=b"\x1b[6~"
    up=b"\x1b[A"
    end=b"\x1b[F"
    with IdleSession(live=True,review=False,rows=12,cols=70,full=full) as s:
        s.start()
        for i in range(40):
            s.peer.send_bytes((f":alice!u@h PRIVMSG #test :SCROLL-{i:02d} history line for page navigation\r\n").encode())
        s.wait(lambda:b"SCROLL-39" in s.raw,"latest chat line did not render")
        assert not s.screen.alternate
        s.type(page_up)
        s.wait(lambda:s.screen.alternate and "Scrollback | #test" in s.screen.text(),
               "Page Up did not open retained chat history")
        first=s.screen.text()
        assert "Compose |" not in first,"Page Up opened the composer instead of chat history"
        assert "SCROLL-39" not in first,"Page Up did not move to an older page"
        visible_before=tuple("".join(row).rstrip() for row in s.screen.cells[1:-1])
        s.peer.send_bytes(b":bob!u@h PRIVMSG #test :SCROLL-NEW live traffic while reading\r\n")
        s.quiet(.15)
        assert tuple("".join(row).rstrip() for row in s.screen.cells[1:-1])==visible_before, \
               "incoming traffic repainted the history page while it was being read"
        s.type(up);s.quiet(.1)
        moved=tuple("".join(row).rstrip() for row in s.screen.cells[1:-1])
        assert moved!=visible_before,"incoming traffic consumed the next one-row history movement"
        assert "SCROLL-NEW" not in s.screen.text(),"new live traffic leaked into the older history page"
        s.type(end)
        s.wait(lambda:not s.screen.alternate,"End did not restore live chat after anchored-history check")
        s.type(page_up)
        s.wait(lambda:s.screen.alternate,"Page Up did not reopen retained chat history")
        first=s.screen.text()
        s.type(page_up);s.quiet(.1)
        older=s.screen.text()
        assert older!=first,"repeated Page Up did not move further back"
        s.type(page_down);s.quiet(.1)
        assert s.screen.alternate,"one Page Down unexpectedly left multi-page history"
        s.type(end)
        s.wait(lambda:not s.screen.alternate,"End did not restore the live chat tail")
        s.type(page_down);s.quiet(.1)
        assert not s.screen.alternate,"Page Down at the live tail opened the composer"

        home=b"\x1b[H"; down=b"\x1b[B"; left=b"\x1b[D"; right=b"\x1b[C"
        delete=b"\x1b[3~"; insert=b"\x1b[2~"
        s.type(home)
        s.wait(lambda:s.screen.alternate and "SCROLL-00" in s.screen.text(),
               "Home did not jump from live chat to the oldest retained page")
        assert "Compose |" not in s.screen.text(),"Home opened the composer"
        s.type(end)
        s.wait(lambda:not s.screen.alternate,"End did not return Home scrollback to live chat")

        s.type(up)
        s.wait(lambda:s.screen.alternate and "Scrollback | #test" in s.screen.text(),
               "Up did not enter one-row retained history")
        s.type(down)
        s.wait(lambda:not s.screen.alternate,"Down did not return one-row history directly to live chat")

        for sequence,name in ((down,"Down"),(end,"End"),(left,"Left"),(right,"Right"),
                              (delete,"Delete"),(b"\x7f","Backspace")):
            s.type(sequence);s.quiet(.08)
            assert not s.screen.alternate, name+" at the live tail unexpectedly opened another screen"

        s.type(insert)
        s.wait(lambda:s.editor(),"Insert did not open an empty composer")
        s.type(b"\x1b")
        s.wait(lambda:not s.screen.alternate,"Escape did not cancel the Insert-opened composer")
        s.type(b"X")
        s.wait(lambda:s.editor("X"),"ordinary compose-on-key behavior was broken")
    print("idle main chat navigation (%s): row/page/Home/End history, inert navigation keys, and Insert compose passed" %
          ("vipirc" if full else "onekeyirc"),flush=True)


def catchup_after_resume():
    extra="[output]\npersistent_logging=true\nprogram_messages=false\nqueue_notices=false\n[session]\nsingle_session=true\n"
    with IdleSession(live=True,review=False,rows=12,cols=88,config_extra=extra) as s:
        s.start()
        s.type("RESUME-CATCHUP-ANCHOR\r")
        s.wait(lambda:any(text==b"RESUME-CATCHUP-ANCHOR" for _,text in s.peer.messages()),
               "resume catch-up anchor was not sent")
        s.command("/background")
        s.wait(lambda:s.proc.poll() is not None and b"--resume " in s.raw,
               "catch-up fixture did not background")
        assert s.proc.returncode==0,s.proc.returncode
        match=re.search(rb"Resume: onekeyirc --resume ([0-9]+)",s.raw)
        assert match,"catch-up fixture did not publish a resume job"
        number=int(match[1])
        path=Path('/tmp').resolve()/('onekeyirc-jobs-%d'%os.getuid())/('job-%d'%number)/'session'
        # More than the 50 retained lines arrive with no foreground terminal.
        for i in range(70):
            s.peer.send_bytes((f":alice!u@h PRIVMSG #test :DETACHED-AWAY-{i:02d}\r\n").encode())
        s.peer.send("PING :catchup-detached-drain")
        deadline=time.monotonic()+5
        while time.monotonic()<deadline and not any(
                b"catchup-detached-drain" in line and line.startswith(b"PONG") for _,line in s.peer.lines):
            time.sleep(.02)
        assert any(b"catchup-detached-drain" in line and line.startswith(b"PONG") for _,line in s.peer.lines), \
               "detached catch-up fixture did not process absence traffic"
        try:
            with IdleSession(args=["--resume"],rows=12,cols=88) as resumed:
                resumed.wait(lambda:b"\x1b[?2004h" in resumed.raw,
                             "catch-up resume did not initialize terminal")
                resumed.wait(lambda:b"DETACHED-AWAY-69" in resumed.raw,
                             "catch-up resume did not repaint newest retained traffic")
                assert b"RESUME-CATCHUP-ANCHOR" not in resumed.raw, \
                       "fixture did not actually roll the anchor out of ordinary retained scrollback"
                resumed.type("/catchup\r")
                resumed.wait(lambda:resumed.screen.alternate and
                             "Catch-up from your most recent sent message" in resumed.screen.text(),
                             "/catchup after --resume did not open durable history")
                assert "RESUME-CATCHUP-ANCHOR" in resumed.screen.text(), \
                       "/catchup after --resume did not restore the sent-message anchor"
                resumed.type(FINISH);resumed.quiet(.1)
                assert "DETACHED-AWAY-69" in resumed.screen.text(), \
                       "resumed catch-up did not reach the newest absence traffic"
                resumed.type(b"\x1b")
                resumed.wait(lambda:not resumed.screen.alternate,
                             "resumed catch-up Escape did not return to live chat")
                resumed.command("/quit")
                resumed.wait(lambda:resumed.proc.poll() is not None,
                             "resumed catch-up quit did not release waiter")
                assert resumed.proc.returncode==0,resumed.proc.returncode
            assert not path.exists(),"catch-up resume endpoint survived orderly quit"
        finally:
            if path.exists():
                with IdleSession(args=["--resume",str(path)]) as cleanup:
                    cleanup.wait(lambda:b"\x1b[?2004h" in cleanup.raw,"catch-up cleanup resume failed")
                    cleanup.type(b"\x03")
                    cleanup.wait(lambda:cleanup.proc.poll() is not None,"catch-up cleanup interrupt failed")
    print("idle catch-up resume: durable per-channel history survives bounded-memory rollover while detached",flush=True)


def catchup_memory_fallback(full=False):
    # With durable logging disabled, /catchup still works while the most recent
    # local send remains inside the bounded in-memory transcript. Once that
    # anchor rolls out, the client must explain the limitation instead of
    # silently pretending it can reconstruct the absence.
    extra="[output]\npersistent_logging=false\nprogram_messages=true\nqueue_notices=false\n"
    with IdleSession(live=True,review=False,rows=12,cols=88,full=full,config_extra=extra) as s:
        s.start()
        s.type("MEMORY-ANCHOR\r")
        s.wait(lambda:any(text==b"MEMORY-ANCHOR" for _,text in s.peer.messages()),
               "memory catch-up anchor was not sent")
        for i in range(8):
            s.peer.send_bytes((f":alice!u@h PRIVMSG #test :MEMORY-AWAY-{i:02d}\r\n").encode())
        s.wait(lambda:b"MEMORY-AWAY-07" in s.raw,"memory catch-up traffic did not arrive")
        s.type("/catchup\r")
        s.wait(lambda:s.screen.alternate and "Scrollback | #test" in s.screen.text(),
               "memory /catchup did not open retained scrollback")
        assert "MEMORY-ANCHOR" in s.screen.text(),"memory /catchup did not begin at the sent-message anchor"
        s.type(FINISH)
        s.wait(lambda:not s.screen.alternate,"End did not return memory catch-up to live chat")

        # Overflow the configured 50-line history. No disk log exists, so the
        # command must refuse to imply that it can recover all missed traffic.
        for i in range(70):
            s.peer.send_bytes((f":bob!u@h PRIVMSG #test :MEMORY-ROLL-{i:02d}\r\n").encode())
        s.wait(lambda:b"MEMORY-ROLL-69" in s.raw,"memory rollover traffic did not arrive")
        marker=len(s.raw)
        s.type("/catchup\r")
        s.wait(lambda:b"older than retained memory" in s.raw[marker:],
               "evicted memory-only catch-up did not explain the durable-history requirement")
        assert "Catch-up from your most recent sent message" not in s.screen.text(), \
               "evicted memory-only /catchup opened a misleading catch-up reader"
        assert "Scrollback | #test" not in s.screen.text(), \
               "evicted memory-only /catchup opened misleading retained scrollback"
    print("idle catch-up memory fallback (%s): retained anchor works and eviction fails explicitly" %
          ("vipirc -i" if full else "onekeyirc"),flush=True)


def catchup_from_last_sent(full=False):
    extra="[output]\npersistent_logging=true\nprogram_messages=false\nqueue_notices=false\n"
    with IdleSession(live=True,review=False,rows=12,cols=88,full=full,config_extra=extra) as s:
        s.start()
        s.type("ANCHOR-CHANNEL\r")
        s.wait(lambda:any(text==b"ANCHOR-CHANNEL" for _,text in s.peer.messages()),
               "channel catch-up anchor was not sent")
        # Exceed the configured 50-line retained history so the anchor must
        # come from the private disk log rather than the in-memory stream.
        for i in range(70):
            s.peer.send_bytes((f":alice!u@h PRIVMSG #test :AWAY-{i:02d} channel history\r\n").encode())
        s.wait(lambda:b"AWAY-69" in s.raw,"channel absence traffic did not arrive")
        log=Path(str(s.config)+".log")
        s.wait(lambda:log.exists() and b"ANCHOR-CHANNEL" in log.read_bytes(),
               "persistent log did not retain the sent-message anchor")
        s.type("/catchup\r")
        s.wait(lambda:s.screen.alternate and "Catch-up from your most recent sent message" in s.screen.text(),
               "/catchup did not open the disk-backed reader")
        assert "ANCHOR-CHANNEL" in s.screen.text(),"catch-up did not begin at the sent-message anchor"
        first=s.screen.text()
        s.peer.send_bytes(b":bob!u@h PRIVMSG #test :ARRIVED-DURING-CATCHUP\r\n")
        s.quiet(.15)
        assert s.screen.text()==first,"new traffic moved the catch-up snapshot"
        s.type(FINISH);s.quiet(.15)
        assert "AWAY-69" in s.screen.text(),"End did not reach the newest line in the catch-up snapshot"
        assert "ARRIVED-DURING-CATCHUP" not in s.screen.text(),"catch-up snapshot included traffic received after it opened"
        s.type(b"\x1b")
        s.wait(lambda:not s.screen.alternate and b"ARRIVED-DURING-CATCHUP" in s.raw,
               "Escape did not return catch-up to the live transcript")

        # Queries have independent anchors and log prefixes. Their catch-up
        # must not accidentally use the channel's most recent activity.
        marker=len(s.raw)
        s.type("/query alice\r")
        s.wait(lambda:b"| alice --" in s.raw[marker:],"query selection did not complete")
        s.quiet(.08)
        s.type("ANCHOR-QUERY\r")
        s.wait(lambda:any(text==b"ANCHOR-QUERY" for _,text in s.peer.messages("alice")),
               "query catch-up anchor was not sent")
        for i in range(55):
            s.peer.send_bytes((f":alice!u@h PRIVMSG tester :QAWAY-{i:02d} private history\r\n").encode())
        s.wait(lambda:b"QAWAY-54" in s.raw,"query absence traffic did not arrive")
        s.type("/catchup\r")
        s.wait(lambda:s.screen.alternate and "Catch-up from your most recent sent message" in s.screen.text(),
               "query /catchup did not open")
        assert "ANCHOR-QUERY" in s.screen.text(),"query catch-up did not use its own sent-message anchor"
        assert "ANCHOR-CHANNEL" not in s.screen.text(),"query catch-up leaked the channel anchor"
        s.type(b"\x1b")
        s.wait(lambda:not s.screen.alternate,"query catch-up Escape did not return live")
    print("idle catch-up (%s): disk-backed channel/query anchors, bounded-memory rollover and stable snapshot passed" %
          ("vipirc -i" if full else "onekeyirc"),flush=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline-only",action="store_true")
    parser.add_argument("--live-only",action="store_true")
    parser.add_argument("--full",action="store_true")
    args=parser.parse_args()
    exited_process_output_is_drained()
    if args.full:
        direct_connect_save_and_reuse(full=True)
        supplied_server_does_not_prompt_for_nick(full=True)
        offline_editor_and_commands(full=True)
        paste_and_review(full=True)
        paste_scalar_boundary(full=True)
        no_target_escape_paths(full=True)
        help_reader_preserves_chat_and_command_context(full=True)
        escape_closes_large_response_then_backgrounds(full=True)
        output_format_and_persistence(full=True)
        presence_departure_messages(full=True)
        catchup_memory_fallback(full=True)
        catchup_from_last_sent(full=True)
        main_chat_scrollback_keys(full=True)
        return
    if not args.live_only:
        offline_editor_and_commands()
        ascii_and_signals()
        fragmented_terminal_keys()
        paste_scalar_boundary()
        no_target_escape_paths()
        startup_profile_diagnostics()
        maximum_visible_draft()
        shell_job_control()
    if not args.offline_only:
        direct_connect_save_and_reuse()
        supplied_server_does_not_prompt_for_nick()
        paste_and_review()
        review_screen_bounds()
        line_mode_live_tail_and_toggle()
        direct_limit_and_bounds()
        help_reader_preserves_chat_and_command_context()
        output_format_and_persistence()
        presence_departure_messages()
        command_destination_is_pinned()
        clear_keeps_other_view_history()
        slow_terminal_keeps_network_alive()
        escape_closes_large_response_then_backgrounds()
        background_resume()
        catchup_after_resume()
        catchup_memory_fallback()
        catchup_from_last_sent()
        main_chat_scrollback_keys()
    print("all selected idle-terminal checks passed",flush=True)


if __name__=="__main__":main()
