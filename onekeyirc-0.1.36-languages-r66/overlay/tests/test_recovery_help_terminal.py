#!/usr/bin/env python3
"""Check localized background, catchup, and logging help on a private offline PTY."""
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
import sys

ap=argparse.ArgumentParser()
ap.add_argument('--binary',required=True)
ap.add_argument('--full-client',action='store_true')
args=ap.parse_args()
root=Path(__file__).resolve().parent.parent
binary=Path(args.binary).resolve()
sys.path.insert(0,str(root/'tools'))
from audit_help_coverage import collect
rows=collect(root)
cases=[]
for kind,name in (('command','background'),('command','catchup'),('topic','logging')):
    lines=[s for k,n,s in rows if (k,n)==(kind,name)]
    assert lines
    cases.append((name,lines[0]))

signal.signal(signal.SIGALRM,lambda *_: (_ for _ in ()).throw(TimeoutError('r4 terminal localization stalled')))
signal.alarm(100)

def capture(fd,out,seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end and len(out)<800000:
        ready,_,_=select.select([fd],[],[],.02)
        if ready:
            try:chunk=os.read(fd,65536)
            except OSError:break
            if not chunk:break
            out.extend(chunk)

for lang in ('id','ms','tr','es','pt','fr','de','ru','it','ar'):
    catalog=json.loads((root/'i18n'/f'{lang}.json').read_text(encoding='utf8'))
    for topic,english in cases:
        if os.environ.get('IRC_R4_DEBUG'): print('checking',lang,topic,flush=True)
        if os.environ.get('IRC_R4_LANG') and lang != os.environ['IRC_R4_LANG']: continue
        if os.environ.get('IRC_R4_TOPIC') and topic != os.environ['IRC_R4_TOPIC']: continue
        expected=catalog[english].encode('utf8')
        with tempfile.TemporaryDirectory(prefix='irc-r4-offline-') as home:
            master,slave=pty.openpty()
            fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',45,240,0,0))
            env=os.environ.copy()
            env.update({'ONEKEYIRC_LANG':lang,'VIPIRC_LANG':'en','LC_ALL':'C.UTF-8','LANG':'C.UTF-8','TERM':'xterm-256color','HOME':home,'XDG_CONFIG_HOME':home})
            cmd=[str(binary)]+(['-i'] if args.full_client else [])+['--test-mode','--no-connect']
            proc=subprocess.Popen(cmd,stdin=slave,stdout=slave,stderr=slave,env=env,start_new_session=True)
            os.close(slave)
            output=bytearray()
            try:
                capture(master,output,.24)
                os.write(master,b'x')
                capture(master,output,.12)
                os.write(master,b'/')
                capture(master,output,.10)
                os.write(master,('help '+topic+'\r').encode('ascii'))
                capture(master,output,.65)
                prefix=expected[:min(40,len(expected))]
                if proc.poll() is not None or prefix not in output:
                    raise AssertionError(f'{lang} /help {topic}: missing {prefix!r}; process={proc.poll()}, tail={bytes(output[-1500:])!r}')
            finally:
                if proc.poll() is None:
                    proc.send_signal(signal.SIGTERM)
                    try:proc.wait(timeout=1)
                    except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=1)
                os.close(master)
print('r4 offline PTY: 10 locales x background/catchup/logging help PASS')
