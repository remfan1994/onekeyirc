#!/usr/bin/env python3
"""Offline real-PTY checks of first-use help in every language."""
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
ROOT = Path(__file__).resolve().parent.parent
BINARY = Path(args.binary).resolve()
assert BINARY.is_file()
signal.signal(signal.SIGALRM, lambda *_: (_ for _ in ()).throw(TimeoutError('first-use PTY stalled')))
signal.alarm(105)
TOPICS = {
    'startup': 'Run the program, dismiss the welcome, then type /connect irc.example.org and /join #channel.',
    'messages': 'Use /join #channel to join and select a channel, or /query NICK to open a private conversation.',
    'limits': 'IRC limits are byte limits, not counts of displayed glyphs. UTF-8 characters can occupy more than one byte.',
}


def capture(fd, out, seconds):
    until = time.monotonic() + seconds
    while time.monotonic() < until and len(out) < 1000000:
        readable, _, _ = select.select([fd], [], [], .03)
        if readable:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                break
            if not chunk:
                break
            out.extend(chunk)


def check(lang, topic, translated):
    with tempfile.TemporaryDirectory(prefix='irc-first-use-') as home:
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 42, 230, 0, 0))
        env = os.environ.copy()
        env.update({'VIPIRC_LANG': lang, 'ONEKEYIRC_LANG': lang,
                    'LANG': 'C.UTF-8', 'LC_ALL': 'C.UTF-8',
                    'HOME': home, 'XDG_CONFIG_HOME': home, 'TERM': 'xterm-256color'})
        command = [str(BINARY)] + (['-i'] if args.full_client else []) + ['--test-mode', '--no-connect']
        proc = subprocess.Popen(command, env=env, stdin=slave, stdout=slave,
                                stderr=slave, start_new_session=True)
        os.close(slave)
        data = bytearray()
        try:
            capture(master, data, .18)
            os.write(master, b'x')
            capture(master, data, .12)
            os.write(master, b'/')
            capture(master, data, .08)
            os.write(master, ('help ' + topic + '\r').encode('ascii'))
            capture(master, data, .6)
            if proc.poll() is not None or translated.encode('utf8') not in data:
                raise AssertionError(f'{lang} {topic}: translation absent, process={proc.poll()}, tail={bytes(data[-900:])!r}')
        finally:
            if proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
                try: proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=2)
            os.close(master)

for locale in ('id', 'ms', 'tr', 'es', 'pt', 'fr', 'de', 'ru', 'it', 'ar'):
    catalog = json.loads((ROOT / 'i18n' / f'{locale}.json').read_text('utf8'))
    for name, source in TOPICS.items():
        check(locale, name, catalog[source])
print('First-use PTY: 10 languages x 3 topics; 30 offline checks PASS')
