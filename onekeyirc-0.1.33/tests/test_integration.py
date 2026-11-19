#!/usr/bin/env python3
"""End-to-end tests against a local fake IRC server and a real terminal PTY.

Only Python's standard library is required. No public server is contacted.
"""
import codecs
import base64
import errno
import fcntl
import os
from pathlib import Path
import pty
import pwd
import re
import select
import signal
import socket
import ssl
import struct
import subprocess
import tempfile
import termios
import threading
import time
import unicodedata

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("VIPIRC_BINARY", str(ROOT / "vipirc")))


class Screen:
    """Small VT screen model for assertions; not a terminal product dependency."""

    def __init__(self, rows=24, cols=80, cursor_visible=True):
        self.cursor_visible = cursor_visible
        self.autowrap = True
        self.resize(rows, cols)
        self.state = "text"
        self.sequence = ""
        self.saved = (0, 0)
        self.charsets = {0: "B", 1: "B"}
        self.charset_target = self.active_charset = 0
        self.decoder = codecs.getincrementaldecoder("utf-8")("replace")

    def resize(self, rows, cols):
        self.rows, self.cols = rows, cols
        self.cells = [[" "] * cols for _ in range(rows)]
        self.reverse_cells = [[False] * cols for _ in range(rows)]
        self.bold_cells = [[False] * cols for _ in range(rows)]
        self.reverse = getattr(self, "reverse", False)
        self.bold = getattr(self, "bold", False)
        self.row = self.col = 0
        self.top, self.bottom = 0, rows - 1

    def text(self):
        return "\n".join("".join(row).rstrip() for row in self.cells)

    def scroll(self, n=1):
        for _ in range(max(0, min(n, self.rows))):
            self.cells.pop(self.top)
            self.cells.insert(self.bottom, [" "] * self.cols)
            self.reverse_cells.pop(self.top)
            self.bold_cells.pop(self.top)
            self.reverse_cells.insert(self.bottom, [False] * self.cols)
            self.bold_cells.insert(self.bottom, [False] * self.cols)

    def newline(self):
        if self.row == self.bottom:
            self.scroll()
        else:
            self.row = min(self.rows - 1, self.row + 1)

    def command(self, final):
        raw = self.sequence.lstrip("?><!")
        numbers = [int(x or "0") for x in raw.split(";") if x == "" or x.isdigit()]
        number = numbers[0] if numbers else 0
        n = number or 1
        if self.sequence.startswith("?") and final in "hl":
            if 25 in numbers:
                self.cursor_visible = final == "h"
            if 7 in numbers:
                self.autowrap = final == "h"
                if not self.autowrap:
                    self.col = min(self.col, self.cols - 1)
        elif final in "Hf":
            self.row = max(0, min(self.rows - 1, n - 1))
            self.col = max(0, min(self.cols - 1, (numbers[1] if len(numbers) > 1 else 1) - 1))
        elif final == "A": self.row = max(self.top, self.row - n)
        elif final in "Be": self.row = min(self.bottom, self.row + n)
        elif final in "Ca": self.col = min(self.cols - 1, self.col + n)
        elif final == "D": self.col = max(0, self.col - n)
        elif final in "G`": self.col = min(self.cols - 1, n - 1)
        elif final == "d": self.row = min(self.rows - 1, n - 1)
        elif final == "J":
            if number in (2, 3):
                self.cells = [[" "] * self.cols for _ in range(self.rows)]
                self.reverse_cells = [[False] * self.cols for _ in range(self.rows)]
                self.bold_cells = [[False] * self.cols for _ in range(self.rows)]
            elif number == 0:
                self.cells[self.row][self.col:] = [" "] * (self.cols - self.col)
                self.reverse_cells[self.row][self.col:] = [False] * (self.cols - self.col)
                self.bold_cells[self.row][self.col:] = [False] * (self.cols - self.col)
                for row in range(self.row + 1, self.rows):
                    self.cells[row] = [" "] * self.cols
                    self.reverse_cells[row] = [False] * self.cols
                    self.bold_cells[row] = [False] * self.cols
            elif number == 1:
                for row in range(self.row):
                    self.cells[row] = [" "] * self.cols
                    self.reverse_cells[row] = [False] * self.cols
                    self.bold_cells[row] = [False] * self.cols
                self.cells[self.row][:self.col + 1] = [" "] * (self.col + 1)
                self.reverse_cells[self.row][:self.col + 1] = [False] * (self.col + 1)
                self.bold_cells[self.row][:self.col + 1] = [False] * (self.col + 1)
        elif final == "K":
            if number == 0:
                self.cells[self.row][self.col:] = [" "] * (self.cols - self.col)
                self.reverse_cells[self.row][self.col:] = [False] * (self.cols - self.col)
                self.bold_cells[self.row][self.col:] = [False] * (self.cols - self.col)
            elif number == 1:
                self.cells[self.row][:self.col + 1] = [" "] * (self.col + 1)
                self.reverse_cells[self.row][:self.col + 1] = [False] * (self.col + 1)
                self.bold_cells[self.row][:self.col + 1] = [False] * (self.col + 1)
            elif number == 2:
                self.cells[self.row] = [" "] * self.cols
                self.reverse_cells[self.row] = [False] * self.cols
                self.bold_cells[self.row] = [False] * self.cols
        elif final == "X":
            end = min(self.cols, self.col + n)
            self.cells[self.row][self.col:end] = [" "] * (end - self.col)
            self.reverse_cells[self.row][self.col:end] = [False] * (end - self.col)
            self.bold_cells[self.row][self.col:end] = [False] * (end - self.col)
        elif final == "P":
            n = min(n, self.cols - self.col)
            self.cells[self.row][self.col:] = self.cells[self.row][self.col + n:] + [" "] * n
            self.reverse_cells[self.row][self.col:] = self.reverse_cells[self.row][self.col + n:] + [False] * n
            self.bold_cells[self.row][self.col:] = self.bold_cells[self.row][self.col + n:] + [False] * n
        elif final == "@":
            n = min(n, self.cols - self.col)
            self.cells[self.row][self.col:] = ([" "] * n + self.cells[self.row][self.col:])[:self.cols - self.col]
            self.reverse_cells[self.row][self.col:] = ([False] * n + self.reverse_cells[self.row][self.col:])[:self.cols - self.col]
            self.bold_cells[self.row][self.col:] = ([False] * n + self.bold_cells[self.row][self.col:])[:self.cols - self.col]
        elif final == "S": self.scroll(n)
        elif final == "T":
            for _ in range(min(n, self.bottom - self.top + 1)):
                self.cells.pop(self.bottom)
                self.cells.insert(self.top, [" "] * self.cols)
                self.reverse_cells.pop(self.bottom)
                self.bold_cells.pop(self.bottom)
                self.reverse_cells.insert(self.top, [False] * self.cols)
                self.bold_cells.insert(self.top, [False] * self.cols)
        elif final in "LM" and self.top <= self.row <= self.bottom:
            for _ in range(min(n, self.bottom - self.row + 1)):
                if final == "L":
                    self.cells.insert(self.row, [" "] * self.cols)
                    self.cells.pop(self.bottom + 1)
                    self.reverse_cells.insert(self.row, [False] * self.cols)
                    self.bold_cells.insert(self.row, [False] * self.cols)
                    self.reverse_cells.pop(self.bottom + 1)
                    self.bold_cells.pop(self.bottom + 1)
                else:
                    self.cells.pop(self.row)
                    self.cells.insert(self.bottom, [" "] * self.cols)
                    self.reverse_cells.pop(self.row)
                    self.bold_cells.pop(self.row)
                    self.reverse_cells.insert(self.bottom, [False] * self.cols)
                    self.bold_cells.insert(self.bottom, [False] * self.cols)
        elif final == "r":
            self.top = max(0, min(self.rows - 1, n - 1))
            self.bottom = max(self.top, min(self.rows - 1, (numbers[1] if len(numbers) > 1 else self.rows) - 1))
            self.row = self.col = 0
        elif final == "s": self.saved = (self.row, self.col)
        elif final == "u": self.row, self.col = self.saved
        elif final == "m":
            for value in numbers or [0]:
                if value == 0:
                    self.reverse = self.bold = False
                elif value == 27: self.reverse = False
                elif value == 7: self.reverse = True
                elif value == 1: self.bold = True
                elif value == 22: self.bold = False
        # Other style and terminal modes do not alter text cells.

    def feed(self, data):
        for char in self.decoder.decode(data):
            if self.state == "osc":
                if char == "\a": self.state = "text"
                elif char == "\x1b": self.state = "osc_escape"
                continue
            if self.state == "osc_escape":
                self.state = "text" if char == "\\" else "osc"
                continue
            if self.state == "charset":
                self.charsets[self.charset_target] = char
                self.state = "text"
                continue
            if self.state == "escape":
                self.state = "text"
                if char == "[": self.state, self.sequence = "csi", ""
                elif char == "]": self.state = "osc"
                elif char in "()":
                    self.state = "charset"
                    self.charset_target = 0 if char == "(" else 1
                elif char == "7": self.saved = (self.row, self.col)
                elif char == "8": self.row, self.col = self.saved
                elif char == "D": self.newline()
                elif char == "M":
                    if self.row == self.top:
                        self.cells.pop(self.bottom)
                        self.cells.insert(self.top, [" "] * self.cols)
                        self.reverse_cells.pop(self.bottom)
                        self.bold_cells.pop(self.bottom)
                        self.reverse_cells.insert(self.top, [False] * self.cols)
                        self.bold_cells.insert(self.top, [False] * self.cols)
                    else:
                        self.row = max(self.top, self.row - 1)
                elif char == "E": self.col = 0; self.newline()
                continue
            if self.state == "csi":
                if "@" <= char <= "~":
                    self.command(char)
                    self.state = "text"
                else:
                    self.sequence += char
                continue
            if char == "\x1b": self.state = "escape"
            elif char == "\r": self.col = 0
            elif char == "\n": self.newline()
            elif char == "\b": self.col = max(0, self.col - 1)
            elif char == "\t": self.col = min(self.cols - 1, (self.col // 8 + 1) * 8)
            elif char == "\x0e": self.active_charset = 1
            elif char == "\x0f": self.active_charset = 0
            elif ord(char) >= 32 and char != "\x7f":
                if self.charsets[self.active_charset] == "0":
                    char = {"q": "-", "x": "|", "l": "+", "k": "+", "m": "+", "j": "+",
                            "t": "+", "u": "+", "v": "+", "w": "+", "n": "+"}.get(char, char)
                if unicodedata.combining(char):
                    if self.col: self.cells[self.row][self.col - 1] += char
                    continue
                if self.col >= self.cols:
                    self.col = 0
                    self.newline()
                width = 2 if unicodedata.east_asian_width(char) in ("W", "F") else 1
                self.cells[self.row][self.col] = char
                self.reverse_cells[self.row][self.col] = self.reverse
                self.bold_cells[self.row][self.col] = self.bold
                if width == 2 and self.col + 1 < self.cols:
                    self.cells[self.row][self.col + 1] = ""
                    self.reverse_cells[self.row][self.col + 1] = self.reverse
                    self.bold_cells[self.row][self.col + 1] = self.bold
                self.col += width
                if not self.autowrap:
                    self.col = min(self.col, self.cols - 1)


class ModeControls:
    """Observe real panels and use their visible numbered category shortcuts.

    Main typing always edits the chat field. F1..F6 open displayed
    destinations; type() itself never changes focus for a caller.
    """

    destinations = ("vipirc", "Servers", "Channels", "Queries", "Mentions/unread", "File transfers")

    @staticmethod
    def category_key(number):
        # xterm and screen terminfo agree on F1..F6. Keep raw bytes here so
        # split-sequence tests exercise the real curses decoder as well.
        return (b"\x1bOP", b"\x1bOQ", b"\x1bOR", b"\x1bOS", b"\x1b[15~", b"\x1b[17~")[number-1]

    def panel_title(self):
        return "".join(self.screen.cells[2]).rstrip()

    def mode(self):
        if self.in_command():
            return "COMMAND"
        if self.is_main():
            return "INPUT"
        title=self.panel_title()
        if any(name in title for name in ("vipirc", "Servers", "Channels", "Query list", "Mentions",
                                          "File transfers", "Settings", "Quickstart", "New server", "Help / Manual", "Insha-Allah", "About", "Resize; Esc returns",
                                          "Authentication settings", "Auto-join settings")):
            return "PANEL"
        return None

    def in_command(self):
        return re.match(r"^Command(?:\s|$)", self.panel_title()) is not None

    def is_main(self):
        # Mouse-only Clear/Send controls are deliberately absent when mouse
        # reporting is disabled. The real editor divider identifies chat.
        return (re.match(r"(?:\[OFFLINE\] )?(?:Channel \d+\b|Query:|mentions\b|No server configured)",
                         self.panel_title(), re.IGNORECASE) is not None and
                any(re.match(r"[+| -]*\d+ bytes\b", "".join(row)) for row in self.screen.cells) and
                self.screen.cells[-2][0]=="+" and self.screen.cells[-2][-1]=="+")

    def header_focus(self):
        header="".join(self.screen.cells[0])
        for found in re.finditer(r"\[([^]]*)\]",header):
            numbered=re.match(r"F([1-6]) ",found[1])
            if numbered:
                return self.destinations[int(numbered[1])-1]
            for label in self.destinations:
                if re.match(r"(?:F[1-6] )?"+re.escape(label)+r"(?:\s|$)",found[1],re.IGNORECASE):
                    return label
        return None

    def main_frame_ready(self):
        if not self.is_main() or not self.screen.cursor_visible:
            return False
        divider=next((i for i,row in enumerate(self.screen.cells)
                      if re.match(r"[+| -]*\d+ bytes\b","".join(row))),None)
        row,col=self.screen.row,self.screen.col
        return (divider is not None and divider<row<self.screen.rows-2 and
                1<=col<=self.screen.cols-2 and self.screen.reverse_cells[row][col] and
                self.screen.cells[-2][0]=="+" and self.screen.cells[-2][-1]=="+")

    def focus_destination(self,label):
        if label=="Unread views": label="Mentions/unread"
        if label=="Mentions":
            self.command("/mentions")
            return
        assert label in self.destinations,label
        title={"vipirc":"vipirc","Servers":"Servers","Channels":"Channels","Queries":"Query list",
               "File transfers":"File transfers","Mentions/unread":"Mentions and unread"}[label]
        empty_message = "That category is empty."
        was_empty = empty_message in "".join(self.screen.cells[-1])
        self.type(self.category_key(self.destinations.index(label)+1))
        try:
            # A preceding rejected function-key shortcut can leave this status visible.
            # It is not a response to the new key while its frame is pending.
            self.wait(lambda:self.destination_visible(label) or
                      (not was_empty and empty_message in "".join(self.screen.cells[-1])),
                      "function-key shortcut did not resolve destination: "+label)
        except AssertionError:
            # Repeating a truly absent category may repaint nothing at all.
            # Keep the existing command fallback after the bounded wait,
            # without treating an old diagnostic as an immediate response.
            if self.proc.poll() is None and empty_message in "".join(self.screen.cells[-1]):
                raise AssertionError("Cannot select destination %s\n%s"%(label,self.screen.text()))
            raise
        if not self.destination_visible(label):
            raise AssertionError("Cannot select destination %s\n%s"%(label,self.screen.text()))
        self.wait(lambda:self.header_focus() is None,"function-key shortcut left focus in the top bar")

    def destination_visible(self,label):
        expected={"vipirc":"vipirc","Servers":"Servers","Channels":"Channels","Queries":"Query list",
                  "File transfers":"File transfers","Mentions/unread":"Mentions and unread"}[label]
        observed=self.panel_title()
        prefix="[OFFLINE] " if observed.startswith("[OFFLINE] ") else ""
        available=self.screen.cols-len(prefix)
        return observed[len(prefix):].lower().startswith(expected[:available].lower())

    def open_destination(self,label):
        if label=="Unread views": label="Mentions/unread"
        if label=="Mentions":
            self.command("/mentions")
            return
        # Test workflows can still inspect an empty list through its command;
        # they must not invent a header entry that the user asked us to hide.
        # Visibility/cycling regressions call focus_destination directly.
        from_header=True
        if label not in "".join(self.screen.cells[0]):
            commands={"Servers":"/servers","Channels":"/channels","Queries":"/ql",
                      "File transfers":"/transfers","Mentions/unread":"/unread"}
            if label in commands:
                self.command(commands[label])
                from_header=False
            else:
                self.focus_destination(label)
        else:
            try:
                self.focus_destination(label)
            except AssertionError as error:
                if "Cannot select destination" not in str(error):
                    raise
                commands={"Servers":"/servers","Channels":"/channels","Queries":"/ql",
                          "File transfers":"/transfers","Mentions/unread":"/unread"}
                if label not in commands:
                    raise
                self.command(commands[label])
                from_header=False
        title={"vipirc":"vipirc","Servers":"Servers","Channels":"Channels","Queries":"Query list",
               "File transfers":"File transfers","Mentions/unread":"Mentions and unread"}[label]
        self.wait(lambda:self.destination_visible(label),"destination did not open: "+label)
        if label=="Mentions":
            self.wait(lambda:self.is_main(),"Mentions did not restore a chat view")
        else:
            self.wait(lambda:not self.is_main(),"destination left the chat editor active: "+label)
        if from_header:
            self.wait(lambda:self.header_focus() is None,"function-key shortcut did not focus the opened destination")

    def app_entry(self,label):
        entries=("Server","Nick","Settings","Manual","Command","Background","About","IALA")
        assert label in entries,label
        self.open_destination("vipirc")
        self.type(b"g"+b"\x1bOB"*entries.index(label)+b"\r")
        expected={"Server":"Settings","Nick":"Settings","IALA":"Insha-Allah","About":"About","Settings":"Settings","Manual":"Help / Manual","Command":"Command","Background":""}[label]
        self.wait(lambda:expected in self.panel_title(),"vipirc entry did not open: "+label)
        if label=="Settings":
            self.wait(lambda:"Interface" in self.screen.text() and "Open" in "".join(self.screen.cells[-2]),
                      "app-wide Settings home did not finish rendering")
        elif label=="Manual":
            self.wait(lambda:"Back" in "".join(self.screen.cells[3]) and
                      (self.screen.cols<40 or "Search" in "".join(self.screen.cells[3])) and
                      "Return to: vipirc" in "".join(self.screen.cells[4]),
                      "manual Search and Back controls did not finish rendering")

    def command_text(self):
        if not self.in_command():
            return ""
        rows = 3 if self.screen.rows >= 14 else 1
        return "".join("".join(row[1:-1]) for row in self.screen.cells[4:4 + rows]).rstrip()

    def open_help(self):
        self.app_entry("Manual")

    def close_help(self):
        assert "Help / Manual" in self.panel_title(),self.screen.text()
        self.type(b"\x1b")
        self.wait(lambda:self.panel_title().startswith("vipirc |"),"Manual did not return to its vipirc parent")
        self.leave_app()
        self.wait(self.main_frame_ready,"Help did not return to chat editing")

    def leave_app(self):
        assert self.panel_title().startswith("vipirc |"),self.screen.text()
        self.type("b")
        self.wait(self.main_frame_ready,"vipirc Back did not restore the chat editor")

    def open_app_diagnostics(self):
        self.open_destination("vipirc")
        self.wait(lambda:"Connection:" in self.screen.text() and "[Nick:" in self.screen.text(),
                  "application connection diagnostics did not finish drawing")

    def close_app_diagnostics(self):
        assert self.panel_title().startswith("vipirc |"),self.screen.text()
        self.leave_app()
        self.wait(self.main_frame_ready,"application diagnostics did not restore chat")

    def app_line(self,label):
        assert self.panel_title().startswith("vipirc |"),self.screen.text()
        for row in self.screen.cells[3:-2]:
            line="".join(row).strip()
            if line.startswith("[") and line.endswith("]"):
                line=line[1:-1]
            if line.startswith(label):
                return line
        raise AssertionError("Application diagnostic is missing: %s\n%s"%(label,self.screen.text()))

    def open_command(self, *, from_chat=False):
        if self.in_command():
            self.type(b"\x1b")
            self.wait(lambda:self.is_main() or self.panel_title().startswith("vipirc |"),
                      "Command cancel did not restore its parent")
        if from_chat:
            self.input_mode()
            self.type(getattr(self,"home_key",b"\x1bOH")+b"/")
        else:
            self.app_entry("Command")
        self.wait(lambda:self.in_command() and self.command_text()=="/" and
                  "Run" in "".join(self.screen.cells[-2]) and "Cancel" in "".join(self.screen.cells[-2]),
                  "Command entry did not open a fresh editor")
        assert self.screen.cursor_visible,"command prompt has no visible hardware caret"

    def finish_command_cancel(self, from_app=True):
        """Check the explicit parent after a caller presses Esc or Cancel."""
        if from_app:
            self.wait(lambda:self.panel_title().startswith("vipirc |"),
                      "Command opened from vipirc did not cancel back to vipirc")
            self.leave_app()
        self.wait(self.main_frame_ready,"Command cancellation did not restore the chat editor")

    def command(self,text,*,error=None,resume_input=True,from_chat=False):
        if not text.startswith("/"):
            self.input_mode()
            self.type(text+"\r")
            return
        self.open_command(from_chat=from_chat)
        self.type(text[1:])
        self.wait(lambda:self.command_text()==text.rstrip(),"command text was not entered before Run: "+text)
        self.type(b"\r")
        if error is not None:
            self.wait(lambda:self.in_command() and error in self.screen.text(),
                      "failed command did not stay editable with diagnostic: "+text)
            return
        self.wait(lambda:not self.in_command(),"Run did not complete command: "+text)
        self.wait(lambda:self.mode() in ("INPUT","PANEL"),"command destination did not finish its title: "+text)
        if self.mode()=="INPUT":
            self.wait(self.main_frame_ready,"command return did not finish the chat editor frame")
        # Kept as a call-compatible argument for old workflow tests. There is
        # no navigation mode to resume or extra focus transition to emulate.
        del resume_input

    def input_text(self):
        if not self.is_main():
            return ""
        for index,row in enumerate(self.screen.cells[:-1]):
            if re.match(r"[+| -]*\d+ bytes\b","".join(row)):
                return "\n".join("".join(cells[1:-1]) for cells in self.screen.cells[index+1:-2])
        return ""

    def input_mode(self):
        self.wait(lambda:self.mode() is not None,"screen did not settle before returning to chat")
        if not self.is_main():
            if self.panel_title().startswith("vipirc |"):
                self.leave_app()
                return
            was_command=self.in_command()
            was_settings=any(title in self.panel_title() for title in ("Settings","Quickstart","New server","Insha-Allah","About")) or \
                         ("Help / Manual" in self.panel_title() and "Return to: vipirc" in self.screen.text())
            self.type(b"\x1b")
            if was_command:
                self.wait(lambda:self.is_main() or self.panel_title().startswith("vipirc |"),
                          "Command Escape did not restore its parent")
                if self.panel_title().startswith("vipirc |"):
                    self.leave_app()
            elif was_settings:
                self.wait(lambda:self.panel_title().startswith("vipirc |"),"Escape did not return to vipirc")
                self.leave_app()
        self.wait(self.main_frame_ready,"Escape did not restore the complete chat editor frame")
        assert self.screen.cursor_visible,"main chat caret is hidden"

    def navigate(self,action=""):
        destinations={"S":"Servers","c":"Channels","q":"Queries","t":"File transfers","m":"Mentions","w":"Unread views"}
        if not action:
            self.focus_destination("vipirc")
        elif action in destinations:
            self.open_destination(destinations[action])
        elif action=="?":
            self.open_help()
        elif action=="/":
            self.open_command()
        elif action in ("x","u","s","0","n","p"):
            self.command({"x":"/clearinput","u":"/users","s":"/server","0":"/server","n":"/next","p":"/prev"}[action])
        else:
            raise AssertionError("Removed main hotkey used by test: "+repr(action))

    def clear_input(self):
        self.command("/clearinput")
        self.wait(lambda:self.is_main() and any(re.match(r"[+| -]*0 bytes\b","".join(row)) for row in self.screen.cells),
                  "/clearinput did not clear the current chat draft")
        assert self.screen.cursor_visible

class IRCFixture:
    def __init__(self, certificate=None, key=None, sasl=None):
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(0.05)
        self.port = self.listener.getsockname()[1]
        self.socket = None
        self.lines = []
        self.errors = []
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.nick = "tester"
        self.certificate, self.key, self.sasl = certificate, key, sasl
        self.sasl_data = bytearray()
        self.has_user = self.cap_end = self.registered = False
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def send_bytes(self, data):
        with self.lock:
            if self.socket is None:
                raise AssertionError("client is not connected")
            self.socket.sendall(data)

    def send(self, line):
        self.send_bytes(line.encode("utf-8") + b"\r\n")

    def names(self, channel):
        self.send(":srv 353 %s = %s :@%s @alice +bob" % (self.nick, channel, self.nick))
        self.send(":srv 366 %s %s :End of NAMES" % (self.nick, channel))

    def handle(self, line):
        text = line.decode("utf-8", "replace")
        if text.startswith("CAP LS"):
            self.send(":srv CAP * LS :multi-prefix server-time" + (" sasl=PLAIN" if self.sasl else ""))
        elif text.startswith("CAP REQ :"):
            self.send(":srv CAP %s ACK :%s" % (self.nick, text.split(" :", 1)[1]))
        elif text == "CAP END": self.cap_end = True
        elif text == "AUTHENTICATE PLAIN":
            self.send("AUTHENTICATE +")
        elif text.startswith("AUTHENTICATE "):
            part = text.split(" ", 1)[1]
            if part != "+": self.sasl_data.extend(part.encode("ascii"))
            if len(part) < 400 or part == "+":
                if self.sasl == "success":
                    self.send(":srv 903 %s :SASL authentication successful" % self.nick)
                else:
                    self.send(":srv 904 %s :SASL authentication failed" % self.nick)
        elif text.startswith("NICK "):
            old = self.nick
            self.nick = text[5:].lstrip(":")
            if self.registered: self.send(":%s!u@localhost NICK :%s" % (old, self.nick))
        elif text.startswith("USER "): self.has_user = True
        elif text.startswith("USERHOST "):
            self.send(":srv 302 %s :%s=+u@localhost" % (self.nick, self.nick))
        elif text.startswith("JOIN "):
            for channel in text.split(" ")[1].split(","):
                self.send(":%s!u@localhost JOIN :%s" % (self.nick, channel))
                self.names(channel)
        elif text.startswith("PART "):
            self.send(":%s!u@localhost PART %s :Leaving" % (self.nick, text.split(" ")[1]))
        elif text.startswith("NAMES "): self.names(text.split(" ")[1])
        elif text.startswith("PING "): self.send(":srv PONG srv :" + text[5:].lstrip(":"))
        if self.has_user and self.cap_end and not self.registered:
            self.registered = True
            self.send(":srv 001 %s :Welcome to the local test server" % self.nick)
            self.send(":srv 005 %s CASEMAPPING=rfc1459 PREFIX=(ov)@+ CHANMODES=b,k,l,imnt CHANTYPES=#& :supported" % self.nick)
            self.send(":srv 375 %s :- Test message of the day" % self.nick)
            self.send(":srv 372 %s :- Local fixture MOTD" % self.nick)
            self.send(":srv 376 %s :End of MOTD" % self.nick)

    def run(self):
        try:
            while not self.stop.is_set():
                try:
                    connection, _ = self.listener.accept()
                    break
                except socket.timeout:
                    continue
            else:
                return
            if self.certificate:
                connection.settimeout(3)
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.minimum_version = ssl.TLSVersion.TLSv1_2
                context.load_cert_chain(self.certificate, self.key)
                connection = context.wrap_socket(connection, server_side=True)
            connection.settimeout(0.05)
            self.socket = connection
            pending = b""
            while not self.stop.is_set():
                try: data = connection.recv(32768)
                except socket.timeout: continue
                if not data: break
                pending += data
                while b"\r\n" in pending:
                    line, pending = pending.split(b"\r\n", 1)
                    assert len(line) + 2 <= 512, "outbound IRC line exceeds 512 bytes"
                    assert b"\0" not in line and b"\n" not in line and b"\r" not in line
                    self.lines.append((time.monotonic(), line))
                    self.handle(line)
        except (ConnectionResetError, BrokenPipeError, OSError) as error:
            if not self.stop.is_set() and not isinstance(error, (ConnectionResetError, BrokenPipeError)):
                self.errors.append(error)
        except Exception as error:
            self.errors.append(error)
        finally:
            if self.socket is not None: self.socket.close()
            self.listener.close()

    def messages(self, target="#test"):
        prefix = ("PRIVMSG " + target + " :").encode("utf-8")
        return [(at, line.split(b" :", 1)[1]) for at, line in self.lines
                if line.startswith(prefix)]

    def close(self):
        self.stop.set()
        if self.socket is not None:
            try: self.socket.shutdown(socket.SHUT_RDWR)
            except OSError: pass
        self.thread.join(timeout=2)
        self.listener.close()
        assert not self.thread.is_alive(), "fixture thread did not exit"
        assert not self.errors, self.errors


class Session(ModeControls):
    def __init__(self, nick=True, channel=True, sasl=None):
        self.channel = channel
        self.temp = tempfile.TemporaryDirectory(prefix="vipirc-integration-")
        self.config = Path(self.temp.name) / "config.ini"
        certificate = key = None
        if sasl:
            from test_net import cert_pair
            certificate, key = cert_pair(Path(self.temp.name), "localhost", "DNS:localhost,IP:127.0.0.1")
            password = Path(self.temp.name) / "password"
            password.write_text("fixture-password\n", encoding="ascii")
            password.chmod(0o600)
            self.config.write_text("[connection]\nauth=sasl\nauth_user=test-account\npassword_file=%s\n" % password,
                                   encoding="ascii")
        else:
            self.config.write_text("", encoding="ascii")
        self.config.chmod(0o600)
        self.server = IRCFixture(certificate, key, sasl)
        self.master, slave = pty.openpty()
        self.term_before = termios.tcgetattr(slave)
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        self.screen = Screen()
        self.raw = bytearray()
        args = [str(BINARY), "--config", str(self.config), "--server", "127.0.0.1",
                "--port", str(self.server.port)]
        args += ["--tls", "--ca-file", str(certificate)] if certificate else ["--plain"]
        if nick: args += ["--nick", "tester"]
        if channel: args += ["--channel", channel if isinstance(channel, str) else "#test"]
        env = dict(os.environ, TERM="xterm-256color", LANG="C.UTF-8", LC_ALL="C.UTF-8")
        def controlling_terminal():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)
        self.proc = subprocess.Popen(args, stdin=slave, stdout=slave, stderr=slave,
                                     cwd=ROOT, env=env, preexec_fn=controlling_terminal)
        os.close(slave)
        os.set_blocking(self.master, False)

    def pump(self, timeout=0.03):
        if select.select([self.master], [], [], timeout)[0]:
            try:
                data = os.read(self.master, 65536)
            except OSError as error:
                if error.errno in (errno.EIO, errno.EAGAIN): return
                raise
            self.raw.extend(data)
            self.screen.feed(data)

    def wait(self, predicate, label, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.pump()
            if predicate(): return
            if self.proc.poll() is not None:
                break
        raise AssertionError("%s\nexit=%r\nscreen:\n%s\nrecent output:%r\nIRC:%r" %
                             (label, self.proc.poll(), self.screen.text(), bytes(self.raw[-1200:]), self.server.lines[-12:]))

    def quiet(self, duration=0.18):
        end = time.monotonic() + duration
        while time.monotonic() < end: self.pump(min(0.03, max(0, end - time.monotonic())))

    def type(self, data):
        if isinstance(data, str): data = data.encode("utf-8")
        deadline = time.monotonic() + 5
        while data:
            try:
                wrote = os.write(self.master, data)
            except BlockingIOError:
                assert time.monotonic() < deadline, "terminal stopped accepting input"
                self.pump(0.01)
                continue
            data = data[wrote:]

    def acknowledge_input_limit(self):
        self.wait(lambda: "Message limit reached" in self.screen.text(),
                  "early message-limit warning missing")
        self.quiet(0.12)
        self.type(b"\x1b")
        self.wait(lambda: "Message limit reached" not in self.screen.text(),
                  "early message-limit acknowledgment failed")
        assert self.mode() == "INPUT", "warning acknowledgment changed editing mode"

    def start(self, nick_prompt=False):
        self.wait(lambda: b"Not vegetarian?" in self.raw, "welcome warning missing")
        assert b"www.bloodguiltcurse.net" in self.raw
        self.type("x")
        if nick_prompt:
            login = pwd.getpwuid(os.getuid()).pw_name
            self.wait(lambda: ("Set user nick: (" + login + ")").encode() in self.raw,
                      "Unix login nickname default not displayed")
            self.type("\r")
        self.wait(lambda: self.server.registered, "IRC registration did not complete")
        if self.channel:
            self.wait(lambda: any(line.startswith(b"JOIN #test") for _, line in self.server.lines),
                      "automatic channel join did not occur")
        else:
            self.wait(lambda: "MOTD" in self.screen.text(), "server view omitted MOTD")
        self.quiet()
        assert self.mode() == "INPUT", (
            "default startup must leave chat input active", self.screen.text())

    def resize(self, rows, cols):
        fcntl.ioctl(self.master, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.screen.resize(rows, cols)
        os.kill(self.proc.pid, signal.SIGWINCH)
        self.quiet()

    def close(self):
        forced = False
        if self.proc.poll() is None:
            self.type(b"\x03")
            try: self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                forced = True
                self.proc.terminate()
                try: self.proc.wait(timeout=2)
                except subprocess.TimeoutExpired: self.proc.kill(); self.proc.wait(timeout=2)
        self.pump(0)
        terminal_after = termios.tcgetattr(self.master)
        mask = termios.ICANON | termios.ISIG | termios.ECHO
        terminal_restored = terminal_after[3] & mask == self.term_before[3] & mask
        paste_restored = b"\x1b[?2004h" not in self.raw or b"\x1b[?2004l" in self.raw
        os.close(self.master)
        self.server.close()
        self.temp.cleanup()
        assert not forced, "Ctrl-C did not exit the interactive client promptly"
        assert terminal_restored, "terminal ICANON/ISIG/ECHO were not restored"
        assert paste_restored, "terminal bracketed-paste mode was not disabled on exit"

    def __enter__(self): return self
    def __exit__(self, kind, value, traceback): self.close()


def editing_and_mentions():
    with Session() as s:
        s.start()
        s.input_mode()
        assert b"x\r\n" not in [line for _, line in s.server.lines], "welcome dismiss key leaked"
        # A flat logical buffer must backspace across screen wraps and resize.
        text = "0123456789" * 16
        s.type(text)
        s.quiet()
        s.resize(24, 40)
        s.type(b"\x7f" * 81 + b"Z\r")
        s.wait(lambda: len(s.server.messages()) == 1, "wrapped input was not sent")
        assert s.server.messages()[0][1] == text[:-81].encode() + b"Z"
        s.resize(24, 80)
        s.type("A\u4e2dB")
        s.type(b"\x7f\x7fZ\r")
        s.wait(lambda: len(s.server.messages()) == 2, "Unicode edit not sent")
        assert s.server.messages()[1][1] == b"AZ"

        before = len(s.server.messages())
        s.type("A")
        for byte in "\u4e2d".encode("utf-8"):
            s.type(bytes([byte]))
            s.quiet(0.04)
        s.type("Z\r")
        s.wait(lambda: len(s.server.messages()) == before + 1, "fragmented UTF-8 input was not sent")
        assert s.server.messages()[-1][1] == "A\u4e2dZ".encode("utf-8"), (
            "fragmented UTF-8 input lost bytes", s.server.messages()[-1][1])

        before = len(s.server.messages())
        s.type("channel draft")
        s.navigate("m")
        s.input_mode()
        s.type("mentions draft")
        s.navigate("n")  # Help is a secondary reader, skipped by conversation cycling.
        s.quiet()
        assert "channel draft" in s.screen.text(), "channel draft was lost when changing views"
        s.input_mode()
        s.type("\r")
        s.wait(lambda: len(s.server.messages()) == before + 1, "restored channel draft could not be sent")
        assert s.server.messages()[-1][1] == b"channel draft"
        s.navigate("m")
        s.quiet()
        assert "mentions draft" in s.screen.text(), "mentions draft was lost when changing views"
        s.clear_input()
        s.command("/view #test")
        s.quiet()

        # Bracketed paste must neither execute commands nor submit newlines.
        before = len(s.server.messages())
        s.type(b"\x1b[200~alpha\r\nbeta\x1b[201~")
        s.quiet()
        assert len(s.server.messages()) == before, "paste submitted without Enter"
        s.wait(lambda: "Message limit reached" in s.screen.text(), "multiline paste warning missing")
        s.server.send("PING :during-input-limit")
        s.wait(lambda: any(line == b"PONG :during-input-limit" for _, line in s.server.lines),
               "early input warning blocked server PING")
        s.acknowledge_input_limit()
        s.type("\r")
        s.wait(lambda: "[y/N]" in s.screen.text(),
               "multiline paste did not offer confirmation")
        s.server.send("PING :during-confirmation")
        s.wait(lambda: any(line == b"PONG :during-confirmation" for _, line in s.server.lines),
               "confirmation blocked server PING")
        s.quiet(0.12)  # A new decision after the triggering key burst.
        s.type("\t")
        s.quiet(0.06)
        assert "[y/N]" in s.screen.text() and len(s.server.messages()) == before, "Tab approved or dismissed send confirmation"
        s.type("\r")
        s.quiet()
        assert len(s.server.messages()) == before, "default confirmation sent text"
        s.type("\r")
        s.wait(lambda: "[y/N]" in s.screen.text(), "declining failed to retain the draft")
        s.quiet()
        s.type("y")
        s.wait(lambda: len(s.server.messages()) == before + 2, "confirmed paste did not send two lines", timeout=6)
        assert [text for _, text in s.server.messages()[-2:]] == [b"alpha", b"beta"]

        # Oversized UTF-8 text is split only after an explicit decision.
        before = len(s.server.messages())
        long_text = "abc " * 210
        s.type(b"\x1b[200~" + long_text.encode("ascii") + b"\x1b[201~")
        s.acknowledge_input_limit()
        s.type("\r")
        s.quiet()
        assert len(s.server.messages()) == before
        s.type("n")
        s.quiet()
        assert len(s.server.messages()) == before
        s.type("\r")
        s.quiet()
        s.type("y")
        s.wait(lambda: sum(len(text) for _, text in s.server.messages()[before:]) == len(long_text),
               "confirmed long message not transmitted intact", timeout=8)
        assert b"".join(text for _, text in s.server.messages()[before:]) == long_text.encode()
        messages = s.server.messages()
        for (previous, _), (current, _) in zip(messages, messages[1:]):
            assert current - previous >= 1.10, "chat pacing was less than the configured 1.2 seconds"

        before = len(s.server.messages())
        s.type(b"\x1b[200~/quit\x1b[201~\r")
        s.wait(lambda: len(s.server.messages()) == before + 1, "pasted slash-command was executed instead of sent literally")
        assert s.server.messages()[-1][1] == b"/quit"
        assert s.proc.poll() is None

        s.command("/watch add creek phrase")
        s.quiet()
        s.server.send(":alice!a@host PRIVMSG #test :hello tester, full mention line")
        s.server.send(":alice!a@host PRIVMSG #test :this creek phrase is watched")
        s.navigate("m")
        s.wait(lambda: "full mention line" in s.screen.text() and "creek phrase is watched" in s.screen.text(),
               "mentions view missing full captured lines")
        assert re.search(r"\d\d:\d\d:\d\d", s.screen.text()), "mention timestamp absent"
        assert "alice" in s.screen.text() and "#test" in s.screen.text(), "mention context absent"

        # Membership and privilege changes, plus visibility toggle.
        s.server.send(":carol!c@host JOIN :#test")
        s.server.send(":alice!a@host MODE #test +o carol")
        s.server.send(":bob!b@host NICK :robert")
        s.server.send(":carol!c@host PART #test :leaving")
        s.command("/view #test")
        def sidebar():
            return "\n".join("".join(row[59:]) for row in s.screen.cells[2:20])
        s.wait(lambda: "robert" in sidebar(), "channel roster did not follow nickname change")
        assert "carol" not in sidebar() and "bob" not in sidebar()
        assert "alice" in sidebar() and "tester" in sidebar()
        s.command("/users off")
        s.wait(lambda: "Users: 3" not in s.screen.text(), "user list did not hide")
        s.command("/users on")
        s.wait(lambda: "Users: 3" in s.screen.text(), "user list did not return")
        assert s.proc.poll() is None
        # Saving a real preference exercises panel staging and config output.
        # Focus styling is fixed; legacy keys must not be regenerated.
        from test_settings import open_settings, edit_field, action, return_main, HISTORY_FIELD
        open_settings(s)
        edit_field(s, HISTORY_FIELD, "200")
        action(s, 1)
        s.wait(lambda: "Preferences applied" in s.screen.text(), "history preference Apply failed")
        return_main(s)
        s.command("/save")
        s.wait(lambda: "[connection]" in s.config.read_text(), "program-generated config missing")
        assert s.config.stat().st_mode & 0o777 == 0o600
        assert "keymap=" not in s.config.read_text(), "/save persisted an obsolete navigation preset"
        assert "focus_style=" not in s.config.read_text(), "/save persisted obsolete focus styling"
        assert "history_lines=200\n" in s.config.read_text(), "/save did not persist the selected history limit"
        checked = subprocess.run([str(BINARY), "--config", str(s.config), "--check-config"],
                                 stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, timeout=3)
        assert checked.returncode == 0, ("saved configuration could not be reloaded", checked.stderr)

        # Dangerous terminal sequences are never copied into output bytes.
        start = len(s.raw)
        s.server.send_bytes(b":alice!a@host PRIVMSG #test :evil\x1b]52;c;U0VDUkVU\x07 end\r\n")
        s.server.send_bytes(b":alice!a@host PRIVMSG tester :\x01VERSION\x01\r\n")
        s.quiet()
        assert b"\x1b]52" not in s.raw[start:], "server text executed a terminal clipboard sequence"
        assert not any(line.startswith(b"NOTICE alice :\x01VERSION") for _, line in s.server.lines), "unsolicited CTCP response"
        assert s.proc.poll() is None
    print("integration: welcome, editing, resize, UTF-8, paste, confirmation, PING, pacing, mentions, users, save and terminal sanitization passed")


def malicious_lines():
    for name, attack in (
        ("NUL", b":alice!a@host PRIVMSG #test :embedded\0NUL\r\n" * 8),
        ("overlong", b":" + b"A" * 18000 + b"\r\n"),
    ):
        with Session() as s:
            s.start()
            s.server.send_bytes(attack)
            s.wait(lambda: "disconnect" in s.screen.text().lower() or "invalid" in s.screen.text().lower()
                   or "malformed" in s.screen.text().lower() or "overlong" in s.screen.text().lower()
                   or "oversized" in s.screen.text().lower()
                   or (name == "NUL" and "server sent a nul byte" in s.screen.text().lower())
                   or "too long" in s.screen.text().lower(), "malformed line was not rejected: " + name)
            s.wait(lambda: not s.server.thread.is_alive(), "malformed input left the transport connected: " + name)
            assert s.proc.poll() is None, "malformed remote data crashed the process"
    print("integration: NUL and oversized remote frames rejected without a crash")


def nickname_default():
    with Session(nick=False) as s:
        s.start(nick_prompt=True)
        expected = pwd.getpwuid(os.getuid()).pw_name.encode()
        assert any(line == b"NICK " + expected for _, line in s.server.lines)
    print("integration: Enter selects the Unix login nickname")


def server_view_default():
    with Session(channel=False) as s:
        s.start()
        assert not any(line.startswith(b"JOIN ") for _, line in s.server.lines)
        assert "Channel 0: Server" in s.screen.text() and "MOTD" in s.screen.text()
        s.navigate("c")
        s.wait(lambda: "Channels" in s.panel_title() and "0: server" in s.screen.text(),
               "channel chooser omitted server when no channel exists")
        s.type("c")
        s.quiet()
        assert "server" in s.screen.text().lower(), "channel chooser deleted the server row"
        s.type("\r")
        s.quiet()
        s.command("/channel 0")
        s.quiet()
        assert "Channel 0: Server" in s.screen.text() and "MOTD" in s.screen.text()
    print("integration: absent channel opens the server MOTD view")


def channel_chooser():
    with Session(channel="#test,#second,#third") as s:
        s.start()
        s.input_mode()
        s.wait(lambda: sum(line.startswith(b"JOIN ") for _, line in s.server.lines) == 3,
               "three startup channels were not joined")

        def in_channel(name):
            return any(re.search(r"Channel \d+: " + re.escape(name) + r"(?:\s|\||\]|$)", row)
                       for row in s.screen.text().splitlines()[:3])

        def open_chooser():
            s.navigate("c")
            s.wait(lambda: "Channels" in s.panel_title() and "#test" in s.screen.text()
                   and "#third" in s.screen.text(), "navigation Channels did not open the chooser")

        # Each view keeps its draft even while the chooser is being navigated.
        s.type("draft one")
        open_chooser()
        s.type(b"\x1bOB\r")
        s.wait(lambda: in_channel("#second"), "chooser Down/Enter did not select the second channel")
        s.input_mode()
        s.type("draft two")
        open_chooser()
        s.type(b"\x1bOB\r")
        s.wait(lambda: in_channel("#third"), "chooser Down/Enter did not select the third channel")
        s.input_mode()
        s.type("draft three")
        open_chooser()
        s.type(b"\x1bOA")
        s.type("p")
        s.wait(lambda: any("#second" in row and "left" in row.lower()
                           for row in s.screen.text().splitlines()),
               "leaving a channel did not retain a visibly left channel row")
        assert any(line.startswith(b"PART #second") for _, line in s.server.lines)

        # Literal paste observes the same joined-channel gate as typed text.
        s.type("\r")
        s.wait(lambda: in_channel("#second"), "left channel could not be inspected")
        s.type(b"\x1b[200~blocked paste\x1b[201~\r")
        s.wait(lambda: "Join this channel" in s.screen.text(), "paste bypassed the left-channel send gate")
        assert not s.server.messages("#second"), "pasted text was sent after parting"
        s.clear_input()
        s.type("draft two")
        open_chooser()

        # Closing a different view cannot discard its draft without a decision.
        s.type("c")
        s.wait(lambda: "[y/N]" in s.screen.text(), "closing an unsent draft did not ask")
        s.quiet(0.12)
        s.type("\t")
        s.quiet(0.06)
        assert "[y/N]" in s.screen.text(), "Tab approved or dismissed close confirmation"
        s.type("\r")
        s.quiet()
        assert "#second" in s.screen.text(), "default-No close removed the channel"
        s.type("c")
        s.wait(lambda: "[y/N]" in s.screen.text(), "retained draft was lost after declining close")
        s.quiet(0.12)
        s.type("y")
        s.quiet()
        s.type("\r")
        s.wait(lambda: in_channel("#third") and s.main_frame_ready() and
               "draft three" in s.input_text(),
               "closing a neighbor did not restore the remaining channel and its draft")
        assert "draft three" in s.screen.text(), "view compaction lost the neighboring draft"
        s.input_mode()
        s.type("\r")
        s.wait(lambda: bool(s.server.messages("#third")), "neighboring draft could not be sent after closing a view")
        assert s.server.messages("#third")[-1][1] == b"draft three"
        assert not s.server.messages("#second"), "closed channel draft was transmitted"
        open_chooser()
        assert not any("#second" in row for row in s.screen.text().splitlines()[3:18]), "closed channel remains in chooser"
        s.type(b"\x1bOA\r")
        s.wait(lambda: in_channel("#test") and s.main_frame_ready() and
               "draft one" in s.input_text(),
               "chooser did not restore the first remaining channel and its draft")
        assert "draft one" in s.screen.text(), "unrelated first-channel draft was lost"
        s.input_mode()
        s.type("\r")
        s.wait(lambda: bool(s.server.messages()), "first-channel draft was not preserved")
        assert s.server.messages()[-1][1] == b"draft one"

        # Channel zero is the permanent server view, and names/numbers agree.
        s.command("/channel 0")
        s.wait(lambda: "Channel 0: Server" in s.panel_title(), "/channel 0 did not select server")
        s.command("/channel #third")
        s.wait(lambda: in_channel("#third"), "/channel NAME did not select a retained channel")
        s.command("/channel 1")
        s.wait(lambda: in_channel("#test"), "/channel 1 did not match chooser numbering")

        # A closed slot can be reused; a closed draft never follows the new view.
        open_chooser()
        s.type("o")
        s.wait(lambda: s.in_command() and s.command_text() == "/join",
               "chooser join action did not open an editable join command")
        s.type("#second\r")
        s.wait(lambda: sum(line.startswith(b"JOIN #second") for _, line in s.server.lines) == 2,
               "closed channel could not be rejoined")
        s.wait(lambda: in_channel("#second"), "rejoined channel did not become current")
        assert "draft two" not in s.screen.text(), "closed draft leaked into a newly created view"
        s.command("/close #second")
        s.quiet()
        assert s.proc.poll() is None
    print("integration: chooser navigation, leave/retain, close confirmation, draft compaction, channel zero, numbering and rejoin passed")


def independent_instances():
    with Session() as first, Session() as second:
        first.start()
        second.start()
        first.command("first instance only")
        first.wait(lambda: bool(first.server.messages()), "first independent instance did not send")
        second.quiet()
        assert not second.server.messages(), "one instance sent through the other instance's server"
        second.command("second instance only")
        second.wait(lambda: bool(second.server.messages()), "second independent instance did not send")
        assert first.server.messages()[-1][1] == b"first instance only"
        assert second.server.messages()[-1][1] == b"second instance only"
        first.server.send(":alice!a@host PRIVMSG #test :tester first-private-mention")
        first.navigate("m")
        first.wait(lambda: "first-private-mention" in first.screen.text(), "first mention missing")
        second.navigate("m")
        second.quiet()
        assert "first-private-mention" not in second.screen.text(), "mention state crossed instances"
    print("integration: two simultaneous instances keep servers, drafts, sends and mentions independent")


def private_message_list():
    with Session() as s:
        s.start()
        s.input_mode()

        def in_view(name):
            prefix = r"Channel \d+: " if name.startswith("#") else r"Query: "
            return s.main_frame_ready() and any(re.search(prefix + re.escape(name) + r"(?:\s|\||\]|$)", row)
                                               for row in s.screen.text().splitlines()[:3])

        s.type("channel remains")
        s.server.send(":alice!a@host PRIVMSG tester :private hello")
        s.navigate("q")
        s.wait(lambda: "Query list" in s.screen.text() and "alice" in s.screen.text(),
               "incoming private conversation missing from query list")
        assert "#test" not in "\n".join(s.screen.text().splitlines()[3:-3]), "channel leaked into private-message list"
        s.type("\r")
        s.wait(lambda: in_view("alice"), "private list Enter did not select alice")
        assert "private hello" in s.screen.text()
        s.command("/ql")
        s.wait(lambda: "Query list" in s.screen.text() and "alice" in s.screen.text(),
               "/ql did not open the query list")
        s.type("\r")
        s.wait(lambda: in_view("alice"), "query-list return did not preserve its selection")
        s.input_mode()
        s.type("private draft")
        s.navigate("c")
        s.wait(lambda: "Channels" in s.panel_title() and "#test" in s.screen.text(),
               "channel list unavailable from a private conversation")
        assert "alice" not in "\n".join(s.screen.text().splitlines()[3:-3]), "private conversation leaked into channel list"
        s.type(b"\x1bOB\r")
        s.wait(lambda: in_view("#test") and "channel remains" in s.input_text(),
               "channel list did not select the channel from a private view")
        assert "channel remains" in s.screen.text(), "opening a private view lost the channel draft"

        # n must preserve this channel draft and use an available command draft.
        s.navigate("q")
        s.quiet()
        s.type("n")
        s.wait(lambda: s.in_command() and s.command_text() == "/pm",
               "private-list new action did not prepare /pm")
        s.type("bob\r")
        s.wait(lambda: in_view("bob"), "/pm bob did not open a private view")
        assert not any(line.startswith(b"PRIVMSG ") for _, line in s.server.lines), "/pm sent text before composition"
        s.navigate("q")
        s.wait(lambda: "alice" in s.screen.text() and "bob" in s.screen.text(), "private list omitted an open conversation")
        s.type(b"\x1bOA")
        s.type("c")
        s.wait(lambda: "[y/N]" in s.screen.text(), "private draft was discarded without confirmation")
        s.quiet(0.12)
        s.type("y")
        s.quiet()
        s.type("\r")
        s.wait(lambda: in_view("bob"), "closing another private view lost the remaining selection")
        s.command("/channel 1")
        s.wait(lambda: in_view("#test"), "channel could not be selected after closing a private view")
        assert "channel remains" in s.screen.text(), "closing a private view lost the channel draft"
        s.type("\r")
        s.wait(lambda: bool(s.server.messages()), "retained channel draft could not be sent")
        assert s.server.messages()[-1][1] == b"channel remains"
        assert not s.server.messages("alice") and not s.server.messages("bob")
        s.navigate("q")
        s.quiet()
        assert "alice" not in "\n".join(s.screen.text().splitlines()[3:-3]), "closed private view remains listed"
    print("integration: separate query list, /ql, incoming private views, open-without-send, close confirmation and channel-draft preservation passed")


def sasl_end_to_end():
    with Session(sasl="success") as s:
        s.start()
        assert base64.b64decode(s.server.sasl_data, validate=True) == b"\0test-account\0fixture-password"
        assert b"fixture-password" not in s.raw and s.server.sasl_data not in s.raw
        assert any(line == b"CAP END" for _, line in s.server.lines)
    with Session(sasl="failure") as s:
        s.wait(lambda: b"Not vegetarian?" in s.raw, "SASL fixture welcome missing")
        s.type("x")
        s.wait(lambda: "authentication failed" in s.screen.text(), "SASL failure was not surfaced")
        assert not s.server.registered
        assert not any(line.startswith(b"JOIN ") for _, line in s.server.lines)
        assert b"fixture-password" not in s.raw and s.server.sasl_data not in s.raw
    print("integration: SASL over verified TLS succeeds, rejects failed auth before joining, and keeps secrets off the terminal")


def main():
    if not BINARY.is_file():
        raise SystemExit("Build ./vipirc first (make).")
    editing_and_mentions()
    malicious_lines()
    nickname_default()
    server_view_default()
    channel_chooser()
    independent_instances()
    private_message_list()
    sasl_end_to_end()
    print("all local PTY integration tests passed")


if __name__ == "__main__":
    main()
