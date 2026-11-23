#!/usr/bin/env python3
"""Check review waiting and retained traffic with a real PTY/local IRC peer."""
import argparse
import json
from pathlib import Path
import resource
import time

import test_idle_terminal as terminal
from process_stats import ProcessStatsError, sample_child


def child_cpu():
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime


def review_wait(binary, full):
    if full:
        terminal.FULL_BINARY = binary
    else:
        terminal.BINARY = binary
    started_cpu = child_cpu()
    with terminal.IdleSession(live=True, review=True, full=full) as session:
        session.start()
        draft = "retain this review draft"
        session.type(draft)
        session.wait(lambda: session.editor(count=len(draft)), "draft not shown")
        session.type(b"\r")
        session.wait(lambda: "Send this message?" in session.screen.text(), "review not shown")
        session.quiet(.3)
        marker = "REVIEW_HELD_REMOTE_RECORD"
        session.peer.send(":remote!u@host PRIVMSG #test :" + marker)
        session.peer.send("PING :review-wait-start")
        session.wait(lambda: any(line == b"PONG :review-wait-start" for _, line in session.peer.lines),
                     "review stopped network keepalives")
        try:
            first = sample_child(session.proc.pid, binary, include_children=False)
        except ProcessStatsError:
            first = None
        started = time.monotonic()
        session.quiet(2)
        elapsed = time.monotonic() - started
        last = sample_child(session.proc.pid, binary, include_children=False) if first else None
        steady_cpu = last["cpu_seconds"] - first["cpu_seconds"] if first else None
        assert "Send this message?" in session.screen.text(), "incoming text replaced the review"
        assert marker not in session.screen.text(), "held chat leaked onto the review screen"
        assert not session.peer.messages(), "waiting for review sent the draft"
        session.peer.send("PING :review-wait-end")
        session.wait(lambda: any(line == b"PONG :review-wait-end" for _, line in session.peer.lines),
                     "review no longer handles keepalives")
        session.type(b"\x1b")
        session.wait(lambda: session.editor(count=len(draft)), "review Escape lost the draft")
        session.quiet(.6)
        session.type(b"\x1b")
        session.wait(lambda: not session.screen.alternate and marker in session.screen.text(),
                     "leaving review lost retained remote traffic")
        assert session.screen.text().count(marker) == 1, "retained remote record duplicated"
        assert not session.peer.messages(), "cancelling the draft sent a message"
    total_cpu = child_cpu() - started_cpu
    result = {"case": "review_wait", "wall_seconds": round(elapsed, 3),
              "steady_client_cpu_seconds": round(steady_cpu, 3) if steady_cpu is not None else None,
              "whole_child_cpu_seconds": round(total_cpu, 3),
              "identity_verified": first is not None, "retained_message_once": True,
              "draft_retained": True, "unexpected_sends": 0}
    print(json.dumps(result, sort_keys=True), flush=True)
    # The fallback counts the complete child lifetime (including startup),
    # allowing BSD hosts to run this check without Linux /proc. A quiet review
    # should sleep; a core-consuming zero-timeout poll loop exceeds this budget.
    observed = steady_cpu if steady_cpu is not None else total_cpu
    budget = elapsed * .35 + (.1 if steady_cpu is not None else .3)
    assert observed <= budget, ("review busy-looped with held remote traffic", observed, budget)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--full", action="store_true", help="exercise vipirc -i")
    args = parser.parse_args()
    review_wait(args.binary.resolve(), args.full)
    print(json.dumps({"result": "PASS", "frontend": "vipirc -i" if args.full else "onekeyirc"}), flush=True)


if __name__ == "__main__":
    main()
