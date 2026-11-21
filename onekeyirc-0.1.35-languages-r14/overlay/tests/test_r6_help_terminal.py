#!/usr/bin/env python3
"""Offline PTY check of actual translated configuration and status help."""
import argparse,fcntl,json,os,pty,select,signal,struct,subprocess,tempfile,termios,time,sys
from pathlib import Path
ap=argparse.ArgumentParser()
ap.add_argument('--binary',required=True)
ap.add_argument('--full-client',action='store_true')
ap.add_argument('--lang')
a=ap.parse_args()
ROOT=Path(__file__).resolve().parent.parent
BIN=Path(a.binary).resolve()
sys.path.insert(0,str(ROOT/'tools'))
from audit_help_coverage import collect
rows=collect(ROOT)
# Display is checked for the source line rather than an invented excerpt.
topics=('configuration','summary')
source={t:next(s for k,n,s in rows if n==t and k==('topic' if t=='configuration' else 'command')) for t in topics}
def capture(fd,output,seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end and len(output)<500000:
        ready,_,_=select.select([fd],[],[],0.02)
        if ready:
            try:chunk=os.read(fd,65536)
            except OSError:break
            if not chunk:break
            output.extend(chunk)
signal.signal(signal.SIGALRM,lambda *_: (_ for _ in ()).throw(TimeoutError('r6 PTY timeout')))
signal.alarm(90)
checks=0
for lang in ('id','ms','tr','es','pt','fr','de','ru','it','ar'):
    if a.lang and a.lang!=lang: continue
    data=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    for name in topics:
        expected=data[source[name]].encode('utf8')
        with tempfile.TemporaryDirectory(prefix='irc-r6-') as home:
            master,slave=pty.openpty()
            fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',45,240,0,0))
            env=os.environ.copy()
            env.update({'ONEKEYIRC_LANG':lang,'VIPIRC_LANG':'en','LC_ALL':'C.UTF-8','LANG':'C.UTF-8',
                        'TERM':'xterm-256color','HOME':home,'XDG_CONFIG_HOME':home})
            argv=[str(BIN)]+(['-i'] if a.full_client else [])+['--test-mode','--no-connect']
            proc=subprocess.Popen(argv,stdin=slave,stdout=slave,stderr=slave,env=env,start_new_session=True)
            os.close(slave);out=bytearray()
            try:
                capture(master,out,0.22)
                os.write(master,b'x')
                capture(master,out,0.10)
                os.write(master,b'/')
                capture(master,out,0.08)
                os.write(master,('help '+name+'\r').encode())
                capture(master,out,0.63)
                if proc.poll() is not None or expected[:min(36,len(expected))] not in out:
                    raise AssertionError(f'{lang} {name}: no translated text; tail={bytes(out[-800:])!r}')
                checks+=1
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    try:proc.wait(timeout=1)
                    except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=1)
                os.close(master)
print('r6 offline terminal:',checks,'localized help cases PASS')
