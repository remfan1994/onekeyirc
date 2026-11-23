#!/usr/bin/env python3
"""Run translated authentication and ZNC /help under a private offline PTY."""
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
signal.signal(signal.SIGALRM, lambda *_: (_ for _ in ()).throw(TimeoutError('auth/bouncer PTY stalled')))
signal.alarm(95)
rows=json.loads((root/'tools/localized-auth-bouncer-lines.json').read_text('utf-8'))['lines']
CASES=[('znc',rows[4]),('authentication',rows[7])]


def capture(fd,output,duration):
    until=time.monotonic()+duration
    while time.monotonic()<until and len(output)<500000:
        readable,_,_=select.select([fd],[],[],.025)
        if readable:
            try:chunk=os.read(fd,65536)
            except OSError:break
            if not chunk:break
            output.extend(chunk)


def exercise(lang,topic,expected):
    with tempfile.TemporaryDirectory(prefix='irc-auth-help-') as home:
        master,slave=pty.openpty()
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',38,220,0,0))
        env=os.environ.copy()
        env.update({'VIPIRC_LANG':lang,'LC_ALL':'C.UTF-8','LANG':'C.UTF-8',
                    'HOME':home,'XDG_CONFIG_HOME':home,'TERM':'xterm-256color'})
        cmd=[str(binary)]+(['-i'] if args.full_client else [])+['--test-mode','--no-connect']
        process=subprocess.Popen(cmd,env=env,stdin=slave,stdout=slave,stderr=slave,start_new_session=True)
        os.close(slave)
        output=bytearray()
        try:
            capture(master,output,.20)
            os.write(master,b'x')
            capture(master,output,.12)
            os.write(master,b'/')
            capture(master,output,.07)
            os.write(master,('help '+topic+'\r').encode('ascii'))
            capture(master,output,.70)
            if process.poll() is not None or expected.encode('utf-8') not in output:
                raise AssertionError(f'{lang} /help {topic}: expected localized help missing; '
                                     f'process={process.poll()}, tail={bytes(output[-1000:])!r}')
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:process.wait(timeout=2)
                except subprocess.TimeoutExpired:process.kill();process.wait(timeout=2)
            os.close(master)


for lang in ('id','ms','tr','es','pt','fr','de','ru','it','ar'):
    data=json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))
    for topic,source in CASES:
        exercise(lang,topic,data[source])
print('Offline PTY: 10 locales x /help znc and /help authentication PASS')
