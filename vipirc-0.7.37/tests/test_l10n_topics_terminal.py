#!/usr/bin/env python3
"""Real offline /help lookup in every locale; no IRC connection or credentials."""
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
root = Path(__file__).resolve().parent.parent

# A stalled PTY is a test failure, not an unbounded build dependency.
signal.signal(signal.SIGALRM, lambda sig, frame: (_ for _ in ()).throw(TimeoutError('help PTY stalled')))
signal.alarm(40)
binary = Path(args.binary).resolve()
assert binary.is_file()


def capture(master, output, seconds):
    limit = time.monotonic() + seconds
    while time.monotonic() < limit and len(output) < 1500000:
        readable, _, _ = select.select([master], [], [], 0.04)
        if readable:
            try:
                part = os.read(master, 65536)
            except OSError:
                break
            if not part:
                break
            output.extend(part)


for lang in ('id', 'ms', 'tr', 'es', 'pt', 'fr', 'de', 'ru', 'it', 'ar'):
    translated = json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))
    expected = translated['Use /panel, select a server, then choose connection type znc or soju.'].encode('utf-8')
    with tempfile.TemporaryDirectory(prefix='irc-help-l10n-') as home:
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 38, 170, 0, 0))
        env = os.environ.copy()
        env.update({'VIPIRC_LANG': lang, 'LANG': 'C.UTF-8', 'LC_ALL': 'C.UTF-8',
                    'HOME': home, 'XDG_CONFIG_HOME': home, 'TERM': 'xterm-256color'})
        cmd = [str(binary)] + (['-i'] if args.full_client else []) + ['--test-mode', '--no-connect']
        proc = subprocess.Popen(cmd, env=env, stdin=slave, stdout=slave, stderr=slave,
                                start_new_session=True)
        os.close(slave)
        raw = bytearray()
        try:
            capture(master, raw, .3)
            os.write(master, b'x')
            capture(master, raw, .16)
            os.write(master, b'/')
            capture(master, raw, .08)
            os.write(master, b'help bouncers\r')
            capture(master, raw, .75)
            if proc.poll() is not None or expected not in raw:
                raise AssertionError(f'{lang}: translated /help bouncers not displayed; '
                                     f'process={proc.poll()}, output={raw[-650:]!r}')
        finally:
            if proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
                try: proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=2)
            os.close(master)
print('localized /help bouncers: 10 languages, real offline terminals PASS')
