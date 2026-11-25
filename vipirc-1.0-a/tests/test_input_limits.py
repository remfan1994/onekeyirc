#!/usr/bin/env python3
"""Real-PTY regressions for the fixed input viewport and early byte warning.

The in-process offline simulator is used throughout. No socket is opened by
this test, and its isolated configuration names unreadable credential FIFOs.
"""
import fcntl
import re
import signal
import struct
import termios
from collections import namedtuple

from test_offline import BINARY, OfflineTerminal


Info = namedtuple("Info", "label_row byte_count budget row col")
WARNING = "Message limit reached"
BEGIN_PASTE = b"\x1b[200~"
END_PASTE = b"\x1b[201~"


def editor_info(session):
    for index, cells in enumerate(session.screen.cells):
        line = "".join(cells)
        count = re.match(r"[+| -]*(\d+) bytes\b", line)
        position = re.search(r"row (\d+) col (\d+)", line)
        # Narrow layouts may omit an entire optional value, but never clip
        # a numeric budget into a different apparent limit.
        budget = re.search(r"text budget <=(\d+)", line)
        if count:
            return Info(index, int(count[1]), int(budget[1]) if budget else None,
                        int(position[1]) if position else None,
                        int(position[2]) if position else None)
    return None


def wait_bytes(session, count):
    def rendered():
        info=editor_info(session)
        if info is None or info.byte_count!=count:
            return False
        if WARNING in session.screen.text():
            return True
        # PTY reads may end after the counter but before the text repaint.
        # A completed editor frame places the physical caret on its painted
        # reverse-video cell; use that visible invariant instead of sleeping.
        row,col=session.screen.row,session.screen.col
        inside=(4 <= row < info.label_row if session.in_command() else
                info.label_row < row < session.screen.rows-2)
        return (inside and session.screen.cursor_visible and 1<=col<=session.screen.cols-2 and
                (info.col is None or col==info.col) and session.screen.reverse_cells[row][col])
    session.wait(rendered,
                 "draft byte count is not %d" % count)
    return editor_info(session)


def assert_no_send(session):
    assert b"<offline_tester>" not in session.raw, "input warning sent an ordinary chat message"
    assert b"* offline_tester " not in session.raw, "input warning sent an action"


def assert_no_warning(session):
    session.quiet(0.06)
    assert WARNING not in session.screen.text(), session.screen.text()
    assert_no_send(session)


def expect_warning(session):
    session.wait(lambda: WARNING in session.screen.text() and
                 "Enter/Esc: keep editing. Nothing sent." in session.screen.text() and
                 editor_info(session) is not None and editor_info(session).budget is not None and
                 editor_info(session).byte_count >= editor_info(session).budget,
                 "message limit warning did not open immediately")
    title = next(line for line in session.screen.text().splitlines() if WARNING in line)
    assert re.search(r"\b%d\b" % editor_info(session).budget, title), "warning omitted the actual byte budget"
    assert "Enter/Esc: keep editing. Nothing sent." in session.screen.text()
    assert_no_send(session)


def acknowledge(session, key=b"\r", count=None):
    before = editor_info(session).byte_count if count is None else count
    # At narrow widths the dialog can cover the byte divider; its underlying
    # editor is still identified by the main/Command title above the dialog.
    mode_before = "COMMAND" if session.in_command() else "INPUT"
    # A deliberately separate user acknowledgment, beyond the burst guard.
    session.quiet(0.13)
    session.type(key)
    session.wait(lambda: WARNING not in session.screen.text(), "Enter/Esc did not acknowledge the warning")
    assert wait_bytes(session, before).byte_count == before
    assert session.mode() == mode_before, "warning acknowledgment changed editing context or sent the draft"
    assert_no_send(session)


def clear(session):
    session.clear_input()  # Explicit local command; no draft is sent.
    wait_bytes(session, 0)
    assert_no_warning(session)


def ready(session):
    session.start()
    title = "Channel 1: #offline-a" if session.screen.cols >= 60 else "Channel 1:"
    session.wait(lambda: title in session.screen.text(),
                 "configured offline channel was not selected")
    info = wait_bytes(session, 0)
    assert session.mode() == "INPUT" and session.header_focus() is None, "startup did not begin in chat editing"
    assert info.budget and info.budget > 200, info
    return info


def in_manual(session):
    return "Help / Manual" in "".join(session.screen.cells[2])


def manual_history(session):
    first = next(index + 1 for index, row in enumerate(session.screen.cells)
                 if "".join(row).startswith("Return to:"))
    return tuple("".join(row) for row in session.screen.cells[first:-2])


def wait_manual(session):
    session.wait(lambda: in_manual(session) and editor_info(session) is None and
                 "Back" in "".join(session.screen.cells[3]) and
                 (session.screen.cols<40 or "Search" in "".join(session.screen.cells[3])),
                 "Help / Manual secondary screen did not finish opening")
    assert session.mode() == "PANEL"
    assert editor_info(session) is None, "manual incorrectly exposes a chat input field"
    assert "Back" in "".join(session.screen.cells[3]), "manual has no explicit Back control"


def leave_manual(session, key=b"\x1b"):
    app_parent="Return to: vipirc" in session.screen.text()
    session.type(key)
    if app_parent:
        session.wait(lambda:session.panel_title().startswith("vipirc |"),"app-origin manual did not return to vipirc")
        session.leave_app()
    # The title can change in an earlier PTY chunk than the main input footer.
    session.wait(lambda: not in_manual(session) and session.mode() == "INPUT",
                 "manual did not return to the previous main view")
    assert session.mode() == "INPUT", "manual return did not restore chat editing"


def wait_manual_return(session, target):
    session.wait(lambda:in_manual(session) and any("".join(row).rstrip()=="Return to: "+target
                 for row in session.screen.cells[3:9]),
                 "manual did not identify its exact return destination: "+target)


def disconnected_manual_navigation():
    for keymap in ("original", "vim"):
        style = "reverse" if keymap == "original" else "brackets"
        with OfflineTerminal(no_connect=True, keymap=keymap, focus_style=style) as session:
            session.start()
            session.open_app_diagnostics()
            assert "OFFLINE TEST MODE (stopped)" in session.app_line("Connection:")
            session.close_app_diagnostics()
            assert "Channel 0: Server" in session.screen.text()
            for command, back in (("/manual", b"\x1b"), ("/help", b"\r"), ("/view help", b"\x1b")):
                session.command(command,from_chat=True)
                wait_manual(session)
                wait_manual_return(session,"Channel 0: Server")
                assert not any("".join(row).startswith(("Server ","Connection:","Nick:"))
                               for row in session.screen.cells[3:8]),"manual still repeats connection diagnostics"
                session.type("\t")
                session.quiet(0.04)
                assert session.header_focus() is None, "manual Tab still navigates the global header"
                assert in_manual(session) and session.mode() == "PANEL"
                session.type(BEGIN_PASTE + b"/quit\r\n\x1b\t" + END_PASTE)
                session.quiet(0.04)
                assert in_manual(session) and session.proc.poll() is None, "manual paste ran a command or escaped"
                leave_manual(session)
                session.command(command,from_chat=True)
                wait_manual(session)
                leave_manual(session, back)
                assert "Channel 0: Server" in session.screen.text()
                wait_bytes(session, 0)

            # Local diagnostics remain useful before registration. A failed
            # command stays editable, and cannot open a conversation or send.
            for command, diagnostic in (("/manual extra", "Usage:"),
                                        ("/help "+"h"*257, "Usage:"),
                                        ("/unrecognized-manual-command", "Unknown command /unrecognized-manual-command"),
                                        ("/join #requires-registration", "registered")):
                session.command(command, error=diagnostic)
                session.wait(lambda: diagnostic in session.screen.text(),
                             "disconnected command diagnostic missing for " + command)
                assert session.in_command() and session.command_text() == command
                assert not in_manual(session)
                session.clear_input()
                assert "Channel 0: Server" in session.screen.text()
                wait_bytes(session, 0)

            server_draft = "server draft kept while reading"
            session.type(server_draft)
            wait_bytes(session, len(server_draft))
            session.navigate("?")
            wait_manual(session)
            mouse_click(session, 3, 2)
            session.wait(lambda:session.panel_title().startswith("vipirc |"), "manual Back mouse control did not return to vipirc")
            session.leave_app()
            wait_bytes(session, len(server_draft))
            assert server_draft in session.screen.text()

            # /pm is local even with no connection. Use an empty mentions
            # draft to create it without replacing the retained server draft.
            session.navigate("m")
            session.command("/pm ManualFriend")
            session.wait(lambda: "Query: ManualFriend" in session.screen.text(), "offline query view did not open")
            query_draft = "query draft kept while reading"
            session.type(query_draft)
            wait_bytes(session, len(query_draft))
            session.navigate("?")
            wait_manual(session)
            wait_manual_return(session,"vipirc")
            session.type(b"\x1b[6~")
            session.quiet(0.04)
            leave_manual(session, b"\x1b")
            wait_bytes(session, len(query_draft))
            assert "Query: ManualFriend" in session.screen.text() and query_draft in session.screen.text()
            session.navigate("s")
            wait_bytes(session, len(server_draft))
            assert server_draft in session.screen.text()
            assert_no_send(session)
            assert session.config.read_bytes() == session.before_config

    with OfflineTerminal() as session:
        ready(session)
        session.input_mode()
        text = "retained channel draft after disconnect"
        session.type(text)
        wait_bytes(session, len(text))
        from test_servers import open_servers, choose_action, SERVER_ACTIONS
        open_servers(session)
        choose_action(session, "Disconnect", SERVER_ACTIONS)
        choose_action(session, "Back", SERVER_ACTIONS)
        session.wait(lambda: session.is_main(), "server context did not return")
        assert "Channel 1: #offline-a" in session.screen.text()
        session.open_app_diagnostics()
        assert "OFFLINE TEST MODE (stopped)" in session.app_line("Connection:"), "server context did not disconnect"
        session.close_app_diagnostics()
        session.navigate("?")
        wait_manual(session)
        wait_manual_return(session,"vipirc")
        leave_manual(session, b"\r")
        wait_bytes(session, len(text))
        assert text in session.screen.text() and "Channel 1: #offline-a" in session.screen.text()
        assert_no_send(session)
    print("manual: disconnected aliases/diagnostics, explicit Back/Escape/Enter, Tab/paste isolation and server/channel/query draft restoration passed")


def resize(session, rows, cols):
    budget = editor_info(session).budget
    count = editor_info(session).byte_count
    fcntl.ioctl(session.master, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    session.screen.resize(rows, cols)
    session.proc.send_signal(signal.SIGWINCH)
    height = min(max(1, rows - 9), budget // (cols - 2) + 1)
    session.wait(lambda: editor_info(session) is not None and
                 editor_info(session).label_row == rows - height - 3 and
                 editor_info(session).byte_count == count,
                 "resize did not preserve the draft and recalculate its fixed viewport")


def input_navigation_and_actions():
    with OfflineTerminal() as session:
        ready(session)
        text = "c q m 0 s u x a h j k l n p r i ?"
        session.type(text)
        wait_bytes(session, len(text))
        assert session.mode() == "INPUT" and text in session.input_text(), "main letters activated commands instead of editing"
        session.type(bytes((1, 2, 4, 6, 7, 11, 12, 14, 15, 16, 18, 20, 21, 23, 24)))
        session.quiet(0.08)
        wait_bytes(session, len(text))
        session.type(session.home_key + b"Z\x1bOFQ")
        text = "Z" + text + "Q"
        wait_bytes(session, len(text))
        assert text in session.input_text(), "physical Home/End editing did not work"
        session.focus_destination("vipirc")
        session.leave_app()
        session.wait(lambda:session.is_main(),"header Escape did not restore chat")
        session.type("rrzza")
        text += "rrzza"
        wait_bytes(session, len(text))
        assert session.header_focus() is None, "header round trip retained navigation focus"
        assert text in session.input_text()
        session.type(b"\x1b\x1b")
        session.wait(lambda:session.is_main() and session.header_focus() is None,"main Escape did not retain chat editing")
        assert text in session.input_text()

        session.open_command()
        session.type(b"\x1b")
        session.finish_command_cancel()
        wait_bytes(session,len(text))
        assert text in session.input_text(), "command prompt replaced the retained chat draft"
        session.type(session.home_key+b"/")
        session.wait(lambda:session.in_command() and session.command_text()=="/","typed leading slash did not open Command")
        session.type(b"\x1b")
        session.wait(lambda:session.is_main(),"leading-slash cancel did not restore chat")
        assert wait_bytes(session,len(text)).col==1
        clear(session)

        text="preserve this draft while reading help"
        session.type(text)
        wait_bytes(session,len(text))
        session.open_help()
        wait_manual(session)
        session.type(BEGIN_PASTE+b"ignored manual paste\r\n/quit"+END_PASTE)
        session.quiet(0.08)
        assert in_manual(session) and session.mode()=="PANEL"
        leave_manual(session)
        assert text in session.input_text()
        session.clear_input()
        wait_bytes(session,0)
        assert session.mode()=="INPUT"
        session.focus_destination("vipirc")
        session.type(BEGIN_PASTE+b"literal from header selection"+END_PASTE)
        session.quiet(0.04)
        assert "vipirc" in session.panel_title() and not session.is_main(), "header paste changed screens"
        session.leave_app()
        session.wait(lambda:session.is_main(),"header paste lost return to chat")
        wait_bytes(session,0)
        assert_no_send(session)
    print("chat editing: ordinary letters and physical keys, ignored controls, safe header round trip, separate Command, explicit Clear and header-paste isolation passed")

def always_available_directions():
    from test_servers import CHANNEL_ACTIONS, focused_action
    from test_settings import open_settings,return_main
    for legacy in (None,"original","vim"):
        with OfflineTerminal(keymap=legacy) as session:
            ready(session)
            literal="h j k l i a ?"
            session.type(literal)
            wait_bytes(session,len(literal))
            assert literal in session.input_text()
            session.open_destination("Channels")
            session.type("j")
            session.wait(lambda:focused_label(session,6,"2: #offline-b"),"j did not focus next channel row")
            session.type("k")
            session.wait(lambda:focused_label(session,5,"1: #offline-a"),"k did not focus previous channel row")
            session.type("l")
            session.wait(lambda:focused_action(session,CHANNEL_ACTIONS)=="Join","Right did not select the row's Join action")
            assert focused_label(session,5,"1: #offline-a"), "action selection hid the selected channel row"
            session.type("h")
            session.wait(lambda:focused_action(session,CHANNEL_ACTIONS)=="Open","footer h did not select Open")
            session.type(" ")
            session.wait(lambda:focused_label(session,5,"1: #offline-a") and focused_action(session,CHANNEL_ACTIONS)=="Open",
                         "Space changed the selected row or action")
            session.type("G")
            session.wait(lambda:focused_label(session,6,"2: #offline-b"),"G did not focus last channel")
            session.type("j")
            session.wait(lambda:focused_label(session,6,"2: #offline-b") and focused_action(session,CHANNEL_ACTIONS)=="Open",
                         "Down past last row changed the row or action")
            session.type("k")
            session.wait(lambda:focused_label(session,5,"1: #offline-a"),"Up did not select the previous row")
            session.type("g")
            session.wait(lambda:focused_label(session,4,"0: server"),"g did not focus channel zero")
            session.type(b"\x1b")
            session.wait(lambda:session.is_main(),"channel list did not return to chat")
            assert literal in session.input_text()
            open_settings(session)
            session.type(b"\x1bOF")
            session.quiet(0.04)
            assert "Navigation keys" not in session.screen.text(),"obsolete keymap setting was offered"
            return_main(session)
            session.command("/next")
            session.wait(lambda:"Channel 2: #offline-b" in session.panel_title(),"next view did not select second channel")
            session.command("/prev")
            session.wait(lambda:"Channel 1: #offline-a" in session.panel_title(),"previous view did not restore first channel")
            assert literal in session.input_text()
            session.open_help();wait_manual(session)
            session.type("g");session.quiet(0.05);oldest=manual_history(session)
            session.type(b"\x1b[6~")
            session.wait(lambda:manual_history(session)!=oldest,"PageDown did not scroll manual")
            session.type(b"\x1b[5~")
            session.wait(lambda:manual_history(session)==oldest,"PageUp did not restore manual")
            session.type("G")
            session.wait(lambda:manual_history(session)!=oldest,"G did not reach manual end")
            session.type("g")
            session.wait(lambda:manual_history(session)==oldest,"g did not restore manual beginning")
            leave_manual(session)
            assert literal in session.input_text() and session.config.read_bytes()==session.before_config
            assert_no_send(session)
    print("directions: main hjkl remains literal; panel arrows/hjkl, simultaneous row/action selection, clamped endpoints and manual scrolling work with legacy configs")

def arrow_header_focus():
    for keymap in (None,"vim"):
        with OfflineTerminal(keymap=keymap,cols=132) as session:
            initial=ready(session)
            session.command("/disconnect")
            session.command("/pm HeaderPeer")
            session.command("/channel 1")
            text="focus draft"
            session.type(text);wait_bytes(session,len(text))
            assert session.header_focus() is None
            labels=tuple(label for label in session.destinations if label in "".join(session.screen.cells[0]))
            for label in labels+tuple(reversed(labels)):
                session.focus_destination(label)
                assert session.header_focus() is None,"function-key shortcut left top-bar arrow focus"
            if session.panel_title().startswith("vipirc |"):
                session.leave_app()
            else:
                session.type(b"\x1b")
            session.wait(session.main_frame_ready,"category Back did not restore chat")
            assert text in session.input_text() and session.screen.cursor_visible
            session.type(b"\x7f")
            text=text[:-1]
            wait_bytes(session,len(text))
            assert session.header_focus() is None and text in session.input_text()
            session.open_destination("Servers")
            session.type(b"g"+b"\x1bOA")
            session.quiet(.04)
            assert session.header_focus() is None and "Servers" in session.panel_title()
            session.type(b" ")
            from test_servers import focused_action,SERVER_ACTIONS,CHANNEL_ACTIONS
            session.wait(lambda:focused_action(session,SERVER_ACTIONS)=="Open","server footer did not gain focus")
            session.type(b"\x1bOC")
            session.wait(lambda:focused_action(session,SERVER_ACTIONS)=="New","Right did not move server footer focus")
            assert "Servers" in session.panel_title(),"footer arrow switched the top category"
            session.open_destination("Channels")
            session.type(b"g ")
            session.wait(lambda:focused_action(session,CHANNEL_ACTIONS)=="Open","channel footer missing")
            session.type(b"\x1bOC")
            session.wait(lambda:focused_action(session,CHANNEL_ACTIONS)=="Join","Right did not move channel footer focus")
            assert "Channels" in session.panel_title() and session.header_focus() is None
            session.type(b"\x1b")
            session.wait(session.main_frame_ready,"secondary Escape did not return to chat")
            assert text in session.input_text()
            clear(session)
            session.type("T"*initial.budget)
            expect_warning(session)
            session.quiet(0.13);session.type(b"\t");session.quiet(0.06)
            assert WARNING in session.screen.text(),"Tab escaped message-limit confirmation"
            acknowledge(session);clear(session)
            session.type(BEGIN_PASTE+b"a\tb"+END_PASTE)
            wait_bytes(session,3)
            assert session.header_focus() is None and "a b" in session.input_text(),"pasted Tab activated global navigation"
            assert_no_send(session)
    print("global selector: stable F1..F6 categories, immediate body focus, independent footer arrows, caret preservation and confirmation/paste boundaries passed")

def fixed_viewport_and_threshold():
    with OfflineTerminal() as session:
        initial = ready(session)
        session.input_mode()
        budget, width = initial.budget, session.screen.cols - 2
        expected_rows = min(max(1, session.screen.rows - 9), budget // width + 1)
        assert session.screen.rows - initial.label_row - 3 == expected_rows, (
            "empty input must already reserve a full single-message viewport", initial, expected_rows)

        # The caret reaches the second visual row, and Backspace returns to
        # the previous row's last character without changing the viewport.
        session.type("A" * width)
        wrapped = wait_bytes(session, width)
        assert (wrapped.row, wrapped.col) == (2, 1), wrapped
        assert wrapped.label_row == initial.label_row
        session.type(b"\x7f")
        unwrapped = wait_bytes(session, width - 1)
        assert (unwrapped.row, unwrapped.col) == (1, width), unwrapped
        assert (session.screen.row, session.screen.col) == (initial.label_row + 1, width), (
            "hardware cursor is not visible at the wrapped Backspace position", session.screen.row, session.screen.col)
        clear(session)

        session.type("L" * (budget - 1))
        below = wait_bytes(session, budget - 1)
        assert below.label_row == initial.label_row
        assert_no_warning(session)
        session.type("X")
        expect_warning(session)
        assert wait_bytes(session, budget).label_row == initial.label_row
        assert "More text will need multiple chat lines." in session.screen.text()

        # Holding ordinary keys, including y/n, must neither dismiss an
        # informational warning nor silently extend or submit the draft.
        session.quiet(0.13)
        session.type("yYNnqZ" * 40)
        session.quiet(0.13)
        assert WARNING in session.screen.text(), "printable auto-repeat dismissed the warning"
        assert editor_info(session).byte_count == budget, "popup leaked auto-repeat into the draft"
        assert_no_send(session)

        # A paste arriving while the dialog is open is not an acknowledgment;
        # in particular, its CR and ESC are clipboard data rather than keys.
        session.type(BEGIN_PASTE + b"y\r\x1bZ" + END_PASTE)
        session.quiet(0.13)
        assert WARNING in session.screen.text(), "clipboard CR/ESC acknowledged a warning"
        assert editor_info(session).byte_count == budget
        acknowledge(session, b"\r")

        session.type("E" * (2 * width))
        above = wait_bytes(session, budget + 2 * width)
        assert above.label_row == initial.label_row, "long draft moved the input's top edge"
        assert above.row > expected_rows, "long draft did not exercise vertical input scrolling"
        assert_no_warning(session)

        # Merely remaining above the boundary does not nag. Going below it
        # rearms the warning for the next crossing.
        session.type(b"\x7f" * (2 * width + 1))
        wait_bytes(session, budget - 1)
        assert_no_warning(session)
        session.type("R")
        expect_warning(session)
        acknowledge(session, b"\x1b")
        assert editor_info(session).label_row == initial.label_row
        clear(session)
        assert editor_info(session).label_row == initial.label_row

        # Resizing recalculates the fixed reservation once, independent of
        # the draft, and preserves wrap/backspace behavior at the new width.
        fcntl.ioctl(session.master, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 40, 0, 0))
        session.screen.resize(24, 40)
        session.proc.send_signal(signal.SIGWINCH)
        narrow_rows = min(24 - 9, budget // 38 + 1)
        session.wait(lambda: editor_info(session) is not None and
                     editor_info(session).label_row == 24 - narrow_rows - 3,
                     "resize did not reserve the recalculated message-height input")
        narrow_top = editor_info(session).label_row
        session.type("N" * 38)
        session.wait(lambda: editor_info(session) is not None and
                     editor_info(session).byte_count == 38, "narrow draft missing")
        assert editor_info(session).label_row == narrow_top
        session.type(b"\x7f")
        wait_bytes(session, 37)
        assert editor_info(session).label_row == narrow_top
        assert_no_send(session)
    print("input limits: fixed empty/wrapped/long viewport, visible wrap Backspace, immediate exact threshold, repeat protection, acknowledgments, rearm and resize passed")


def unicode_byte_threshold():
    with OfflineTerminal() as session:
        initial = ready(session)
        session.input_mode()
        budget, width = initial.budget, session.screen.cols - 2
        prefix = "\u00e9" * ((budget - 2) // 2) + "a" * ((budget - 2) % 2)
        session.type(prefix)
        wait_bytes(session, budget - 2)
        assert_no_warning(session)

        # The two UTF-8 bytes arrive separately, as they can over SSH. Neither
        # the partial scalar nor its completion may be dropped or miscounted.
        session.type(b"\xc3")
        session.quiet(0.05)
        assert editor_info(session).byte_count == budget - 2
        assert WARNING not in session.screen.text()
        session.type(b"\xa9")
        expect_warning(session)
        assert editor_info(session).byte_count == budget
        assert editor_info(session).row < budget // width + 1, "warning counted characters instead of UTF-8 bytes"
        acknowledge(session, b"\x1b")
        session.type(b"\x7f")
        wait_bytes(session, budget - 2)
        session.type("\u00e9")
        expect_warning(session)
        acknowledge(session)
        clear(session)

        session.type("\u00e9" * width)
        wrapped = wait_bytes(session, width * 2)
        assert (wrapped.row, wrapped.col) == (2, 1), wrapped
        session.type(b"\x7f")
        unwrapped = wait_bytes(session, (width - 1) * 2)
        assert (unwrapped.row, unwrapped.col) == (1, width), unwrapped
        assert unwrapped.label_row == initial.label_row
        assert_no_warning(session)
    print("input limits: UTF-8 byte threshold, fragmented scalar, Unicode Backspace and wrap passed")


def chat_commands_and_literal_paste():
    with OfflineTerminal() as session:
        initial = ready(session)
        session.input_mode()
        budget = initial.budget
        # Commands add syntax bytes that are not part of the transmitted
        # message. ACTION adds nine protocol bytes; // transmits one slash.
        cases = (
            ("/msg bob ", budget + len("#offline-a") - len("bob"), 0),
            ("/me ", budget - 9, 0),
            ("//", budget, 1),
        )
        for prefix, payload_budget, existing_payload in cases:
            draft = prefix + "C" * (payload_budget - existing_payload - 1)
            session.open_command()
            session.type(draft[1:])
            wait_bytes(session, payload_budget - 1)
            assert_no_warning(session)
            command_divider = editor_info(session).label_row
            assert command_divider == 7, "command field did not keep its three-row reservation"
            session.type("X")
            expect_warning(session)
            wait_bytes(session, payload_budget)
            assert editor_info(session).label_row == command_divider
            acknowledge(session)
            clear(session)
            assert editor_info(session).label_row == initial.label_row, "command changed the chat viewport height"

        for prefix in ("/help ", "/whois ", "/unknown-command "):
            draft = prefix + "D" * (budget + 100)
            session.open_command()
            session.type(draft[1:])
            wait_bytes(session, len(draft))
            assert_no_warning(session)
            assert editor_info(session).label_row == 7
            clear(session)
            assert editor_info(session).label_row == initial.label_row

        # Bracketed paste is captured in full, even past the first-message
        # budget. A leading /help remains literal and does not open Help.
        pasted = "/help " + "P" * (budget + 99) + "\r\nPASTE_TAIL_KEPT"
        normalized = pasted.replace("\r\n", "\n")
        session.type(BEGIN_PASTE + pasted.encode("ascii"))
        wait_bytes(session, len(normalized) - 1)
        assert_no_warning(session)
        session.type(END_PASTE)
        expect_warning(session)
        assert "This draft needs multiple chat lines." in session.screen.text()
        assert editor_info(session).byte_count == len(normalized) - 1
        acknowledge(session, b"\x1b")
        assert "PASTE_TAIL_KEPT" in session.screen.text(), "paste was truncated at the warning threshold"
        assert "Channel 1: #offline-a" in session.screen.text(), "literal /help paste ran as a command"
        assert editor_info(session).label_row == initial.label_row
        assert_no_send(session)
    print("input limits: /msg, /me and // payloads; non-chat command exclusion; literal CRLF paste preservation passed")


def clear_input_editing():
    with OfflineTerminal() as session:
        initial = ready(session)
        budget, width = initial.budget, session.screen.cols - 2
        clear(session)  # Empty Clear is harmless and does not submit.
        hidden = "S" * (budget + 3 * width) + "OFFSCREEN_END"
        session.type(BEGIN_PASTE + hidden.encode("ascii") + END_PASTE)
        expect_warning(session)
        wait_bytes(session, len(hidden))
        acknowledge(session)
        assert editor_info(session).row > session.screen.rows - initial.label_row - 3
        clear(session)
        assert "OFFSCREEN_END" not in session.screen.text(), "Clear retained offscreen draft text"
        replacement = "replacement \u4e2d"
        session.type(replacement)
        wait_bytes(session, len(replacement.encode("utf-8")))
        session.type("a")
        wait_bytes(session,len(replacement.encode("utf-8"))+1)
        session.type(b"\x7f")
        wait_bytes(session,len(replacement.encode("utf-8")))
        session.type(b"\x7f")
        wait_bytes(session, len("replacement "))
        assert "".join(session.screen.cells[editor_info(session).label_row + 1][1:-1]).startswith("replacement "), "Backspace unexpectedly deleted an entire selection"
        clear(session)

        first, second = "first \u00e9 draft", "second independent draft"
        session.type(first)
        wait_bytes(session, len(first.encode("utf-8")))
        session.navigate("c")
        session.wait(lambda: "Channels" in session.panel_title(), "channel chooser did not open")
        session.type(b"\x1bOB\r")
        session.wait(lambda: "Channel 2: #offline-b" in session.screen.text(), "second draft view not selected")
        session.input_mode()
        session.type(second)
        wait_bytes(session, len(second))
        session.navigate("c")
        session.wait(lambda: "Channels" in session.panel_title(), "channel chooser did not reopen")
        session.type(b"\x1bOA\r")
        session.wait(lambda: "Channel 1: #offline-a" in session.screen.text(), "first draft view not restored")
        wait_bytes(session, len(first.encode("utf-8")))
        assert first in session.screen.text(), "view change lost the first draft"
        clear(session)
        session.type("first replacement")
        wait_bytes(session, len("first replacement"))
        session.navigate("c")
        session.wait(lambda: "Channels" in session.panel_title(), "channel chooser did not reopen")
        session.type(b"\x1bOB\r")
        session.wait(lambda: "Channel 2: #offline-b" in session.screen.text(), "second draft view not restored")
        wait_bytes(session, len(second))
        assert second in session.screen.text(), "clearing one view modified another draft"
        assert_no_send(session)
    print("input clear: empty/offscreen compositions, rearmed byte warning, ordinary Backspace and independent view drafts passed")

def parallel_terminal_locales():
    with OfflineTerminal(cols=100, term="xterm-256color", locale="C.UTF-8") as wide, \
            OfflineTerminal(cols=40, term="screen", locale="C") as narrow:
        wide_initial, narrow_initial = ready(wide), ready(narrow)
        wide.input_mode()
        narrow.input_mode()
        assert b"ASCII terminal mode" in narrow.raw, "C locale was silently treated as UTF-8"
        assert b"ASCII terminal mode" not in wide.raw, "UTF-8 terminal was incorrectly restricted to ASCII"
        wide_text, narrow_text = "wide \u00e9 draft", "narrow draft"
        wide.type(wide_text)
        narrow.type(narrow_text)
        wait_bytes(wide, len(wide_text.encode("utf-8")))
        wait_bytes(narrow, len(narrow_text))
        # Reserve the complete narrow status row for the rejection reason;
        # the separate mouse suite verifies the optional footer buttons.
        narrow.command("/mouse off")

        narrow.type(b"\xe9Z")
        wait_bytes(narrow, len(narrow_text) + 1)
        narrow.wait(lambda: "ASCII terminal mode: non-ASCII" in narrow.screen.text(),
                    "non-ASCII input rejection was not explained in C locale")
        assert editor_info(wide).byte_count == len(wide_text.encode("utf-8"))

        # Rejected bytes and an empty/entirely invalid paste preserve the
        # current composition. The next valid character appends normally.
        narrow.type(b"\xe9")
        narrow.quiet(0.06)
        wait_bytes(narrow, len(narrow_text) + 1)
        narrow.type(BEGIN_PASTE + END_PASTE)
        narrow.quiet(0.06)
        wait_bytes(narrow, len(narrow_text) + 1)
        narrow.type(BEGIN_PASTE + b"\xc3\xa9" + END_PASTE)
        narrow.quiet(0.06)
        wait_bytes(narrow, len(narrow_text) + 1)
        narrow.type("R")
        wait_bytes(narrow, len(narrow_text) + 2)
        clear(narrow)
        narrow.type("R")
        wait_bytes(narrow, 1)
        narrow.type(BEGIN_PASTE + b"P\xc3\xa9Q" + END_PASTE + b"Z")
        wait_bytes(narrow, 4)  # R, P, Q, Z; no non-ASCII scalar or lost ASCII.
        # At this width the divider omits the caret column. Its new byte
        # count can arrive before the old painted caret/text are replaced.
        def complete_ascii_paste():
            info = editor_info(narrow)
            return (info is not None and info.byte_count == 4 and narrow.main_frame_ready() and
                    narrow.input_text().splitlines()[0].rstrip() == "RPQZ" and
                    (narrow.screen.row, narrow.screen.col) == (info.label_row + 1, 5))
        narrow.wait(complete_ascii_paste, "ASCII paste did not finish rendering its exact draft and caret")
        assert "RPQZ" in narrow.screen.text(), "rejected UTF-8 paste swallowed following ASCII input"
        clear(narrow)

        budget = narrow_initial.budget
        narrow.type("A" * (budget - 1))
        wait_bytes(narrow, budget - 1)
        assert_no_warning(narrow)
        narrow.type("X")
        narrow.wait(lambda: WARNING in narrow.screen.text(), "ASCII/screen terminal did not warn at its exact byte budget")
        wide.type("!")
        wait_bytes(wide, len(wide_text.encode("utf-8")) + 1)
        assert WARNING not in wide.screen.text(), "one terminal's dialog affected another instance"
        assert WARNING in narrow.screen.text()
        acknowledge(narrow, b"\x1b", count=budget)
        clear(narrow)
        narrow.type("narrow survives")
        wait_bytes(narrow, len("narrow survives"))

        # Each SSH-like terminal has its own width, TERM, locale and draft;
        # resizing either must not change the other process's viewport.
        resize(wide, 24, 60)
        assert narrow.screen.cols == 40 and editor_info(narrow).label_row == narrow_initial.label_row
        assert editor_info(narrow).byte_count == len("narrow survives")
        wide_top = editor_info(wide).label_row
        resize(narrow, 24, 50)
        assert wide.screen.cols == 60 and editor_info(wide).label_row == wide_top
        assert editor_info(wide).byte_count == len(wide_text.encode("utf-8")) + 1
        assert wide_initial.label_row != wide_top
        assert_no_send(wide)
        assert_no_send(narrow)
    print("input limits: parallel UTF-8/xterm and C/screen terminals, ASCII rejection, draft preservation, exact warning and independent resize/drafts passed")


def mouse_event(session, button, row, col, release=False):
    session.type("\x1b[<%d;%d;%d%s" % (button, col + 1, row + 1, "m" if release else "M"))


def mouse_click(session, row, col):
    mouse_event(session, 0, row, col)
    mouse_event(session, 0, row, col, release=True)


def click_label(session, label):
    for row, cells in enumerate(session.screen.cells):
        line = "".join(cells)
        if label in line:
            mouse_click(session, row, line.index(label) + len(label) // 2)
            return
    raise AssertionError("mouse control is missing: %s\n%s" % (label, session.screen.text()))


def mouse_modes(data):
    modes = []
    for match in re.finditer(rb"\x1b\[\?([0-9;]+)([hl])", data):
        numbers = set(int(number) for number in match[1].split(b";"))
        if numbers.intersection({1000, 1002, 1003, 1006}):
            modes.append(match[2])
    return modes


def focused_label(session, row, label, style=None):
    # Legacy focus_style values no longer select a rendering scheme. Every
    # focus target has the same printable outline and bright text.
    line = "".join(session.screen.cells[row])
    found = re.search(re.escape(label), line, re.IGNORECASE)
    if not found:
        return False
    outlined = any(label.lower() in match[1].lower() for match in re.finditer(r"\[([^]]*)\]", line))
    return outlined and all(session.screen.bold_cells[row][offset]
                            for offset in range(found.start(), found.end()) if line[offset] != " ")

def status_focus(session,label,style=None):
    del style
    session.focus_destination({"Transfers":"File transfers","unread view":"Unread views"}.get(label,label))

def focus_controls_and_actions():
    from test_servers import (CHANNEL_ACTIONS,QUERY_ACTIONS,SERVER_ACTIONS,
                              choose_action,focused_action)
    from test_settings import open_settings,return_main
    for style in ("reverse","brackets"):
        with OfflineTerminal(focus_style=style,cols=132) as session:
            ready(session)
            session.command("/pm ExistingPeer")
            session.command("/channel 1")
            assert "Channel 1: #offline-a" in session.panel_title()
            header="".join(session.screen.cells[0])
            assert all(label in header for label in ("vipirc","Servers","Channels","Queries")),header
            assert "File transfers" not in header,"empty transfer category occupies header space"
            assert "Query: ExistingPeer" not in header and "Channel 1:" not in header
            assert "NAVIGATION" not in header and "INPUT" not in header
            footer=session.screen.rows-1
            assert "Clear" in "".join(session.screen.cells[footer]) and "Send" in "".join(session.screen.cells[footer])
            session.focus_destination("Servers")
            assert session.header_focus() is None,"function-key category intercepted body focus"
            session.wait(lambda:"Servers" in session.panel_title(),"Servers did not open")
            assert focused_action(session,SERVER_ACTIONS)=="Open"
            session.type("l")
            session.wait(lambda:"Servers" in session.panel_title() and focused_action(session,SERVER_ACTIONS)=="New",
                         "server-row l did not select New locally")
            choose_action(session,"Open",SERVER_ACTIONS)
            session.wait(lambda:session.main_frame_ready() and "Channel 0: Server" in session.panel_title(),
                         "server Open did not open its message view")
            session.command("/channel #offline-a")
            session.open_destination("Channels")
            session.wait(lambda:"Channels" in session.panel_title(),"channel category did not open its list")
            session.wait(lambda:focused_label(session,5,"1: #offline-a"),
                         "opened channel list did not focus the active channel")
            session.type(" l")
            session.wait(lambda:focused_action(session,CHANNEL_ACTIONS)=="Join","selected-row action change failed")
            session.type("h ")
            session.wait(lambda:focused_label(session,5,"1: #offline-a"),"changing action hid selected channel")
            session.type(b"\x1b")
            session.wait(lambda:session.is_main(),"Channels did not return to chat")
            session.open_destination("Queries")
            session.open_destination("File transfers")
            session.wait(lambda:"File transfers" in session.panel_title(),"transfer command did not open hidden category")
            session.open_destination("Mentions")
            session.wait(lambda:session.is_main() and "mentions" in session.panel_title().lower(),"Mentions did not open")
            session.command("/server")
            assert "Channel 0: Server" in session.panel_title()
            open_settings(session)
            assert "Settings | Server 0" in session.panel_title(),"Settings did not expose the selected server context"
            return_main(session)
            session.open_app_diagnostics()
            assert not any("".join(row).startswith("Mode:") for row in session.screen.cells[:8])
            assert "Server 0: must-not-resolve.invalid" in session.app_line("Server ")
            assert session.app_line("Connection:") and "Nick: offline_tester" in session.app_line("Nick:")
            session.close_app_diagnostics()
            session.type("clear only this draft");wait_bytes(session,len("clear only this draft"))
            mouse_click(session,footer,2)
            wait_bytes(session,0)
            assert session.mode()=="INPUT" and session.screen.cursor_visible

            session.open_destination("Channels")
            session.type(b"\x1bOB")
            choose_action(session,"Part",CHANNEL_ACTIONS)
            session.wait(lambda:any("#offline-a" in line and "left" in line for line in session.screen.text().splitlines()),
                         "Part did not leave selected non-last channel")
            session.wait(lambda:focused_action(session,CHANNEL_ACTIONS)=="Open",
                         "Part did not reset the channel action to Open")
            choose_action(session,"Join",CHANNEL_ACTIONS)
            session.wait(lambda:session.in_command() and session.command_text()=="/join","Join did not open command editor")
            session.type("#focus-test\r")
            session.wait(lambda:"Channel 3: #focus-test" in session.panel_title(),"Join command did not join")
            session.open_destination("Channels")
            choose_action(session,"Close",CHANNEL_ACTIONS)
            session.wait(lambda:not any("#focus-test" in line for line in session.screen.text().splitlines()[3:-2]),"Close did not remove selected channel")
            session.wait(lambda:focused_action(session,CHANNEL_ACTIONS)=="Open",
                         "Close did not reset the channel action to Open")
            choose_action(session,"Back",CHANNEL_ACTIONS)
            session.wait(lambda:session.is_main(),"channel Back did not return")
            session.open_destination("Queries")
            choose_action(session,"New",QUERY_ACTIONS)
            session.wait(lambda:session.in_command() and session.command_text()=="/pm","New did not open query command")
            session.type("FocusPeer\r")
            session.wait(lambda:"Query: FocusPeer" in session.panel_title(),"New did not open query")
            assert "Query: FocusPeer" not in "".join(session.screen.cells[0]),"query identity replaced fixed header"
            session.open_destination("Queries")
            row=3;line="".join(session.screen.cells[row])
            mouse_click(session,row,line.index("Close")+2)
            session.wait(lambda:not any("FocusPeer" in line for line in session.screen.text().splitlines()[3:-2]),"query Close button did not remove selected row")
            session.wait(lambda:focused_action(session,QUERY_ACTIONS)=="Open",
                         "Close did not reset the query action to Open")
            choose_action(session,"Back",QUERY_ACTIONS)
            session.wait(lambda:session.is_main(),"query Back did not return")
            assert session.config.read_bytes()==session.before_config
            assert_no_send(session)
    print("global controls: nonempty categories, separate chat identity, panel traversal, retained-row actions, config/manual access, safe Close/Part and input-only Clear passed")

def clear_input_and_mouse():
    with OfflineTerminal() as session:
        ready(session)
        assert b"h" in mouse_modes(session.raw), "supported terminal did not enable mouse reporting"
        click_label(session, "Servers")
        session.wait(lambda: "Servers" in session.panel_title(), "current-view status click did not open Servers")
        from test_servers import choose_action, SERVER_ACTIONS
        choose_action(session,"Open",SERVER_ACTIONS)
        session.wait(lambda:session.main_frame_ready() and "Channel 0: Server" in session.panel_title(),
                     "server Open did not select its message view")
        click_label(session, "Channels")
        session.wait(lambda: "Channels" in session.panel_title() and
                     any(re.search(r"\| 2: #offline-b", "".join(cells)) for cells in session.screen.cells),
                     "selected server Channels category did not draw its list")
        row = next(index for index, cells in enumerate(session.screen.cells)
                   if re.search(r"\| 2: #offline-b", "".join(cells)))
        mouse_click(session, row, 8)
        session.wait(lambda: session.main_frame_ready() and "Channel 2: #offline-b" in session.panel_title(),
                     "single-click did not switch to the selected channel")
        session.command("/pm MousePeer")
        session.wait(lambda: "Query: MousePeer" in session.screen.text(), "query fixture could not be opened")
        click_label(session, "Queries")
        session.wait(lambda: "Query list" in session.panel_title() and "| MousePeer" in session.screen.text(),
                     "query mouse control did not draw the chooser")
        row = next(index for index, cells in enumerate(session.screen.cells)
                   if "| MousePeer" in "".join(cells))
        mouse_click(session, row, 3)
        session.wait(lambda: session.main_frame_ready() and "Query: MousePeer" in session.panel_title(),
                     "single-click did not switch to the selected query")
        assert session.mode() == "INPUT", "chooser click did not restore chat editing"
        mouse_click(session, editor_info(session).label_row + 1, 1)
        session.wait(lambda: session.mode() == "INPUT", "clicking the input did not enter INPUT")
        session.type("abcd")
        info = wait_bytes(session, 4)
        mouse_click(session, info.label_row + 1, 2)
        session.type("X")
        wait_bytes(session, 5)
        assert "aXbcd" in session.screen.text(), "input click did not position the insertion caret"
        click_label(session, "Clear")
        wait_bytes(session, 0)
        assert session.mode() == "INPUT", "mouse Clear moved focus out of the input field"
        session.type("hjkl")
        wait_bytes(session, 4)
        assert "".join(session.screen.cells[editor_info(session).label_row + 1][1:-1]).startswith("hjkl"), \
            "typing after mouse Clear invoked navigation instead of inserting literal text"
        session.clear_input()
        wait_bytes(session, 0)

        budget = editor_info(session).budget
        session.type(BEGIN_PASTE + b"Q" * (budget + 300) + END_PASTE)
        expect_warning(session)
        wait_bytes(session, budget + 300)
        acknowledge(session)
        session.clear_input()
        wait_bytes(session, 0)
        assert_no_warning(session)

        # Help has enough static history to verify wheel movement without
        # generating outgoing chat or waiting for simulator timers.
        session.command("/help")
        wait_manual(session)

        def history():
            return manual_history(session)

        history_before = history()
        mouse_event(session, 65, 7, 4)
        session.wait(lambda: history() != history_before, "mouse wheel did not scroll history")
        mouse_event(session, 64, 7, 4)
        session.wait(lambda: history() == history_before, "reverse mouse wheel did not restore the previous history rows")

        leave_manual(session)
        assert "Query: MousePeer" in session.screen.text(), "manual Back did not restore its originating query"
        session.command("/channel 1")
        session.wait(lambda: "Channel 1: #offline-a" in session.screen.text(), "channel could not be restored for roster scrolling")
        resize(session, 10, 100)
        session.navigate("u")
        session.wait(lambda: "Users:" in session.screen.text(), "roster did not become visible")

        def roster():
            return tuple("".join(row[79:]) for row in session.screen.cells[4:5])

        roster_before = roster()
        mouse_event(session, 65, 3, 90)
        session.wait(lambda: roster() != roster_before, "mouse wheel over the roster did not scroll its names")
        mouse_event(session, 64, 3, 90)
        session.wait(lambda: roster() == roster_before, "reverse roster wheel did not restore its original names")
        resize(session, 24, 100)

        start = len(session.raw)
        session.command("/mouse off")
        session.wait(lambda: b"l" in mouse_modes(session.raw[start:]), "/mouse off did not disable mouse reporting")
        assert "Clear" not in "".join(session.screen.cells[-1]) and "Send" not in "".join(session.screen.cells[-1]), \
            "mouse-only controls remained visible with mouse disabled"
        assert b"l" in mouse_modes(session.raw[start:]), "/mouse off did not disable terminal mouse reporting"
        session.type("keyboard still works")
        wait_bytes(session, len("keyboard still works"))
        mouse_click(session, session.screen.rows-1, 2)
        session.quiet(0.06)
        wait_bytes(session, len("keyboard still works"))
        session.clear_input()
        start = len(session.raw)
        session.command("/mouse on")
        session.wait(lambda: b"h" in mouse_modes(session.raw[start:]), "/mouse on did not enable reporting")
        session.wait(lambda:"Clear" in "".join(session.screen.cells[-1]) and "Send" in "".join(session.screen.cells[-1]),
                     "mouse controls did not return after enabling mouse")
        assert b"h" in mouse_modes(session.raw[start:]), "/mouse on did not enable terminal mouse reporting"
        assert_no_send(session)
    assert mouse_modes(session.raw)[-1] == b"l", "normal exit left mouse reporting enabled"
    print("input limits: direct Clear Input, mouse caret/clear, channel/query click, history/roster wheel, enable/disable and exit cleanup passed")


def main():
    if not BINARY.is_file():
        raise SystemExit("Build ./vipirc first (make).")
    input_navigation_and_actions()
    disconnected_manual_navigation()
    always_available_directions()
    arrow_header_focus()
    fixed_viewport_and_threshold()
    unicode_byte_threshold()
    chat_commands_and_literal_paste()
    clear_input_editing()
    parallel_terminal_locales()
    focus_controls_and_actions()
    clear_input_and_mouse()
    print("all socket-free input-limit PTY tests passed")


if __name__ == "__main__":
    main()
