#!/usr/bin/env python3
"""Verify display-only CLI and private-log translation with a real C catalog lookup."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
prog=root.name.split('-')[0]
keys=json.loads((root/'tests/cli-log-keys.json').read_text('utf8'))
langs=('id','ms','tr','es','pt','fr','de','ru','it','ar')
src=(root/'src/main.c').read_text('utf8')
app=(root/'src/app.c').read_text('utf8')
assert len(keys)==23
for key in keys[:11]:
    assert 'cli_usage_line(out, '+json.dumps(key,ensure_ascii=True)+');' in src,key
for key in keys[11:13]:
    assert src.count('cli_usage_line(out, '+json.dumps(key,ensure_ascii=True)+');')==2,key
assert 'irc_tr("Persistent logging unavailable for this server:")' in app
assert 'irc_tr("Cannot open private log file:")' in app
assert 'snprintf(a->status, sizeof a->status, "%s %s",' in app
for key in keys[15:19]:
    assert 'persistent_log_error(a, '+json.dumps(key,ensure_ascii=True)+');' in app,key
with tempfile.TemporaryDirectory() as d:
    exe=Path(d)/'catalog-lookup'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                    '-Isrc','-DONEKEY_ONLY','tests/test_cli_log_lookup.c',
                    '-o',str(exe)],cwd=root,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    for lang in langs:
        catalog=json.loads((root/'i18n'/f'{lang}.json').read_text('utf8'))
        assert set(keys).issubset(catalog)
        env=os.environ.copy()
        env['ONEKEYIRC_LANG']=env['VIPIRC_LANG']=''
        env[prog.upper()+'_LANG']=lang
        result=subprocess.run([str(exe)],env=env,check=True,capture_output=True,text=True,timeout=5)
        assert result.stdout.splitlines()==[catalog[k] for k in keys],(prog,lang)
        app_exe=root/prog
        if app_exe.is_file():
            out=subprocess.run([str(app_exe),'--help'],env=env,check=True,capture_output=True,text=True,timeout=10).stdout
            for key in keys[:13]:
                assert catalog[key] in out,(prog,lang,key)
            for token in ('--sessions','/connect','/join','send_mode=review','use_signals=false'):
                assert token in out,(prog,lang,token)
    env=os.environ.copy()
    env['ONEKEYIRC_LANG']=env['VIPIRC_LANG']='C'
    result=subprocess.run([str(exe)],env=env,check=True,capture_output=True,text=True,timeout=5)
    assert result.stdout.splitlines()==keys
print(prog,'23 CLI/private-log catalog keys x 10 locales; runtime lookups: PASS')
