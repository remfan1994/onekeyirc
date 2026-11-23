#!/usr/bin/env python3
"""Run a no-network pseudo-terminal smoke test of the live minimalist UI."""
import argparse
import fcntl
import json
import os
import pty
import select
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--binary',required=True)
parser.add_argument('--full-client',action='store_true')
parser.add_argument('--english-only',action='store_true')
args=parser.parse_args()
binary=Path(args.binary).resolve()
assert binary.is_file(), f'build client first: {binary}'

# Exercise all shipped locales in the real terminal renderer. The expected
# panel labels come from the checked catalog, not from a hard-coded screenshot.
# This verifies lookup/display and the test-mode safety boundary; it is not
# proofreading or a right-to-left layout certification.
root=Path(__file__).resolve().parent.parent
for lang, cols in [('id',100),('ms',80),('tr',45),('es',60),('pt',80),
                   ('fr',80),('de',65),('ru',80),('it',80),('ar',90)]:
    term=('Settings panel' if args.english_only else
          json.loads((root/'i18n'/f'{lang}.json').read_text(encoding='utf-8'))['Settings panel'])
    with tempfile.TemporaryDirectory(prefix='irc-l10n-') as home:
        master,slave=pty.openpty()
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',28,cols,0,0))
        env=os.environ.copy()
        env.update({'HOME':home,'XDG_CONFIG_HOME':home,'TERM':'xterm-256color',
                    'LANG':'C.UTF-8','LC_ALL':'C.UTF-8','VIPIRC_LANG':lang})
        cmd=[str(binary)]
        if args.full_client: cmd+=['-i']
        cmd+=['--test-mode','--no-connect']
        proc=subprocess.Popen(cmd,stdin=slave,stdout=slave,stderr=slave,
                              env=env,start_new_session=True)
        os.close(slave)
        buf=bytearray()
        def gather(seconds):
            end=time.monotonic()+seconds
            while time.monotonic()<end and len(buf)<2_000_000:
                readable,_,_=select.select([master],[],[],0.05)
                if readable:
                    try: chunk=os.read(master,65536)
                    except OSError: break
                    if not chunk: break
                    buf.extend(chunk)
        try:
            gather(.35)
            # The application's notice requires one key, then /panel is entered
            # as a command. Do not send it to any IRC network.
            os.write(master,b'x')
            gather(.18)
            os.write(master,b'/')
            gather(.07)
            os.write(master,b'panel\r')
            gather(.65)
            decoded=buf.decode('utf-8','replace')
            if proc.poll() is not None or term not in decoded:
                raise AssertionError(f'{lang} {cols} columns: translated panel missing, '
                                     f'process={proc.poll()}; output tail={decoded[-700:]!r}')
        finally:
            if proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
                try: proc.wait(timeout=2)
                except subprocess.TimeoutExpired: proc.kill();proc.wait(timeout=2)
            os.close(master)
print('minimalist terminal localization: '+('English-only' if args.english_only else 'all 10 locales')+', 45-100 columns, no network PASS')
