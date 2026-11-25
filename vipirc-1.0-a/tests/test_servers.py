#!/usr/bin/env python3
"""Real-PTY regressions for isolated server sessions; loopback or offline only."""
import argparse
import configparser
import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
import stat
import struct
import subprocess
import tempfile
import termios
import time

from test_integration import BINARY, ROOT, IRCFixture, ModeControls, Screen, Session

UP, DOWN, LEFT, RIGHT = b"\x1bOA", b"\x1bOB", b"\x1bOD", b"\x1bOC"
HOME, END = b"\x1bOH", b"\x1bOF"
SERVER_ACTIONS = ("Open", "New", "Connect", "Disconnect", "Close", "Away", "Back")
NEW_ACTIONS = ("Edit", "Add", "Connect", "Advanced", "Basic", "Cancel")
CHANNEL_ACTIONS = ("Open", "Join", "Part", "Close", "Back")
QUERY_ACTIONS = ("Open", "New", "Close", "Back")


def header(session):
    return session.panel_title()


def in_servers(session):
    return re.search(r"\bServers(?:\s*\||\s*$)", header(session), re.M) is not None


def in_new(session):
    return "New server" in header(session)


def has_input(session):
    return session.main_frame_ready()


def focused_action(session, labels):
    row = 3 if re.match(r"(?:\[OFFLINE\] )?(?:Servers|Channels|Query list)(?:\s|$)",
                        session.panel_title()) else session.screen.rows - 2
    text = "".join(session.screen.cells[row])
    reverse = session.screen.reverse_cells[row]
    for label in labels:
        for found in re.finditer(r"(?<!\w)" + re.escape(label) + r"(?!\w)", text):
            if any(reverse[found.start():found.end()]):
                return label
        if "[" + label + "]" in text:
            return label
    return None


def choose_action(session, label, labels, activate=True):
    session.wait(lambda: focused_action(session, labels) is not None,
                 "Action bar did not show its selected action")
    for unused in range(len(labels) + 1):
        previous = focused_action(session, labels)
        if previous == label:
            if activate:
                session.type(b"\r")
            return
        session.type(RIGHT)
        session.wait(lambda: focused_action(session, labels) not in (None, previous),
                     "Right did not change the selected action from " + previous)
    raise AssertionError("Cannot focus %s\n%s" % (label, session.screen.text()))


def open_servers(session):
    if in_servers(session):
        return
    session.navigate("S")
    session.wait(lambda: in_servers(session), "Servers picker did not open")


def select_server_row(session, slot):
    open_servers(session)
    # Slots do not compact, but rows omit closed slots.
    session.wait(lambda: slot in server_rows(session),
                 "server slot did not finish rendering: %s" % slot)
    rows = server_rows(session)
    assert slot in rows, ("server slot is missing", slot, rows, session.screen.text())
    position = list(rows).index(slot)
    session.type(HOME + DOWN * position)
    session.quiet(0.035)


def server_rows(session):
    found = {}
    for cells in session.screen.cells[4:-2]:
        text = "".join(cells).strip()
        match = re.search(r"\|\s*\*?\s*(\d+):\s", text)
        if match:
            found[int(match[1])] = text
    return found


def counts(text):
    match = re.search(r"\bnew (\d+)\+? !(\d+)\+?\b", text)
    assert match, ("activity counters are missing", text)
    return int(match[1]), int(match[2])


def begin_new(session):
    open_servers(session)
    session.type("n")
    session.wait(lambda: in_new(session), "New server form did not open")
    assert session.mode() == "PANEL"


def select_field(session, index):
    choose_action(session, "Edit", NEW_ACTIONS, activate=False)
    session.type(HOME + DOWN * index)
    session.quiet(0.03)


def edit_field(session, index, value):
    select_field(session, index)
    session.type(b"\r")
    session.wait(lambda: "Edit: " in session.screen.text(), "New-server field editor did not open")
    session.type(value if value else b"\x7f")
    session.type(b"\r")
    session.wait(lambda: "Edit: " not in session.screen.text() and
                 any(marker in "".join(session.screen.cells[-1]) for marker in ("Value staged", "Server changed")),
                 "New-server field was not staged")


def set_boolean(session, index, label, value):
    select_field(session, index)
    line = next(("".join(row) for row in session.screen.cells[4:-3]
                 if label + ":" in "".join(row)), "")
    on,off=("TLS (verified)","Plain IRC") if label=="Transport" else ("[x] on","[ ] off")
    assert on in line or off in line, (label, session.screen.text())
    if (on in line) != bool(value):
        session.type(b"\r")
        expected = on if value else off
        session.wait(lambda: any(label + ":" in "".join(row) and expected in "".join(row)
                                 for row in session.screen.cells[4:-3]),
                     "New-server checkbox did not change: " + label)


def show_advanced(session):
    choose_action(session, "Advanced", NEW_ACTIONS)
    session.wait(lambda: "All connection fields" in header(session) and
                 "Basic" in "".join(session.screen.cells[-2]), "Advanced fields did not appear")


def set_auth_sasl(session):
    select_field(session, 8)
    session.type(b"\r")
    session.wait(lambda: "(*)" in session.screen.text(), "Authentication choice did not open")
    session.type(DOWN + b"\r")
    session.wait(lambda: "Choice staged" in session.screen.text(), "SASL choice was not staged")


def finish_new(session, connect):
    choose_action(session, "Connect" if connect else "Add", NEW_ACTIONS)
    session.wait(lambda: not in_new(session) and has_input(session), "New server was not created")
    assert session.mode() == "INPUT", "New server should return to chat editing"


def snapshot(directory):
    result = {}
    for path in sorted(directory.rglob("*")):
        info = path.lstat()
        content = path.read_bytes() if stat.S_ISREG(info.st_mode) else None
        result[str(path.relative_to(directory))] = (
            stat.S_IFMT(info.st_mode), stat.S_IMODE(info.st_mode), info.st_size,
            info.st_mtime_ns, info.st_ctime_ns, content)
    return result


class MultiServerSession(ModeControls):
    """Reusable isolated PTY for server and transfer integration suites.

    start() opens the primary session. add_server() creates through the real
    form and returns (slot, IRCFixture), or (slot, None) in offline mode.
    HOME and XDG directories are private to this fixture; no public IRC is used.
    """

    pump = Session.pump
    quiet = Session.quiet
    type = Session.type
    command = Session.command
    input_text = Session.input_text
    resize = Session.resize

    def __init__(self, offline=False, rows=26, cols=132, nick="tester", channel="#test", profiles=(), launch_cwd=None):
        self.offline = offline
        self.primary_nick = "offline_first" if offline else nick
        self.primary_channel = channel
        self.temp = tempfile.TemporaryDirectory(prefix="vipirc-servers-")
        self.directory = Path(self.temp.name)
        self.home = self.directory / "home"
        self.home.mkdir(mode=0o700)
        self.data_home = self.directory / "data"
        self.config = self.directory / "config.ini"
        self.secret = self.directory / "secret-must-not-be-read"
        self.peers = []
        self.server = None
        if offline:
            os.mkfifo(self.secret, 0o600)
            connection = (
                "server=first-must-not-resolve.invalid\nport=6697\ntls=true\n"
                "auth=sasl\nauth_user=first-no-real-account\n"
                "password_file=%s\nserver_password_file=%s\nca_file=%s\n" %
                (self.secret, self.secret, self.secret))
        else:
            self.server = IRCFixture()
            self.peers.append(self.server)
            connection = "server=127.0.0.1\nport=%d\ntls=false\nauth=none\n" % self.server.port
        self.config.write_text(
            "[connection]\n" + connection +
            "nick=%s\nusername=first_user\nrealname=First session\nchannel=%s\nautoconnect=true\n"
            "[interface]\nuser_list=false\nmouse=true\nkeymap=original\nfocus_style=reverse\n"
            "history_lines=100\n[mentions]\nword=primary-watch\n" %
            (self.primary_nick, channel), encoding="ascii")
        self.config.chmod(0o600)
        self.profile_peers = []
        self.profile_paths = []
        if profiles:
            profile_dir = Path(str(self.config) + ".d")
            profile_dir.mkdir(mode=0o700)
            catalog = Path(str(self.config) + ".servers")
            entries = []
            for number, (profile_nick, profile_channel, autoconnect) in enumerate(profiles, 1):
                peer = None if offline else IRCFixture()
                if peer is not None:
                    self.peers.append(peer)
                self.profile_peers.append(peer)
                profile = profile_dir / ("saved-%d.ini" % number)
                profile.write_text("[connection]\nserver=%s\nport=%d\ntls=%s\nnick=%s\n"
                                   "username=profile_user\nchannel=%s\nautoconnect=%s\nauth=none\n"
                                   "[interface]\nmouse=true\nuser_list=false\nhistory_lines=100\n" %
                                   ("saved-must-not-resolve.invalid" if offline else "127.0.0.1",
                                    6697 if offline else peer.port, "true" if offline else "false",
                                    profile_nick, profile_channel, "true" if autoconnect else "false"),
                                   encoding="ascii")
                profile.chmod(0o600)
                entries.append("profile=" + profile.name)
                self.profile_paths.append(profile)
            catalog.write_text("[servers]\n" + "\n".join(entries) + "\n", encoding="ascii")
            catalog.chmod(0o600)
        self.before = snapshot(self.directory)
        self.master, slave = pty.openpty()
        self.term_before = termios.tcgetattr(slave)
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.screen = Screen(rows, cols)
        self.raw = bytearray()
        args = [str(BINARY), "--config", str(self.config)]
        if offline:
            args.append("--test-mode")
        process_env = dict(os.environ, TERM="xterm-256color", LANG="C.UTF-8", LC_ALL="C.UTF-8",
                           HOME=str(self.home), XDG_CONFIG_HOME=str(self.directory / "xdg-config"),
                           XDG_DATA_HOME=str(self.data_home))

        def controlling_terminal():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        try:
            self.proc = subprocess.Popen(args, stdin=slave, stdout=slave, stderr=slave,
                                         cwd=ROOT if launch_cwd is None else launch_cwd,
                                         env=process_env, preexec_fn=controlling_terminal)
        finally:
            os.close(slave)
        os.set_blocking(self.master, False)
        self.started = False
        self.next_slot = len(profiles) + 1

    def wait(self, predicate, label, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.pump()
            if predicate():
                return
            if self.proc.poll() is not None:
                break
        traces = [peer.lines[-10:] for peer in self.peers]
        raise AssertionError("%s\nexit=%r\nscreen:\n%s\nrecent output:%r\nIRC:%r" %
                             (label, self.proc.poll(), self.screen.text(), bytes(self.raw[-1600:]), traces))

    def start(self):
        self.wait(lambda: b"Not vegetarian?" in self.raw, "welcome missing")
        self.type("x")
        if self.offline:
            self.wait(lambda: "[OFFLINE]" in self.screen.text() and self.primary_channel in
                      self.screen.text(), "primary offline simulation did not start")
        else:
            self.wait(lambda: self.server.registered, "primary IRC registration failed")
            self.wait(lambda: any(line.startswith(("JOIN " + self.primary_channel).encode())
                                 for unused, line in self.server.lines), "primary channel did not join")
        self.quiet()
        self.started = True
        assert self.mode() == "INPUT"

    def add_server(self, nick="secondnick", channel="#test", connect=True, autoconnect=False):
        peer = None if self.offline else IRCFixture()
        if peer is not None:
            self.peers.append(peer)
        slot = self.next_slot
        self.next_slot += 1
        begin_new(self)
        edit_field(self, 0, "second-must-not-resolve.invalid" if self.offline else "127.0.0.1")
        edit_field(self, 1, str(6697 if self.offline else peer.port))
        set_boolean(self, 2, "Transport", self.offline)
        edit_field(self, 3, nick)
        edit_field(self, 4, channel)
        set_boolean(self, 5, "Auto-connect", autoconnect)
        finish_new(self, connect)
        if connect:
            if self.offline:
                self.wait(lambda: "[OFFLINE]" in self.screen.text() and channel in self.screen.text(),
                          "secondary offline simulation did not start")
                self.open_app_diagnostics()
                assert "Nick: " + nick in self.app_line("Nick:"), "secondary offline identity was lost"
                self.close_app_diagnostics()
            else:
                self.wait(lambda: peer.registered and peer.nick == nick, "secondary registration failed")
                self.wait(lambda: any(line.startswith(("JOIN " + channel).encode())
                                     for unused, line in peer.lines), "secondary channel did not join")
            self.quiet()
        return slot, peer

    def select_server(self, slot):
        select_server_row(self, slot)
        choose_action(self, "Open", SERVER_ACTIONS)
        self.wait(lambda: has_input(self) and "Channel 0: Server" in self.panel_title(),
                  "selected server did not open its channel zero message view")
        assert self.mode() == "INPUT"

    def close(self):
        forced = False
        interrupted = self.proc.poll() is None
        if interrupted:
            self.type(b"\x03")
            try:
                self.proc.wait(timeout=4)
            except subprocess.TimeoutExpired:
                forced = True
                self.proc.terminate()
                try:
                    self.proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait(timeout=2)
        self.pump(0)
        restored = termios.tcgetattr(self.master)
        os.close(self.master)
        try:
            for peer in self.peers:
                peer.close()
            assert not forced, "Ctrl-C did not exit the multi-server interface"
            assert not self.started or (self.proc.returncode == 130 if interrupted else
                                        self.proc.returncode in (0, 130)), self.proc.returncode
            mask = termios.ISIG | termios.ICANON | termios.ECHO
            assert restored[3] & mask == self.term_before[3] & mask, "terminal flags were not restored"
            if self.offline:
                assert snapshot(self.directory) == self.before, "offline workspace created or changed files"
        finally:
            self.temp.cleanup()

    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        self.close()


def row_activity(session, needle):
    for cells in session.screen.cells[4:-2]:
        text = "".join(cells)
        if needle in text:
            match = re.search(r"\bnew (\d+)\+? !(\d+)\+?\b", text)
            if match:
                return int(match[1]), int(match[2])
    return None


def wait_server_activity(session, slot, expected):
    session.wait(lambda: slot in server_rows(session) and
                 counts(server_rows(session)[slot]) == expected,
                 "server %d activity should be %r" % (slot, expected))


def assert_draft(session, expected):
    session.wait(lambda: expected in session.input_text(), "draft changed: " + expected)
    assert re.search(r"\b%d bytes \|" % len(expected.encode("utf-8")), session.screen.text()), (
        expected, session.screen.text())


def live_activity_and_drafts():
    with MultiServerSession() as session:
        session.start()
        slot, second = session.add_server(nick="secondnick")
        assert slot == 1 and second.nick == "secondnick" and session.server.nick == "tester"
        for index in (0, slot):
            session.select_server(index)
            session.command("/server")
            session.command("/channel #test")
        session.select_server(0)
        session.command("/server")
        open_servers(session)

        first = session.server
        first.send(":alice!u@localhost PRIVMSG #test :ALPHA_CHANNEL_ONLY")
        first.send(":alice!u@localhost PRIVMSG #test :tester ALPHA_MENTION_ONLY")
        first.send(":alice!u@localhost PRIVMSG tester :tester ALPHA_QUERY_ONLY")
        second.send(":alice!u@localhost PRIVMSG #test :BETA_CHANNEL_ONLY")
        second.send(":alice!u@localhost PRIVMSG #test :secondnick BETA_MENTION_ONLY")
        second.send(":alice!u@localhost PRIVMSG secondnick :secondnick BETA_QUERY_ONLY")
        first.send("PING :background-alpha")
        second.send("PING :background-beta")
        session.wait(lambda: any(b"background-alpha" in line and line.startswith(b"PONG")
                                 for unused, line in first.lines) and
                     any(b"background-beta" in line and line.startswith(b"PONG")
                         for unused, line in second.lines), "a background server was not serviced")
        wait_server_activity(session, 0, (3, 2))
        wait_server_activity(session, slot, (3, 2))
        session.focus_destination("Channels")
        session.wait(lambda: "Channels" in session.panel_title() and session.header_focus() is None,
                     "function-key navigation did not open the channel body")
        assert "Channels" in session.panel_title()
        session.type(LEFT)
        session.wait(lambda: "Channels" in session.panel_title() and
                     focused_action(session, CHANNEL_ACTIONS) == "Back",
                     "channel-row Left did not select its Back action")
        session.focus_destination("Servers")
        session.wait(lambda: "Servers" in session.panel_title() and session.header_focus() is None,
                     "F2 did not return to Servers")
        wait_server_activity(session, slot, (3, 2))

        # Activity must remain visible before long connection details.
        session.resize(12, 20)
        session.wait(lambda: "new 3 !2" in session.screen.text(),
                     "narrow server activity indicator did not finish drawing")
        assert "new 3 !2" in session.screen.text(), session.screen.text()
        session.resize(26, 132)
        session.type(b"\x1b")
        session.wait(lambda: has_input(session), "server picker Escape failed")
        session.navigate("c")
        session.wait(lambda: "Channels" in header(session) and "127.0.0.1" in header(session),
                     "channel picker title did not finish drawing")
        assert "127.0.0.1" in header(session), "channel picker omitted its server"
        session.wait(lambda: row_activity(session, "#test") == (2, 1), "channel activity was lost")
        session.type(HOME + DOWN + b"\r")
        session.wait(lambda: has_input(session) and "ALPHA_CHANNEL_ONLY" in session.screen.text(), "first channel did not open")
        assert "BETA_CHANNEL_ONLY" not in session.screen.text(), "same-name channels were merged"
        session.navigate("q")
        session.wait(lambda: "Query list" in header(session) and "127.0.0.1" in header(session),
                     "query picker title did not finish drawing")
        assert "127.0.0.1" in header(session), "query picker omitted its server"
        session.wait(lambda: row_activity(session, "alice") == (1, 1), "query activity was lost")
        session.type(b"\r")
        session.wait(lambda: has_input(session) and "ALPHA_QUERY_ONLY" in session.screen.text(), "first query did not open")
        assert "BETA_QUERY_ONLY" not in session.screen.text(), "same-name queries were merged"
        open_servers(session)
        wait_server_activity(session, 0, (0, 0))
        wait_server_activity(session, slot, (3, 2))

        session.select_server(slot)
        session.command("/channel #test")
        session.wait(lambda: "BETA_CHANNEL_ONLY" in session.screen.text(), "second channel history missing")
        assert "ALPHA_CHANNEL_ONLY" not in session.screen.text()
        session.navigate("q")
        session.wait(lambda: row_activity(session, "alice") == (1, 1), "second query activity missing")
        session.type(b"\r")
        session.wait(lambda: has_input(session) and "BETA_QUERY_ONLY" in session.screen.text(), "second query history missing")
        assert "ALPHA_QUERY_ONLY" not in session.screen.text()
        session.navigate("q")  # The selected query is covered, not visible.
        session.wait(lambda: "Query list" in header(session) and row_activity(session, "alice") == (0, 0),
                     "query picker did not finish covering the current query")
        second.send(":alice!u@localhost PRIVMSG secondnick :secondnick COVERED_QUERY")
        session.wait(lambda: row_activity(session, "alice") == (1, 1),
                     "covered current query was incorrectly marked read")
        session.type(b"\x1b")
        session.wait(lambda: has_input(session) and "COVERED_QUERY" in session.screen.text(), "covered query did not return")
        session.command("/mentions")
        session.wait(lambda: "BETA_MENTION_ONLY" in session.screen.text(), "second mentions missing")
        assert "ALPHA_MENTION_ONLY" not in session.screen.text(), "mentions crossed server identities"

        session.select_server(0)
        session.command("/channel #test")
        session.type("ALPHA_DRAFT")
        session.type(HOME + RIGHT * 3)
        session.select_server(slot)
        session.command("/channel #test")
        session.type("BETA_DRAFT_LONG")
        session.type(HOME + RIGHT * 5)
        session.select_server(0)
        session.command("/channel #test")
        session.input_mode()
        session.type("@")
        assert_draft(session, "ALP@HA_DRAFT")
        session.select_server(slot)
        session.command("/channel #test")
        session.input_mode()
        session.type("#")
        assert_draft(session, "BETA_#DRAFT_LONG")
        session.clear_input()
        session.command("SENT_ONLY_SECOND")
        session.wait(lambda: any(text == b"SENT_ONLY_SECOND" for unused, text in second.messages()),
                     "second-server send was not routed")
        assert not any(text == b"SENT_ONLY_SECOND" for unused, text in first.messages())
        session.select_server(0)
        session.command("/channel #test")
        assert_draft(session, "ALP@HA_DRAFT")
        session.clear_input()
        session.command("SENT_ONLY_FIRST")
        session.wait(lambda: any(text == b"SENT_ONLY_FIRST" for unused, text in first.messages()),
                     "first-server send was not routed")
        assert not any(text == b"SENT_ONLY_FIRST" for unused, text in second.messages())
        session.command("/servers")
        session.wait(lambda: in_servers(session), "/servers did not share the picker")
        session.select_server(slot)
        session.type("hk")
        session.quiet(0.04)
        assert session.mode() == "INPUT" and "hk" in session.input_text(), "main hjkl did not edit the second server draft"
        assert "original" not in header(session) and "vim" not in header(session)
        session.select_server(0)
        session.type("hl")
        session.quiet(0.04)
        assert session.mode() == "INPUT" and "hl" in session.input_text(), "main hjkl did not edit the first server draft"
        assert "original" not in header(session) and "vim" not in header(session)
    print("servers live: isolated names/nicks/views/mentions, background polling, activity/read state, narrow counters, drafts/carets, routing and ordinary main typing passed")


def live_profile_and_close():
    with MultiServerSession() as session:
        session.start()
        original = session.config.read_bytes()
        before = snapshot(session.directory)
        slot, second = session.add_server(nick="profile_second", connect=False, autoconnect=True)
        session.quiet(0.1)
        assert second.socket is None, "Add connected despite being the disconnected action"
        assert snapshot(session.directory) == before, "New/Add implicitly saved configuration"
        select_server_row(session, slot)
        assert "unsaved" in server_rows(session)[slot]
        choose_action(session, "Connect", SERVER_ACTIONS)
        session.wait(lambda: second.registered, "Connect did not register the added server")
        session.select_server(slot)
        session.command("/save")
        session.wait(lambda: f"Saved server {slot}." in session.screen.text(),
                     "secondary save did not confirm the selected profile")
        catalog = Path(str(session.config) + ".servers")
        profile_directory = Path(str(session.config) + ".d")
        entries = re.findall(r"^profile=(server-[0-9a-f]{24}\.ini)$", catalog.read_text(), re.M)
        assert len(entries) == 1, catalog.read_text()
        profile = profile_directory / entries[0]
        parsed = configparser.ConfigParser(strict=False)
        parsed.read(profile)
        assert parsed.get("connection", "nick") == "profile_second"
        assert parsed.getint("connection", "port") == second.port
        assert parsed.get("connection", "auth") == "none"
        assert parsed.get("connection", "password_file") == ""
        assert parsed.get("connection", "server_password_file") == ""
        assert not parsed.has_option("mentions", "word"), "watch strings were inherited into a new server"
        assert session.config.read_bytes() == original, "secondary save overwrote primary configuration"
        assert stat.S_IMODE(profile.stat().st_mode) == 0o600
        assert stat.S_IMODE(profile_directory.stat().st_mode) == 0o700
        assert stat.S_IMODE(catalog.stat().st_mode) == 0o600
        session.command("/nick renamed_second")
        session.wait(lambda: second.nick == "renamed_second", "secondary nick change failed")
        session.command("/save")
        session.wait(lambda: "nick=renamed_second\n" in profile.read_text(), "secondary update used the wrong file")
        saved = profile.read_bytes()
        assert session.config.read_bytes() == original
        assert re.findall(r"^profile=", catalog.read_text(), re.M) == ["profile="]

        session.type("SECOND_DRAFT_BEFORE_CLOSE")
        select_server_row(session, slot)
        choose_action(session, "Disconnect", SERVER_ACTIONS)
        session.wait(lambda: "disconnected" in server_rows(session)[slot], "Disconnect state missing")
        first = session.server
        first.send("PING :survives-other-disconnect")
        session.wait(lambda: any(b"survives-other-disconnect" in line and line.startswith(b"PONG")
                                 for unused, line in first.lines), "Disconnect affected the other server")
        choose_action(session, "Open", SERVER_ACTIONS)
        session.wait(lambda: has_input(session) and "Channel 0: Server" in session.panel_title(),
                     "disconnected server did not reopen its message view")
        assert_draft(session, "SECOND_DRAFT_BEFORE_CLOSE")

        select_server_row(session, slot)
        choose_action(session, "Close", SERVER_ACTIONS)
        session.wait(lambda: "[y/N]" in session.screen.text(), "server Close did not confirm")
        session.quiet(0.12)
        session.type(b"\t")
        session.quiet(0.03)
        assert "[y/N]" in session.screen.text(), "Tab approved server Close"
        session.type(b"\r")
        session.wait(lambda: "[y/N]" not in session.screen.text(), "default-No did not cancel Close")
        assert slot in server_rows(session) and profile.read_bytes() == saved
        choose_action(session, "Close", SERVER_ACTIONS)
        session.wait(lambda: "[y/N]" in session.screen.text(), "second Close prompt missing")
        session.quiet(0.12)
        session.type("y")
        session.wait(lambda: has_input(session) and not in_servers(session), "confirmed Close did not return to primary")
        assert profile.read_bytes() == saved, "Close deleted or rewrote the saved profile"
        assert entries[0] not in (catalog.read_text() if catalog.exists() else ""), "Close kept its startup reference"
        assert session.config.read_bytes() == original
        open_servers(session)
        assert list(server_rows(session)) == [0]
        choose_action(session, "Close", SERVER_ACTIONS)
        session.wait(lambda: "primary entry" in session.screen.text(), "primary Close was not refused")
        assert "[y/N]" not in session.screen.text() and list(server_rows(session)) == [0]
    print("servers persistence: Add/no implicit connect/save, explicit Connect, private isolated profiles, update identity, retained disconnect state, default-No Close and kept profile passed")


def offline_servers():
    with MultiServerSession(offline=True) as session:
        session.start()
        begin_new(session)
        session.type(b"\t")
        session.wait(lambda: any("[Port:" in "".join(row) for row in session.screen.cells),
                     "New-server Tab did not move to its next field")
        assert in_new(session) and session.header_focus() is None
        edit_field(session, 0, "second-must-not-resolve.invalid")
        edit_field(session, 3, "offline_second")
        edit_field(session, 4, "#test")
        show_advanced(session)
        assert "Auto-authentication: none" in session.screen.text()
        assert "Password file: (empty)" in session.screen.text()
        select_field(session, 13)
        assert "Server password file: (empty)" in session.screen.text()
        edit_field(session, 4, "second_user")
        set_auth_sasl(session)
        edit_field(session, 9, "second-no-real-account")
        edit_field(session, 10, str(session.secret))
        edit_field(session, 13, str(session.secret))
        edit_field(session, 16, str(session.secret))
        finish_new(session, True)
        session.wait(lambda: "#test" in session.screen.text() and "[OFFLINE]" in session.screen.text(),
                     "second simulation did not start without DNS/credential/CA access")
        session.open_app_diagnostics()
        assert "Nick: offline_second" in session.app_line("Nick:")
        session.close_app_diagnostics()
        assert session.peers == [] and session.server is None
        session.input_mode()
        session.type("SECOND_OFFLINE_DRAFT")
        session.select_server(0)
        session.input_mode()
        session.type("FIRST_OFFLINE_DRAFT")
        session.select_server(1)
        session.command("/channel #test")
        assert_draft(session, "SECOND_OFFLINE_DRAFT")
        session.clear_input()
        session.command("/save", error="disabled")
        session.wait(lambda: "disabled" in session.screen.text().lower(), "offline secondary Save was not blocked")
        session.clear_input()
        open_servers(session)
        session.resize(12, 20)
        session.wait(lambda: "[OFFLINE]" in header(session),
                     "narrow Servers picker hid offline mode after resize")
        assert "[OFFLINE]" in header(session), "narrow Servers picker hid offline mode"
        session.resize(26, 132)
        select_server_row(session, 1)
        choose_action(session, "Close", SERVER_ACTIONS)
        session.wait(lambda: "[y/N]" in session.screen.text(), "offline Close prompt missing")
        session.quiet(0.12)
        session.type("y")
        session.wait(lambda: has_input(session) and not in_servers(session), "offline secondary did not close")
        assert_draft(session, "FIRST_OFFLINE_DRAFT")
        assert snapshot(session.directory) == session.before, "offline multi-server flow wrote files"
    print("servers offline: fresh auth defaults, Advanced credentials without I/O, independent simulations/drafts, narrow marker, blocked Save, and no filesystem changes passed")


def saved_profiles_autoconnect():
    profiles = (("saved_alpha", "#alpha", True), ("saved_beta", "#beta", True),
                ("saved_idle", "#idle", False))
    with MultiServerSession(profiles=profiles) as session:
        session.start()
        for peer, (nick, channel, enabled) in zip(session.profile_peers, profiles):
            if enabled:
                session.wait(lambda peer=peer: peer.registered,
                             "enabled saved server did not autoconnect")
                session.wait(lambda peer=peer, channel=channel:
                             any(line == ("JOIN " + channel).encode() for _, line in peer.lines),
                             "saved server's own autojoin was lost")
                assert peer.nick == nick
            else:
                session.quiet(0.1)
                assert peer.socket is None, "disabled saved server autoconnected"
        session.open_app_diagnostics()
        assert "Nick: tester" in session.app_line("Nick:"), "startup background profiles changed the active chat"
        session.close_app_diagnostics()
        open_servers(session)
        assert set(server_rows(session)) == {0, 1, 2, 3}
        for index, peer in enumerate([session.server] + session.profile_peers[:2]):
            peer.send("PING :multi-startup-%d" % index)
        session.wait(lambda: all(any(line == ("PONG :multi-startup-%d" % index).encode()
                                          for _, line in peer.lines)
                                 for index, peer in enumerate([session.server] + session.profile_peers[:2])),
                     "one of the automatically connected servers stopped responding")
        assert snapshot(session.directory) == session.before, "startup changed saved profiles"
    print("saved profiles: three parallel startup connections use separate nicks/channels; disabled fourth stays disconnected; active chat/configs preserved")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--offline-only", action="store_true")
    selection.add_argument("--live-only", action="store_true")
    args = parser.parse_args()
    if not args.live_only:
        offline_servers()
    if not args.offline_only:
        saved_profiles_autoconnect()
        live_activity_and_drafts()
        live_profile_and_close()


if __name__ == "__main__":
    main()
