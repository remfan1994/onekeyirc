#!/usr/bin/env python3
"""Arabic editing through real PTYs and a private loopback IRC fixture.

Only Python's standard library and the existing VT/IRC test helpers are used.
No public server, user configuration, screenshot, or terminal font is used.
The fixture checks terminal cells/caret movement and exact outgoing UTF-8;
it does not claim to validate a particular terminal's Arabic font rendering.
"""
import argparse
import ctypes
import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time

from test_idle_terminal import IdleScreen
from test_integration import IRCFixture


LEFT, RIGHT = b"\x1b[D", b"\x1b[C"
HOME, FINISH, DELETE = b"\x1b[H", b"\x1b[F", b"\x1b[3~"
BEH = "\u0628"
MARKED_BEH = "\u0628\u064e"
JOINED_BEH = "\ufe90\ufe91"


def runtime_available():
    names = ("libfribidi.so",) if sys.platform.startswith("openbsd") else (
        "libfribidi.so.0", "libfribidi.so", "libfribidi.so.0.0",
        "libfribidi.so.1", "libfribidi.so.0.4.0")
    required = ("fribidi_log2vis", "fribidi_get_bidi_types", "fribidi_get_par_direction",
                "fribidi_get_bracket_types", "fribidi_get_par_embedding_levels_ex",
                "fribidi_get_joining_types", "fribidi_join_arabic", "fribidi_shape",
                "fribidi_reorder_line")
    for name in names:
        try:
            library = ctypes.CDLL(name)
        except OSError:
            continue
        if all(hasattr(library, symbol) for symbol in required):
            return True
    return False


class Terminal:
    def __init__(self, args, visual):
        self.full = args.full_client and not args.minimalist
        self.temp = tempfile.TemporaryDirectory(prefix="irc-arabic-editor-")
        self.directory = Path(self.temp.name)
        self.peer = IRCFixture()
        self.config = self.directory / "config.ini"
        self.config.write_text(
            "[connection]\nserver=127.0.0.1\nport=%d\ntls=false\nnick=tester\n"
            "username=tester\nchannel=#test\nautoconnect=true\nauth=none\n"
            "[input]\nsend_mode=direct\ncomposer_mode=fullscreen\n"
            "[history]\nlines=100\n" % self.peer.port,
            encoding="ascii")
        self.config.chmod(0o600)
        self.saved_config = self.config.read_bytes()
        self.master, self.slave = pty.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 96, 0, 0))
        self.original_tty = termios.tcgetattr(self.slave)
        self.screen = IdleScreen(24, 96)
        self.raw = bytearray()
        env = dict(os.environ, TERM="xterm-256color", LC_ALL="C.UTF-8", LANG="C.UTF-8",
                   HOME=str(self.directory), XDG_CONFIG_HOME=str(self.directory / "unused"),
                   ONEKEYIRC_LANG="en", VIPIRC_LANG="en",
                   ONEKEYIRC_ARABIC_VISUAL="1" if visual else "0",
                   VIPIRC_ARABIC_VISUAL="1" if visual else "0")
        command = [str(args.binary)]
        if args.full_client:
            command += ["--idle" if args.minimalist else "--ncurses"]
        command += ["--config", str(self.config)]

        def controlling_terminal():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        self.proc = subprocess.Popen(command, stdin=self.slave, stdout=self.slave,
                                     stderr=self.slave, cwd=self.directory, env=env,
                                     preexec_fn=controlling_terminal, close_fds=True)
        os.set_blocking(self.master, False)

    def pump(self, timeout=0.025):
        if not select.select([self.master], [], [], timeout)[0]:
            return
        try:
            data = os.read(self.master, 65536)
        except OSError as error:
            if error.errno in (errno.EIO, errno.EAGAIN):
                return
            raise
        self.raw.extend(data)
        assert len(self.raw) < 2_000_000, "unexpected unbounded terminal output"
        self.screen.feed(data)

    def wait(self, predicate, label, timeout=4):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.pump()
            if predicate():
                return
            if self.proc.poll() is not None:
                break
        raise AssertionError("%s; exit=%r; screen=%s; recent output=%r; IRC=%r" % (
            label, self.proc.poll(), ascii(self.screen.text()), bytes(self.raw[-1000:]),
            self.peer.lines[-8:]))

    def quiet(self, duration=0.08):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            self.pump(min(0.025, max(0, deadline - time.monotonic())))

    def type(self, value):
        data = value.encode("utf-8") if isinstance(value, str) else value
        deadline = time.monotonic() + 3
        while data:
            try:
                count = os.write(self.master, data)
                data = data[count:]
            except BlockingIOError:
                assert time.monotonic() < deadline, "terminal stopped accepting input"
                self.pump()

    def has_count(self, count):
        return bool(self.screen.cursor_visible and re.search(
            r"(?<![0-9])%d(?: UTF-8 bytes|/[0-9]+ bytes| bytes)" % count,
            self.screen.text()))

    def edited(self, text, label):
        self.wait(lambda: self.has_count(len(text.encode("utf-8"))), label)
        self.quiet()

    def start(self):
        self.wait(lambda: b"Not vegetarian?" in self.raw, "welcome did not appear")
        self.type("x")
        self.wait(lambda: self.peer.registered and any(
            line.startswith(b"JOIN #test") for _, line in self.peer.lines),
            "private loopback fixture did not register and join")
        self.peer.send("PING :arabic-start")
        self.wait(lambda: any(line.startswith(b"PONG") and b"arabic-start" in line
                              for _, line in self.peer.lines), "startup PING was not answered")
        self.quiet()
        # Select the same known target in both UIs through their real command editor.
        self.type("/")
        self.wait(lambda: "Command" in self.screen.text() and self.screen.cursor_visible,
                  "command editor did not open")
        self.type("channel #test")
        self.wait(lambda: "/channel #test" in self.screen.text(),
                  "target selection command was not composed")
        self.type(b"\r")
        if self.full:
            self.wait(lambda: "Channel" in "".join(self.screen.cells[2]) and self.has_count(0),
                      "ncurses chat editor did not regain focus")
        else:
            self.wait(lambda: not self.screen.alternate, "minimalist command did not return to WATCH")
        self.quiet()

    def resize(self, rows, cols, text):
        marker = len(self.raw)
        fcntl.ioctl(self.master, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.screen.resize(rows, cols)
        os.kill(self.proc.pid, signal.SIGWINCH)
        self.wait(lambda: len(self.raw) > marker and self.has_count(len(text.encode("utf-8"))),
                  "resize did not redraw the retained draft")
        self.quiet()
        assert 0 <= self.screen.row < rows and 0 <= self.screen.col < cols, \
            "resized hardware caret left the viewport"

    def close(self):
        try:
            if self.proc.poll() is None:
                self.proc.terminate()
                try:
                    self.proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait(timeout=2)
                    raise AssertionError("client required forced termination")
            self.quiet(0.03)
            assert termios.tcgetattr(self.slave) == self.original_tty, \
                "client did not restore terminal attributes"
            assert self.config.read_bytes() == self.saved_config, "editing modified saved configuration"
        finally:
            os.close(self.master)
            os.close(self.slave)
            self.peer.close()
            self.temp.cleanup()

    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        self.close()


def exercise(args, requested, shaped):
    with Terminal(args, requested) as terminal:
        terminal.start()
        terminal.type(BEH + BEH)
        terminal.edited(BEH + BEH, "Arabic typing did not update the draft byte count")
        display = terminal.screen.text()
        if shaped:
            assert JOINED_BEH in display, "Arabic editor was not shaped; rebuild the r11 binary or check FriBidi"
            assert BEH + BEH not in display, "logical Arabic leaked into the visual editor frame"
        else:
            assert BEH + BEH in display, "logical/fallback editor changed Arabic typing"
            assert not any(0xfe70 <= ord(char) <= 0xfefc for char in display), \
                "disabled/unavailable converter still shaped the editor"
        end = terminal.screen.row, terminal.screen.col
        terminal.type(HOME)
        terminal.wait(lambda: (terminal.screen.row, terminal.screen.col) != end,
                      "Home did not move to the draft's logical beginning")
        start = terminal.screen.row, terminal.screen.col
        assert start[0] == end[0], "two Arabic letters unexpectedly crossed editor rows"
        assert start[1] == end[1] + (2 if shaped else -2), \
            "Home/End did not map to the correct Arabic visual sides"
        terminal.type(LEFT if shaped else RIGHT)
        middle = start[0], start[1] + (-1 if shaped else 1)
        terminal.wait(lambda: (terminal.screen.row, terminal.screen.col) == middle,
                      "physical arrow did not reach the insertion cell between Arabic letters")
        terminal.type("X")
        draft = BEH + "X" + BEH
        terminal.edited(draft, "insertion between Arabic letters changed the wrong logical location")
        terminal.type(FINISH)
        terminal.quiet()
        suffix = " /help --resume 123 https://example.invalid/a?x=7 \u0644\u0627 "
        terminal.type(suffix + MARKED_BEH)
        draft += suffix + MARKED_BEH
        terminal.edited(draft, "mixed literal syntax or combining input was lost")
        terminal.type(b"\x7f")
        draft = draft[:-2]
        terminal.edited(draft, "Backspace did not remove the complete base-plus-mark group")
        terminal.type(MARKED_BEH)
        draft += MARKED_BEH
        terminal.edited(draft, "combining group could not be reinserted")
        terminal.type(HOME + DELETE)
        draft = draft[1:]
        terminal.edited(draft, "Delete did not remove the first logical Arabic character")
        terminal.type(HOME)
        terminal.quiet()
        terminal.type(BEH)
        draft = BEH + draft
        terminal.edited(draft, "logical beginning was not retained after Delete")
        terminal.type(FINISH)
        terminal.quiet()
        terminal.resize(24, 48, draft)
        terminal.peer.send(":alice!u@localhost PRIVMSG #test :RTL_BACKGROUND_123")
        terminal.peer.send("PING :arabic-retained")
        terminal.wait(lambda: any(line.startswith(b"PONG") and b"arabic-retained" in line
                                  for _, line in terminal.peer.lines),
                      "editing or resize blocked the IRC event loop")
        terminal.edited(draft, "incoming traffic damaged the retained draft")
        terminal.resize(24, 96, draft)
        assert not terminal.peer.messages(), "typing/navigation sent a message prematurely"
        terminal.type(b"\r")
        expected = draft.encode("utf-8")
        terminal.wait(lambda: bool(terminal.peer.messages()), "Enter did not send the edited message")
        messages = [payload for _, payload in terminal.peer.messages()]
        assert messages == [expected], "wire text differs from the expected logical UTF-8: %r" % messages
        sent = messages[0].decode("utf-8", "strict")
        assert "/help --resume 123 https://example.invalid/a?x=7" in sent, \
            "literal command, option, digits, or URL changed on the wire"
        assert not any(0xfb50 <= ord(char) <= 0xfdff or 0xfe70 <= ord(char) <= 0xfeff
                       or ord(char) in (0x200e, 0x200f) for char in sent), \
            "presentation forms or temporary bidi marks reached the wire"
        terminal.quiet()
        bytes(terminal.raw).decode("utf-8", "strict")
    frontend = "vipirc ncurses" if args.full_client and not args.minimalist else (
        "vipirc -i" if args.full_client else "onekeyirc")
    mode = "visual" if shaped else "requested fallback" if requested else "disabled"
    print("Arabic editor PTY: PASS (%s, %s, physical arrows, cluster edits, resize, retained draft, exact logical wire text)" %
          (frontend, mode), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--full-client", action="store_true", help="run vipirc's actual ncurses frontend")
    parser.add_argument("--minimalist", action="store_true", help="with --full-client, select vipirc -i")
    parser.add_argument("--expect-fallback", action="store_true", help="assert logical output even when visual mode is requested")
    args = parser.parse_args()
    if args.minimalist and not args.full_client:
        parser.error("--minimalist requires --full-client")
    args.binary = args.binary.resolve()
    if not args.binary.is_file() or not os.access(args.binary, os.X_OK):
        parser.error("--binary must name an executable client")
    available = runtime_available()
    exercise(args, requested=False, shaped=False)
    exercise(args, requested=True, shaped=available and not args.expect_fallback)
    if not available and not args.expect_fallback:
        print("Arabic editor PTY: system FriBidi unavailable; requested logical fallback verified", flush=True)


if __name__ == "__main__":
    try:
        main()
    except AssertionError as error:
        print("Arabic editor PTY: FAIL: " + str(error).encode("ascii", "backslashreplace").decode("ascii"),
              file=sys.stderr)
        sys.exit(1)
