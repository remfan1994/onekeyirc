#!/usr/bin/env python3
"""Offline PTY: localized command prose and unknown-command diagnostics."""
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

parser=argparse.ArgumentParser()
parser.add_argument('--binary',required=True)
parser.add_argument('--full-client',action='store_true')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
binary=Path(args.binary).resolve()
assert binary.is_file()
signal.signal(signal.SIGALRM,lambda *_: (_ for _ in ()).throw(TimeoutError('command localization PTY stalled')))
signal.alarm(75)


def capture(master,out,seconds):
    until=time.monotonic()+seconds
    while time.monotonic()<until and len(out)<500000:
        rd,_,_=select.select([master],[],[],.03)
        if rd:
            try: part=os.read(master,65536)
            except OSError:break
            if not part:break
            out.extend(part)


def check(lang,command,expected):
    with tempfile.TemporaryDirectory(prefix='irc-command-trans-') as home:
        master,slave=pty.openpty()
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',35,190,0,0))
        env=os.environ.copy()
        env.update({'VIPIRC_LANG':lang,'LANG':'C.UTF-8','LC_ALL':'C.UTF-8',
                    'HOME':home,'XDG_CONFIG_HOME':home,'TERM':'xterm-256color'})
        cmd=[str(binary)]+(['-i'] if args.full_client else [])+['--test-mode','--no-connect']
        proc=subprocess.Popen(cmd,env=env,stdin=slave,stdout=slave,stderr=slave,start_new_session=True)
        os.close(slave)
        out=bytearray()
        try:
            capture(master,out,.28)
            os.write(master,b'x')
            capture(master,out,.14)
            os.write(master,b'/')
            capture(master,out,.10)
            os.write(master,command.encode('ascii')+b'\r')
            capture(master,out,.85)
            if proc.poll() is not None or expected.encode('utf-8') not in out:
                raise AssertionError(f'{lang} {command}: localized text missing; '
                                     f'process={proc.poll()} output={bytes(out[-900:])!r}')
        finally:
            if proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
                try:proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill();proc.wait(timeout=2)
            os.close(master)

for lang in ('id','ms','tr','es','pt','fr','de','ru','it','ar'):
    t=json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))
    check(lang,'help back',t['Equivalent to /away with no message. Requires a registered connection.'])
    check(lang,'not_a_command',t['Unknown command. Use /help commands or /help COMMAND; shell and raw IRC commands are unavailable.'])
print('Offline PTY: /help back and unknown-command diagnostic in all 10 locales PASS')
