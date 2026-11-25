#!/usr/bin/env python3
"""Exercise new r8 /help text in real offline terminal sessions."""
import argparse,fcntl,json,os,pty,select,signal,struct,subprocess,tempfile,termios,time,sys
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--binary',required=True)
ap.add_argument('--full-client',action='store_true');ap.add_argument('--lang');a=ap.parse_args()
ROOT=Path(__file__).resolve().parents[1];BIN=Path(a.binary).resolve()
sys.path.insert(0,str(ROOT/'tools'))
from audit_help_coverage import collect
rows=collect(ROOT)
names=('clear','ignore')
src={name:next(s for k,n,s in rows if k=='command' and n==name) for name in names}
def capture(fd,output,seconds):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline and len(output)<400000:
        ready,_,_=select.select([fd],[],[],0.03)
        if ready:
            try:data=os.read(fd,32768)
            except OSError:break
            if not data:break
            output.extend(data)
signal.signal(signal.SIGALRM, lambda *_: (_ for _ in ()).throw(TimeoutError('r8 terminal timeout')))
signal.alarm(100)
checks=0
for lang in ('id','ms','tr','es','pt','fr','de','ru','it','ar'):
    if a.lang and lang!=a.lang: continue
    translations=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf8'))
    for name in names:
        target=translations[src[name]].encode('utf8')
        with tempfile.TemporaryDirectory(prefix='irc-r8-') as home:
            master,slave=pty.openpty()
            fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',44,240,0,0))
            env=os.environ.copy()
            env.update({'HOME':home,'XDG_CONFIG_HOME':home,'TERM':'xterm-256color',
                        'LANG':'C.UTF-8','LC_ALL':'C.UTF-8',
                        'ONEKEYIRC_LANG':lang if not a.full_client else 'en',
                        'VIPIRC_LANG':lang if a.full_client else 'en',
                        'ONEKEYIRC_ARABIC_VISUAL':'0','VIPIRC_ARABIC_VISUAL':'0'})
            args=[str(BIN)]+(['-i'] if a.full_client else [])+['--test-mode','--no-connect']
            proc=subprocess.Popen(args,stdin=slave,stdout=slave,stderr=slave,
                                  env=env,start_new_session=True)
            os.close(slave);out=bytearray()
            try:
                capture(master,out,0.23);os.write(master,b'x');capture(master,out,0.11)
                os.write(master,b'/');capture(master,out,0.09)
                os.write(master,('help '+name+'\r').encode());capture(master,out,0.52)
                assert proc.poll() is None
                assert target[:min(36,len(target))] in out,(lang,name,bytes(out[-500:]))
                checks+=1
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    try:proc.wait(timeout=1)
                    except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=1)
                os.close(master)
print('r8 help PTY:',checks,'localized cases PASS')
