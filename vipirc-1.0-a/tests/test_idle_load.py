#!/usr/bin/env python3
"""Bounded multi-server load/recovery through a real minimalist PTY.

Private loopback peers and temporary configurations only. This is a short
stress check, not a long-running production soak or a full-curses test.
"""
import argparse
from collections import deque
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import select
import socket
import ssl
import tempfile
import threading
import time

import test_idle_terminal as terminal
from process_stats import ProcessStatsError, sample_child
from test_net import cert_pair


class Peer(terminal.IRCFixture):
    """One thread owns all socket/TLS operations, including queued controls."""
    def __init__(self, channels, certificate=None, key=None):
        self.channels = channels
        self.outgoing = deque()
        self.out_lock = threading.Lock()
        self.restart = threading.Event()
        self.flood = False
        self.generations = self.messages_attempted = self.messages_sent = self.wire_bytes = 0
        self.joined = set()
        super().__init__(certificate=certificate, key=key)

    def send_bytes(self, data):
        with self.out_lock:
            assert len(self.outgoing) < 256, 'private fixture queue exceeded its bound'
            self.outgoing.append(bytes(data))

    def handle(self, line):
        assert not line.startswith((b'PRIVMSG ', b'NOTICE ')), 'unsent draft became an outgoing message'
        super().handle(line)
        if line.startswith(b'JOIN '):
            self.joined.update(line.split()[1].decode('ascii').split(','))

    def run(self):
        connection = None
        context = None
        try:
            if self.certificate:
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.minimum_version = ssl.TLSVersion.TLSv1_2
                context.load_cert_chain(self.certificate, self.key)
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
                self.joined.clear()
                self.generations += 1
                pending = b''
                next_message = time.monotonic()
                while not self.stop.is_set() and not self.restart.is_set():
                    for unused in range(32):
                        with self.out_lock:
                            block = self.outgoing.popleft() if self.outgoing else None
                        if block is None:
                            break
                        connection.sendall(block)
                        self.wire_bytes += len(block)
                    now = time.monotonic()
                    if self.flood and len(self.joined) == len(self.channels) and now >= next_message:
                        frames = []
                        for unused in range(2):
                            serial = self.messages_attempted
                            channel = self.channels[serial % len(self.channels)]
                            body = 'load-%09d %s' % (serial, 'x' * 180)
                            if serial % 97 == 0:
                                body += '\x1b]52;c;ZmFrZQ==\x07'
                            frames.append((':peer!u@localhost PRIVMSG %s :%s\r\n' % (channel, body)).encode('ascii'))
                            self.messages_attempted += 1
                        block = b''.join(frames)
                        connection.sendall(block)
                        self.messages_sent += len(frames)
                        self.wire_bytes += len(block)
                        next_message = now + .025
                    ready = context and connection.pending()
                    if not ready and not select.select([connection], [], [], .005)[0]:
                        continue
                    data = connection.recv(65536)
                    if not data:
                        break
                    pending += data
                    assert len(pending) < 65536
                    while b'\r\n' in pending:
                        line, pending = pending.split(b'\r\n', 1)
                        assert len(line) <= 510 and not any(c in line for c in (b'\0', b'\r', b'\n'))
                        self.lines.append((time.monotonic(), line))
                        if len(self.lines) > 4096:
                            del self.lines[:2048]
                        self.handle(line)
                self.registered = False
                self.socket = None
                connection.close()
                connection = None
                self.restart.clear()
                with self.out_lock:
                    self.outgoing.clear()
        except Exception as error:
            if not self.stop.is_set():
                self.errors.append(repr(error))
        finally:
            if connection is not None:
                connection.close()
            self.socket = None
            self.listener.close()

    def close(self):
        # The peer thread owns TLS shutdown as well as reads/writes. Its
        # longest socket operation has a three-second timeout.
        self.stop.set()
        self.thread.join(timeout=4)
        assert not self.thread.is_alive(), 'private fixture thread did not exit'
        assert not self.errors, self.errors


def check_output(session):
    assert b'\x1b]52;' not in session.raw, 'remote terminal control reached output'
    assert b'AddressSanitizer' not in session.raw and b'runtime error:' not in session.raw, \
        'sanitizer diagnostic reached the terminal'


def bounded_pump(session, timeout=.01):
    session.pump(timeout)
    assert session.proc.poll() is None, 'client exited during load'
    check_output(session)
    if len(session.raw) > 65536:
        del session.raw[:-65536]


def bounded_quiet(session, seconds):
    end = time.monotonic() + seconds
    while True:
        remaining = end - time.monotonic()
        if remaining <= 0:
            break
        bounded_pump(session, min(.03, remaining))


def pings(session, peers, serial):
    start = time.monotonic()
    tags = ['load-probe-%d-%d' % (serial, i) for i in range(len(peers))]
    for peer, tag in zip(peers, tags):
        peer.send('PING :' + tag)
    pending = set(range(len(peers)))
    times = {}
    while pending:
        bounded_pump(session)
        for i in list(pending):
            assert not peers[i].errors, peers[i].errors
            for at, line in reversed(peers[i].lines):
                if at < start:
                    break
                if line == ('PONG :' + tags[i]).encode('ascii'):
                    times[i] = round((at - start) * 1000, 3)
                    pending.remove(i)
                    break
        assert time.monotonic() - start < 10, ('PING deadline', pending)
    return [times[i] for i in range(len(peers))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--full', action='store_true', help='exercise vipirc -i')
    parser.add_argument('--tls', action='store_true')
    parser.add_argument('--seconds', type=int, default=60)
    parser.add_argument('--servers', type=int, choices=range(1, 9), default=8)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    assert 12 <= args.seconds <= 600
    binary = args.binary.resolve(strict=True)
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    harness_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    if args.full:
        terminal.FULL_BINARY = binary
    else:
        terminal.BINARY = binary
    channels = ['#test'] + ['#load%d' % i for i in range(1, 8)]
    draft = 'unsent stability draft 0123456789 - keep across reconnect and resize'
    samples, timings, errors, reconnects = [], [], [], []
    with tempfile.TemporaryDirectory(prefix='irc-idle-load-') as temporary, ExitStack() as cleanup:
        directory = Path(temporary)
        certificate, key = cert_pair(directory, 'localhost', 'DNS:localhost,IP:127.0.0.1') if args.tls else (None, None)
        peers = []
        for unused in range(args.servers):
            peer = Peer(channels, certificate, key)
            peers.append(peer)
            cleanup.callback(peer.close)
        config = directory / 'config.ini'
        profiles = Path(str(config) + '.d')
        profiles.mkdir(mode=0o700)
        names = []
        for i, peer in enumerate(peers):
            path = config if not i else profiles / ('load-%d.ini' % i)
            path.write_text('[connection]\nserver=127.0.0.1\nport=%d\ntls=%s\nca_file=%s\n'
                            'nick=load%d\nusername=tester\nchannel=%s\nautoconnect=true\nauth=none\n'
                            '[history]\nlines=50\n[input]\nsend_mode=review\ncomposer_mode=fullscreen\n' %
                            (peer.port, 'true' if args.tls else 'false', certificate or '', i, ','.join(channels)))
            path.chmod(0o600)
            if i:
                names.append(path.name)
        catalog = Path(str(config) + '.servers')
        catalog.write_text('[servers]\n' + ''.join('profile=%s\n' % name for name in names))
        catalog.chmod(0o600)
        command = (['-i'] if args.full else []) + ['--config', str(config), '--reconnect']
        began = time.monotonic()
        try:
            with terminal.IdleSession(full=args.full, args=command, rows=24, cols=100) as session:
                session.start()
                session.wait(lambda: all(peer.registered and peer.joined == set(channels) for peer in peers),
                             'all private servers must register and autojoin', timeout=15)
                session.type(draft)
                session.wait(lambda: session.editor(draft, len(draft)), 'initial draft missing')
                for peer in peers:
                    peer.flood = True
                started = time.monotonic()
                actions = set()
                required_actions = {'review', 'restart-0', 'edit', 'restart-last', 'resize'}
                next_probe = next_sample = started
                # Reconnect waits can extend past a short smoke duration.
                # Finish every bounded recovery action before ending the run.
                while time.monotonic() - started < args.seconds or actions != required_actions:
                    bounded_pump(session)
                    elapsed = time.monotonic() - started
                    assert all(not peer.errors for peer in peers), [peer.errors for peer in peers]
                    if time.monotonic() >= next_probe:
                        timings.append(pings(session, peers, len(timings)))
                        next_probe = time.monotonic() + 10
                    if time.monotonic() >= next_sample:
                        try:
                            sample = sample_child(session.proc.pid, binary)
                            sample['elapsed_seconds'] = round(elapsed, 3)
                            samples.append(sample)
                        except ProcessStatsError as error:
                            if str(error) not in errors:
                                errors.append(str(error))
                        next_sample = time.monotonic() + 10
                    if elapsed >= args.seconds / 5 and 'review' not in actions:
                        session.type('\r')
                        session.wait(lambda: 'Send this message?' in session.screen.text(), 'review missing')
                        actions.add('review')
                    if elapsed >= args.seconds / 3 and 'restart-0' not in actions:
                        before = peers[0].generations
                        peers[0].restart.set()
                        session.wait(lambda: peers[0].generations > before and peers[0].joined == set(channels),
                                     'primary reconnect/rejoin failed', timeout=15)
                        reconnects.append(0)
                        actions.add('restart-0')
                    if elapsed >= args.seconds / 2 and 'edit' not in actions:
                        session.type(b'\x1b')
                        session.wait(lambda: session.editor(draft, len(draft)), 'review recovery lost draft')
                        actions.add('edit')
                    if elapsed >= args.seconds * 2 / 3 and 'restart-last' not in actions:
                        before = peers[-1].generations
                        peers[-1].restart.set()
                        session.wait(lambda: peers[-1].generations > before and peers[-1].joined == set(channels),
                                     'secondary reconnect/rejoin failed', timeout=15)
                        reconnects.append(len(peers) - 1)
                        actions.add('restart-last')
                    if elapsed >= args.seconds * 3 / 4 and 'resize' not in actions:
                        session.resize(4, 10)
                        bounded_quiet(session, .2)
                        session.resize(24, 100)
                        session.wait(lambda: session.editor(draft, len(draft)), 'resize lost draft')
                        actions.add('resize')
                for peer in peers:
                    peer.flood = False
                timings.append(pings(session, peers, len(timings)))
                session.wait(lambda: session.editor(draft, len(draft)), 'final draft changed')
                bounded_quiet(session, .5)
                first = last = None
                if samples:
                    try:
                        first = sample_child(session.proc.pid, binary)
                    except ProcessStatsError as error:
                        if str(error) not in errors:
                            errors.append(str(error))
                quiet_start = time.monotonic()
                bounded_quiet(session, 2)
                if first:
                    try:
                        last = sample_child(session.proc.pid, binary)
                    except ProcessStatsError as error:
                        if str(error) not in errors:
                            errors.append(str(error))
                quiet_seconds = time.monotonic() - quiet_start
                quiet_cpu = last['cpu_seconds'] - first['cpu_seconds'] if last else None
                if quiet_cpu is not None:
                    assert quiet_cpu <= quiet_seconds * .35 + .1, ('quiet client busy-looped', quiet_cpu, quiet_seconds)
                assert actions == required_actions
                assert all(peer.messages_sent >= 100 for peer in peers)
                assert all(not peer.messages(channel) for peer in peers for channel in channels)
                # Composer/review deliberately holds chat display. Require a
                # known hostile record to be rendered after cancelling the
                # preserved draft, so the sanitization assertion is exercised.
                marker = 'visible-sanitization-probe'
                peers[0].send(':peer!u@localhost PRIVMSG #test :' + marker + ' \x1b]52;c;ZmFrZQ==\x07')
                timings.append(pings(session, peers, len(timings)))
                session.cancel()
                expected = (marker + ' ?]52;c;ZmFrZQ==?').encode('ascii')
                session.wait(lambda: expected in session.raw, 'hostile display probe did not render safely')
                check_output(session)
                assert session.proc.poll() is None, 'client exited before cleanup'
        finally:
            # All callbacks run even if one peer reports a fixture failure.
            # The outer ExitStack also covers earlier configuration failures.
            cleanup.close()
        assert all(peer.messages_attempted == peer.messages_sent for peer in peers)
        assert hashlib.sha256(binary.read_bytes()).hexdigest() == binary_hash, 'binary changed during load test'
        assert hashlib.sha256(Path(__file__).read_bytes()).hexdigest() == harness_hash, 'load harness changed during test'
        report = {'status': 'PASS', 'scope': 'Private TLS/TCP peers and actual minimalist PTY; bounded load, not long soak.',
                  'binary_sha256': binary_hash, 'harness_sha256': harness_hash,
                  'frontend': 'vipirc -i' if args.full else 'onekeyirc', 'tls': args.tls,
                  'requested_seconds': args.seconds, 'elapsed_seconds': round(time.monotonic() - began, 3),
                  'servers': len(peers), 'channels_per_server': len(channels),
                  'messages_attempted': [peer.messages_attempted for peer in peers],
                  'messages_sent': [peer.messages_sent for peer in peers],
                  'traffic_count_scope': 'Peer load PRIVMSG attempts and completed sendall frames; not counts of messages rendered by the client.',
                  'reconnected_servers': reconnects,
                  'ping_milliseconds': timings, 'draft_preserved': True, 'unexpected_chat_sends': 0,
                  'sanitized_record_rendered': True, 'terminal_restored': True,
                  'rendered_probe_scope': 'One additional hostile marker from primary server #test was verified on the terminal after draft cancellation.',
                  'quiet_cpu_checked': quiet_cpu is not None,
                  'quiet_cpu_seconds': quiet_cpu, 'quiet_wall_seconds': quiet_seconds,
                  'process_samples': samples, 'measurement_limits': errors,
                  'process_measurement_scope': 'Identity-checked client CPU/RSS samples; unavailable fd/executable fields remain null with their error. Quiet CPU is checked only when both samples are available.'}
        if args.output:
            args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({key: value for key, value in report.items() if key not in ('process_samples', 'ping_milliseconds')},
                         sort_keys=True), flush=True)


if __name__ == '__main__':
    main()
