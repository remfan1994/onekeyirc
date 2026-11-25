#!/usr/bin/env python3
"""Async setup/paste boundaries through real PTYs and isolated GPG fixtures."""
import argparse
import os
from pathlib import Path
import stat
import sys
import tempfile
from unittest.mock import patch

from test_idle_terminal import IdleSession, BEGIN, END, LEFT, RIGHT, HOME, FINISH
from test_idle_setup import open_setup


FINGERPRINT = "0123456789ABCDEF0123456789ABCDEF01234567"
SECRET = "\u4e2d" * 24 + "a\u0301" * 8 + "tail"
FAKE_CIPHERTEXT = b"ENCRYPTED ASYNC SETUP FIXTURE"


def helper(directory, outcome):
    program = directory / "gpg"
    program.write_text("#!" + sys.executable + "\n" + '''import os, sys, time
assert sys.argv[-1] == "--encrypt"
secret = sys.stdin.buffer.read(1024)
assert secret and len(secret) < 1024
marker = os.open(os.environ["IRC_SETUP_MARKER"], os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
os.write(marker, (str(os.getpid()) + "\\n").encode("ascii"))
os.close(marker)
while not os.path.exists(os.environ["IRC_SETUP_GATE"]):
    time.sleep(.01)
if os.environ["IRC_SETUP_OUTCOME"] == "failure":
    sys.exit(2)
os.write(3, b"[GNUPG:] BEGIN_ENCRYPTION 2 9\\n")
os.write(1, b"ENCRYPTED ASYNC SETUP FIXTURE")
os.write(3, b"[GNUPG:] END_ENCRYPTION\\n")
''', encoding="utf-8")
    program.chmod(0o700)
    return {
        "PATH": str(directory) + os.pathsep + os.environ.get("PATH", "/usr/bin:/bin"),
        "GNUPGHOME": str(directory / "unused-keyring"),
        "IRC_SETUP_MARKER": str(directory / "helper-started"),
        "IRC_SETUP_GATE": str(directory / "helper-release"),
        "IRC_SETUP_OUTCOME": outcome,
    }


def alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False


def delayed_gui_paste_begin():
    from test_servers import MultiServerSession
    with MultiServerSession(cols=132) as s:
        s.start()
        s.type(b"\x1b")
        s.quiet(.20)
        s.server.send("PING :opening-paste-prefix")
        s.wait(lambda: any(line == b"PONG :opening-paste-prefix" for _, line in s.server.lines),
               "delayed opening marker blocked IRC PING", timeout=2)
        assert s.mode() == "INPUT", "a fragmented opening marker activated Escape navigation"
        s.type(b"[200~/quit\r" + END)
        s.wait(lambda: s.mode() == "INPUT" and "/quit" in s.input_text(),
               "delayed opening marker did not capture clipboard text literally")
        s.quiet(.20)
        assert s.proc.poll() is None and not s.in_command()
        assert s.input_text().strip() == "/quit" and not s.server.messages()
        s.server.send("PING :opening-paste-complete")
        s.wait(lambda: any(line == b"PONG :opening-paste-complete" for _, line in s.server.lines),
               "captured clipboard blocked IRC PING", timeout=2)
    print("GUI paste opening: 200 ms delayed marker stays literal; /quit never executes and PING remains responsive", flush=True)


class Frontend:
    def __init__(self, session, mode):
        self.s, self.mode = session, mode
        self.native = mode != "gui"
        self.peer = session.peer if self.native else session.server

    def question(self):
        return "".join(self.s.screen.cells[2 if self.native else 5]).strip()

    def active(self):
        if self.native:
            return self.s.screen.alternate and any(
                title in "".join(self.s.screen.cells[0]) for title in (" - Settings panel", " - Quickstart"))
        return self.s.panel_title().startswith(("Settings panel", "Quickstart"))

    def answer_rows(self):
        return self.s.screen.cells[-5:-3] if self.native else self.s.screen.cells[-5:-2]

    def input_ready(self):
        return self.s.screen.cursor_visible and self.s.screen.rows - 5 <= self.s.screen.row <= self.s.screen.rows - 3

    def begin(self):
        self.s.start()
        if self.native:
            open_setup(self.s, "panel", self.mode == "idle")
        else:
            self.s.open_command(from_chat=True)
            self.s.type("panel")
            self.s.wait(lambda: self.s.command_text() == "/panel", "panel command was not composed")
            self.s.type(b"\r")
            self.wait("Choose a panel")

    def wait(self, question):
        self.s.wait(lambda: self.active() and self.question() == question and self.input_ready(),
                    "setup question missing: " + question)

    def enter(self, question, answer):
        self.wait(question)
        self.s.type(answer)
        if answer:
            if question in ("Authentication password to encrypt", "Confirm authentication password"):
                self.s.wait(lambda: sum("".join(row).count("*") for row in self.answer_rows()) == 60,
                            "wide/combining password was not fully masked")
            else:
                self.s.wait(lambda: answer in "".join("".join(row) for row in self.answer_rows()),
                            "setup answer was not shown")
        self.s.quiet(.30 if self.native else .12)
        self.s.type(b"\r")
        self.s.wait(lambda: self.active() and self.question() != question and self.input_ready(),
                    "setup did not advance from " + question)

    def masked_geometry(self):
        question = "Authentication password to encrypt"
        self.wait(question)
        self.s.resize(self.s.screen.rows, 48)
        self.wait(question)
        self.s.type(SECRET)
        top = self.s.screen.rows - 5
        inset = 0 if self.native else 1
        up, down = (b"\x1b[A", b"\x1b[B") if self.native else (b"\x1bOA", b"\x1bOB")
        left, right, home, finish = (LEFT, RIGHT, HOME, FINISH) if self.native else (
            b"\x1bOD", b"\x1bOC", b"\x1bOH", b"\x1bOF")
        self.s.wait(lambda: self.s.screen.row == top + 1 and self.s.screen.col == 14 + inset,
                    "masked Unicode field used a different caret geometry from its arrows")
        self.s.type(up)
        self.s.wait(lambda: self.s.screen.row == top and self.s.screen.col == 14 + inset,
                    "Up did not preserve the masked visual column")
        self.s.type(down)
        self.s.wait(lambda: self.s.screen.row == top + 1 and self.s.screen.col == 14 + inset,
                    "Down did not restore the masked visual column")
        self.s.type(left * 4)
        self.s.wait(lambda: self.s.screen.col == 10 + inset, "Left did not reach the combining-text boundary")
        self.s.type(left)
        self.s.wait(lambda: self.s.screen.col == 9 + inset, "Left split a masked combining cluster")
        self.s.type(right)
        self.s.wait(lambda: self.s.screen.col == 10 + inset, "Right split a masked combining cluster")
        self.s.type(home + right * 23)
        self.s.wait(lambda: self.s.screen.row == top + 1 and self.s.screen.col == inset,
                    "masked wrap-boundary caret was not visible")
        self.s.type(b"\x7f")
        self.s.wait(lambda: self.s.screen.row == top and self.s.screen.col == 44 + inset,
                    "masked backspace did not return to the previous visual line")
        self.s.type("\u4e2d")
        self.s.type(finish)
        self.s.wait(lambda: self.s.screen.row == top + 1 and self.s.screen.col == 14 + inset,
                    "masked edit did not preserve the password's cluster layout")
        self.s.resize(self.s.screen.rows, 132 if not self.native else 120)
        self.wait(question)
        self.s.quiet(.30 if self.native else .12)
        self.s.type(b"\r")
        self.wait("Confirm authentication password")

    def stage(self, destination, check_geometry):
        self.enter("Choose a panel", "1")
        values = {
            "Use TLS": "yes",
            "Auto-authentication": "sasl",
            "Authentication account": "fixture-account",
            "Authentication password source": "gpg",
            "Authentication GPG file": "create",
            "Authentication password file": str(destination),
            "Authentication GPG key fingerprint": FINGERPRINT,
            "Authentication password to encrypt": SECRET,
            "Confirm authentication password": SECRET,
        }
        for _ in range(48):
            question = self.question()
            if question == "Finish":
                return
            if check_geometry and question == "Authentication password to encrypt":
                self.masked_geometry()
            else:
                self.enter(question, values.get(question, ""))
        raise AssertionError("setup exceeded its bounded question count")

    def finish(self):
        self.wait("Finish")
        self.s.type("4")
        self.s.quiet(.30 if self.native else .12)
        self.s.type(b"\r")
        self.s.wait(lambda: self.active() and self.question() == "Creating encrypted credentials",
                    "Finish did not start the asynchronous GPG job")

    def ping(self, token):
        self.peer.send("PING :" + token)
        self.s.wait(lambda: any(line == ("PONG :" + token).encode() for _, line in self.peer.lines),
                    "setup/helper/paste blocked IRC PING", timeout=2)


def run_case(mode, outcome):
    print("async setup: %s %s" % (mode, outcome), flush=True)
    with tempfile.TemporaryDirectory(prefix="irc-setup-paste-") as temporary:
        directory = Path(temporary)
        environment = helper(directory, outcome)
        marker, gate = Path(environment["IRC_SETUP_MARKER"]), Path(environment["IRC_SETUP_GATE"])
        with patch.dict(os.environ, environment):
            if mode == "gui":
                from test_servers import MultiServerSession
                session = MultiServerSession(cols=132)
            else:
                session = IdleSession(live=True, review=False, full=mode == "idle", cols=120)
        with session as s:
            ui = Frontend(s, mode)
            destination = s.directory / "encrypted-credential.gpg"
            ui.begin()
            raw_start = len(s.raw)
            ui.stage(destination, outcome == "success")
            assert not destination.exists(), "answering questions created a credential before Finish"
            ui.finish()
            s.wait(lambda: marker.exists() and marker.read_text().strip(), "isolated GPG helper did not start")
            child = int(marker.read_text().strip())
            s.type(BEGIN + b"discard-this-paste")
            s.quiet(.05)
            ui.ping("before-async-result")
            if outcome != "timeout":
                gate.write_bytes(b"release")
            s.wait(lambda: ui.active() and ui.question() == "Finishing setup",
                   "helper result changed screens before the split paste ended", timeout=8)
            s.wait(lambda: not alive(child), "completed/timed-out helper was not reaped while the paste remained open")
            ui.ping("after-async-result")
            assert s.proc.poll() is None
            if outcome == "success":
                assert destination.read_bytes() == FAKE_CIPHERTEXT
                assert stat.S_IMODE(destination.stat().st_mode) == 0o600
            else:
                assert not destination.exists(), "failed encryption published a credential"
            # The closing marker is itself split across reads. Preserve its
            # decoder state, and quarantine the post-marker burst as well.
            s.type(b"/quit\r" + END[:1])
            s.quiet(.20)
            for byte in END[1:-1]:
                assert ui.active() and ui.question() == "Finishing setup"
                s.type(bytes((byte,)))
                s.quiet(.08)
            assert ui.active() and ui.question() == "Finishing setup"
            s.type(END[-1:] + b"/quit\r" * 128)
            if outcome == "success":
                s.wait(lambda: not ui.active(), "successful setup did not finish after paste drainage")
                # Completion changes the input owner; its newly shown result
                # must not execute another suffix before fresh user review.
                s.type(b"/quit\r" * 8)
            else:
                s.wait(lambda: ui.active() and ui.question() == "Finish" and
                       ("timed out" if outcome == "timeout" else "encryption failed") in s.screen.text().lower(),
                       "deferred encryption failure did not return to its editable Finish screen")
            s.quiet(.35)
            assert s.proc.poll() is None, "pasted /quit executed across asynchronous setup completion"
            assert not ui.peer.messages(), "asynchronous paste completion sent a chat draft"
            assert len(marker.read_text().splitlines()) == 1, "queued Enter restarted encryption"
            assert SECRET.encode("utf-8") not in s.raw[raw_start:], "masked password leaked to terminal output"
            ui.ping("after-paste-drained")
    print("async setup: %s %s passed; split paste/marker and queued commands remained inert, PING stayed responsive" %
          (mode, outcome), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("one", "idle", "gui", "all"), default="all")
    parser.add_argument("--outcome", choices=("success", "failure", "timeout", "all"), default="all")
    args = parser.parse_args()
    for mode in (("one", "idle", "gui") if args.mode == "all" else (args.mode,)):
        if mode == "gui":
            delayed_gui_paste_begin()
        for outcome in (("success", "failure", "timeout") if args.outcome == "all" else (args.outcome,)):
            run_case(mode, outcome)
    print("all selected async setup/paste regressions passed", flush=True)


if __name__ == "__main__":
    main()
