#!/usr/bin/env python3
"""Source-linked, compiled C safety/display lookup test for ten locales."""
import json, os, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
KEYS=json.loads((ROOT/'tests/safety-display-keys.json').read_text('utf-8'))
APP=(ROOT.name).split('-')[0]
source=(ROOT/'src/app.c').read_text('utf-8')
client=(ROOT/'src/client.c').read_text('utf-8')
for k in KEYS[:11]:
    target='app_disconnect(a, irc_tr('+json.dumps(k, ensure_ascii=True)+'))'
    assert source.count(target)==(2 if k=='Own relay identity exceeds supported bounds' else 1),k
for k in KEYS[11:13]:
    assert source.count('app_log(a, 0, "%s", irc_tr('+json.dumps(k, ensure_ascii=True)+'))')==1,k
assert source.count('app_disconnect(a, irc_tr("Disconnected by user"))')==1
assert client.count('app_disconnect(slot->app, irc_tr("Disconnected by user"))')==1
base=json.loads((ROOT/'i18n/en.json').read_text('utf-8'))
assert all(k in base for k in KEYS)
with tempfile.TemporaryDirectory() as tmp:
    out=str(Path(tmp)/'lookup')
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Isrc',
                    'tests/test_safety_display_l10n.c','-o',out],
                    cwd=ROOT,check=True)
    for lang in ('id','ms','tr','es','pt','fr','de','ru','it','ar','C'):
        env=os.environ.copy();env['VIPIRC_LANG']='';env['ONEKEYIRC_LANG']=''
        env[APP.upper()+'_LANG']=lang
        cat={k:k for k in KEYS} if lang=='C' else json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
        for k in KEYS:
            result=subprocess.run([out,k],env=env,cwd=ROOT,capture_output=True,text=True,check=True)
            assert result.stdout==cat[k],(APP,lang,k)
        unknown='Untrusted failure detail %n with non-ASCII test: \u03bb'
        result=subprocess.run([out,unknown],env=env,cwd=ROOT,capture_output=True,text=True,check=True)
        assert result.stdout==unknown
print(APP,len(KEYS),'fixed display messages in 10 locales and English fallback: PASS')
