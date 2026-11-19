#!/usr/bin/env python3
"""Real detached-worker CPU and PING check for failed automatic-away output.

Uses private loopback IRC and LossSession's real managed SIGHUP detach. Initial
managed-state setup and descriptor reattachment are fixtures, so this does not
validate the native AF_UNIX session handshake. Verified /proc samples measure
only the worker; portable fallback counts its complete reaped child lifetime.
"""
import argparse
import json
import math
import os
from pathlib import Path
import resource
import signal
import time

from process_stats import ProcessStatsError, sample_child
from test_idle_hangup import LossSession


FAILURE = b"Outgoing command exceeds the protocol byte limit."


def child_cpu():
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime


def ping_latency(session, marker):
    expected = b"PONG :" + marker.encode("ascii")
    started = time.monotonic()
    session.peer.send("PING :" + marker)
    session.wait(lambda: any(line == expected for _, line in session.peer.lines),
                 "detached automatic-away retry blocked IRC PING", timeout=2)
    arrived = next(at for at, line in session.peer.lines if line == expected)
    return arrived - started


def detached_presence_wait(binary, seconds=3.0):
    assert binary.is_file() and os.access(binary, os.X_OK), binary
    started_cpu = child_cpu()
    first = last = None
    sample_errors = []
    latencies = []
    with LossSession(binary) as session:
        session.start()
        # Keep the long setting visible on one row for the shared command
        # helper's exact-text assertion. This worker has no controlling tty,
        # so deliver the resize signal explicitly after changing the PTY.
        session.resize(18, 160)
        session.proc.send_signal(signal.SIGWINCH)
        session.command("/set background_away on", "Presence preference changed")
        session.command("/set background_away_message " + "A" * 100,
                        "Presence preference changed")
        session.peer.send(":srv 005 tester LINELEN=64 :supported")
        ping_latency(session, "presence-limit-installed")
        wire_start = len(session.peer.lines)

        detached_at = time.monotonic()
        session.proc.send_signal(signal.SIGHUP)
        session.wait_detached()
        latencies.append(ping_latency(session, "presence-detached-ready"))
        try:
            first = sample_child(session.proc.pid, binary, include_children=False)
        except ProcessStatsError as error:
            sample_errors.append(str(error))

        started = time.monotonic()
        deadline = started + seconds
        next_ping = started
        ping_count = 0
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_ping:
                latencies.append(ping_latency(session, "presence-tick-%d" % ping_count))
                ping_count += 1
                next_ping = started + ping_count * .5
            remaining = min(deadline, next_ping) - time.monotonic()
            if remaining > 0:
                session.pump(min(.03, remaining))
        elapsed = time.monotonic() - started
        if first is not None:
            try:
                last = sample_child(session.proc.pid, binary, include_children=False)
            except ProcessStatsError as error:
                sample_errors.append(str(error))
        if last is not None:
            assert first["outer_pid"] == last["outer_pid"]
            assert first["start_ticks"] == last["start_ticks"], "worker identity changed during sample"
            assert last["cpu_seconds"] >= first["cpu_seconds"]
            steady_cpu = last["cpu_seconds"] - first["cpu_seconds"]
        else:
            steady_cpu = None
        latencies.append(ping_latency(session, "presence-detached-end"))

        # Reattach only after the CPU interval. This stops automatic-away
        # retries and exposes the retained status records without injecting
        # a hook or adding logging to production code.
        session.reattach()
        resumed_after = time.monotonic() - detached_at
        session.wait(lambda: FAILURE in session.raw,
                     "reduced LINELEN did not cause the intended away failure")
        latencies.append(ping_latency(session, "presence-restored"))
        session.quiet(.1)
        failures_retained = session.raw.count(FAILURE)
        # This is an upper bound on visible retained records, not an exact
        # callback counter: an unfixed loop can roll over bounded history.
        failure_budget = math.ceil(resumed_after) + 2
        observed_wire = [line for _, line in session.peer.lines[wire_start:]]
        away_lines = [line.decode("ascii", "replace") for line in observed_wire
                      if line == b"AWAY" or line.startswith(b"AWAY ")]
        oversized = [len(line) + 2 for line in observed_wire if len(line) + 2 > 64]
        unexpected_chat = session.peer.messages()
        assert session.proc.poll() is None, "presence retry terminated the worker"
    total_cpu = child_cpu() - started_cpu

    # Allow scheduler variation and conservative whole-lifetime startup cost
    # on hosts without verified /proc, while rejecting a core-consuming loop.
    observed = steady_cpu if steady_cpu is not None else total_cpu
    cpu_budget = elapsed * .35 + (.1 if steady_cpu is not None else .3)
    result = {
        "case": "detached_failed_automatic_away", "wall_seconds": round(elapsed, 6),
        "detached_seconds": round(resumed_after, 6),
        "steady_client_cpu_seconds": round(steady_cpu, 6) if steady_cpu is not None else None,
        "whole_child_cpu_seconds": round(total_cpu, 6),
        "cpu_budget_seconds": round(cpu_budget, 6),
        "cpu_fraction": round(observed / elapsed, 6),
        "cpu_source": "identity_verified_proc" if steady_cpu is not None else "whole_child_rusage",
        "identity_verified": last is not None,
        "identity": {key: first[key] for key in
                     ("pid", "outer_pid", "outer_parent_pid", "namespace_depth", "start_ticks", "exe_verified")}
                    if last is not None else None,
        "sample_errors": sample_errors,
        "ping_count": len(latencies),
        "ping_latencies_ms": [round(value * 1000, 3) for value in latencies],
        "max_ping_latency_ms": round(max(latencies) * 1000, 3),
        "retained_failure_notices": failures_retained,
        "retained_failure_notice_budget": failure_budget,
        "away_lines": away_lines, "overlimit_wire_lengths": oversized,
        "unexpected_chat_count": len(unexpected_chat),
        "real_loopback_irc": True, "real_managed_sighup_detach": True,
        "descriptor_reattachment_fixture": True, "native_af_unix_handshake_tested": False,
    }
    violations = []
    if observed > cpu_budget:
        violations.append("failed automatic-away output busy-looped")
    if failures_retained > failure_budget:
        violations.append("automatic-away retry status was not bounded")
    if max(latencies) >= 1:
        violations.append("PING response exceeded one second")
    if away_lines or oversized:
        violations.append("unusable automatic-away output reached the server")
    if unexpected_chat:
        violations.append("test unexpectedly sent chat")
    result["violations"] = violations
    result["result"] = "FAIL" if violations else "PASS"
    print(json.dumps(result, sort_keys=True), flush=True)
    assert not violations, violations
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--seconds", type=float, default=3.0,
                        help="steady detached CPU interval (3 to 10 seconds)")
    args = parser.parse_args()
    if not 3 <= args.seconds <= 10:
        parser.error("--seconds must be between 3 and 10")
    detached_presence_wait(args.fixture.resolve(), args.seconds)


if __name__ == "__main__":
    main()
