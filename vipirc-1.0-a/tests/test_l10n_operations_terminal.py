#!/usr/bin/env python3
"""Verify translated deployment/security/clear help in an actual offline PTY.

The PTY tests displayed UTF-8 bytes, not bidirectional visual shaping.
"""
import argparse, fcntl, json, os, pty, select, signal, struct, subprocess
import tempfile, termios, time
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('--binary',required=True)
parser.add_argument('--full-client',action='store_true')
args=parser.parse_args()
ROOT=Path(__file__).resolve().parents[1]
BINARY=Path(args.binary).resolve()
assert BINARY.is_file()
import sys
sys.path.insert(0,str(ROOT/'tools'))
import generate_locale_operations_guides as ops
cases=[('deployment',ops.sections()[0][1][2]),
       ('security',ops.sections()[1][1][2]),
       ('logging',ops.LOG_CLEAR)]
signal.signal(signal.SIGALRM,lambda *_: (_ for _ in ()).throw(TimeoutError('operations PTY stalled')))
signal.alarm(100)
def capture(fd,out,seconds):
    until=time.monotonic()+seconds
    while time.monotonic()<until and len(out)<800000:
        readable,_,_=select.select([fd],[],[],.03)
        if readable:
            try: chunk=os.read(fd,65536)
            except OSError: break
            if not chunk: break
            out.extend(chunk)

def exercise(lang,topic,expected):
    with tempfile.TemporaryDirectory(prefix='irc-ops-pty-') as home:
        master,slave=pty.openpty()
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',40,220,0,0))
        env=os.environ.copy()
        env.update({'VIPIRC_LANG':lang,'LC_ALL':'C.UTF-8','LANG':'C.UTF-8',
                    'HOME':home,'XDG_CONFIG_HOME':home,'TERM':'xterm-256color'})
        cmd=[str(BINARY)]+(['-i'] if args.full_client else [])+['--test-mode','--no-connect']
        proc=subprocess.Popen(cmd,env=env,stdin=slave,stdout=slave,stderr=slave,start_new_session=True)
        os.close(slave)
        out=bytearray()
        try:
            capture(master,out,.20)
            os.write(master,b'x')
            capture(master,out,.12)
            os.write(master,b'/')
            capture(master,out,.08)
            os.write(master,('help '+topic+'\r').encode('ascii'))
            capture(master,out,.65)
            if proc.poll() is not None or expected.encode('utf8') not in out:
                raise AssertionError(f'{lang} /help {topic} missing or not rendered; process={proc.poll()}, tail={bytes(out[-950:])!r}')
        finally:
            if proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
                try: proc.wait(timeout=2)
                except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=2)
            os.close(master)

for lang in ops.LANGS:
    catalog=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf8'))
    for topic,key in cases:exercise(lang,topic,catalog[key])
print('Offline PTY: 10 locales x deployment, security and logging translated help PASS')
