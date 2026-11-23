#!/usr/bin/env python3
"""Real client, verified loopback TLS and PTY bouncer compatibility fixtures.

These are protocol fixtures, not installed ZNC/soju daemons or public accounts.
All credentials are temporary canaries. No external IRC connection is made.
"""
import argparse
import base64
import json
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import time

import test_idle_terminal as terminal
from test_integration import IRCFixture


PASSWORD = 'BNC_CONFIG_PASSWORD_CANARY:with spaces'


class BouncerFixture(IRCFixture):
    def __init__(self, certificate, key, kind, reject=False):
        self.kind, self.reject = kind, reject
        self.sessions = []
        self.cap_requests = []
        self.authenticated = False
        super().__init__(certificate, key, 'failure' if reject else 'success')

    def handle(self, line):
        text = line.decode('utf-8', 'strict')
        session = self.sessions[-1]
        if text.startswith('PASS '):
            assert self.kind == 'znc', 'soju profile sent server PASS'
            assert text[5:].lstrip(':') == 'alice@desktop/ircnow:' + PASSWORD
            session['pass_verified'] = True
            return
        if text.startswith('CAP LS'):
            self.send(':bnc CAP * LS * :server-time batch multi-prefix')
            extra = 'znc.in/self-message' if self.kind == 'znc' else 'sasl=EXTERNAL,PLAIN'
            self.send(':bnc CAP * LS :' + extra + ' echo-message draft/chathistory znc.in/playback')
            return
        if text.startswith('CAP REQ :'):
            requested = text.split(' :', 1)[1].split()
            assert not set(requested) & {'echo-message', 'draft/chathistory', 'znc.in/playback'}
            self.cap_requests.extend(requested)
            self.send(':bnc CAP %s ACK :%s' % (self.nick, ' '.join(requested)))
            return
        if text == 'AUTHENTICATE PLAIN':
            assert self.kind == 'soju'
            self.send('AUTHENTICATE +')
            return
        if text.startswith('AUTHENTICATE '):
            part = text.split(' ', 1)[1]
            if part != '+': self.sasl_data.extend(part.encode('ascii'))
            assert len(self.sasl_data) <= 2048, 'fixture SASL accumulation limit exceeded'
            if len(part) < 400 or part == '+':
                decoded = base64.b64decode(self.sasl_data, validate=True)
                assert decoded == b'\0alice/ircnow@desktop\0' + PASSWORD.encode()
                session['sasl_verified'] = True
                self.authenticated = not self.reject
                self.send(':bnc %s %s :Fixture authentication %s' %
                          ('904' if self.reject else '903', self.nick, 'failed' if self.reject else 'successful'))
            return
        if text == 'CAP END':
            assert session.get('pass_verified') if self.kind == 'znc' else self.authenticated
        was_registered = self.registered
        super().handle(line)
        if self.registered and not was_registered:
            session['registered'] = True
            # Membership belongs to the bouncer, not the client's stale list.
            self.send(':%s!u@fixture JOIN :#test' % self.nick)
            self.names('#test')
        target = '*status' if self.kind == 'znc' else 'BouncerServ'
        if text.startswith('PRIVMSG ' + target + ' :'):
            self.send(':%s!service@fixture NOTICE %s :BNC_SERVICE_REPLY_CANARY' % (target, self.nick))

    def run(self):
        connection = None
        try:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            context.load_cert_chain(self.certificate, self.key)
            while not self.stop.is_set():
                try: connection, _ = self.listener.accept()
                except socket.timeout: continue
                connection.settimeout(3)
                connection = context.wrap_socket(connection, server_side=True)
                connection.settimeout(.05)
                self.socket = connection
                self.nick = 'tester'
                self.has_user = self.cap_end = self.registered = self.authenticated = False
                self.sasl_data = bytearray()
                session = {'lines': [], 'registered': False}
                self.sessions.append(session)
                pending = b''
                try:
                    while not self.stop.is_set():
                        try: data = connection.recv(32768)
                        except socket.timeout: continue
                        if not data: break
                        pending += data
                        assert len(pending) <= 65536
                        while b'\r\n' in pending:
                            line, pending = pending.split(b'\r\n', 1)
                            assert len(line) + 2 <= 512 and not any(x in line for x in (b'\0', b'\n', b'\r'))
                            self.lines.append((time.monotonic(), line))
                            session['lines'].append(line)
                            self.handle(line)
                except (ConnectionResetError, BrokenPipeError):
                    pass
                finally:
                    connection.close()
                    connection = self.socket = None
        except Exception as error:
            if not self.stop.is_set(): self.errors.append(repr(error))
        finally:
            if connection is not None: connection.close()
            self.listener.close()


def certificate(directory):
    cert, key = directory / 'cert.pem', directory / 'key.pem'
    command = ['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
               '-subj', '/CN=127.0.0.1', '-addext', 'subjectAltName=IP:127.0.0.1',
               '-keyout', str(key), '-out', str(cert)]
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=30)
    key.chmod(0o600)
    return cert, key


def run_case(binary, full, kind, cert, key, reject=False):
    if full: terminal.FULL_BINARY = binary
    else: terminal.BINARY = binary
    original = terminal.IRCFixture
    terminal.IRCFixture = lambda: BouncerFixture(cert, key, kind, reject)
    credential = 'auth=none\nserver_password=' if kind == 'znc' else 'auth=sasl\npassword='
    extra = ('[connection]\ntls=true\nca_file=%s\n%s%s\n'
             '[bouncer]\ntype=%s\nusername=alice\nnetwork=ircnow\nclient=desktop\nautomatic_away=false\n'
             '[output]\nformat=full\npersistent_logging=true\n'
             '[presence]\nbackground_away=true\nautoaway_minutes=1\n') % (cert, credential, PASSWORD, kind)
    result = {'kind': kind, 'rejected': reject, 'frontend': 'vipirc -i' if full else 'onekeyirc'}
    try:
        with terminal.IdleSession(live=True, full=full, review=False, channel='#stale',
                                  rows=28, cols=120, config_extra=extra) as s:
            terminal.bare_start(s)
            peer = s.peer
            if reject:
                s.wait(lambda: peer.sessions and peer.sessions[0].get('sasl_verified'), 'SASL fixture did not receive credentials')
                s.wait(lambda: b'failed' in s.raw.lower() or b'failure' in s.raw.lower(), 'authentication failure was not displayed')
                s.quiet(.3)
                assert not peer.registered and len(peer.sessions) == 1
                assert not any(line.startswith((b'JOIN ', b'PRIVMSG ')) for _, line in peer.lines)
                assert PASSWORD.encode() not in s.raw
                result.update(authentication_rejected=True, unexpected_joins=0, unexpected_messages=0)
                return result
            s.wait(lambda: peer.registered and b'joined #test' in s.raw, 'bouncer membership was not restored')
            s.command('/channel #test')
            assert not any(line.startswith((b'JOIN ', b'PART ')) for _, line in peer.lines)
            assert {'server-time', 'batch', 'multi-prefix'} <= set(peer.cap_requests)
            assert ('znc.in/self-message' if kind == 'znc' else 'sasl') in peer.cap_requests
            batch_type = 'znc.in/playback' if kind == 'znc' else 'chathistory'
            peer.send(':bnc BATCH +replay %s #test' % batch_type)
            peer.send('@batch=replay;time=2001-02-03T04:05:06.789Z;example=one\\:two\\svalue :friend!u@host PRIVMSG #test :BNC_OLD_CHANNEL tester')
            peer.send('@batch=replay;time=2001-02-03T04:05:07.000Z :tester!u@host PRIVMSG friend :BNC_OWN_PM')
            peer.send('@batch=replay;time=2001-02-03T04:05:08.000Z :friend!u@host PRIVMSG tester :BNC_INCOMING_PM')
            peer.send('@batch=replay :friend!u@host PRIVMSG tester :\001VERSION\001')
            peer.send('@batch=replay :friend!u@host PRIVMSG tester :\001DCC SEND ignored.txt 2130706433 12345 1\001')
            peer.send('@batch=replay :tester!u@host PART #test :old event')
            peer.send(':bnc BATCH -replay')
            peer.send('PING :bouncer-replay-finished')
            s.wait(lambda: any(line == b'PONG :bouncer-replay-finished' for _, line in peer.lines), 'replay blocked keepalive')
            s.wait(lambda: b'BNC_OLD_CHANNEL' in s.raw, 'channel playback was not displayed')
            assert b'2001-02-03 04:05:06' in s.raw or b'2001-02-03T04:05:06' in s.raw
            s.command('/query friend')
            s.wait(lambda: b'BNC_OWN_PM' in s.raw and b'BNC_INCOMING_PM' in s.raw, 'replayed PM directions were not retained')
            s.command('/channel #test')
            s.type('BNC_LIVE_SEND')
            s.wait(lambda: s.editor('BNC_LIVE_SEND'), 'live draft missing after playback')
            s.type(b'\r')
            s.wait(lambda: any(line == b'PRIVMSG #test :BNC_LIVE_SEND' for _, line in peer.lines), 'historical PART damaged live membership')
            s.wait(lambda: not s.screen.alternate, 'live send did not return to log')
            # Bounded pushed backlog, interspersed keepalives; no second replay store.
            started = time.monotonic()
            lines = [':bnc BATCH +burst %s #test' % batch_type]
            for i in range(2000):
                lines.append('@batch=burst;time=2001-02-03T04:05:09.000Z :friend!u@host PRIVMSG #test :BACKLOG_%04d tester' % i)
                if i % 500 == 0: lines.append('PING :burst-%d' % i)
            lines += [':bnc BATCH -burst', 'PING :burst-complete']
            peer.send_bytes(('\r\n'.join(lines) + '\r\n').encode())
            s.wait(lambda: any(line == b'PONG :burst-complete' for _, line in peer.lines), 'backlog blocked the event loop', timeout=15)
            replay_seconds = time.monotonic() - started
            s.wait(lambda: b'BACKLOG_1999' in s.raw, 'last backlog record was not displayed')
            for i in (0, 500, 1000, 1500):
                assert any(line == ('PONG :burst-%d' % i).encode() for _, line in peer.lines)
            s.command('/znc ListNetworks' if kind == 'znc' else '/soju network status')
            control = b'PRIVMSG *status :ListNetworks' if kind == 'znc' else b'PRIVMSG BouncerServ :network status'
            s.wait(lambda: any(line == control for _, line in peer.lines), 'bouncer control command not sent exactly once')
            assert sum(line == control for _, line in peer.lines) == 1
            s.command('/server')
            s.wait(lambda: b'BNC_SERVICE_REPLY_CANARY' in s.raw, 'bouncer service reply missing')
            assert not any(line.startswith(b'NOTICE ') or line.startswith(b'AWAY') for _, line in peer.lines)
            assert PASSWORD.encode() not in s.raw
            log = Path(str(s.config) + '.log')
            assert log.exists(), 'fixture did not exercise optional persistent log'
            saved = log.read_bytes()
            assert PASSWORD.encode() not in saved and b'BNC_SERVICE_REPLY_CANARY' not in saved
            assert b'BNC_OLD_CHANNEL' in saved and b'2001-02-03' in saved
            s.command('/disconnect')
            s.command('/connect')
            s.wait(lambda: len(peer.sessions) == 2 and peer.sessions[1]['registered'], 'bouncer reconnect failed')
            peer.send('PING :bouncer-reconnected')
            s.wait(lambda: any(line == b'PONG :bouncer-reconnected' for _, line in peer.lines), 'reconnected keepalive missing')
            assert not any(line.startswith((b'JOIN ', b'PART ')) for line in peer.sessions[1]['lines'])
            assert not peer.errors
            result.update(verified_tls=True, exact_login=True, restored_membership=True,
                          replay_records=2000, replay_seconds=round(replay_seconds, 6),
                          replay_ping_replies=5, own_private_replay=True,
                          historical_actions_suppressed=True, config_secret_exposure=False,
                          control_reply_excluded_from_persistent_log=True, reconnect=True)
    finally:
        terminal.IRCFixture = original
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--full', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='irc-bouncer-tls-') as path:
        cert, key = certificate(Path(path))
        for kind, reject in [('znc', False), ('soju', False), ('soju', True)]:
            print(json.dumps(run_case(args.binary.resolve(), args.full, kind, cert, key, reject), sort_keys=True), flush=True)
    print('bouncer terminal: all verified TLS/PTY compatibility fixtures passed', flush=True)


if __name__ == '__main__': main()
