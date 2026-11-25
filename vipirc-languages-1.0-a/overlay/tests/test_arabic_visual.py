#!/usr/bin/env python3
"""Optional Arabic display: exercise help and live WATCH in offline PTYs.

Requires libfribidi at runtime for visual shaping. If unavailable, only the
logical/default case is tested, and the optional visual case is UNAVAILABLE.
The core C regression separately verifies unchanged retained text and queue
backpressure. These PTYs verify emitted UTF-8, not a terminal font's glyphs.
"""
import argparse,os,pty,fcntl,termios,subprocess,struct,select,time,tempfile,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser()
ap.add_argument('--binary',default='./onekeyirc')
ap.add_argument('--full-client',action='store_true')
ap.add_argument('--expect-fallback',action='store_true',
                help='binary was built without optional Arabic visual support')
a=ap.parse_args()
BIN=Path(a.binary).resolve()
assert BIN.is_file()

def session(visual):
    with tempfile.TemporaryDirectory(prefix='irc-rtl-') as home:
        master,slave=pty.openpty()
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',42,100,0,0))
        env=os.environ.copy()
        env.update({'TERM':'xterm-256color','LC_ALL':'C.UTF-8',
                    'LANG':'C.UTF-8','HOME':home,'XDG_CONFIG_HOME':home,
                    'ONEKEYIRC_LANG':'ar' if not a.full_client else 'en',
                    'VIPIRC_LANG':'en' if not a.full_client else 'ar',
                    'ONEKEYIRC_ARABIC_VISUAL':'1' if (visual and not a.full_client) else '0',
                    'VIPIRC_ARABIC_VISUAL':'1' if (visual and a.full_client) else '0'})
        proc=subprocess.Popen([str(BIN)]+(['-i'] if a.full_client else [])+['--test-mode','--no-connect'],
                              stdin=slave,stdout=slave,stderr=slave,env=env,
                              start_new_session=True)
        os.close(slave)
        output=bytearray()
        def drain(t):
            end=time.monotonic()+t
            while time.monotonic()<end and len(output)<300000:
                rr,_,_=select.select([master],[],[],0.02)
                if rr:
                    try: data=os.read(master,65536)
                    except OSError: break
                    if not data: break
                    output.extend(data)
        try:
            drain(0.25)
            os.write(master,b'x');drain(0.12)
            os.write(master,b'/');drain(0.10)
            os.write(master,b'help back\r');drain(0.8)
            assert proc.poll() is None,'client exited unexpectedly'
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(timeout=1)
                except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=1)
            os.close(master)
        return output.decode('utf-8')

def live_session(visual, width):
    """Submit to the in-process peer, then inspect WATCH after editor exit."""
    with tempfile.TemporaryDirectory(prefix='irc-rtl-live-') as home:
        master,slave=pty.openpty()
        fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',30,width,0,0))
        env=os.environ.copy()
        env.update({'TERM':'xterm-256color','LC_ALL':'C.UTF-8','LANG':'C.UTF-8',
                    'HOME':home,'XDG_CONFIG_HOME':home,
                    'ONEKEYIRC_LANG':'ar' if not a.full_client else 'en',
                    'VIPIRC_LANG':'en' if not a.full_client else 'ar',
                    'ONEKEYIRC_ARABIC_VISUAL':'1' if (visual and not a.full_client) else '0',
                    'VIPIRC_ARABIC_VISUAL':'1' if (visual and a.full_client) else '0'})
        args=[str(BIN)]+(['-i'] if a.full_client else [])+[
            '--test-mode','--server','sim.local','--nick','tester','--channel','#rtl']
        proc=subprocess.Popen(args,stdin=slave,stdout=slave,stderr=slave,
                              env=env,start_new_session=True)
        os.close(slave)
        def drain(seconds):
            data=bytearray();end=time.monotonic()+seconds
            while time.monotonic()<end and len(data)<400000:
                if select.select([master],[],[],0.02)[0]:
                    try:part=os.read(master,65536)
                    except OSError:break
                    if not part:break
                    data.extend(part)
            return bytes(data)
        try:
            drain(0.35);os.write(master,b'x');drain(0.65)
            os.write(master,b'/');drain(0.2)
            os.write(master,'msg #rtl \u0628\u0628 /away'.encode('utf-8'));drain(0.3)
            os.write(master,b'\r');output=drain(0.7).decode('utf-8')
            assert proc.poll() is None,'client exited in live Arabic output'
            assert '\x1b[?1049l' in output,'message did not leave the editor'
            return output.rsplit('\x1b[?1049l',1)[1]
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(timeout=1)
                except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=1)
            os.close(master)

plain=session(False)
assert '\u0645\u062b\u0644 /away \u0628\u0644\u0627 \u0631\u0633\u0627\u0644\u0629' in plain,'logical Arabic help was not preserved'
assert not any(0xfe70<=ord(c)<=0xfefc for c in plain),'visual shaping not disabled'
print('Arabic logical help display: PASS')
live_plain=live_session(False,100)
assert '\u0628\u0628 /away' in live_plain,'logical WATCH message was not preserved'
assert not any(0xfe70<=ord(c)<=0xfefc for c in live_plain),'WATCH shaping not disabled'
print('Arabic logical WATCH display: PASS')
visual=session(True)
if a.expect_fallback:
    assert not any(0xfe70<=ord(c)<=0xfefc for c in visual),'expected unavailable converter'
    live_fallback=live_session(True,40)
    assert '\u0628\u0628 /away' in live_fallback,'unavailable converter changed WATCH text'
    print('Arabic unavailable-converter PTY fallback: PASS')
    sys.exit(0)
if not any(0xfe70<=ord(c)<=0xfefc for c in visual):
    print('Arabic visual display: UNAVAILABLE (system FriBidi not found)')
    sys.exit(77)
assert '/away' in visual,'/away reversed or missing in visual output'
assert '\u0645\u062b\u0644 /away \u0628\u0644\u0627 \u0631\u0633\u0627\u0644\u0629' not in visual,'visual display remained logical'
assert '\u200e' not in visual and '\ufeff' not in visual,'direction marks escaped into output'
print('Arabic visual display: PASS (shaped, LTR tokens preserved)')
for width in (40,100):
    live_visual=live_session(True,width)
    assert '\ufe90\ufe91' in live_visual,'joined beh pair missing from WATCH output'
    assert '/away' in live_visual,'command token changed in WATCH output'
    assert '\u0628\u0628' not in live_visual,'WATCH still bypasses visual conversion'
    assert '\u200e' not in live_visual and '\ufeff' not in live_visual,'temporary marks escaped'
print('Arabic visual WATCH display: PASS (40/100 columns, joined letters, literal /away)')
