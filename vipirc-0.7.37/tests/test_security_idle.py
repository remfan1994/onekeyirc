#!/usr/bin/env python3
"""Hostile-text and backpressure idle stress; private localhost fixtures only.

Usage: python3 tests/test_security_idle.py --binary ./onekeyirc --seconds 60
       python3 tests/test_security_idle.py --binary ./vipirc --full --seconds 60
The test uses no credentials and explicitly disables persistent logging.
"""
import argparse
import codecs
import errno
import json
import os
from pathlib import Path
import select
import socket
import statistics
import time

import test_idle_terminal as terminal
from process_stats import ProcessStatsError, sample_child


class OutputProbe:
    """Inspect an idle watch stream without growing an unbounded capture."""
    def __init__(self):
        self.decoder = codecs.getincrementaldecoder("utf-8")("strict")
        self.bytes = 0
        self.tail = b""
        self.omission_notices = 0

    def feed(self, data):
        self.bytes += len(data)
        self.tail = (self.tail + data)[-16384:]
        text = self.decoder.decode(data)
        for character in text:
            code = ord(character)
            if ((code < 32 and code not in (10, 13)) or 0x7f <= code <= 0x9f or
                    code in (0x061c, 0x200e, 0x200f) or
                    0x202a <= code <= 0x202e or 0x2066 <= code <= 0x2069):
                raise AssertionError("unsafe terminal scalar U+%04X in remote watch output" % code)
        self.omission_notices += data.count(b"earlier records omitted")

    def drain(self, descriptor, timeout=0):
        ready, _, _ = select.select([descriptor], [], [], timeout)
        if not ready:
            return
        # Bound the harness work too; a fast producer must not prevent pings.
        for _ in range(8):
            try:
                data = os.read(descriptor, 65536)
            except OSError as error:
                if error.errno in (errno.EAGAIN, errno.EIO):
                    return
                raise
            if not data:
                return
            self.feed(data)


def process_sample(pid, binary):
    """Verify the child PID namespace before reporting optional Linux metrics."""
    try:
        sample = sample_child(pid, binary, include_children=False)
        return {"rss_kib": sample["rss_kib"], "fds": sample["fd_count"],
                "cpu_seconds": sample["cpu_seconds"],
                "identity_verified": sample["identity_verified"]}
    except ProcessStatsError:
        # Platforms without an identity-checked /proc view still run every
        # protocol and terminal assertion; their resource metrics stay null.
        return None


def hostile_record(index, dcc_port):
    # All wire records remain correctly framed and below the IRC line limit.
    # Controls and invalid UTF-8 occur in displayed data, never CR/LF framing.
    attacks = (
        b"\x1b]52;c;YXVkaXQtY2xpcGJvYXJk\x07 OSC52",
        b"\x1b]0;audit-window-title\x1b\\ OSC-title",
        b"\x1bP1;2|audit-DCS\x1b\\ DCS",
        b"\x1b[2J\x1b[1;1H CSI-erase",
        b"\x9b2J\xc2\x9d52;c;YQ==\xc2\x9c C1",
        b"\xff\xc0\xaf\xed\xa0\x80\xf4\x90\x80\x80 invalid-UTF8",
        b"\xe2\x80\xaeexe.txt\xe2\x80\xac bidi",
        b"\x07\x08\x0b\x0c\x7f controls",
        b"\x1b]8;;https://invalid.example/\x1b\\link\x1b]8;;\x1b\\ OSC8",
        b"/msg Intruder should-never-be-sent; /quit literal-commands",
        b"\x02bold\x0f\x0304,12color\x04aabbcc,112233hex\x0f styling",
        b"valid-UTF8 \xe2\x98\x83 end",
    )
    body = b"SECURITY-IDLE-%08d " % index + attacks[index % len(attacks)] + b" retained-data"
    choice = index % 16
    prefix = b":Attacker!user@localhost "
    if choice in (0, 1, 2, 3, 4, 5):
        record = prefix + b"PRIVMSG #test :" + body
    elif choice == 6:
        record = prefix + b"NOTICE #test :" + body
    elif choice == 7:
        record = prefix + b"TOPIC #test :" + body
    elif choice == 8:
        record = b":srv 332 tester #test :" + body
    elif choice == 9:
        record = prefix + b"PRIVMSG #test :\x01ACTION " + body + b"\x01"
    elif choice == 10:
        record = prefix + b"PRIVMSG tester :\x01VERSION\x01"
    elif choice == 11:
        record = prefix + b"PRIVMSG tester :\x01PING must-not-reply\x01"
    elif choice == 12:
        record = (prefix + b"PRIVMSG tester :\x01DCC SEND pending-%02d.bin 2130706433 %d 4096\x01" %
                  ((index // 16) % 16, dcc_port))
    elif choice == 13:
        record = b":Query%02d!user@localhost PRIVMSG tester :" % ((index // 16) % 64) + body
    elif choice == 14:
        record = b":Roster%03d!user@localhost JOIN :#test" % ((index // 16) % 1024)
    else:
        record = b":Roster%03d!user@localhost PART #test :" % ((index // 16) % 1024) + body
    assert len(record) + 2 <= 512 and b"\r" not in record and b"\n" not in record
    return record + b"\r\n"


def exercise(binary, full, seconds, rate, ping_limit, rss_limit_mib):
    if full:
        terminal.FULL_BINARY = binary
    else:
        terminal.BINARY = binary
    honey = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    honey.bind(("127.0.0.1", 0))
    honey.listen(8)
    honey.setblocking(False)
    try:
        with terminal.IdleSession(live=True, full=full, config_extra=
                "[output]\npersistent_logging=false\nprogram_messages=false\nqueue_notices=false\n") as session:
            session.start()
            session.quiet(0.2)
            session.raw.clear()
            probe = OutputProbe()
            started = time.monotonic()
            end = started + seconds
            next_burst = next_ping = next_sample = started
            batch_size = 32
            sent = 0
            ping_number = 0
            pending = {}
            latencies = []
            samples = []
            observed = len(session.peer.lines)
            stopped_reading_at = started + seconds / 3
            resumed_reading_at = started + 2 * seconds / 3
            dcc_port = honey.getsockname()[1]

            def harvest():
                nonlocal observed
                received = session.peer.lines[observed:]
                observed += len(received)
                for when, line in received:
                    assert line.startswith(b"PONG :audit-security-"), (
                        "unexpected client send during remote-only traffic", line)
                    token = line[6:]
                    assert token in pending, ("unexpected or duplicate PONG", line)
                    latency = when - pending.pop(token)
                    assert 0 <= latency <= ping_limit, ("PING response delayed", latency)
                    latencies.append(latency)
                assert not pending or time.monotonic() - min(pending.values()) <= ping_limit, (
                    "PING stalled under hostile traffic or terminal backpressure", pending)
                assert not session.peer.errors, session.peer.errors
                assert session.proc.poll() is None, ("idle client exited", session.proc.returncode, probe.tail)
                assert not select.select([honey], [], [], 0)[0], "unsolicited DCC offer initiated a peer connection"

            while time.monotonic() < end:
                now = time.monotonic()
                if now >= next_burst:
                    session.peer.send_bytes(b"".join(hostile_record(sent + n, dcc_port)
                                                    for n in range(batch_size)))
                    sent += batch_size
                    next_burst = max(next_burst + batch_size / rate, now)
                if now >= next_ping:
                    token = ("audit-security-%05d" % ping_number).encode("ascii")
                    pending[token] = time.monotonic()
                    session.peer.send_bytes(b"PING :" + token + b"\r\n")
                    ping_number += 1
                    next_ping = now + 0.5
                if now >= next_sample:
                    sample = process_sample(session.proc.pid, binary)
                    if sample:
                        samples.append(dict(elapsed=now-started, **sample))
                    next_sample = now + 1
                harvest()
                if now < stopped_reading_at or now >= resumed_reading_at:
                    probe.drain(session.master, min(0.005, max(0, next_burst-now)))
                else:
                    # Deliberately leave the PTY unread to fill the kernel and
                    # client's output buffers while continuing IRC PING checks.
                    time.sleep(min(0.005, max(0, next_burst-now)))

            marker = b"SECURITY-IDLE-FINAL-MARKER"
            session.peer.send_bytes(b":Attacker!u@localhost PRIVMSG #test :" + marker + b"\r\n")
            deadline = time.monotonic() + ping_limit + 3
            while (pending or marker not in probe.tail) and time.monotonic() < deadline:
                probe.drain(session.master, 0.01)
                harvest()
            harvest()
            assert not pending and marker in probe.tail, ("final traffic was not processed", pending, probe.tail)
            assert probe.omission_notices > 0, "terminal stall did not exercise bounded-output omission"
            assert sent >= 500, "insufficient traffic to roll the configured history repeatedly"
            assert len(latencies) >= max(2, int(seconds)), "insufficient keepalive observations"
            assert not list(session.directory.rglob("*.log")), "persistent logging was not disabled"
            steady = [sample for sample in samples if sample["elapsed"] >= min(10, seconds / 4)]
            peak_rss = growth = baseline_fds = max_fds = None
            if len(steady) >= 2:
                warm_count = min(3, len(steady))
                warm_rss = max(sample["rss_kib"] for sample in steady[:warm_count])
                peak_rss = max(sample["rss_kib"] for sample in steady)
                growth = peak_rss - warm_rss
                descriptor_samples = [sample["fds"] for sample in steady if sample["fds"] is not None]
                if descriptor_samples:
                    baseline_fds = descriptor_samples[0]
                    max_fds = max(descriptor_samples)
                    assert max_fds <= baseline_fds + 2, ("client descriptors grew during idle traffic", samples)
                assert growth <= rss_limit_mib * 1024, ("client RSS failed to plateau", growth, samples)
            return {"result": "PASS", "seconds": round(time.monotonic()-started, 3),
                    "wire_records": sent, "ping_replies": len(latencies),
                    "ping_max_ms": round(1000*max(latencies), 3),
                    "ping_median_ms": round(1000*statistics.median(latencies), 3),
                    "pty_bytes_inspected": probe.bytes,
                    "history_omission_notices": probe.omission_notices,
                    "unread_pty_seconds": round(seconds/3, 3),
                    "resource_identity_verified": bool(samples),
                    "client_cpu_seconds": round(samples[-1]["cpu_seconds"]-samples[0]["cpu_seconds"], 3) if len(samples) > 1 else None,
                    "steady_peak_rss_kib": peak_rss, "steady_rss_growth_kib": growth,
                    "steady_baseline_fds": baseline_fds, "steady_max_fds": max_fds,
                    "unexpected_client_sends": 0, "unsolicited_dcc_connections": 0,
                    "terminal_controls_from_remote_data": 0, "persistent_log_files": 0}
    finally:
        honey.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--full", action="store_true", help="exercise vipirc --idle")
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--rate", type=float, default=1500, help="target IRC records per second")
    parser.add_argument("--ping-limit", type=float, default=3)
    parser.add_argument("--rss-limit-mib", type=float, default=8,
                        help="allowed RSS growth after warm-up; increase for sanitizer quarantine")
    args = parser.parse_args()
    if args.seconds < 2 or args.rate < 100 or args.ping_limit <= 0 or args.rss_limit_mib < 0:
        parser.error("seconds >= 2, rate >= 100 and positive limits are required")
    binary = args.binary.resolve()
    assert binary.is_file() and os.access(binary, os.X_OK), binary
    print(json.dumps(exercise(binary, args.full, args.seconds, args.rate,
                              args.ping_limit, args.rss_limit_mib), sort_keys=True), flush=True)


if __name__ == "__main__":
    main()
