#!/usr/bin/env python3
"""Run first-use, security and document help in offline UTF-8 PTYs.

This checks the translation path, not RTL shaping or native wording quality.
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

parser=argparse.ArgumentParser()
parser.add_argument('--binary',required=True)
parser.add_argument('--full-client',action='store_true')
args=parser.parse_args()
root=Path(__file__).resolve().parent.parent
binary=Path(args.binary).resolve()
assert binary.is_file()
signal.signal(signal.SIGALRM, lambda s,f: (_ for _ in ()).throw(TimeoutError('PTY localization stalled')))
signal.alarm(90)
CASES=(
    ('documentation', 'Find a task with /help ls; /help commands explains command entry and lists supported commands. Use one topic or command name at a time.'),
    ('security', 'TLS verifies the server identity. SASL, server PASS and recognized private authentication commands require verified TLS. Keep the system TLS library patched as well as the client; see SECURITY.md.'),
    ('quickstart', 'Verified TLS is the default. A plaintext connection must be selected explicitly.'),
)

def capture(fd, buf, secs):
    end=time.monotonic()+secs
    while time.monotonic()<end and len(buf)<1_000_000:
        r,_,_=select.select([fd], [], [], .03)
        if r:
            try: packet=os.read(fd,65536)
            except OSError: return
            if not packet: return
            buf.extend(packet)

for lang in ('id','ms','tr','es','pt','fr','de','ru','it','ar'):
    catalog=json.loads((root/'i18n'/f'{lang}.json').read_text('utf8'))
    for topic,source in CASES:
        expected=catalog[source].encode('utf8')
        # Exact first characters survive wrapping and color changes in the line reader.
        prefix=catalog[source][:36].encode('utf8')
        with tempfile.TemporaryDirectory(prefix='irc-practical-') as home:
            master,slave=pty.openpty()
            fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',40,180,0,0))
            env=os.environ.copy()
            env.update({'ONEKEYIRC_LANG':lang,'VIPIRC_LANG':'en',
                        'LANG':'C.UTF-8','LC_ALL':'C.UTF-8','HOME':home,
                        'XDG_CONFIG_HOME':home,'TERM':'xterm-256color'})
            cmd=[str(binary)]+(['-i'] if args.full_client else [])+['--test-mode','--no-connect']
            proc=subprocess.Popen(cmd,env=env,stdin=slave,stdout=slave,stderr=slave,start_new_session=True)
            os.close(slave)
            raw=bytearray()
            try:
                capture(master,raw,.26)
                os.write(master,b'x')
                capture(master,raw,.12)
                os.write(master,b'/')
                capture(master,raw,.09)
                os.write(master,f'help {topic}\r'.encode('ascii'))
                capture(master,raw,.53)
                if proc.poll() is not None or prefix not in raw:
                    raise AssertionError(f'{lang} /help {topic}: expected {prefix!r}; '
                                         f'process={proc.poll()}, output={raw[-500:]!r}')
                if source.encode('utf8')==expected:
                    raise AssertionError(f'{lang} /help {topic} was not translated')
            finally:
                if proc.poll() is None:
                    proc.send_signal(signal.SIGTERM)
                    try:proc.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        proc.kill(); proc.wait(timeout=2)
                os.close(master)
print('Localized documentation, security and quickstart help: 30 offline PTY cases PASS')
