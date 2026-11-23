#!/usr/bin/env python3
"""Actual PTY hotkeys: offline simulation, no public IRC or saved preferences."""
import argparse
from test_idle_terminal import IdleSession

F6, F7, F8, F9, F10, F12 = (b"\x1b[17~", b"\x1b[18~", b"\x1b[19~",
                            b"\x1b[20~", b"\x1b[21~", b"\x1b[24~")
BEGIN, END = b"\x1b[200~", b"\x1b[201~"
KEYS = ("[hotkeys]\nF7=summary\nF8=command\nF9=clearinput\n"
        "F10=settings\nF12=quit\nAlt-s=summary\nCtrl-D=background\n")


def empty_defaults(full):
    with IdleSession(full=full, rows=24, cols=100) as s:
        s.start()
        s.type(b"\x1bOP\x1b[15~" + F7 + F12 + b"\x1bs\x04\x0c")
        s.quiet(.2)
        assert not s.screen.alternate and s.proc.poll() is None, s.screen.text()
        s.type(b"\r")
        s.wait(lambda: s.editor(), "ordinary Enter stopped opening composition")
        s.cancel()


def command_assignment_and_fragmented_keys(full):
    with IdleSession(full=full, rows=24, cols=100) as s:
        s.start()
        s.command("/bind F7 summary")
        s.type(b"\x1b[")
        s.quiet(.60)  # A begun CSI receives the longer continuation grace.
        assert not s.screen.alternate and s.proc.poll() is None
        s.type(b"18~")
        s.wait(s.reader, "fragmented function-key binding did not open summary")
        s.close_reader()
        s.command("/bind F7 command")
        s.type(F7)
        s.wait(lambda: s.editor("/", 1), "replacement binding did not open command entry")
        s.cancel()
        s.command("/unbind F7")
        s.type(F7)
        s.quiet(.15)
        assert not s.screen.alternate and s.proc.poll() is None
        s.command("/bind Alt-s summary")
        s.type(b"\x1b")
        s.quiet(.12)
        s.type(b"s")
        s.wait(s.reader, "fragmented Alt key did not open summary")
        s.close_reader()
        s.command("/bind Ctrl-D summary")
        s.type(b"\x04")
        s.wait(s.reader, "explicit Ctrl-D binding did not run")
        s.close_reader()
        s.command("/unbind all")
        s.type(b"\x1bs\x04" + F7)
        s.quiet(.15)
        assert not s.screen.alternate and s.proc.poll() is None


def retained_drafts_and_repeat_isolation(full):
    with IdleSession(full=full, rows=24, cols=100, config_extra=KEYS) as s:
        s.start()
        text = "retained chat draft"
        s.type(text)
        s.wait(lambda: s.editor(text, len(text)), "chat draft missing")
        s.type(b"\x1b[D\x1b[D")
        s.type(F7 + b"q" + F7)
        s.wait(s.reader, "summary shortcut did not open a reader")
        s.quiet(.15)
        assert s.reader(), "same-read keys escaped the shortcut's new reader"
        s.type(b"/")
        s.quiet(.12)
        assert s.reader(), "reader command entry overwrote the retained draft"
        s.type(b"\x1b")
        s.wait(lambda: s.editor(text, len(text)), "reader lost the original chat draft")
        s.type("X")
        expected = "retained chat draXft"
        s.wait(lambda: s.editor(expected, len(expected)), "reader changed the draft caret")
        s.type(F10)
        s.wait(lambda: "Finish or cancel" in s.screen.text(), "settings did not protect a nonempty draft")
        assert s.editor(expected, len(expected))
        s.type(F9)
        s.wait(lambda: s.editor(count=0), "explicit clearinput did not clear the draft")
        s.type(F8)
        s.wait(lambda: s.editor("/", 1), "command shortcut did not replace an empty composer")
        s.type("msg someone unfinished")
        command = "/msg someone unfinished"
        s.wait(lambda: s.editor(command, len(command)), "unfinished command missing")
        s.type(F7)
        s.wait(s.reader, "command shortcut did not open summary")
        s.type(b"q\r")
        s.wait(lambda: s.editor(command, len(command)), "summary lost the unfinished command")
        s.quiet(.15)
        assert s.editor(command, len(command)), "reader return executed a buffered Enter"
        s.cancel()


def buffered_enter_does_not_execute_command(full):
    with IdleSession(full=full, rows=24, cols=100, config_extra="[hotkeys]\nF6=cancel\n") as s:
        s.start()
        s.type("/quit")
        s.wait(lambda: s.editor("/quit", 5), "queued command fixture missing")
        s.type(F6 + b"\r")
        s.quiet(.2)
        assert s.proc.poll() is None and s.editor("/quit", 5), "shortcut executed buffered Enter"
        s.cancel()


def paste_setup_and_review_boundaries(full):
    with IdleSession(full=full, rows=24, cols=100, config_extra=KEYS) as s:
        s.start()
        s.type(BEGIN + b"retained-paste" + F12 + b"\x04" + END + F12 + b"\x04")
        s.wait(lambda: s.editor("retained-paste"), "paste was not retained in the editor")
        s.quiet(.2)
        assert s.proc.poll() is None and "Settings" not in s.screen.text()
        assert b"Resume:" not in s.raw and b"Background" not in s.raw[-500:]
        s.cancel()
        s.type("review-draft")
        s.wait(lambda: s.editor("review-draft", 12), "review fixture draft missing")
        s.type(b"\r")
        s.wait(lambda: "Send this message?" in s.screen.text(), "review screen missing")
        s.type(F12 + F9 + b"\x04")
        s.quiet(.15)
        assert s.proc.poll() is None and "Send this message?" in s.screen.text()
        s.type(b"\x1b")
        s.wait(lambda: s.editor("review-draft", 12), "review shortcut changed the pending draft")
        s.cancel()
        s.type(F10)
        s.wait(lambda: s.screen.alternate and "Settings" in s.screen.text(), "settings shortcut did not open setup")
        before = "".join(s.screen.cells[0])
        s.type(F12 + F7 + b"\x04")
        s.quiet(.15)
        assert s.proc.poll() is None and "".join(s.screen.cells[0]) == before


def paste_suffix_cannot_escape_review(full):
    with IdleSession(full=full, rows=24, cols=100) as s:
        s.start()
        s.type("/")
        s.wait(lambda: s.editor("/", 1), "command editor missing")
        s.type(BEGIN + b"help" + END + b"\x1b\x1b/quit\r")
        s.quiet(.35)
        assert s.proc.poll() is None and s.editor("/help", 5), "paste suffix escaped and executed a command"
        s.cancel()


def pasted_command_needs_visible_frame_and_quiet_input(full):
    with IdleSession(full=full, rows=24, cols=100) as s:
        s.start()
        s.type("/")
        s.wait(lambda: s.editor("/", 1), "command editor missing")
        s.resize(4, 10)
        s.type(BEGIN + b"quit" + END)
        s.quiet(.35)
        s.type(b"\r")
        s.quiet(.3)
        assert s.proc.poll() is None, "unseen paste ran on the tiny-terminal notice"
        s.resize(24, 100)
        s.wait(lambda: s.editor("/quit", 5), "resizing did not display the preserved command")
        for _ in range(40):
            s.type(b"\r")
            s.quiet(.025)
            assert s.proc.poll() is None, "held Enter executed an unreviewed pasted command"
        s.quiet(.35)
        s.type(b"\r")
        s.wait(lambda: s.proc.poll() is not None, "a deliberate fresh Enter did not run the reviewed command")
        assert s.proc.returncode == 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("one", "vip", "both"), default="one")
    args = parser.parse_args()
    modes = [False, True] if args.mode == "both" else [args.mode == "vip"]
    for full in modes:
        name = "vipirc -i" if full else "onekeyirc"
        for check in (empty_defaults, command_assignment_and_fragmented_keys,
                      retained_drafts_and_repeat_isolation, buffered_enter_does_not_execute_command,
                      paste_setup_and_review_boundaries, paste_suffix_cannot_escape_review,
                      pasted_command_needs_visible_frame_and_quiet_input):
            check(full)
            print("%s: %s passed" % (name, check.__name__), flush=True)


if __name__ == "__main__":
    main()
