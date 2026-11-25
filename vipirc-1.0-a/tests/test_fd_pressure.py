#!/usr/bin/env python3
"""Whole-client descriptor exhaustion: only child limits are reduced."""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import time

from bench import BenchSession
from process_stats import sample_child
from test_integration import BINARY
from test_servers import in_servers, open_servers, select_server_row, server_rows


def process_state(session):
    assert session.proc.poll() is None, ('client exited during fd pressure', session.proc.returncode,
                                         session.screen.text(), bytes(session.raw[-1200:]))
    state = sample_child(session.proc.pid, BINARY)
    assert state['identity_verified'] and state['exe_verified'], state
    assert state['fd_count'] is not None, state
    return state


def settled_servers(session, peers):
    """Wait for completed local attempts, not a fixed startup sleep."""
    last_state = {}
    pending_labels = ('resolving', 'connecting', 'registering', 'verifying TLS')

    def settled():
        nonlocal last_state
        process_state(session)
        rows = server_rows(session)
        if len(rows) != len(peers) or any(label in row for row in rows.values() for label in pending_labels):
            return False
        connected_rows = sum('plaintext connected' in row for row in rows.values())
        if connected_rows != sum(peer.registered for peer in peers):
            return False
        last_state = process_state(session)
        return last_state['child_count'] == 0

    session.wait(settled, 'server attempts did not settle under the descriptor limit', timeout=6)
    return last_state


def run(expected_sha256=None):
    before = resource.getrlimit(resource.RLIMIT_NOFILE)
    binary_hash = hashlib.sha256(BINARY.read_bytes()).hexdigest()
    if expected_sha256 is not None:
        assert binary_hash == expected_sha256, (binary_hash, expected_sha256)
    results = []
    for limit in (5, 8, 16):
        assert hashlib.sha256(BINARY.read_bytes()).hexdigest() == binary_hash, 'binary changed between cases'
        s = BenchSession(['#test'], server_count=8, fd_limit=limit)
        began = time.monotonic()
        try:
            s.wait(lambda: b'Not vegetarian?' in s.raw, 'resource limit blocked startup before its welcome')
            welcome = process_state(s)
            assert welcome['child_count'] == 0
            s.type('x')
            s.wait(lambda: s.mode() == 'INPUT', 'resource failure prevented usable controls')
            peers = [s.server] + s.additional_servers
            open_servers(s)
            state = settled_servers(s, peers)
            registered = [peer for peer in peers if peer.registered]
            failed_slots = [i for i, peer in enumerate(peers) if not peer.registered]
            diagnoses = []
            if limit == 16:
                assert len(registered) == 8, 'ample descriptor budget did not connect every session'
            else:
                # At a tiny budget, even every connection may legitimately
                # fail. The controls must still work and explain exhaustion.
                assert len(registered) < 8, 'fixture failed to exercise descriptor exhaustion'
                for slot in failed_slots:
                    select_server_row(s, slot)
                    s.quiet(0.025)
                    diagnoses.append({'slot': slot, 'status': ''.join(s.screen.cells[-1]).rstrip()})
                assert any('Too many open files' in item['status'] for item in diagnoses), (
                    'descriptor exhaustion had no visible diagnosis', diagnoses, s.screen.text())
            assert state['fd_count'] <= limit and state['child_count'] == 0, state
            assert state['fd_count'] == welcome['fd_count'] + len(registered), (
                'failed attempts retained descriptors', welcome, state, len(registered))
            for i, peer in enumerate(registered):
                token = 'fd-pressure-%d-%d' % (limit, i)
                peer.send('PING :' + token)
                expected = ('PONG :' + token).encode('ascii')
                s.wait(lambda peer=peer, expected=expected: any(line == expected for unused,line in peer.lines),
                       'resource failure froze a healthy connection')
            s.type(b'\x1b')
            s.wait(lambda: not in_servers(s), 'server list did not return to the main controls')
            s.input_mode(); s.type('retained draft after fd pressure')
            s.wait(lambda: 'retained draft after fd pressure' in s.input_text(), 'editor unusable after descriptor failure')
            open_servers(s)
            s.type(b'\x1b')
            s.wait(lambda: 'retained draft after fd pressure' in s.input_text(), 'server-list return lost draft')
            s.navigate('?')
            s.wait(lambda: 'Manual' in s.screen.text(), 'manual unavailable after resource failure')
            s.type(b'\x1b')
            s.wait(lambda: s.panel_title().startswith('vipirc |'),
                   'manual did not return to its vipirc parent after descriptor failure')
            s.leave_app()
            s.wait(lambda: 'retained draft after fd pressure' in s.input_text(), 'manual return lost draft')
            after = process_state(s)
            assert after['fd_count'] == state['fd_count'] and after['child_count'] == 0, after
            results.append({'limit':limit,'connected_servers':len(registered), 'fd_count':after['fd_count'],
                            'failed_servers':len(failed_slots),'visible_diagnoses':diagnoses,
                            'welcome_fd_count':welcome['fd_count'],'retained_failed_attempt_fds':0,
                            'children':after['child_count'],'rss_kib':after['rss_kib'],
                            'seconds':round(time.monotonic()-began,3),'draft_preserved':True,
                            'manual_usable':True,'healthy_connections_answered_ping':len(registered),
                            'proc_identity_verified':after['identity_verified'] and after['exe_verified']})
        finally:
            s.close()
        assert s.proc.returncode == 130 and s.terminal_restored
        results[-1]['terminal_restored'] = True
        results[-1]['exit_status'] = s.proc.returncode
    assert resource.getrlimit(resource.RLIMIT_NOFILE) == before
    assert hashlib.sha256(BINARY.read_bytes()).hexdigest() == binary_hash, 'binary changed during test'
    return {'binary_sha256':binary_hash, 'status':'passed',
            'parent_limits_unchanged':True,'results':results}


if __name__=='__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--expected-sha256')
    parser.add_argument('--output', type=Path)
    arguments = parser.parse_args()
    report = json.dumps(run(arguments.expected_sha256),indent=2,sort_keys=True) + '\n'
    if arguments.output is not None:
        arguments.output.write_text(report, encoding='ascii')
    print(report, end='')
