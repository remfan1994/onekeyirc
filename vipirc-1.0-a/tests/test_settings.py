#!/usr/bin/env python3
"""Settings form regressions in real PTYs; network fixtures are loopback only."""
import os
import re
from pathlib import Path
import stat
import subprocess
import sys

from test_offline import BINARY, OfflineTerminal
from test_integration import Session
from test_input_limits import BEGIN_PASTE, END_PASTE, editor_info, wait_bytes, focused_label
from test_servers import MultiServerSession

UP, DOWN, LEFT, RIGHT = b"\x1bOA", b"\x1bOB", b"\x1bOD", b"\x1bOC"
HOME = b"\x1bOH"
LABELS = ["Server", "Port", "Transport", "Nickname", "Username", "Real name",
          "Auto-join channels", "Auto-connect", "Auto-authentication", "Authentication user",
          "Password file", "GPG password file", "Plaintext password",
          "Server password file", "GPG server password file", "Plaintext server password",
          "CA file", "Connection type", "Bouncer username", "Bouncer network",
          "Bouncer client name", "Bouncer automatic away", "User list", "Mouse", "Hints", "Shortcut labels", "Queue notices",
          "Program messages", "Persistent logging", "Single-session mode", "PART message", "QUIT message",
          "Background away", "Background away message", "Auto-away minutes", "Auto-away message",
          "Command suggestions", "Default interface", "History lines"]
INTERFACE_FIELDS = range(LABELS.index("User list"), LABELS.index("History lines"))
HINTS_FIELD = LABELS.index("Hints")
SHORTCUTS_FIELD = LABELS.index("Shortcut labels")
HISTORY_FIELD = LABELS.index("History lines")
SUGGESTIONS_FIELD = LABELS.index("Command suggestions")
DEFAULT_INTERFACE_FIELD = LABELS.index("Default interface")
WATCH_FIRST_FIELD = len(LABELS)


def in_settings(session):
    return re.match(r"(?:\[OFFLINE\] )?(?:ASCII \| )?Settings?", session.panel_title()) is not None


def open_settings(session, *, target=0, interface=False, home=False):
    session.app_entry("Settings")
    session.wait(lambda: in_settings(session) and "[Open]" in "".join(session.screen.cells[-2]),
                 "vipirc Settings entry did not open the application-wide home")
    assert session.mode() == "PANEL"
    if home:
        return
    # 'g' is the program's list Home key across xterm/screen/SSH terminfo;
    # xterm's ESC O H is not the Home sequence advertised by TERM=screen.
    session.type(b"g" + DOWN * (0 if interface else target + 1) + b"\r")
    expected = "Settings | Interface" if interface else "Settings | Server %d" % target
    session.wait(lambda: in_settings(session) and "[Edit]" in "".join(session.screen.cells[-2]) and
                 (session.screen.cols < len(expected) + 10 or expected in session.panel_title()),
                 "Settings target did not open: " + expected)


def return_main(session):
    session.type(b"\x1b")
    session.wait(lambda: session.panel_title().startswith("vipirc |"),
                 "Settings Escape did not return to vipirc")
    session.leave_app()


def select_field(session, index):
    if "Settings | Interface" in session.panel_title():
        assert index in INTERFACE_FIELDS, "non-interface field requested in Interface"
        row = index - INTERFACE_FIELDS.start
    else:
        assert index not in INTERFACE_FIELDS, "application preferences have their own Settings group"
        row = index - len(INTERFACE_FIELDS) if index >= INTERFACE_FIELDS.stop else index
    session.type(b"g" + DOWN * row)
    session.quiet(0.04)


def edit_field(session, index, value, *, paste=False):
    select_field(session, index)
    session.type(b"\r")
    label = LABELS[index] if index < len(LABELS) else "Watch term" if index == len(LABELS) else "Add watch term"
    session.wait(lambda: "Edit: " in session.screen.text(), "field editor did not open for " + label)
    encoded = value.encode("utf-8") if isinstance(value, str) else value
    if paste:
        session.type(BEGIN_PASTE + encoded + END_PASTE)
    elif encoded:
        session.type(encoded)
    else:
        session.type(b"\x7f")
    session.type(b"\r")
    session.wait(lambda: "Edit: " not in session.screen.text() and
                 any(marker in "".join(session.screen.cells[-1]) for marker in ("Value staged", "Server changed")),
                 "field value was not staged for " + label)


def toggle(session, index):
    select_field(session, index)
    session.type(b"\r")
    session.wait(lambda: "Choice staged" in session.screen.text(), "boolean field did not toggle")


def choose(session, index, value):
    select_field(session, index)
    session.type(b"\r")
    session.wait(lambda: "(*)" in session.screen.text(), "choice selector did not open")
    session.type((DOWN if value else UP) + b"\r")
    session.wait(lambda: "Choice staged" in session.screen.text() and "(*)" not in session.screen.text(),
                 "choice selector did not stage its value")


def action(session, number):
    # Field editing/choice staging and successful Apply leave Edit selected.
    session.type(RIGHT * number + b"\r")


def offline_settings():
    with OfflineTerminal() as session:
        session.start()
        session.input_mode()
        draft = "chat draft survives settings"
        session.type(draft)
        wait_bytes(session, len(draft))
        open_settings(session)
        session.type(b"\t")
        session.wait(lambda: focused_label(session, 5, "Port"), "Settings Tab did not move to the next field")
        assert in_settings(session) and session.header_focus() is None

        # Pasting in the form cannot choose controls or run local commands.
        session.type(BEGIN_PASTE + b"/quit\r\x1b\t" + END_PASTE)
        session.quiet(0.08)
        assert in_settings(session) and session.proc.poll() is None

        # A text field uses the real editor. Clipboard controls cannot stage
        # the value, escape the form, submit IRC text, or become commands.
        select_field(session, 5)
        session.type(b"\r")
        session.wait(lambda: "Edit: Real name" in session.screen.text(), "real-name editor missing")
        session.type(BEGIN_PASTE + b"/quit\tliteral\r\nvalue\x1b" + END_PASTE)
        session.wait(lambda: "Paste contained rejected controls" in session.screen.text(),
                     "unsafe field paste was not reported")
        assert "Edit: Real name" in session.screen.text() and in_settings(session)
        assert "/quit literalvalue" in session.screen.text()
        session.type(b"\t")
        session.wait(lambda: "Edit: Real name" not in session.screen.text() and
                     "Value staged" in "".join(session.screen.cells[-1]), "Settings field Tab did not stage the field")
        assert in_settings(session) and "/quit literalvalue" in session.screen.text()
        return_main(session)  # Escape returns directly even from a field editor.
        wait_bytes(session, len(draft))
        assert session.config.read_bytes() == session.before_config
        assert b"<offline_tester>" not in session.raw

        open_settings(session)
        # Server changes clear inherited credentials and auto-join settings
        # in the staged profile, without changing this running simulator.
        edit_field(session, 0, "next-settings.invalid")
        assert "inherited auth and auto-join values cleared" in session.screen.text()
        select_field(session, 8)
        assert "Auto-authentication: none" in session.screen.text()
        assert "Password file: (empty)" in session.screen.text()
        edit_field(session, HISTORY_FIELD, "100")
        edit_field(session, WATCH_FIRST_FIELD + 1, "settings-watch")  # One configured term already exists.
        action(session, 1)
        session.wait(lambda: "Preferences applied in memory" in session.screen.text(), "Apply did not remain in Settings")
        assert in_settings(session) and "[Edit]" in "".join(session.screen.cells[-2])
        assert session.config.read_bytes() == session.before_config

        # Save is unavailable in test mode. It cannot read FIFO secret paths
        # or write the profile, including after a changed future server.
        action(session, 2)
        session.wait(lambda: "Save is disabled" in session.screen.text(), "offline Save was not blocked")
        assert in_settings(session) and session.config.read_bytes() == session.before_config
        return_main(session)
        open_settings(session, interface=True)
        toggle(session, 17)
        toggle(session, 18)
        action(session, 1)
        session.wait(lambda: "Application preferences applied" in session.screen.text(), "global Interface Apply failed")
        return_main(session)
        wait_bytes(session, len(draft))
        assert "Users:" in session.screen.text(), "live user-list preference was not applied"
        assert "offline_tester" in session.screen.text(), "pending nickname/server changed the current session"

        open_settings(session)
        assert "Server: next-settings.invalid" in session.screen.text(), "applied startup choice was not retained in memory"
        select_field(session, WATCH_FIRST_FIELD + 1)
        assert "settings-watch" in session.screen.text(), "applied mention term was not retained"
        return_main(session)
        session.input_mode()
        wait_bytes(session, len(draft))
        assert session.config.read_bytes() == session.before_config
    print("settings offline: Escape/Tab, staged cancel, literal field paste, live Apply, future-server isolation, inherited-auth clearing and disabled Save passed")


def numeric_validation_and_resize():
    with OfflineTerminal() as session:
        session.start()
        open_settings(session)
        select_field(session, 1)
        session.type(b"\r")
        session.wait(lambda: "Edit: Port" in session.screen.text(), "port editor missing")
        session.type("70000\r")
        session.wait(lambda: "Port must be an integer from 1 to 65535." in session.screen.text(),
                     "out-of-range port was accepted")
        assert "Edit: Port" in session.screen.text()
        session.type(HOME + b"\x7f")
        # Home cancels initial selection; select the value with physical
        # Home/Delete to test the field editor rather than a chat action.
        session.type(HOME + b"\x1b[3~" * 5 + b"6697\r")
        session.wait(lambda: "Value staged" in session.screen.text(), "corrected port was not staged")
        return_main(session)
        assert session.config.read_bytes() == session.before_config
    with OfflineTerminal(cols=20, rows=9, locale="C", term="screen") as session:
        session.start()
        open_settings(session)
        assert in_settings(session) and not session.screen.cursor_visible
        assert "[OFFLINE]" in session.screen.text()
        session.type(b"\t")
        assert in_settings(session)
        return_main(session)
    print("settings fields: numeric rejection/correction and minimum-size ASCII panel navigation passed")


def suggestions_choice_at_minimum_size():
    with OfflineTerminal(cols=20, rows=9, locale="C", term="screen") as session:
        session.start()
        open_settings(session, interface=True)
        # At twenty columns the title cannot include the group name. Use the
        # known interface row directly, then inspect the actual terminal cells.
        session.type(b"g" + DOWN * (SUGGESTIONS_FIELD - INTERFACE_FIELDS.start) + b"\r")
        session.wait(lambda: "Suggestions" in session.screen.text() and "Contextual" in session.screen.text(),
                     "five-choice popup did not fit the minimum terminal")
        title_row = next(i for i, row in enumerate(session.screen.cells) if "Suggestions" in "".join(row))
        assert title_row >= 2, "shared header/divider overwrote the choice title"
        for label in ("Alphabetic", "Common", "History", "Most used", "Contextual"):
            assert label in session.screen.text(), (label, session.screen.text())
        assert "(*) Common" in session.screen.text(), "new default was not Common"
        session.type(DOWN * 3 + b"\r")
        session.wait(lambda: "Choice staged" in session.screen.text() and "(*)" not in session.screen.text(),
                     "Contextual choice could not be staged at minimum size")
        action(session, 1)
        session.wait(lambda: "Application prefer" in session.screen.text(), "minimum-size Apply failed")
        return_main(session)
        # Reopen the picker to check the applied value, without depending on
        # a long field label that is naturally clipped at twenty columns.
        open_settings(session, interface=True)
        session.type(b"g" + DOWN * (SUGGESTIONS_FIELD - INTERFACE_FIELDS.start) + b"\r")
        session.wait(lambda: "(*) Contextual" in session.screen.text(), "applied suggestion mode was not retained")
        return_main(session)
        assert session.config.read_bytes() == session.before_config
    print("settings suggestions: all five modes, Common default, title below shared divider and Contextual Apply at9x20 passed")


def ignore_mask_settings():
    with OfflineTerminal() as session:
        session.start()
        session.input_mode()
        draft = "draft survives ignore settings"
        session.type(draft)
        wait_bytes(session, len(draft))
        open_settings(session)
        # Server fields, one existing watch term, its Add row,
        # then the ignore list. Empty lists still have an Add control.
        select_field(session, WATCH_FIRST_FIELD + 2)
        assert "Add ignore mask" in session.screen.text()
        session.type(b"\r")
        session.wait(lambda: "Edit: Add ignore mask" in session.screen.text(), "ignore editor missing")
        session.type("invalid mask\r")
        session.wait(lambda: "Use an exact nickname" in session.screen.text(), "invalid mask was accepted")
        assert "Edit: Add ignore mask" in session.screen.text()
        session.type(HOME + b"\x1b[3~" * len("invalid mask") + b"Noisy!*@*\r")
        session.wait(lambda: "Value staged" in session.screen.text(), "corrected ignore mask was not staged")
        return_main(session)
        wait_bytes(session, len(draft))

        open_settings(session)
        select_field(session, WATCH_FIRST_FIELD + 2)
        assert "Ignore mask 1:" not in session.screen.text(), "canceled ignore edit was applied"
        edit_field(session, WATCH_FIRST_FIELD + 2, "Noisy!*@*")
        action(session, 1)
        session.wait(lambda: "Preferences applied" in session.screen.text(), "ignore Apply failed")
        return_main(session)
        wait_bytes(session, len(draft))
        open_settings(session)
        select_field(session, WATCH_FIRST_FIELD + 2)
        assert "Ignore mask 1: Noisy!*@*" in session.screen.text(), "live ignore mask was not retained"
        edit_field(session, WATCH_FIRST_FIELD + 2, "")
        action(session, 1)
        session.wait(lambda: "Preferences applied" in session.screen.text(), "ignore removal Apply failed")
        return_main(session)
        wait_bytes(session, len(draft))
        open_settings(session)
        select_field(session, WATCH_FIRST_FIELD + 2)
        assert "Ignore mask 1:" not in session.screen.text(), "empty ignore field did not remove the mask"
        return_main(session)
        assert session.config.read_bytes() == session.before_config
        assert b"<offline_tester>" not in session.raw
    print("settings ignore: invalid/corrected masks, staged cancel, live Apply, empty removal and draft/config preservation passed")


def live_save_and_pending_connection():
    with Session() as session:
        session.start()
        session.input_mode()
        draft = "pending chat draft"
        session.type(draft)
        session.wait(lambda: draft in session.input_text(), "draft missing")
        initial_messages = len(session.server.messages())
        initial_nicks = sum(line.startswith(b"NICK ") for _, line in session.server.lines)
        open_settings(session)
        edit_field(session, 0, "next-settings.invalid")
        edit_field(session, 1, "6668")
        edit_field(session, 3, "nextnick")
        toggle(session, 2)  # Future TLS, while this fixture is still plaintext.
        edit_field(session, HISTORY_FIELD, "200")
        edit_field(session, WATCH_FIRST_FIELD, "config-watch")
        edit_field(session, WATCH_FIRST_FIELD + 2, "SavedPeer!*@*")
        action(session, 2)
        session.wait(lambda: "configuration saved" in session.screen.text().lower(),
                     "Save did not write the selected server profile")
        assert in_settings(session), "Save unexpectedly left the application Settings panel"
        return_main(session)
        assert session.mode() == "INPUT"
        assert session.screen.cursor_visible and "Channel 1: #test" in session.panel_title(), "Save did not restore the chat view and caret"
        assert draft in session.input_text(), "saving Settings lost the chat draft"
        session.open_app_diagnostics()
        assert "plaintext connected" in session.app_line("Connection:"), "future TLS mislabeled the current connection"
        session.close_app_diagnostics()
        assert len(session.server.messages()) == initial_messages
        assert sum(line.startswith(b"NICK ") for _, line in session.server.lines) == initial_nicks
        session.server.send("PING :settings-still-live")
        session.wait(lambda: any(line == b"PONG :settings-still-live" for _, line in session.server.lines),
                     "applying configuration disrupted the active connection")
        data = session.config.read_text(encoding="utf-8")
        for expected in ("server=next-settings.invalid\n", "port=6668\n", "tls=true\n", "nick=nextnick\n",
                         "channel=\n", "history_lines=200\n", "word=config-watch\n",
                         "[ignore]\n", "mask=SavedPeer!*@*\n"):
            assert expected in data, (expected, data)
        assert "keymap=" not in data and "focus_style=" not in data, "saved obsolete interface preferences"
        assert stat.S_IMODE(session.config.stat().st_mode) == 0o600
        check = subprocess.run([str(BINARY), "--config", str(session.config), "--check-config"],
                               stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=3)
        assert check.returncode == 0, (check.returncode, check.stderr)
        # A later /save must preserve the future profile rather than silently
        # writing the still-active old server, port, TLS mode and nickname.
        session.clear_input()
        session.command("/save")
        session.wait(lambda: "Saved server 0 (including application preferences)." in session.screen.text(),
                     "later /save did not confirm the selected primary profile")
        again = session.config.read_text(encoding="utf-8")
        for expected in ("server=next-settings.invalid\n", "port=6668\n", "tls=true\n", "nick=nextnick\n",
                         "mask=SavedPeer!*@*\n"):
            assert expected in again, (expected, again)
    print("settings live: private Save, parser reload, pending startup persistence, live TLS/identity isolation, draft retention and continued PING passed")


def save_failure_is_transactional():
    with Session() as session:
        session.start()
        open_settings(session)
        edit_field(session, HISTORY_FIELD, "200")
        protected = Path(session.temp.name) / "untouched"
        protected.write_text("untouched\n", encoding="ascii")
        protected.chmod(0o600)
        session.config.unlink()
        session.config.symlink_to(protected)
        action(session, 2)
        session.wait(lambda: "unsafe configuration" in session.screen.text().lower() or
                     "symlink" in session.screen.text().lower() or "regular" in session.screen.text().lower(),
                     "unsafe Save destination was not rejected")
        assert in_settings(session) and focused_label(session, session.screen.rows - 2, "Save", "reverse"), "failed Save lost visible form focus"
        assert protected.read_text(encoding="ascii") == "untouched\n"
        return_main(session)
    print("settings save: unsafe destination failure leaves file and live preferences unchanged")


def hostile_config_display():
    direction = "\u202e"
    extra = "[connection]\nrealname=alpha%sbeta\npassword_file=/tmp/path%sname\n[mentions]\nword=watch%sterm\n" % (
        direction, direction, direction)
    with OfflineTerminal(config_extra=extra) as session:
        session.start()
        before = len(session.raw)
        open_settings(session)
        session.quiet(0.05)
        assert direction.encode() not in session.raw[before:], "Settings emitted a stored direction control"
        select_field(session, 5)
        session.type(b"\r")
        session.wait(lambda: "filtered for editing" in session.screen.text(), "unsafe stored field was not explained")
        assert direction.encode() not in session.raw[before:], "field editor emitted a stored direction control"
        return_main(session)
        assert session.config.read_bytes() == session.before_config, "viewing or canceling modified a stored config value"
    print("settings display: stored Unicode controls are sanitized; viewing and canceled edits preserve original config bytes")


def mouse_fields():
    def click(session, x, y, tail=b""):
        session.type(("\x1b[<0;%d;%dM\x1b[<0;%d;%dm" % (x + 1, y + 1, x + 1, y + 1)).encode() + tail)
    with OfflineTerminal() as session:
        session.start()
        open_settings(session)
        click(session, 5, 12)  # Auto-authentication, field8.
        session.wait(lambda: "(*)" in session.screen.text(), "mouse did not open the choice selector")
        choice_row = next(i for i, row in enumerate(session.screen.cells)
                          if re.search(r"\(.[)] none\b", "".join(row)))
        click(session, 32, choice_row)  # Stage none, without applying credentials.
        session.wait(lambda: "Choice staged" in session.screen.text(), "mouse did not stage the selected choice")
        click(session, 9, 22)  # Apply.
        session.wait(lambda: "[Edit]" in "".join(session.screen.cells[-2]) and "Preferences applied" in session.screen.text(),
                     "mouse Apply did not activate the choice")
        click(session, 5, 9)  # Real-name field5.
        session.wait(lambda: "Edit: Real name" in session.screen.text(), "mouse did not open a text field")
        click(session, 2, 6, b"Z")  # Press/release + immediate typing at field start.
        session.wait(lambda: "ZOffline configuration test" in session.screen.text(),
                     "field mouse caret was applied after the typed character")
        return_main(session)
        assert session.config.read_bytes() == session.before_config
    print("settings mouse: field/choice selection, Apply, and immediate caret click-plus-typing passed")


def concurrent_identity_update():
    with Session() as session:
        session.start()
        open_settings(session)
        session.server.send(":tester!u@localhost NICK :laternick")
        session.server.send("PING :settings-new-nick")
        session.wait(lambda: any(line == b"PONG :settings-new-nick" for _, line in session.server.lines),
                     "nickname update was not processed while Settings was open")
        edit_field(session, HISTORY_FIELD, "200")
        action(session, 2)
        session.wait(lambda: "configuration saved" in session.screen.text().lower(), "Save did not finish after the nickname update")
        assert "nick=laternick\n" in session.config.read_text(), "unedited stale nickname overwrote a live update"
        return_main(session)
    print("settings save: unedited fields preserve server-driven identity updates while the panel is open")


def universal_saved_servers():
    profiles = (("saved_active", "#other", True), ("saved_idle", "#idle", False))
    with MultiServerSession(profiles=profiles) as session:
        session.start()
        session.wait(lambda: session.profile_peers[0].registered, "background saved server did not connect")
        original_primary = session.config.read_bytes()
        original_other = session.profile_paths[0].read_bytes()
        draft = "PRIMARY_DRAFT_REMAINS_CURRENT"
        session.type(draft)
        wait_bytes(session, len(draft))
        open_settings(session, target=2)
        assert session.profile_peers[1].socket is None, "viewing saved settings opened a connection"
        edit_field(session, 3, "future_idle_nick")
        edit_field(session, 6, "#saved-one,#saved-two")
        toggle(session, 7)
        toggle(session, 2)  # Verified TLS for the future authenticated connection.
        choose(session, 8, 1)
        edit_field(session, 9, "idle-account")
        secret_path = str(session.directory / "unopened-password.gpg")
        edit_field(session, 11, secret_path)
        action(session, 2)
        session.wait(lambda: "configuration saved" in session.screen.text().lower(), "inactive profile Save failed")
        assert "Settings | Server 2" in session.panel_title()
        saved = session.profile_paths[1].read_text()
        for expected in ("nick=future_idle_nick\n", "channel=#saved-one,#saved-two\n", "autoconnect=true\n",
                         "tls=true\n", "auth=sasl\n", "auth_user=idle-account\n", "password_gpg_file=" + secret_path + "\n"):
            assert expected in saved, (expected, saved)
        assert session.config.read_bytes() == original_primary
        assert session.profile_paths[0].read_bytes() == original_other
        assert not Path(secret_path).exists() and session.profile_peers[1].socket is None
        action(session, 3)  # Back to the universal home, independently of chat.
        session.wait(lambda: "Settings | All servers" in session.panel_title() and
                     "Server 2:" in session.screen.text() and "auto-connect on" in session.screen.text(),
                     "Back did not return to Settings home")
        assert "Server 2:" in session.screen.text() and "auto-connect on" in session.screen.text()
        session.type(HOME + b"\r")
        session.wait(lambda: "Settings | Interface" in session.panel_title(), "Interface group did not open")
        toggle(session, 18)
        action(session, 2)
        session.wait(lambda: "Application preferences saved" in session.screen.text(), "application-wide preferences Save failed")
        assert "mouse=false\n" in session.config.read_text()
        assert session.profile_paths[0].read_bytes() == original_other
        assert session.profile_paths[1].read_text() == saved
        return_main(session)
        wait_bytes(session, len(draft))
        assert draft in session.input_text()
        assert "Clear" not in "".join(session.screen.cells[-1]) and "Send" not in "".join(session.screen.cells[-1])
        session.open_app_diagnostics()
        assert "Nick: tester" in session.app_line("Nick:"), "editing another saved server switched the active chat"
        session.close_app_diagnostics()
        for i, peer in enumerate((session.server, session.profile_peers[0])):
            peer.send("PING :settings-universal-%d" % i)
        session.wait(lambda: all(any(line == ("PONG :settings-universal-%d" % i).encode()
                                          for _, line in peer.lines)
                                 for i, peer in enumerate((session.server, session.profile_peers[0]))),
                     "Settings disrupted a live connection")
        assert not session.server.messages() and session.profile_peers[1].socket is None
    print("universal settings: edit/save a disconnected saved server without switching chat, grouped startup/auth, isolated files, global mouse preference, retained draft and live PING passed")


def settings_new_server_keeps_chat():
    with MultiServerSession(offline=True) as session:
        session.start()
        draft = "DRAFT_BEFORE_SETTINGS_ADD"
        session.type(draft)
        wait_bytes(session, len(draft))
        open_settings(session, home=True)
        session.type(b"\x1bOF\r")
        session.wait(lambda: "New server" in session.panel_title(), "Settings New server entry did not open")
        # New-server basics use their compact original field order.
        from test_servers import edit_field as new_edit, set_boolean, choose_action, NEW_ACTIONS
        new_edit(session, 0, "settings-added.invalid")
        new_edit(session, 3, "settings_added")
        new_edit(session, 4, "#settings-added")
        set_boolean(session, 5, "Auto-connect", True)
        choose_action(session, "Add", NEW_ACTIONS)
        session.wait(lambda: "Settings | Server 1" in session.panel_title(), "Settings Add did not stay in the new profile form")
        return_main(session)
        wait_bytes(session, len(draft))
        assert draft in session.input_text()
        session.open_app_diagnostics()
        assert "Nick: offline_first" in session.app_line("Nick:"), "Settings Add switched the active chat"
        session.close_app_diagnostics()
        open_settings(session, target=1)
        assert "Server: settings-added.invalid" in session.screen.text()
        assert "Nickname: settings_added" in session.screen.text()
        action(session, 2)
        session.wait(lambda: "Save is disabled" in session.screen.text(), "offline universal profile Save was not blocked")
        return_main(session)
        assert draft in session.input_text()
    print("universal settings new server: offline Add stays in Settings and preserves original chat/draft; independent target and no disk writes passed")


def hints_are_global_persistent_and_noncritical():
    from test_input_limits import expect_warning, acknowledge
    from test_servers import focused_action, SERVER_ACTIONS, CHANNEL_ACTIONS
    from test_transfers import IncomingPeer, offer_payload, open_transfers, action as transfer_action
    with MultiServerSession(rows=26,cols=132) as session:
        session.start()
        slot, second = session.add_server(nick="hints_second")
        assert slot == 1
        session.select_server(0)
        draft = "HINT_PREFERENCES_KEEP_DRAFT"
        session.type(draft);wait_bytes(session,len(draft))
        open_settings(session,interface=True)
        select_field(session,HINTS_FIELD)
        session.wait(lambda:"Hints: [x] on" in session.screen.text(),"default Hints field missing")
        assert "show optional tips" in "".join(session.screen.cells[-4])
        toggle(session,HINTS_FIELD)
        action(session,2)
        session.wait(lambda:"Application preferences saved" in session.screen.text() and
                     "hints=false\n" in session.config.read_text(),"Hints Save did not persist false")
        session.wait(lambda:not "".join(session.screen.cells[-4]).strip(),"field hint survived disabled Hints")
        assert "Config:" in "".join(session.screen.cells[3]),"Hints hid the configuration destination"
        assert "[Edit]" in "".join(session.screen.cells[-2]) and "Save" in "".join(session.screen.cells[-2])
        return_main(session)
        assert draft in session.input_text()
        for selected in (0,1):
            session.select_server(selected)
            session.open_destination("Servers")
            session.wait(lambda:focused_action(session,SERVER_ACTIONS)=="Open","server action missing with Hints off")
            assert not "".join(session.screen.cells[-2]).strip(),"Hints off did not apply to server list"
            session.open_destination("Channels")
            session.wait(lambda:focused_action(session,CHANNEL_ACTIONS)=="Open","channel action missing with Hints off")
            assert not "".join(session.screen.cells[-2]).strip(),"Hints off did not reach another server"
            session.open_command()
            assert "Run" in "".join(session.screen.cells[-2]) and "Cancel" in "".join(session.screen.cells[-2])
            assert "Right: complete" not in "".join(session.screen.cells[-2]),"command tip survived Hints off"
            session.type(b"\x1b");session.finish_command_cancel()
        session.select_server(0)
        assert draft in session.input_text()
        session.open_destination("vipirc")
        session.wait(lambda:"Back" in "".join(session.screen.cells[-2]),"app controls missing with Hints off")
        assert "Up/Down:" not in "".join(session.screen.cells[-2])
        session.app_entry("Manual")
        assert "Search" in session.screen.text(),"Hints off hid the manual Search control"
        assert not any("".join(row).lstrip().startswith(("Server ","Connection:","Nick:"))
                       for row in session.screen.cells[3:8]),"manual still contains the removed diagnostic block"
        assert "Back" in "".join(session.screen.cells[3])
        assert "Up/Down:" not in "".join(session.screen.cells[-2]),"manual tip survived Hints off"
        session.close_help()
        session.app_entry("IALA")
        session.wait(lambda:"Line " in "".join(session.screen.cells[-1]),"license position disappeared with Hints off")
        assert "Up/Down:" not in "".join(session.screen.cells[-2]),"license tip survived Hints off"
        session.type(b"\x1b")
        session.wait(lambda:session.panel_title().startswith("vipirc |"),"license Escape failed with Hints off")
        session.leave_app()
        assert draft in session.input_text()
        open_settings(session)
        select_field(session,1);session.type(b"\r")
        session.wait(lambda:"Edit: Port" in session.screen.text(),"port editor missing with Hints off")
        session.type(b"70000\r")
        session.wait(lambda:"Port must be an integer from 1 to 65535." in session.screen.text(),
                     "Hints off hid a field validation error")
        return_main(session)
        session.clear_input();wait_bytes(session,0)
        session.command("/channel #test")
        budget=editor_info(session).budget;assert budget
        session.type("X"*budget)
        expect_warning(session)
        acknowledge(session)
        session.clear_input();wait_bytes(session,0)
        incoming=IncomingPeer()
        try:
            offer_payload(session.server,"HintPeer","hints-warning.bin",incoming.port,1)
            session.server.send("PING :hints-off-transfer")
            session.wait(lambda:any(line==b"PONG :hints-off-transfer" for _,line in session.server.lines),"Hints changed IRC service")
            open_transfers(session)
            transfer_action(session,"Accept")
            session.wait(lambda:"Receive file" in session.panel_title() and "Unencrypted" in "".join(session.screen.cells[3]),
                         "Hints off hid the transfer transport/IP warning")
            assert "IP" in "".join(session.screen.cells[3])
            incoming.assert_unopened()
            session.type(b"\x1b");session.wait(session.main_frame_ready,"transfer Escape failed with Hints off")
        finally:incoming.close()
        for number,peer in enumerate((session.server,second)):
            peer.send("PING :hints-global-%d"%number)
        session.wait(lambda:all(any(line==("PONG :hints-global-%d"%number).encode() for _,line in peer.lines)
                               for number,peer in enumerate((session.server,second))),"Hints preference disrupted a live server")
        result=subprocess.run([str(BINARY),"--config",str(session.config),"--check-config"],capture_output=True,timeout=3)
        assert result.returncode==0,(result.stdout,result.stderr)
        assert "hints=false\n" in session.config.read_text()
        assert not session.server.messages() and not second.messages(),"Hint controls submitted chat"
    print("settings Hints: global live Apply/Save, private reload, all-server lists, field/command-tip suppression and retained actions/errors/split/DCC safety warnings passed")


def shortcut_labels_are_global_without_disabling_keys():
    from test_input_limits import mouse_click
    with MultiServerSession(rows=26,cols=132) as session:
        session.start();slot,second=session.add_server(nick="labels_second")
        assert slot==1
        session.select_server(0)
        draft="FUNCTION_KEYS_KEEP_THE_DRAFT"
        session.type(draft);wait_bytes(session,len(draft))
        assert "F1 vipirc" in "".join(session.screen.cells[0])
        open_settings(session,interface=True)
        select_field(session,SHORTCUTS_FIELD)
        session.wait(lambda:"Shortcut labels: [x] on" in session.screen.text(),"Shortcut labels default is not visible")
        toggle(session,SHORTCUTS_FIELD);action(session,2)
        session.wait(lambda:"Application preferences saved" in session.screen.text() and
                     "shortcut_labels=false\n" in session.config.read_text(),"Shortcut labels did not save")
        assert "hints=true\n" in session.config.read_text(),"label preference changed the independent Hints preference"
        return_main(session)
        assert draft in session.input_text()
        for selected in (0,1):
            session.select_server(selected)
            header="".join(session.screen.cells[0])
            assert not re.search(r"\bF[1-6]\b",header),"disabled labels remained on another server"
            session.type(session.category_key(2))
            session.wait(lambda:"Servers" in session.panel_title() and "Up/Down:" in "".join(session.screen.cells[-2]) and
                         "Open" in "".join(session.screen.cells[3]),"hidden F2 label disabled its key or independent list hints")
            session.type(session.category_key(3))
            session.wait(lambda:"Channels" in session.panel_title() and "Open" in "".join(session.screen.cells[3]),
                         "hidden F3 label disabled its key")
            header="".join(session.screen.cells[0])
            mouse_click(session,0,header.index("Servers")+3)
            session.wait(lambda:"Servers" in session.panel_title(),"unlabelled header mouse target is wrong")
            session.type(b"\x1b");session.wait(session.main_frame_ready,"unlabelled category did not return to chat")
        session.select_server(0)
        assert draft in session.input_text()
        open_settings(session,interface=True)
        toggle(session,SHORTCUTS_FIELD);action(session,1)
        session.wait(lambda:"Application preferences applied" in session.screen.text() and
                     "F1 vipirc" in "".join(session.screen.cells[0]),"Apply did not restore function-key labels")
        assert "shortcut_labels=false\n" in session.config.read_text(),"Apply rewrote saved labels without Save"
        return_main(session)
        for selected in (0,1):
            session.select_server(selected)
            assert "F2 Servers (2)" in "".join(session.screen.cells[0]),"restored labels did not reach all servers"
        for number,peer in enumerate((session.server,second)):
            peer.send("PING :labels-alive-%d"%number)
        session.wait(lambda:all(any(line==("PONG :labels-alive-%d"%number).encode() for _,line in peer.lines)
                               for number,peer in enumerate((session.server,second))),"label preference disrupted a connection")
        assert not session.server.messages() and not second.messages()
    print("shortcut labels: enabled defaults, primary-only Save/global Apply, independent Hints, active F keys and correct mouse geometry with hidden labels, drafts and PING passed")


def default_interface_keeps_current_frontend():
    with Session() as session:
        session.start()
        session.input_mode()
        draft = "default interface keeps this draft"
        session.type(draft)
        session.wait(lambda: draft in session.input_text(), "draft missing")
        initial_messages = len(session.server.messages())
        open_settings(session, interface=True)
        choose(session, DEFAULT_INTERFACE_FIELD, 1)
        action(session, 2)
        session.wait(lambda: "Application preferences saved" in session.screen.text() and
                     "default_interface=idle\n" in session.config.read_text(),
                     "default interface was not saved in the primary config")
        assert in_settings(session), "saving the default unexpectedly switched the live frontend"
        return_main(session)
        assert draft in session.input_text(), "changing the default lost the draft"
        session.server.send("PING :default-interface-live")
        session.wait(lambda: any(line == b"PONG :default-interface-live" for _, line in session.server.lines),
                     "saving the default disrupted the current IRC connection")
        open_settings(session, interface=True)
        choose(session, DEFAULT_INTERFACE_FIELD, 0)
        action(session, 1)
        session.wait(lambda: "Application preferences applied" in session.screen.text(),
                     "default interface Apply failed")
        assert "default_interface=idle\n" in session.config.read_text(), "Apply saved without consent"
        return_main(session)
        assert draft in session.input_text() and len(session.server.messages()) == initial_messages
    print("default interface: Settings choice, primary save, Apply without saving, unchanged frontend/draft and live PING passed")


def main():
    if "--live-only" not in sys.argv:
        offline_settings()
        numeric_validation_and_resize()
        suggestions_choice_at_minimum_size()
        ignore_mask_settings()
        hostile_config_display()
        mouse_fields()
        settings_new_server_keeps_chat()
    if "--offline-only" not in sys.argv:
        live_save_and_pending_connection()
        save_failure_is_transactional()
        concurrent_identity_update()
        universal_saved_servers()
        hints_are_global_persistent_and_noncritical()
        shortcut_labels_are_global_without_disabling_keys()
        default_interface_keeps_current_frontend()
    print("all settings PTY tests passed")


if __name__ == "__main__":
    main()
