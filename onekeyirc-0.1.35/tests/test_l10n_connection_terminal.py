#!/usr/bin/env python3
"""Offline live-terminal regression for translated /help connect and autoauth.

Does not connect to IRC, inspect real credentials or assert right-to-left
visual shaping; verifies the actual runtime emits the selected UTF-8 text.
"""
import argparse
import fcntl
import json
import os
import pty
import select
import signal
import struct
import subprocess
import tempfile
import termios
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--binary', required=True)
parser.add_argument('--full-client', action='store_true')
args = parser.parse_args()
ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(args.binary).resolve()
assert BINARY.is_file()
signal.signal(signal.SIGALRM, lambda *_: (_ for _ in ()).throw(TimeoutError('connection-help PTY stalled')))
signal.alarm(90)

CASES = (
    ('connect', 'Start with /connect irc.example.org. No profile, alias or server number needs to be created first.'),
    ('autoauth', "Changes the selected server's authentication for its next connection. Verified TLS is required."),
)


def capture(fd, output, seconds):
    limit = time.monotonic() + seconds
    while time.monotonic() < limit and len(output) < 750000:
        readable, _, _ = select.select([fd], [], [], .03)
        if readable:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                break
            if not chunk:
                break
            output.extend(chunk)


def run(lang, topic, expected):
    with tempfile.TemporaryDirectory(prefix='irc-connection-help-') as home:
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 38, 220, 0, 0))
        env = os.environ.copy()
        env.update({'VIPIRC_LANG': lang, 'LC_ALL': 'C.UTF-8', 'LANG': 'C.UTF-8',
                    'HOME': home, 'XDG_CONFIG_HOME': home, 'TERM': 'xterm-256color'})
        cmd = [str(BINARY)] + (['-i'] if args.full_client else []) + ['--test-mode', '--no-connect']
        proc = subprocess.Popen(cmd, env=env, stdin=slave, stdout=slave, stderr=slave,
                                start_new_session=True)
        os.close(slave)
        out = bytearray()
        try:
            capture(master, out, .23)
            os.write(master, b'x')  # dismiss local welcome without network I/O
            capture(master, out, .12)
            os.write(master, b'/')
            capture(master, out, .08)
            os.write(master, ('help ' + topic + '\r').encode('ascii'))
            capture(master, out, .72)
            if proc.poll() is not None or expected.encode('utf-8') not in out:
                raise AssertionError(f'{lang} /help {topic}: translated help not displayed; '
                                     f'process={proc.poll()}, tail={bytes(out[-900:])!r}')
        finally:
            if proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
                try:
                    proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=2)
            os.close(master)


for language in ('id', 'ms', 'tr', 'es', 'pt', 'fr', 'de', 'ru', 'it', 'ar'):
    catalog = json.loads((ROOT / 'i18n' / f'{language}.json').read_text('utf-8'))
    for name, key in CASES:
        run(language, name, catalog[key])
print('Live offline PTY: 10 locales x /help connect and /help autoauth PASS')
