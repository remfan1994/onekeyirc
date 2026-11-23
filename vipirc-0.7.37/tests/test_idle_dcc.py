#!/usr/bin/env python3
"""Native transfer commands over private IRC/TCP fixtures; no public network."""
import argparse
import os
import re
import select
import socket
import stat
import struct
import time

from test_idle_terminal import IdleSession


def rejected(session, command, expected):
    marker = len(session.raw)
    session.type(command)
    session.wait(lambda: session.editor(command), "command editor missing")
    session.type(b"\r")
    session.wait(lambda: expected.encode() in session.raw[marker:], "expected rejection missing: " + expected)
    if session.reader():
        session.type(b"\x1b")
        session.wait(lambda: session.editor(command), "error reader lost command draft")
    session.cancel()


def pong(session, name):
    session.peer.send("PING :" + name)
    session.wait(lambda: any(line == ("PONG :" + name).encode() for _, line in session.peer.lines),
                 "transfer prevented IRC keepalive")


def native_transfers(full):
    with IdleSession(live=True, review=False, full=full) as session:
        payload = bytes(range(256)) * 89
        name = "launch caf\u00e9 file.bin"
        (session.directory / name).write_bytes(payload)
        session.start()
        session.command("/dcc", "No file transfers.")
        session.command("/dcc FilePeer " + name, "File offer #1 to FilePeer")
        offers = lambda: [line for _, line in session.peer.lines if b"\x01DCC SEND " in line]
        session.wait(lambda: len(offers()) == 1, "native send did not emit one DCC offer")
        body = offers()[0].split(b"\x01", 2)[1].decode()
        match = re.fullmatch(r'DCC SEND "([^"]+)" (\d+) (\d+) (\d+)', body)
        assert match and match[1] == name and int(match[2]) == 2130706433 and int(match[4]) == len(payload), body
        received = bytearray()
        with socket.create_connection(("127.0.0.1", int(match[3])), timeout=2) as peer:
            peer.setblocking(False)
            deadline = time.monotonic() + 6
            pong(session, "while-sending")
            while len(received) < len(payload) and time.monotonic() < deadline:
                session.pump(.01)
                if not select.select([peer], [], [], 0)[0]:
                    continue
                data = peer.recv(65536)
                assert data, "outgoing file closed early"
                received.extend(data)
                peer.sendall(struct.pack("!I", len(received)))
            assert bytes(received) == payload, "relative path used a different working directory or corrupted bytes"
            session.wait(lambda: b"File transfer #1 complete" in session.raw, "final ACK not reflected")

        download = session.directory / "received files"
        download.mkdir(mode=0o700)
        incoming = b"Native receiver must preserve these bytes.\x00\xff\x1b" * 41
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(("127.0.0.1", 0)); listener.listen(1)
            port = listener.getsockname()[1]
            session.type("unfinished draft")
            session.wait(lambda: session.editor("unfinished draft"), "chat draft missing")
            session.peer.send(":FilePeer!u@local PRIVMSG tester :\x01DCC SEND incoming.bin 2130706433 %d %d\x01" % (port, len(incoming)))
            session.quiet(.15)
            assert session.editor("unfinished draft"), "offer interrupted composition"
            assert not select.select([listener], [], [], 0)[0], "incoming offer was automatically accepted"
            session.cancel()
            session.command("/transfers list", "incoming.bin")
            pong(session, "offer-pending")
            session.command("/dcc accept 2 received files", "File transfer #2 accepted")
            session.wait(lambda: bool(select.select([listener], [], [], 0)[0]), "explicit Accept did not connect")
            peer, _ = listener.accept()
            with peer:
                peer.sendall(incoming)
                peer.setblocking(False)
                ack = bytearray(); latest = 0
                deadline = time.monotonic() + 5
                while latest < len(incoming) and time.monotonic() < deadline:
                    session.pump(.01)
                    if select.select([peer], [], [], 0)[0]:
                        block = peer.recv(4096)
                        assert block, "receiver closed before final acknowledgement"
                        ack.extend(block)
                        while len(ack) >= 4:
                            latest = struct.unpack("!I", ack[:4])[0]
                            del ack[:4]
                assert latest == len(incoming)
            session.wait(lambda: (download / "incoming.bin").exists(), "received file missing")
            assert (download / "incoming.bin").read_bytes() == incoming
            assert stat.S_IMODE((download / "incoming.bin").stat().st_mode) == 0o600
            session.peer.send(":FilePeer!u@local PRIVMSG tester :\x01DCC SEND incoming.bin 2130706433 %d %d\x01" % (port, len(incoming)))
            session.wait(lambda: b"File offer #3" in session.raw, "second incoming offer missing")
            rejected(session, "/dcc accept 3 received files", "exist")
            assert (download / "incoming.bin").read_bytes() == incoming, "receive overwrote an existing file"
            session.command("/dcc reject 3", "File transfer #3 rejected")
            assert not select.select([listener], [], [], 0)[0]

        previous = len(offers())
        rejected(session, "/dcc send -port 65536 FilePeer " + name, "Usage:")
        rejected(session, "/dcc cancel 18446744073709551616", "Usage:")
        assert len(offers()) == previous
        session.command("/dcc FilePeer " + name, "File offer #4")
        session.command("/dcc cancel 4", "File transfer #4 cancelled")
        pong(session, "after-cancel")
        session.command("/dcc list", "cancelled")


def offline_no_files(full):
    with IdleSession(full=full) as session:
        os.mkfifo(session.directory / "never-open", 0o600)
        session.start()
        rejected(session, "/dcc FilePeer never-open", "disabled in offline test mode")
        session.command("/dcc list", "No file transfers.")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--full", action="store_true")
    args = parser.parse_args()
    offline_no_files(args.full)
    native_transfers(args.full)
    print("native DCC: literal launch-cwd paths, send/receive bytes and ACKs, explicit consent, private no-overwrite receive, draft preservation, keepalive responsiveness, rejection/cancellation, argument bounds and offline no-I/O passed")


if __name__ == "__main__":
    main()
