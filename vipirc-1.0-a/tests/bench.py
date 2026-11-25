#!/usr/bin/env python3
"""Optional short, local benchmark; not a production workload or comparison.

Run after an optimized build: python3 tests/bench.py
Only loopback and a real PTY are used. No public IRC server is contacted.
The application remains unchanged; Linux measurements use /proc externally.
"""
import argparse
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import pty
import resource
import select
import struct
import subprocess
import sys
import tempfile
import termios
import threading
import time

from test_integration import BINARY, ROOT, IRCFixture, Screen, Session
from process_stats import sample_child


class BenchSession(Session):
    """Reuse the fake server and session controls, with a larger history cap."""

    def __init__(self, channels, server_count=1, tls=False, fixture_factory=IRCFixture,
                 fd_limit=None):
        self.channel = True
        self.fast = False
        self.usage = None
        self.terminal_output_bytes = 0
        self.osc52_seen = False
        self._osc52_tail = b""
        self.temp = tempfile.TemporaryDirectory(prefix="vipirc-bench-")
        self.config = Path(self.temp.name) / "config.ini"
        self.config.write_text("[interface]\nhistory_lines=5000\n", encoding="ascii")
        self.config.chmod(0o600)
        certificate = key = None
        if tls:
            from test_net import cert_pair
            certificate, key = cert_pair(Path(self.temp.name), "localhost", "DNS:localhost,IP:127.0.0.1")
        self.server = fixture_factory(certificate=certificate, key=key)
        self.additional_servers = [fixture_factory(certificate=certificate, key=key)
                                   for unused in range(1, server_count)]
        if self.additional_servers:
            directory = Path(str(self.config) + ".d")
            directory.mkdir(mode=0o700)
            ids = []
            for number, server in enumerate(self.additional_servers, 1):
                profile_id = "bench-%d.ini" % number
                ids.append(profile_id)
                profile = directory / profile_id
                profile.write_text("[connection]\nserver=127.0.0.1\nport=%d\ntls=%s\nca_file=%s\n"
                    "nick=bench%d\nusername=bench\nchannel=%s\nautoconnect=true\n"
                    "[interface]\nhistory_lines=5000\n" %
                    (server.port, "true" if tls else "false", certificate or "", number, ",".join(channels)), encoding="ascii")
                profile.chmod(0o600)
            catalog = Path(str(self.config) + ".servers")
            catalog.write_text("[servers]\n" + "".join("profile=%s\n" % name for name in ids), encoding="ascii")
            catalog.chmod(0o600)
        self.master, slave = pty.openpty()
        self.term_before = termios.tcgetattr(slave)
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        self.screen = Screen()
        self.raw = bytearray()
        args = [str(BINARY), "--config", str(self.config), "--server", "127.0.0.1",
                "--port", str(self.server.port), "--nick", "tester",
                "--channel", ",".join(channels)]
        args += ["--tls", "--ca-file", str(certificate)] if tls else ["--plain"]
        env = dict(os.environ, TERM="xterm-256color", LANG="C.UTF-8", LC_ALL="C.UTF-8")

        def controlling_terminal():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)
            if fd_limit is not None:
                resource.setrlimit(resource.RLIMIT_NOFILE, (fd_limit, fd_limit))

        self.proc = subprocess.Popen(args, stdin=slave, stdout=slave, stderr=slave,
                                     cwd=ROOT, env=env, preexec_fn=controlling_terminal)
        os.close(slave)
        os.set_blocking(self.master, False)

    def pump(self, timeout=0.01):
        if not select.select([self.master], [], [], timeout)[0]:
            return
        while True:
            try:
                data = os.read(self.master, 65536)
            except OSError as error:
                if error.errno in (errno.EIO, errno.EAGAIN):
                    return
                raise
            if not data:
                return
            self.terminal_output_bytes += len(data)
            # Keep a lifetime result even after the raw-output ring advances.
            # Four carried bytes detect the five-byte prefix across reads.
            scan = self._osc52_tail + data
            self.osc52_seen |= b"\x1b]52;" in scan
            self._osc52_tail = scan[-4:]
            self.raw.extend(data)
            if len(self.raw) > 131072:
                del self.raw[:-65536]
            if not self.fast:
                self.screen.feed(data)
            if len(data) < 65536:
                return

    def reconstruct_screen(self):
        """Reconstruct a discarded VT model using actual terminal resizes.

        Fast draining deliberately skips terminal interpretation. A signal
        without a size change is not a portable full-repaint request, so
        change the PTY geometry and restore it before inspecting the model.
        No keyboard event is used to wake the application.
        """
        rows, cols = self.screen.rows, self.screen.cols
        visible = self.screen.cursor_visible
        self.fast = False
        self.screen = Screen(rows=rows, cols=cols, cursor_visible=visible)
        self.resize(rows + 1, cols)
        self.wait(lambda: self.is_main() and self.screen.cursor_visible,
                  "temporary real resize did not reconstruct the chat screen")
        self.resize(rows, cols)
        self.wait(lambda: self.is_main() and self.screen.cursor_visible,
                  "restored real resize did not reconstruct the chat screen")

    def close(self):
        if self.proc.returncode is None:
            try:
                self.type(b"\x03")
            except OSError:
                pass
            deadline = time.monotonic() + 3
            if hasattr(os, "wait4"):
                while True:
                    try:
                        pid, status, usage = os.wait4(self.proc.pid, os.WNOHANG)
                    except ChildProcessError:
                        break
                    if pid:
                        self.proc.returncode = os.waitstatus_to_exitcode(status)
                        self.usage = usage
                        break
                    if time.monotonic() >= deadline:
                        self.proc.kill()
                        pid, status, usage = os.wait4(self.proc.pid, 0)
                        self.proc.returncode = os.waitstatus_to_exitcode(status)
                        self.usage = usage
                        break
                    self.pump(0.01)
            else:
                try:
                    self.proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait(timeout=2)
        self.pump(0)
        self.terminal_restored = termios.tcgetattr(self.master) == self.term_before
        os.close(self.master)
        self.server.close()
        for server in self.additional_servers:
            server.close()
        self.temp.cleanup()


def linux_sample(pid):
    """Return verified target-process fields, or unavailable on other systems."""
    if not sys.platform.startswith("linux"):
        return None
    return sample_child(pid, expected_exe=BINARY, include_children=False)


def metric_delta(before, after, elapsed):
    if before is None or after is None:
        return {"cpu_seconds": None, "cpu_percent_of_one_core": None,
                "rss_kib": None, "peak_rss_kib": None}
    cpu = after["cpu_seconds"] - before["cpu_seconds"]
    return {"cpu_seconds": round(cpu, 6),
            "cpu_percent_of_one_core": round(100 * cpu / elapsed, 4),
            "rss_kib": after["rss_kib"],
            "peak_rss_kib": after["peak_rss_kib"]}


def pong_seen(session, marker):
    expected = ("PONG :" + marker).encode("ascii")
    return any(line == expected for _, line in session.server.lines)


def measure_idle(sessions, duration):
    """Drain all independent terminals during one simultaneous idle window."""
    for session in sessions:
        session.fast = True
    before = [linux_sample(session.proc.pid) for session in sessions]
    started = time.monotonic()
    deadline = started + duration
    while time.monotonic() < deadline:
        readable, _, _ = select.select([s.master for s in sessions], [], [],
                                       min(0.02, max(0, deadline - time.monotonic())))
        for session in sessions:
            if session.master in readable:
                session.pump(0)
            if session.proc.poll() is not None:
                raise AssertionError("an idle client exited unexpectedly")
    elapsed = time.monotonic() - started
    after = [linux_sample(session.proc.pid) for session in sessions]
    metrics = [metric_delta(old, new, elapsed) for old, new in zip(before, after)]
    for index, metric in enumerate(metrics, 1):
        metric["instance"] = index
    totals_available = all(metric["cpu_seconds"] is not None and metric["rss_kib"] is not None
                           for metric in metrics)
    total_cpu = sum(metric["cpu_seconds"] for metric in metrics) if totals_available else None
    result = {
        "count": len(sessions),
        "elapsed_seconds": round(elapsed, 6),
        "per_process": metrics,
        "summed_cpu_seconds": round(total_cpu, 6) if total_cpu is not None else None,
        "summed_cpu_percent_of_one_core": round(100 * total_cpu / elapsed, 4)
                                          if total_cpu is not None else None,
        "summed_rss_kib": sum(metric["rss_kib"] for metric in metrics)
                          if totals_available else None,
        "rss_accounting_note": "Summed RSS counts shared resident pages in each process.",
    }
    return result, before


def stress_phase(session, channels, first, count, phase, deadline):
    marker = "bench-phase-%d-complete" % phase
    failures = []
    sent_bytes = [0]
    done = threading.Event()
    payload = "bounded history payload " + "x" * 226

    def send_messages():
        try:
            batch = []
            for index in range(first, first + count):
                channel = channels[index % len(channels)]
                body = "%08d %s" % (index, payload)
                wire = (":peer!u@localhost PRIVMSG %s :%s\r\n" %
                        (channel, body)).encode("ascii")
                assert len(wire) <= 512
                batch.append(wire)
                if len(batch) == 200:
                    block = b"".join(batch)
                    session.server.send_bytes(block)
                    sent_bytes[0] += len(block)
                    batch.clear()
            if batch:
                block = b"".join(batch)
                session.server.send_bytes(block)
                sent_bytes[0] += len(block)
            session.server.send("PING :" + marker)
        except Exception as error:
            failures.append(str(error))
        finally:
            done.set()

    before = linux_sample(session.proc.pid)
    started = time.monotonic()
    sender = threading.Thread(target=send_messages, daemon=True)
    sender.start()
    probe_started = None
    probe_latency = None
    initial_line_count = len(session.server.lines)
    while not (done.is_set() and pong_seen(session, marker) and probe_latency is not None):
        session.pump(0.002)
        now = time.monotonic()
        if probe_started is None and now - started >= 0.02:
            initial_line_count = len(session.server.lines)
            probe_started = now
            session.fast = False
            session.command("/nick tester")
            session.fast = True
        if probe_started is not None and probe_latency is None:
            for at, line in session.server.lines[initial_line_count:]:
                if line == b"NICK tester":
                    probe_latency = max(0, at - probe_started)
                    break
        if failures:
            raise AssertionError("stress sender failed: " + repr(failures))
        if session.proc.poll() is not None:
            raise AssertionError("client exited during incoming message stress")
        if now >= deadline:
            raise AssertionError("benchmark deadline exceeded; no completion marker")
    elapsed = time.monotonic() - started
    sender.join(timeout=1)
    if sender.is_alive():
        raise AssertionError("stress sender did not finish")
    after = linux_sample(session.proc.pid)
    result = {"phase": phase, "cumulative_messages": first + count,
              "messages": count, "wire_bytes": sent_bytes[0],
              "elapsed_seconds": round(elapsed, 6),
              "messages_per_second": round(count / elapsed, 2),
              "input_command_latency_ms": round(1000 * probe_latency, 3)}
    result.update(metric_delta(before, after, elapsed))
    return result


def shared_server_measurement(channels, duration, count):
    """Measure one real process with separate active loopback connections."""
    session = BenchSession(channels, count)
    try:
        session.start()
        fixtures = [session.server] + session.additional_servers
        for number, fixture in enumerate(fixtures):
            session.wait(lambda fixture=fixture: fixture.registered and
                sum(line.startswith(b"JOIN ") for unused, line in fixture.lines) >= len(channels),
                "shared-process server did not register/join")
            marker = "shared-ready-%d" % number
            fixture.send("PING :" + marker)
            expected = ("PONG :" + marker).encode("ascii")
            session.wait(lambda fixture=fixture, expected=expected:
                any(line == expected for unused, line in fixture.lines), "shared-process PING not serviced")
        idle, unused = measure_idle([session], duration)
        result = dict(idle["per_process"][0])
        result.update(server_count=count, process_count=1, conversation_count_per_server=len(channels),
                      elapsed_seconds=idle["elapsed_seconds"])
        probes = []
        for number, fixture in enumerate(fixtures):
            marker = "shared-probe-%d" % number
            expected = ("PONG :" + marker).encode("ascii")
            started = time.monotonic()
            fixture.send("PING :" + marker)
            session.wait(lambda fixture=fixture, expected=expected:
                any(line == expected for unused, line in fixture.lines), "background PING not serviced")
            probes.append(round(1000 * (time.monotonic() - started), 3))
        result["ping_roundtrip_ms"] = probes
        result["all_background_pings_serviced"] = True
    finally:
        session.close()
    result["client_exit_status"] = session.proc.returncode
    if session.proc.returncode != 0:
        raise AssertionError("shared-process benchmark did not exit normally")
    return result


def run(args):
    if not BINARY.is_file():
        raise SystemExit("Build ./vipirc first (make).")
    channels = ["#test"] + ["#bench%02d" % i for i in range(1, 20)]
    report = {
        "benchmark": "vipirc local PTY idle and bounded-history stress",
        "binary": str(BINARY.resolve()),
        "binary_sha256": hashlib.sha256(BINARY.read_bytes()).hexdigest(),
        "binary_size_bytes": BINARY.stat().st_size,
        "platform": platform.platform(),
        "terminal": {"TERM": "xterm-256color", "rows": 24, "columns": 80},
        "transport": "plain IRC over IPv4 loopback; no external server",
        "history_lines_per_view": 5000,
        "conversation_count": len(channels),
        "idle_instance_count": args.idle_instances,
        "requested_messages": args.messages,
        "measurement": "/proc target-process CPU and RSS" if sys.platform.startswith("linux")
                       else "elapsed time; wait4 child resource usage if available",
        "limitations": [
            "Short synthetic run, not a long-duration soak or security audit.",
            "No ircii baseline or direct performance comparison.",
            "Plain local transport does not measure TLS record overhead.",
            "Retained-history bounds are not a total RSS cap.",
            "Input latency includes the fixture and scheduler, and uses /nick tester.",
            "During stress the PTY is drained without emulating every screen update.",
        ],
    }
    s = BenchSession(channels)
    extras = []
    try:
        s.start()
        s.input_mode()
        s.wait(lambda: sum(line.startswith(b"JOIN ") for _, line in s.server.lines) >= len(channels),
               "benchmark channels not joined")
        s.server.send("PING :bench-ready")
        s.wait(lambda: pong_seen(s, "bench-ready"), "benchmark setup not completed")
        s.quiet(0.1)
        for index in range(1, args.idle_instances):
            extra = BenchSession(channels)
            extras.append(extra)
            extra.start()
            extra.input_mode()
            extra.wait(lambda: sum(line.startswith(b"JOIN ") for _, line in extra.server.lines) >= len(channels),
                       "additional benchmark channels not joined")
            marker = "bench-idle-ready-%d" % index
            extra.server.send("PING :" + marker)
            extra.wait(lambda: pong_seen(extra, marker), "additional idle client not ready")
        idle, before = measure_idle([s] + extras, args.idle_seconds)
        idle["conversation_count_per_instance"] = len(channels)
        report["idle_instances"] = idle
        report["idle"] = {"elapsed_seconds": idle["elapsed_seconds"]}
        report["idle"].update(idle["per_process"][0])
        if before[0]:
            report["cpu_tick_seconds"] = before[0]["cpu_tick_seconds"]
        other_exits = []
        while extras:
            extra = extras.pop()
            extra.close()
            other_exits.append(extra.proc.returncode)
            if extra.proc.returncode != 0:
                raise AssertionError("additional idle client did not exit normally")
        idle["other_client_exit_statuses"] = other_exits
        s.server.socket.settimeout(0.5)
        deadline = time.monotonic() + args.timeout
        phases = []
        first = 0
        for phase in range(1, args.phases + 1):
            count = args.messages // args.phases + (1 if phase <= args.messages % args.phases else 0)
            phases.append(stress_phase(s, channels, first, count, phase, deadline))
            first += count
        report["stress_phases"] = phases
        report["total_stress_wire_bytes"] = sum(p["wire_bytes"] for p in phases)
        report["total_stress_elapsed_seconds"] = round(sum(p["elapsed_seconds"] for p in phases), 6)
        report["max_input_command_latency_ms"] = max(p["input_command_latency_ms"] for p in phases)
        s.server.send(":peer!u@localhost PRIVMSG #test :BENCHMARK COMPLETE: input and redraw responsive")
        s.server.send("PING :bench-finish")
        s.wait(lambda: pong_seen(s, "bench-finish"), "final benchmark marker not processed")
        s.reconstruct_screen()
        s.wait(lambda: "BENCHMARK COMPLETE" in s.screen.text(), "final screen failed to redraw")
        report["final_redraw_verified"] = True
        report["terminal_output_bytes"] = s.terminal_output_bytes
        report["final_process_sample"] = linux_sample(s.proc.pid)
    finally:
        for extra in extras:
            extra.close()
        s.close()
    report["client_exit_status"] = s.proc.returncode
    if s.usage is not None:
        report["child_resource_usage"] = {
            "user_cpu_seconds": round(s.usage.ru_utime, 6),
            "system_cpu_seconds": round(s.usage.ru_stime, 6),
            "maxrss_native": s.usage.ru_maxrss,
            "maxrss_unit": "KiB" if sys.platform.startswith(("linux", "freebsd", "openbsd", "netbsd", "dragonfly"))
                           else "platform-native units",
        }
    if s.proc.returncode != 0:
        raise AssertionError("client did not exit normally: %r" % s.proc.returncode)
    report["shared_process_servers"] = shared_server_measurement(channels, args.idle_seconds, args.shared_servers)
    if hashlib.sha256(BINARY.read_bytes()).hexdigest() != report["binary_sha256"]:
        raise AssertionError("binary changed during measurement; rebuild, then rerun the benchmark")
    report["binary_unchanged_during_run"] = True
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--messages", type=int, default=100000)
    parser.add_argument("--phases", type=int, default=4)
    parser.add_argument("--idle-seconds", type=float, default=3.0)
    parser.add_argument("--idle-instances", type=int, choices=(1, 2, 3), default=1,
                        help="independent servers and real PTYs in the simultaneous idle measurement")
    parser.add_argument("--shared-servers", type=int, choices=range(1, 9), default=3,
                        help="connected servers inside one process in the additional idle measurement")
    parser.add_argument("--timeout", type=float, default=30.0,
                        help="stress deadline in seconds; excludes setup and idle")
    args = parser.parse_args()
    if not 4 <= args.messages <= 2000000 or not 1 <= args.phases <= min(20, args.messages):
        parser.error("messages must be 4..2000000 and phases 1..min(20,messages)")
    if not 0.1 <= args.idle_seconds <= 30 or not 1 <= args.timeout <= 120:
        parser.error("idle-seconds must be 0.1..30 and timeout 1..120")
    print(json.dumps(run(args), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
