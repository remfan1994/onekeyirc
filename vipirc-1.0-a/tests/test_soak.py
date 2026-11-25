#!/usr/bin/env python3
"""Bounded real-PTY longevity test against local IRC fixtures.

Default: 60 seconds. A release soak uses --seconds 1200 --servers 8 --tls.
Measures the client, not the Python driver; records its executable hash.
No public IRC server, user configuration, or user data file is contacted.
"""
import argparse
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import select
import socket
import ssl
import sys
import threading
import time

from bench import BenchSession
from test_integration import BINARY, IRCFixture
from test_servers import open_servers, select_server_row, choose_action, SERVER_ACTIONS
from process_stats import sample_child


class SoakServer(IRCFixture):
    """One I/O owner per socket, including TLS; reconnects reuse the listener."""

    def __init__(self, certificate=None, key=None):
        self.outgoing = deque()
        self.out_lock = threading.Lock()
        self.generations = 0
        self.frames_sent = self.chat_sent = self.wire_sent = self.discarded_frames = 0
        super().__init__(certificate=certificate, key=key)

    def send_bytes(self, data):
        with self.out_lock:
            if len(self.outgoing) >= 256:
                raise AssertionError("test fixture outgoing bound reached")
            self.outgoing.append(bytes(data))

    def queued(self):
        with self.out_lock:
            return len(self.outgoing)

    def run(self):
        connection = None
        context = None
        if self.certificate:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            context.load_cert_chain(self.certificate, self.key)
        try:
            while not self.stop.is_set():
                try:
                    connection, unused = self.listener.accept()
                except socket.timeout:
                    continue
                connection.settimeout(3)
                if context:
                    connection = context.wrap_socket(connection, server_side=True)
                self.socket = connection
                self.has_user = self.cap_end = self.registered = False
                self.generations += 1
                pending = b""
                while not self.stop.is_set():
                    for unused in range(8):
                        with self.out_lock:
                            block = self.outgoing.popleft() if self.outgoing else None
                        if block is None:
                            break
                        connection.sendall(block)
                        self.frames_sent += block.count(b"\n")
                        self.chat_sent += block.count(b" PRIVMSG ")
                        self.wire_sent += len(block)
                    ready = context and connection.pending()
                    if not ready and not select.select([connection], [], [], 0.005)[0]:
                        continue
                    data = connection.recv(65536)
                    if not data:
                        break
                    pending += data
                    assert len(pending) < 65536, "client produced unbounded incomplete frame"
                    while b"\r\n" in pending:
                        line, pending = pending.split(b"\r\n", 1)
                        assert len(line) + 2 <= 512 and b"\0" not in line and b"\n" not in line and b"\r" not in line
                        assert not line.startswith(b"PRIVMSG "), "unexpected chat send during a read/edit-only soak"
                        self.lines.append((time.monotonic(), line))
                        if len(self.lines) > 8192:
                            del self.lines[:4096]
                        self.handle(line)
                self.registered = False
                self.socket = None
                connection.close()
                connection = None
                with self.out_lock:
                    self.discarded_frames += sum(block.count(b"\n") for block in self.outgoing)
                    self.outgoing.clear()
        except Exception as error:
            if not self.stop.is_set():
                self.errors.append(repr(error))
        finally:
            self.registered = False
            self.socket = None
            if connection is not None:
                connection.close()
            self.listener.close()


def frame_block(server, channels, serial, count):
    frames = []
    for i in range(count):
        number = serial + i
        channel = channels[number % len(channels)]
        target = server.nick if number % 41 == 0 else channel
        peer = "Query%02d" % (number % 37) if target == server.nick else "speaker"
        mention = server.nick + " watch-signal " if number % 17 == 0 else ""
        body = "%012d %s%s" % (number, mention, "bounded-history " + "x" * 225)
        if number % 127 == 0:
            body += " \x1b]52;c;cmVtb3Rl\x07\x02format\x02"
        wire = (":%s!u@localhost PRIVMSG %s :%s\r\n" % (peer, target, body)).encode("utf-8")
        assert len(wire) <= 512
        frames.append(wire)
    return b"".join(frames)


def ping_all(session, peers, marker, limit=4):
    started = time.monotonic()
    expected = {}
    for i, peer in enumerate(peers):
        tag = "%s-%d" % (marker, i)
        expected[i] = ("PONG :" + tag).encode("ascii")
        peer.send("PING :" + tag)
    found = {}
    deadline = started + limit
    while len(found) < len(peers):
        session.pump(0.005)
        for i, peer in enumerate(peers):
            for at, line in reversed(peer.lines):
                if at < started:
                    break
                if line == expected[i]:
                    found[i] = round((at - started) * 1000, 3)
                    break
            assert not peer.errors, peer.errors
        assert session.proc.poll() is None, "client exited during PING probe"
        assert time.monotonic() < deadline, ("server responsiveness deadline", marker, found)
    return [found[i] for i in range(len(peers))]


def run(args):
    channels = ["#test"] + ["#soak%02d" % i for i in range(1, 20)]
    digest = hashlib.sha256(BINARY.read_bytes()).hexdigest()
    session = BenchSession(channels, args.servers, tls=args.tls, fixture_factory=SoakServer)
    peers = [session.server] + session.additional_servers
    ending = threading.Event()
    paused = threading.Event()
    failures = []
    workers = []
    serials = [0] * len(peers)
    samples = []
    pings = []
    input_latencies = []
    reconnects = resizes = reconstruction_resizes = 0
    draft = "soak draft 0123456789 - never send this"
    report = {"binary_sha256": digest, "servers": args.servers, "channels_per_server": len(channels),
              "tls": args.tls, "requested_seconds": args.seconds, "batch": args.batch,
              "interval_seconds": args.interval, "warmup_messages_per_server": args.warmup}

    def produce(index, peer):
        try:
            while not ending.is_set():
                if not paused.is_set() and peer.registered and peer.queued() < 8:
                    block = frame_block(peer, channels, serials[index], args.batch)
                    peer.send_bytes(block)
                    serials[index] += args.batch
                ending.wait(args.interval)
        except Exception as error:
            failures.append(repr(error))
            ending.set()

    try:
        session.start()
        for peer in peers:
            session.wait(lambda peer=peer: peer.registered and
                sum(line.startswith(b"JOIN ") for unused, line in peer.lines) >= len(channels),
                "initial server did not register and join all channels", timeout=15)
        session.input_mode()
        session.type(draft)
        session.wait(lambda: draft in session.input_text(), "initial draft missing")
        session.input_mode()
        session.fast = True
        warm_started = time.monotonic()
        warm_sent = [0] * len(peers)
        while min(warm_sent) < args.warmup or any(peer.queued() for peer in peers):
            for i, peer in enumerate(peers):
                if warm_sent[i] < args.warmup and peer.queued() < 8:
                    count = min(256, args.warmup - warm_sent[i])
                    peer.send_bytes(frame_block(peer, channels, serials[i], count))
                    serials[i] += count
                    warm_sent[i] += count
            session.pump(0.001)
            assert time.monotonic() - warm_started < 120, "warmup deadline exceeded"
            assert session.proc.poll() is None, "client exited during warmup"
        pings.append(ping_all(session, peers, "warmup"))
        base = sample_child(session.proc.pid, expected_exe=BINARY)
        assert base["fd_count"] is not None and base["exe_verified"], ("resource identity/access unavailable", base)
        assert base["child_count"] == 0, ("resolver not reaped after registration", base)
        report["warmup_seconds"] = round(time.monotonic() - warm_started, 3)
        report["baseline"] = base
        for i, peer in enumerate(peers):
            worker = threading.Thread(target=produce, args=(i, peer), daemon=True)
            worker.start()
            workers.append(worker)
        started = time.monotonic()
        deadline = started + args.seconds
        next_sample, next_probe, next_ui, next_notice = started, started, started + 20, started + 30
        while time.monotonic() < deadline:
            session.pump(0.01)
            now = time.monotonic()
            assert session.proc.poll() is None, "client exited during sustained load"
            assert not failures, failures
            for peer in peers:
                assert not peer.errors, peer.errors
            if now >= next_probe:
                pings.append(ping_all(session, peers, "probe-%d" % len(pings)))
                next_probe = time.monotonic() + 10
            if now >= next_sample:
                value = sample_child(session.proc.pid, expected_exe=BINARY)
                value["elapsed_seconds"] = round(now - started, 3)
                assert value["start_ticks"] == base["start_ticks"], "measured process identity changed"
                assert value["fd_count"] == base["fd_count"], ("descriptor growth", base, value)
                assert value["child_count"] == 0, ("resolver child retained after a settled connection", value)
                assert value["rss_kib"] <= args.servers * 20 * 1024 + 32 * 1024, ("RSS envelope exceeded", value)
                samples.append(value)
                next_sample = now + 5
            if now >= next_ui:
                session.reconstruct_screen()
                reconstruction_resizes += 2
                session.wait(lambda: session.mode() == "INPUT" and draft in session.input_text(),
                             "draft lost or UI not responsive during load", timeout=5)
                session.input_mode()
                session.type("!")
                session.wait(lambda: draft + "!" in session.input_text(), "editor stopped responding")
                session.type(b"\x7f")
                session.wait(lambda: draft in session.input_text() and draft + "!" not in session.input_text(),
                             "Backspace did not restore held draft")
                session.input_mode()
                geometry = [(9, 20), (12, 40), (40, 160), (24, 80)][(resizes // 2) % 4]
                session.resize(*geometry)
                session.quiet(0.03)
                session.resize(24, 80)
                session.wait(lambda: draft in session.input_text(), "resize lost held draft")
                resizes += 2
                session.clear_input()
                probe_started = time.monotonic()
                session.command("/nick tester")
                session.wait(lambda: any(at >= probe_started and line == b"NICK tester" for at, line in peers[0].lines),
                             "input command was starved by incoming traffic", timeout=5)
                at = next(at for at, line in peers[0].lines if at >= probe_started and line == b"NICK tester")
                input_latencies.append(round((at - probe_started) * 1000, 3))
                session.type(draft)
                session.wait(lambda: draft in session.input_text(), "draft could not be restored")
                session.input_mode()
                if args.servers > 1 and resizes % 6 == 0:
                    paused.set()
                    drain_deadline = time.monotonic() + 10
                    while any(peer.queued() for peer in peers):
                        session.pump(0.005)
                        assert session.proc.poll() is None, "client exited while draining before reconnect"
                        for peer in peers:
                            assert not peer.errors, peer.errors
                        assert time.monotonic() < drain_deadline, (
                            "fixture output did not drain before reconnect", [peer.queued() for peer in peers])
                    slot = args.servers - 1
                    generations = peers[slot].generations
                    select_server_row(session, slot)
                    choose_action(session, "Disconnect", SERVER_ACTIONS)
                    session.wait(lambda: not peers[slot].registered, "background disconnect did not close socket")
                    choose_action(session, "Connect", SERVER_ACTIONS)
                    session.wait(lambda: peers[slot].generations == generations + 1 and peers[slot].registered,
                                 "background reconnect failed", timeout=15)
                    choose_action(session, "Back", SERVER_ACTIONS)
                    session.wait(lambda: session.mode() == "INPUT" and draft in session.input_text(),
                                 "server reconnect changed primary draft")
                    pings.append(ping_all(session, peers, "reconnect-%d" % reconnects))
                    reconnects += 1
                    paused.clear()
                session.fast = True
                next_ui = time.monotonic() + 20
            if now >= next_notice:
                recent = samples[-1]
                print("soak %.1fs: %d delivered chat lines, RSS=%dKiB, fds=%d, reconnects=%d" %
                      (now-started, sum(peer.chat_sent for peer in peers), recent["rss_kib"], recent["fd_count"], reconnects),
                      file=sys.stderr, flush=True)
                next_notice = now + 30
        elapsed = time.monotonic() - started
        ending.set()
        for worker in workers:
            worker.join(timeout=2)
            assert not worker.is_alive(), "traffic producer did not stop"
        pings.append(ping_all(session, peers, "finish"))
        final = sample_child(session.proc.pid, expected_exe=BINARY)
        assert final["start_ticks"] == base["start_ticks"], "measured process identity changed"
        assert final["fd_count"] == base["fd_count"], "descriptors leaked across reconnects"
        assert final["child_count"] == 0, "resolver children remain after reconnects"
        tail = samples[len(samples)//2:]
        growth = max(value["rss_kib"] for value in tail) - min(value["rss_kib"] for value in tail)
        assert growth <= max(8192, args.servers * 1024), ("late RSS growth exceeded bound", growth)
        session.reconstruct_screen()
        reconstruction_resizes += 2
        session.wait(lambda: draft in session.input_text(), "final draft did not survive longevity run")
        assert not session.osc52_seen, "remote terminal instruction escaped sanitization"
        report.update(elapsed_seconds=round(elapsed, 3), samples=samples, final=final,
                      late_rss_range_kib=growth, ping_roundtrips_ms=pings,
                      max_ping_ms=max(max(values) for values in pings),
                      input_command_latencies_ms=input_latencies, resizes=resizes + reconstruction_resizes,
                      scheduled_resizes=resizes, reconstruction_resizes=reconstruction_resizes,
                      reconnects=reconnects,
                      delivered_chat_lines=sum(peer.chat_sent for peer in peers),
                      delivered_frames=sum(peer.frames_sent for peer in peers),
                      delivered_wire_bytes=sum(peer.wire_sent for peer in peers),
                      discarded_fixture_frames=sum(peer.discarded_frames for peer in peers),
                      draft_preserved=True, unexpected_sends=0)
    finally:
        ending.set()
        for worker in workers:
            worker.join(timeout=2)
        session.close()
    assert session.proc.returncode == 130, session.proc.returncode
    assert session.terminal_restored, "terminal settings were not restored"
    assert hashlib.sha256(BINARY.read_bytes()).hexdigest() == digest, "binary changed during soak"
    report["client_exit_status"] = session.proc.returncode
    report["terminal_restored"] = session.terminal_restored
    report["binary_unchanged"] = True
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--servers", type=int, choices=range(1, 9), default=8)
    parser.add_argument("--tls", action="store_true")
    parser.add_argument("--warmup", type=int, default=40000)
    parser.add_argument("--batch", type=int, default=128)
    parser.add_argument("--interval", type=float, default=0.2)
    args = parser.parse_args()
    if not 20 <= args.seconds <= 3600 or not 1 <= args.warmup <= 200000 or not 1 <= args.batch <= 1024 or not 0.02 <= args.interval <= 2:
        parser.error("bounded ranges: seconds20..3600, warmup1..200000, batch1..1024, interval0.02..2")
    print(json.dumps(run(args), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
