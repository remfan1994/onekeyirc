#!/usr/bin/env python3
"""Exercise actual offline command help through a UTF-8 pseudo-terminal."""
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

def capture(fd,parts,delay):
    until=time.monotonic()+delay
    while time.monotonic()<until and len(parts)<500000:
        ready,_,_=select.select([fd],[],[],0.03)
        if ready:
            try: data=os.read(fd,65536)
            except OSError: break
            if not data: break
            parts.extend(data)

def check(lang,phrase):
    with tempfile.TemporaryDirectory(prefix='irc-nav-') as home:
        master,slave=pty.openpty()
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',38,200,0,0))
        env=os.environ.copy()
        env.update({'HOME':home,'XDG_CONFIG_HOME':home,'VIPIRC_LANG':lang,
                    'LC_ALL':'C.UTF-8','LANG':'C.UTF-8','TERM':'xterm-256color'})
        cmd=[str(binary)]+(['-i'] if args.full_client else [])+['--test-mode','--no-connect']
        proc=subprocess.Popen(cmd,env=env,stdin=slave,stdout=slave,stderr=slave,start_new_session=True)
        os.close(slave)
        data=bytearray()
        try:
            capture(master,data,0.25)
            os.write(master,b'x')
            capture(master,data,0.15)
            os.write(master,b'/')
            capture(master,data,0.10)
            os.write(master,b'help channels\r')
            capture(master,data,0.80)
            expected=phrase.encode('utf-8')
            if proc.poll() is not None or expected not in data:
                raise AssertionError(f'{lang}: translated /help channels missing; process={proc.poll()} output={bytes(data[-1300:])!r}')
        finally:
            if proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
                try: proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill();proc.wait(timeout=2)
            os.close(master)

for lang in ('id','ms','tr','es','pt','fr','de','ru','it','ar'):
    translations=json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))
    phrase=translations['Rows show joined/parted state and unread/mention counts.']
    check(lang,phrase)
print('offline PTY /help channels: 10 localized outputs PASS')
