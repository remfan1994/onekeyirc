#!/usr/bin/env python3
"""Compiled ten-locale C lookup and display-boundary regression checks."""
import json,os,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
KEYS=json.loads((ROOT/'tests/network-translation-keys.json').read_text('utf-8'))
PREF=KEYS['prefixes']
NET=KEYS['network']
STATE=KEYS['state']
locale_codes=('id','ms','tr','es','pt','fr','de','ru','it','ar')
app_name=ROOT.name.split('-')[0]
base=json.loads((ROOT/'i18n/en.json').read_text('utf-8'))
source=(ROOT/'src/app.c').read_text('utf-8')
section=source.split('const char *app_connection_label(const App *a)',1)[1].split('/* End protocol/queue state',1)[0]
for key in STATE:
    assert 'irc_tr('+json.dumps(key,ensure_ascii=True)+')' in section,('state not translated in app.c',key)
for key in NET+STATE:
    assert key in base,key
with tempfile.TemporaryDirectory() as tmp:
    out=Path(tmp)/'test_network_display'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
        '-Isrc','tests/test_network_display_l10n.c','-o',str(out)],cwd=ROOT,check=True)
    for lang in (*locale_codes,'en'):
        env=os.environ.copy()
        env['ONEKEYIRC_LANG' if app_name=='onekeyirc' else 'VIPIRC_LANG']=lang
        cat=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8')) if lang!='en' else {k:k for k in base}
        for key in NET:
            p=subprocess.run([str(out),key],cwd=ROOT,capture_output=True,text=True,env=env,check=True)
            assert p.stdout==cat[key],(app_name,lang,key,p.stdout,cat[key])
        for key in PREF:
            raw=key+' OS diagnostic: file%name_\u00e9/\u03bb'
            expected=cat[key]+' OS diagnostic: file%name_\u00e9/\u03bb'
            p=subprocess.run([str(out),raw],cwd=ROOT,capture_output=True,text=True,env=env,check=True)
            assert p.stdout==expected,(app_name,lang,key)
        for raw in ('@null','Unrecognized error: %n', 'raw server reply: 123'):
            p=subprocess.run([str(out),raw],cwd=ROOT,capture_output=True,text=True,env=env,check=True)
            assert p.stdout==('No connection' if raw=='@null' and lang=='en'
                else cat.get('No connection','No connection') if raw=='@null' else raw),(app_name,lang,raw,p.stdout)
    print(app_name,len(NET),'network keys',len(STATE),'state keys',len(locale_codes),'locales, compiled C display tests PASS')
