#!/usr/bin/env python3
"""Autojoin diagnostics and explicit reconnect through all terminal frontends.

Only owned loopback fixtures and temporary configurations are used.
"""
import argparse
from pathlib import Path

import test_integration as gui
import test_idle_terminal as native


class ReturningFixture(native.ReconnectingIRCFixture):
    def __init__(self, *unused):
        assert not any(unused), "this fixture is deliberately plaintext and credential-free"
        super().__init__()


def check(mode, binary):
    full_screen = mode == "gui"
    gui.IRCFixture = ReturningFixture
    native.IRCFixture = ReturningFixture
    if full_screen:
        gui.BINARY = binary
        session = gui.Session(channel=False)
    else:
        if mode == "idle": native.FULL_BINARY = binary
        else: native.BINARY = binary
        session = native.IdleSession(live=True, full=mode == "idle", channel="", rows=30, cols=120)

    with session as s:
        peer = s.server if full_screen else s.peer
        if full_screen: s.start()
        else:
            native.bare_start(s)
            s.wait(lambda: peer.registered, "owned IRC fixture did not register")

        def joins(number):
            return [line for line in peer.sessions[number]["lines"] if line.startswith(b"JOIN ")]

        def barrier(name):
            peer.send("PING :" + name)
            s.wait(lambda: any(line == ("PONG :" + name).encode() for _, line in peer.lines),
                   "inquiry blocked the live IRC connection")

        def report(command, title):
            if full_screen:
                s.open_command()
                s.type(command[1:])
                s.wait(lambda: s.command_text() == command, "inquiry command not composed")
                s.type(b"\r")
                s.wait(lambda: s.panel_title() == title and not s.screen.cursor_visible,
                       "full inquiry did not open its local reader")
            else:
                s.command(command, keep_reader=True)
                s.wait(s.reader, "native inquiry did not open its response reader")

        def back():
            if full_screen:
                s.type(b"\x1b")
                s.wait(lambda: s.panel_title().startswith("vipirc |"), "inquiry lost its parent screen")
                s.leave_app()
                s.wait(s.main_frame_ready, "inquiry Back did not restore the conversation")
            else: s.close_reader()

        assert joins(0) == []
        s.command("/autojoin add #nastycode")
        s.command("/autojoin add #vipirc")
        s.command("/query kept-peer")
        if full_screen:
            s.type("AUTOJOIN_DRAFT_KEEP")
            s.wait(lambda: "AUTOJOIN_DRAFT_KEEP" in s.input_text(), "conversation draft missing")
        report("/autojoin list", "Auto-join settings")
        s.wait(lambda: "registered: yes" in s.screen.text() and
               "Running auto-join: (none)" in s.screen.text() and
               "#nastycode,#vipirc" in s.screen.text() and "Pending auto-join" in s.screen.text(),
               "inquiry did not distinguish pending channels from running channels")
        assert "automatic retries" in s.screen.text()
        barrier("pending-channels-visible")
        assert joins(0) == []
        if full_screen:
            s.type("r")
            s.wait(lambda: s.panel_title() == "Auto-join settings" and "Pending auto-join" in s.screen.text(),
                   "Refresh replaced the inquiry with a different report")
            s.type("/")
            s.wait(lambda: s.in_command() and s.command_text() == "/", "inquiry child command missing")
            s.type("join #never-sent")
            s.type(b"\x1b")
            s.wait(lambda: s.panel_title() == "Auto-join settings", "command Cancel did not restore inquiry")
        back()
        if full_screen:
            assert "AUTOJOIN_DRAFT_KEEP" in s.input_text(), "inquiry lost the conversation draft"
        else:
            s.type("/")
            s.wait(lambda: s.editor("/", 1), "selected-conversation check did not open command editor")
            assert "kept-peer" in "".join(s.screen.cells[0]), "inquiry changed the selected conversation"
            s.cancel()
        s.command("/save")
        assert b"channel=#nastycode,#vipirc" in s.config.read_bytes()
        if not full_screen: s.before_config = s.config.read_bytes()
        barrier("saved-pending-channels")
        assert joins(0) == []

        for line in ("/autojoin #nastycode,#vipirc", '/autojoin "#nastycode,#vipirc"'):
            if full_screen:
                s.command(line, error="Usage: /autojoin")
                s.input_mode()
            else:
                s.command(line, "Usage: /autojoin", keep_reader=True)
                s.type(b"\x1b")
                s.wait(lambda: s.editor(line), "rejected command did not remain editable")
                s.cancel()
        assert b"channel=#nastycode,#vipirc" in s.config.read_bytes()
        s.command("/disconnect")
        s.command("/connect")
        s.wait(lambda: len(peer.sessions) == 2 and peer.sessions[1]["registered"],
               "explicit disconnected /connect did not register")
        s.wait(lambda: joins(1) == [b"JOIN #nastycode", b"JOIN #vipirc"],
               "explicit connection did not join both pending channels exactly once")
        barrier("joined-memberships")
        report("/autojoin", "Auto-join settings")
        s.wait(lambda: "Running auto-join: #nastycode,#vipirc" in s.screen.text() and
               "Channel #nastycode: joined" in s.screen.text() and
               "Channel #vipirc: joined" in s.screen.text(), "inquiry did not show actual joined memberships")
        assert "Pending auto-join" not in s.screen.text()
        back()

        report("/autoauth status", "Authentication settings")
        s.wait(lambda: "registered: yes" in s.screen.text() and "Required SASL: off" in s.screen.text(),
               "authentication inquiry omitted the running state")
        assert "No credential file was read" in s.screen.text()
        barrier("auth-inquiry-no-credential-read")
        back()

        s.command("/server add other.invalid")
        secondary = 1 if full_screen else 2
        s.command("/server " + str(secondary))
        report("/autojoin", "Auto-join settings")
        s.wait(lambda: ("Server %d: other.invalid:" % secondary) in s.screen.text() and
               "registered: no" in s.screen.text() and "Running auto-join: (none)" in s.screen.text(),
               "inquiry did not identify the selected independent server profile")
        assert "#nastycode" not in s.screen.text() and "#vipirc" not in s.screen.text()
        barrier("other-profile-inquiry")
        back()
        s.command("/server " + ("0" if full_screen else "1"))
        report("/autojoin list", "Auto-join settings")
        s.wait(lambda: "Channel #nastycode: joined" in s.screen.text() and
               "Channel #vipirc: joined" in s.screen.text(), "selecting another profile changed original autojoin state")
        back()
        assert len(peer.sessions) == 2
        assert joins(1) == [b"JOIN #nastycode", b"JOIN #vipirc"]
        assert not any(line.startswith((b"PRIVMSG ", b"JOIN #never-sent")) for _, line in peer.lines)

    print(mode + ": autojoin current/pending/server diagnostics, saved staging, exact reconnect joins, membership, "
          "authentication state, literal syntax rejection, inquiry parent/selection and live PING passed", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("gui", "idle", "onekey"), required=True)
    parser.add_argument("--binary", type=Path)
    args = parser.parse_args()
    binary = args.binary or (native.BINARY if args.mode == "onekey" else gui.BINARY)
    check(args.mode, binary.resolve())


if __name__ == "__main__": main()
