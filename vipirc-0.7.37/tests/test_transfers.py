#!/usr/bin/env python3
"""Real terminal DCC controls, using only local IRC and file-transfer peers."""
import argparse
import os
from pathlib import Path
import re
import select
import socket
import stat
import struct
import tempfile
import time

from test_offline import OfflineTerminal
from test_input_limits import BEGIN_PASTE, END_PASTE, mouse_click


HOME, DOWN, RIGHT = b"\x1bOH", b"\x1bOB", b"\x1bOC"


def in_transfers(session):
    return re.fullmatch(r"(?:\[OFFLINE\] )?(?:File transfers|Receive file|Send file|Choose file)",
                        session.panel_title()) is not None


def open_transfers(session, command="/dcc"):
    session.command(command)
    session.wait(lambda: in_transfers(session), "file-transfer list did not open")
    assert session.mode() == "PANEL"


def back_to_chat(session):
    session.type(b"\x1b")
    session.wait(lambda: not in_transfers(session) and session.mode() == "INPUT" and session.screen.cursor_visible,
                 "Escape did not return from transfers to chat")
    assert session.mode() == "INPUT"


def action(session, label):
    # Actions stay next to their heading or review warning, even in a tall tty.
    def find():
        for row in (3, 4):
            if re.search(r"(?<![A-Za-z])" + re.escape(label) + r"(?![A-Za-z])", "".join(session.screen.cells[row])):
                return row
        return None
    session.wait(lambda: find() is not None, "near-heading action missing: " + label)
    row = find()
    assert row < session.screen.rows - 3, "DCC actions remained at the bottom"
    line = "".join(session.screen.cells[row])
    mouse_click(session, row, line.index(label) + max(0, len(label) // 2))


def edit_field(session, index, label, value):
    row = 5 + index
    session.wait(lambda: label + ":" in "".join(session.screen.cells[row]), "form field missing: " + label)
    mouse_click(session, row, 4)
    if label == "Source file":
        session.wait(lambda: session.panel_title() == "Choose file", "Source file did not open its browser")
        action(session, "Type path")
    session.wait(lambda: "Edit: " + label in session.screen.text(), "field editor missing: " + label)
    session.type(value if value else b"\x7f")
    session.type(b"\r")
    session.wait(lambda: "Edit: " not in session.screen.text(), "Enter did not stage " + label)


def select_transfer(session, filename):
    def find():
        for index, row in enumerate(session.screen.cells[4:-3]):
            line = "".join(row)
            if re.match(r"\s*\[?#\d+ ", line) and filename in line:
                return index
        return None
    session.wait(lambda: find() is not None, "transfer row missing: " + filename)
    session.type(HOME + DOWN * find())
    session.quiet(0.04)


def transfer_state(session, filename, state):
    return any(filename in "".join(row) and "| " + state + " |" in "".join(row)
               for row in session.screen.cells[4:-3])


def prepare_receive(session, filename, directory, save_as):
    select_transfer(session, filename)
    action(session, "Accept")
    session.wait(lambda: "Receive file" in session.screen.text(), "Accept did not open its review form")
    edit_field(session, 0, "Directory", str(directory))
    edit_field(session, 1, "Save as", save_as)


def check_receive_review_size(session, peer, directory, byte_count):
    original_size = session.screen.rows, session.screen.cols
    session.type(RIGHT)  # The last field edit left Edit selected; focus Accept.
    for rows, cols in ((11, 20), (12, 19)):
        session.resize(rows, cols)
        session.wait(lambda: "Resize;" in session.screen.text(), "undersized transfer review was not blocked")
        session.type(b"\r")
        session.quiet(0.08)
        peer.assert_unopened()
        assert not directory.exists(), "Accept in an undersized review created a destination"

    session.resize(12, 20)
    session.wait(lambda: "Receive file" in session.screen.text(), "minimum-size receive review did not return")
    lines = ["".join(row).rstrip() for row in session.screen.cells]
    assert lines[7] == "Peer 127.0.0.1", session.screen.text()
    assert lines[8] == "%d bytes" % byte_count, session.screen.text()
    assert lines[3] == "Unencrypted; IP sent", session.screen.text()
    assert lines[9].startswith("Port %d |" % peer.port), session.screen.text()
    assert "Accept" in lines[4], "minimum-size review lost its nearby Accept action"
    peer.assert_unopened()
    assert not directory.exists(), "resizing the review implicitly accepted an offer"
    session.resize(*original_size)
    session.wait(lambda: "Receive file" in session.screen.text(), "full-size receive review did not return")


def offer_payload(peer, nick, filename, port, size):
    quoted = '"' + filename + '"' if " " in filename else filename
    peer.send(":%s!user@localhost PRIVMSG %s :\x01DCC SEND %s 2130706433 %d %d\x01" %
              (nick, peer.nick, quoted, port, size))


class IncomingPeer:
    def __init__(self):
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.port = self.listener.getsockname()[1]
        self.connection = None
        self.pending = bytearray()
        self.acknowledged = 0

    def assert_unopened(self):
        assert not select.select([self.listener], [], [], 0)[0], "DCC offer connected before explicit Accept"

    def accept(self, session):
        session.wait(lambda: bool(select.select([self.listener], [], [], 0)[0]),
                     "explicit Accept did not connect to its loopback peer")
        self.connection, _ = self.listener.accept()
        self.connection.settimeout(2)

    def send(self, data):
        assert self.connection is not None
        self.connection.sendall(data)

    def wait_ack(self, session, count):
        deadline = time.monotonic() + 5
        while self.acknowledged < count and time.monotonic() < deadline:
            session.pump(0.01)
            if not select.select([self.connection], [], [], 0)[0]:
                continue
            data = self.connection.recv(4096)
            assert data, "receiver disconnected before acknowledging accepted bytes"
            self.pending.extend(data)
            while len(self.pending) >= 4:
                value = struct.unpack("!I", self.pending[:4])[0]
                del self.pending[:4]
                assert self.acknowledged <= value <= count, "invalid cumulative DCC acknowledgment"
                self.acknowledged = value
        assert self.acknowledged == count, (self.acknowledged, count)

    def wait_closed(self, session):
        session.wait(lambda: bool(select.select([self.connection], [], [], 0)[0]),
                     "cancelled peer socket remained open")
        assert self.connection.recv(4096) == b"", "cancel left unexpected outgoing peer data"

    def close(self):
        if self.connection is not None:
            self.connection.close()
        self.listener.close()


def offline_transfer_forms():
    with OfflineTerminal() as session:
        session.start()
        open_transfers(session)
        session.wait(lambda: "preview.txt" in session.screen.text(), "offline sample offer missing")
        session.type("\t")
        session.quiet(0.04)
        assert session.header_focus() is None, "transfer-list Tab unexpectedly entered header navigation"
        assert in_transfers(session)
        session.type(BEGIN_PASTE + b"/quit\r\n\x1b" + END_PASTE)
        session.quiet(0.04)
        assert in_transfers(session) and session.proc.poll() is None
        destination = session.directory / "must-not-create-downloads"
        prepare_receive(session, "preview.txt", destination, "offline.bin")
        session.type("\t")
        session.quiet(0.04)
        assert session.header_focus() is None, "receive-form Tab unexpectedly entered header navigation"
        assert in_transfers(session)
        action(session, "Accept")
        session.wait(lambda: "OFFLINE TEST MODE" in "".join(session.screen.cells[-1]) and
                     "disabled" in "".join(session.screen.cells[-1]), "offline Accept did not refuse before I/O")
        assert not destination.exists()
        action(session, "Cancel")
        action(session, "Send...")
        session.wait(lambda: "Send file" in session.screen.text(), "Send did not open a form")
        edit_field(session, 0, "Nickname", "Peer")
        edit_field(session, 1, "Source file", str(session.directory / "secret-must-not-be-read"))
        edit_field(session, 2, "Advertised IPv4", "127.0.0.1")
        session.type(RIGHT + b"\r")  # Edit -> Offer, explicit keyboard activation.
        session.wait(lambda: "OFFLINE TEST MODE" in "".join(session.screen.cells[-1]) and
                     "disabled" in "".join(session.screen.cells[-1]), "offline Offer accessed its FIFO source")
        assert "Send file" in session.screen.text() and not destination.exists()
        back_to_chat(session)
        open_transfers(session, "/transfers")
        action(session, "Back")
        session.wait(lambda: not in_transfers(session), "transfer-list Back did not return to chat")
    print("transfers offline: local offer review, Tab/paste isolation, explicit Back, and receive/send refusal before path access passed")


def outgoing_binary(session, peer, other_peer, source, payload):
    open_transfers(session)
    action(session, "Send...")
    session.wait(lambda: "Send file" in session.screen.text(), "outgoing send form missing")
    edit_field(session, 0, "Nickname", "FilePeer")
    edit_field(session, 1, "Source file", str(source))
    edit_field(session, 2, "Advertised IPv4", "127.0.0.1")
    edit_field(session, 3, "Listen port", "0")
    action(session, "Offer")
    session.wait(lambda: any(b"\x01DCC SEND " in line for _, line in peer.lines),
                 "outgoing offer was not sent on its originating IRC server")
    offers = [line for _, line in peer.lines if b"\x01DCC SEND " in line]
    assert len(offers) == 1 and not any(b"\x01DCC SEND " in line for _, line in other_peer.lines), (
        "DCC offer crossed server sessions", offers, other_peer.lines)
    assert offers[0].startswith(b"PRIVMSG FilePeer :\x01DCC SEND ")
    body = offers[0].split(b"\x01", 2)[1].decode("ascii")
    match = re.fullmatch(r'DCC SEND "([^\"]+)" (\d+) (\d+) (\d+)', body)
    assert match and match[1] == source.name and int(match[2]) == 2130706433 and int(match[4]) == len(payload), body
    result = bytearray()
    with socket.create_connection(("127.0.0.1", int(match[3])), timeout=2) as receiver:
        receiver.setblocking(False)
        deadline = time.monotonic() + 5
        while not result and time.monotonic() < deadline:
            session.pump(0.01)
            if select.select([receiver], [], [], 0)[0]: result.extend(receiver.recv(65536))
        assert result, "DCC sender did not produce file bytes"
        peer.send("PING :dcc-active-second")
        other_peer.send("PING :dcc-active-primary")
        session.wait(lambda: any(line == b"PONG :dcc-active-second" for _, line in peer.lines) and
                     any(line == b"PONG :dcc-active-primary" for _, line in other_peer.lines),
                     "a blocked transfer stalled IRC PING processing on another server", timeout=3)
        acknowledged = struct.pack("!I", len(result))
        receiver.sendall(acknowledged[:1])
        session.quiet(0.02)
        receiver.sendall(acknowledged[1:])
        deadline = time.monotonic() + 8
        while len(result) < len(payload) and time.monotonic() < deadline:
            session.pump(0.01)
            if not select.select([receiver], [], [], 0)[0]: continue
            data = receiver.recv(65536)
            assert data, "DCC sender closed before all selected bytes were transferred"
            result.extend(data)
            receiver.sendall(struct.pack("!I", len(result)))
        assert bytes(result) == payload, "outgoing DCC bytes differ from the selected file"
        session.wait(lambda: transfer_state(session, source.name, "complete"), "outgoing transfer did not complete after final ACK")
    assert source.read_bytes() == payload
    back_to_chat(session)


def cancel_queued_offer(session, peer, source):
    source.write_bytes(b"This selected file must never be advertised after cancellation.")
    source.chmod(0o600)
    for i in range(4):
        session.command("/msg QueuePeer queued-before-dcc-%d" % i)
    session.wait(lambda: len(peer.messages("QueuePeer")) >= 1, "pacing fixture did not send its first local chat line")
    open_transfers(session)
    action(session, "Send...")
    session.wait(lambda: "Send file" in session.screen.text(), "queued-offer form missing")
    edit_field(session, 0, "Nickname", "FilePeer")
    edit_field(session, 1, "Source file", str(source))
    edit_field(session, 2, "Advertised IPv4", "127.0.0.1")
    action(session, "Offer")
    session.wait(lambda: transfer_state(session, source.name, "waiting for peer"), "file offer was not queued")

    def was_advertised():
        return any(b"\x01DCC SEND " in line and source.name.encode("ascii") in line for _, line in peer.lines)

    assert not was_advertised(), "test did not retain a pacing window before cancellation"
    action(session, "Cancel transfer")
    session.wait(lambda: transfer_state(session, source.name, "cancelled"), "queued transfer did not cancel")
    session.wait(lambda: len(peer.messages("QueuePeer")) == 4, "earlier chat queue did not drain", timeout=8)
    session.quiet(1.4)  # The cancelled offer's next eligible send interval.
    assert not was_advertised(), "cancelled DCC offer remained queued and advertised a dead listener"
    back_to_chat(session)


def browser_row(session, name):
    for row in range(5, session.screen.rows - 2):
        line = "".join(session.screen.cells[row]).rstrip()
        if line in (name, "[" + name + "]"):
            return row
    return None


def wait_browser(session, directory, entries=()):
    def complete():
        footer = "".join(session.screen.cells[-2]).strip()
        return re.fullmatch(r"\d+ entries(?:; \d+ unsupported entries hidden)?", footer) or \
            footer.startswith("Listing limit reached;")
    session.wait(lambda: session.panel_title() == "Choose file" and
                 "Directory: " + str(directory) in "".join(session.screen.cells[3]) and
                 complete() and all(browser_row(session, entry) is not None for entry in entries),
                 "file browser did not finish in " + str(directory))


def open_source_browser(session, directory, entries=()):
    session.wait(lambda: session.panel_title() == "Send file", "source form missing")
    mouse_click(session, 6, 4)
    wait_browser(session, directory, entries)


def finish_browsed_send(session, source, payload, previous_offers):
    def offers():
        return [line for _, line in session.server.lines if b"\x01DCC SEND " in line]

    assert len(offers()) == previous_offers, "selecting a file implicitly sent a DCC offer"
    action(session, "Offer")
    session.wait(lambda: len(offers()) == previous_offers + 1,
                 "explicit Offer did not advertise the staged file")
    body = offers()[-1].split(b"\x01", 2)[1].decode("utf-8")
    match = re.fullmatch(r'DCC SEND (?:"([^"]+)"|(\S+)) (\d+) (\d+) (\d+)', body)
    assert match and (match[1] or match[2]) == source.name, body
    assert int(match[3]) == 2130706433 and int(match[5]) == len(payload), body
    result = bytearray()
    with socket.create_connection(("127.0.0.1", int(match[4])), timeout=2) as receiver:
        receiver.setblocking(False)
        deadline = time.monotonic() + 5
        while len(result) < len(payload) and time.monotonic() < deadline:
            session.pump(0.01)
            if not select.select([receiver], [], [], 0)[0]:
                continue
            data = receiver.recv(65536)
            assert data, "DCC sender closed before the staged file was sent"
            result.extend(data)
            receiver.sendall(struct.pack("!I", len(result)))
        assert bytes(result) == payload, "file picker or relative path selected the wrong file"
        session.wait(lambda: transfer_state(session, source.name, "complete"),
                     "selected file did not complete after its final ACK")
    back_to_chat(session)


def live_file_browser():
    from test_servers import MultiServerSession

    with tempfile.TemporaryDirectory(prefix="vipirc-picker-ui-") as temporary:
        launch = Path(temporary)
        child = launch / "choose-here"
        child.mkdir()
        chosen = child / "chosen caf\u00e9 file.bin"
        payload = bytes(range(256)) * 31
        chosen.write_bytes(payload)
        relative = launch / "launch-only.bin"
        relative_payload = b"This file is relative to the process launch directory.\n"
        relative.write_bytes(relative_payload)
        many = launch / "many"
        many.mkdir()
        for index in range(540):
            (many / ("entry-%04d.txt" % index)).write_bytes(b"bounded")
        os.symlink(chosen, launch / "symlink-file")
        os.symlink(child, launch / "symlink-dir")
        os.mkfifo(launch / "not-a-file")
        (launch / "bad\nname").write_bytes(b"not displayed")
        with MultiServerSession(launch_cwd=launch) as session:
            # A different file with the same basename catches accidental HOME resolution.
            (session.home / relative.name).write_bytes(b"wrong directory")
            session.start()
            open_transfers(session)
            action(session, "Send...")
            edit_field(session, 0, "Nickname", "FilePeer")
            edit_field(session, 1, "Source file", relative.name)
            edit_field(session, 2, "Advertised IPv4", "127.0.0.1")
            edit_field(session, 3, "Listen port", "0")

            open_source_browser(session, launch, ("choose-here/", relative.name))
            assert browser_row(session, "choose-here/") is not None, session.screen.text()
            assert browser_row(session, relative.name) is not None, session.screen.text()
            assert "symlink-file" not in session.screen.text() and "symlink-dir" not in session.screen.text()
            assert "not-a-file" not in session.screen.text() and "bad" not in session.screen.text()
            session.type(BEGIN_PASTE + b"/quit\r\n\x1b" + END_PASTE)
            session.quiet(0.04)
            assert session.panel_title() == "Choose file" and session.proc.poll() is None
            row = browser_row(session, "choose-here/")
            session.type(HOME + b"j" * (row - 5) + b"\r")
            wait_browser(session, child, (chosen.name,))
            session.type(b"\x1b")
            session.wait(lambda: session.panel_title() == "Send file" and
                         "Source file: " + relative.name in session.screen.text(),
                         "browser Esc did not restore its form with the prior source path")
            assert "Source file: " + relative.name in session.screen.text(), "Esc discarded the prior source path"

            open_source_browser(session, launch, ("choose-here/", relative.name))
            mouse_click(session, browser_row(session, "many/"), 2)
            session.wait(lambda: str(many) in "".join(session.screen.cells[3]), "directory click did not open it")
            ping_at = time.monotonic()
            session.server.send("PING :file-browser-responsive")
            session.wait(lambda: any(line == b"PONG :file-browser-responsive" for _, line in session.server.lines),
                         "directory scanning stalled IRC PING", timeout=2)
            assert time.monotonic() - ping_at < 2
            session.wait(lambda: "Listing limit reached" in session.screen.text(),
                         "large directory did not stop at its listing limit")
            session.resize(12, 20)
            session.wait(lambda: session.panel_title() == "Choose file", "small browser did not remain usable")
            session.resize(26, 132)
            session.wait(lambda: "Listing limit reached" in session.screen.text(), "browser resize lost its listing")
            session.type(b"\x7f")
            wait_browser(session, launch, ("choose-here/", relative.name))

            # Two queued clicks at the same coordinates must not open a directory and
            # then accidentally stage the unrelated file occupying its new row.
            row = browser_row(session, "choose-here/")
            click = ("\x1b[<0;3;%dM\x1b[<0;3;%dm" % (row + 1, row + 1)).encode("ascii")
            session.type(click + click)
            wait_browser(session, child, (chosen.name,))
            session.quiet(0.08)
            assert session.panel_title() == "Choose file", "stale directory click selected a new-view file"
            assert not any(b"\x01DCC SEND " in line for _, line in session.server.lines)
            mouse_click(session, browser_row(session, chosen.name), 2)
            session.wait(lambda: session.panel_title() == "Send file" and
                         "Source file: " + str(chosen) in session.screen.text(),
                         "file click did not restore the form with the exact selected source path")
            assert str(chosen) in session.screen.text(), "selected source path was lost"
            finish_browsed_send(session, chosen, payload, 0)

            # Browsing elsewhere must not change the process cwd used by typed paths.
            open_transfers(session)
            action(session, "Send...")
            edit_field(session, 0, "Nickname", "FilePeer")
            edit_field(session, 1, "Source file", relative.name)
            edit_field(session, 2, "Advertised IPv4", "127.0.0.1")
            finish_browsed_send(session, relative, relative_payload, 1)
            assert chosen.read_bytes() == payload and relative.read_bytes() == relative_payload
    print("file browser: launch cwd, directories/parent, hjkl/mouse, Esc preservation, staged-only offers, stale clicks, bounded responsive listing, Unicode and relative-path fallback passed")


def live_transfer_controls():
    from test_servers import MultiServerSession, server_rows

    listeners = []
    try:
        with MultiServerSession() as session:
            session.start()
            second_slot, second_server = session.add_server(nick="secondnick", channel="#test", connect=True)
            session.select_server(second_slot)
            payload = bytes(range(256)) * 257
            source = session.directory / "outgoing payload.bin"
            source.write_bytes(payload)
            source.chmod(0o600)
            outgoing_binary(session, second_server, session.server, source, payload)
            cancel_queued_offer(session, second_server, session.directory / "cancelled-offer.bin")
            session.select_server(0)

            downloads = session.data_home / "vipirc" / "downloads"
            assert not downloads.exists()
            reject = IncomingPeer(); listeners.append(reject)
            offer_payload(session.server, "RejectPeer", "rejected.bin", reject.port, 12)
            session.wait(lambda: b"RejectPeer" in session.raw, "unsolicited offer was not surfaced")
            session.quiet(0.12)
            reject.assert_unopened()
            assert not downloads.exists(), "an unsolicited offer created its download directory"
            open_transfers(session)
            select_transfer(session, "rejected.bin")
            action(session, "Reject")
            session.wait(lambda: transfer_state(session, "rejected.bin", "rejected"), "Reject did not update offer state")
            reject.assert_unopened()
            assert not downloads.exists()

            incoming = IncomingPeer(); listeners.append(incoming)
            received = bytes(reversed(range(256))) * 131
            offer_payload(session.server, "ReceivePeer", "incoming.bin", incoming.port, len(received))
            session.wait(lambda: "incoming.bin" in session.screen.text(), "incoming offer did not update the open list")
            incoming.assert_unopened()
            prepare_receive(session, "incoming.bin", downloads, "received.bin")
            assert not downloads.exists(), "merely reviewing Accept created a file or directory"
            check_receive_review_size(session, incoming, downloads, len(received))
            action(session, "Accept")
            incoming.accept(session)
            incoming.send(received)
            incoming.wait_ack(session, len(received))
            session.wait(lambda: transfer_state(session, "incoming.bin", "complete"), "accepted receive did not complete")
            destination = downloads / "received.bin"
            assert destination.read_bytes() == received and stat.S_IMODE(destination.stat().st_mode) == 0o600
            assert stat.S_IMODE(downloads.stat().st_mode) == 0o700

            cancelled = IncomingPeer(); listeners.append(cancelled)
            offer_payload(session.server, "CancelPeer", "cancel.bin", cancelled.port, 65536)
            prepare_receive(session, "cancel.bin", downloads, "cancelled-partial.bin")
            action(session, "Accept")
            cancelled.accept(session)
            cancelled.send(b"partial bytes")
            cancelled.wait_ack(session, len(b"partial bytes"))
            select_transfer(session, "cancel.bin")
            action(session, "Cancel transfer")
            session.wait(lambda: transfer_state(session, "cancel.bin", "cancelled"), "Cancel transfer did not update state")
            assert not (downloads / "cancelled-partial.bin").exists()
            cancelled.wait_closed(session)

            detached = IncomingPeer(); listeners.append(detached)
            continuation = b"continued after IRC disconnect" * 500
            offer_payload(session.server, "DetachedPeer", "detached.bin", detached.port, len(continuation))
            prepare_receive(session, "detached.bin", downloads, "detached.bin")
            action(session, "Accept")
            detached.accept(session)
            detached.send(continuation[:32]); detached.wait_ack(session, 32)
            back_to_chat(session)
            session.command("/disconnect")
            session.open_app_diagnostics()
            session.wait(lambda: "disconnected" in session.app_line("Connection:").lower(), "IRC disconnect did not finish")
            session.close_app_diagnostics()
            detached.send(continuation[32:]); detached.wait_ack(session, len(continuation))
            open_transfers(session)
            session.wait(lambda: transfer_state(session, "detached.bin", "complete"), "IRC disconnect stopped an accepted DCC transfer")
            assert (downloads / "detached.bin").read_bytes() == continuation
            back_to_chat(session)

            session.select_server(second_slot)
            closed = IncomingPeer(); listeners.append(closed)
            offer_payload(second_server, "ClosePeer", "close-server.bin", closed.port, 65536)
            open_transfers(session)
            prepare_receive(session, "close-server.bin", downloads, "closed-server-partial.bin")
            action(session, "Accept")
            closed.accept(session)
            closed.send(b"close removes only this partial file")
            closed.wait_ack(session, len(b"close removes only this partial file"))
            back_to_chat(session)
            session.navigate("S")
            session.wait(lambda: "Servers" in session.panel_title(), "server list did not open for Close")
            session.type(HOME + DOWN + b" " + RIGHT * 4 + b"\r")
            session.wait(lambda: "Close server" in session.screen.text(), "server Close did not request confirmation")
            session.quiet(0.13)
            session.type("y")
            session.wait(lambda: session.is_main(), "closing the current server did not return to chat")
            session.open_app_diagnostics()
            assert "Server 0:" in session.app_line("Server ") and "Nick: tester" in session.app_line("Nick:"), \
                "closing the current server did not return to the primary session"
            session.close_app_diagnostics()
            session.navigate("S")
            session.wait(lambda: set(server_rows(session)) == {0} and
                         re.search(r"\bServers \(1\)", "".join(session.screen.cells[0])) is not None,
                         "closed secondary server remained in the server list")
            remaining = server_rows(session)
            assert second_slot not in remaining and "127.0.0.1:%d" % session.server.port in remaining[0], \
                "closing the secondary session removed or replaced the primary server"
            session.type(b"\x1b")
            session.wait(lambda: session.is_main(), "server list did not return to the primary view")
            open_transfers(session)
            session.wait(lambda: transfer_state(session, "close-server.bin", "cancelled"), "closing a server did not cancel its direct transfer")
            assert not (downloads / "closed-server-partial.bin").exists()
            closed.wait_closed(session)
            assert destination.read_bytes() == received and (downloads / "detached.bin").read_bytes() == continuation
    finally:
        for peer in listeners: peer.close()
    print("transfers live: no implicit accept, minimum-size endpoint review, binary receive/send, correct IRC routing, responsive PING, queued-offer cancellation, partial cleanup and server lifetime rules passed")


def main():
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--offline-only", action="store_true")
    group.add_argument("--live-only", action="store_true")
    args = parser.parse_args()
    if not args.live_only: offline_transfer_forms()
    if not args.offline_only:
        live_file_browser()
        live_transfer_controls()
    print("all selected transfer PTY tests passed")


if __name__ == "__main__":
    main()
