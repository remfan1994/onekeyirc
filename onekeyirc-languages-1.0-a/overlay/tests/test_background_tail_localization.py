#!/usr/bin/env python3
"""35 sourced display templates: translations, literal arguments and runtime smoke."""
import json
import os
import re
import subprocess
import tempfile
import unicodedata
from pathlib import Path

root=Path(__file__).resolve().parent.parent
langs=('id','ms','tr','es','pt','fr','de','ru','it','ar')
keys=json.loads((root/'tests/background-tail-localization-keys.json').read_text('utf-8'))
assert len(keys)==35 and len(set(keys))==35
english=json.loads((root/'i18n/en.json').read_text('utf-8'))
source=(root/'src/background.c').read_text('utf-8')
assert 'bg_render_fields(' in source and 'bg_valid_template(' in source
assert set(keys).issubset(english)
assert 'memcpy(request.magic, "VIPIRCD1", 8)' in source
assert 'memcpy(request.magic, "VIPIRCC1", 8)' in source
assert 'memcpy(request.magic, "VIPIRCB3"' not in source  # ternary wire magic retained
assert '"VIPIRCB3"' in source and '"VIPIRCB4"' in source
assert 'BG_WIRE_PROGRAM "1keyirc"' in source
assert 'SO_PEERCRED' in source and 'SCM_RIGHTS' in source
assert 'bg_stdout_fields(' in source and 'bg_job_notice(' in source
for key in keys:
    assert '"'+key+'"' in source,key

old=None
for lang in langs:
    cat=json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))
    assert set(cat)==set(english),(lang,len(cat),len(english))
    for key in keys:
        value=cat[key]
        assert isinstance(value,str) and value and value!=key,(lang,key)
        assert unicodedata.normalize('NFC',value)==value,(lang,key)
        assert all(unicodedata.category(c) not in {'Cc','Cf','Cs'} for c in value),(lang,key)
        assert len(value.encode('utf-8'))<=256,(lang,key)
        assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]',value),(lang,key)
        keep=r'\{[a-z]+\}|--[a-z-]+|TERM|LC_CTYPE|0600|0700'
        assert sorted(re.findall(keep,key))==sorted(re.findall(keep,value)),(lang,key,value)
    exe=root/root.name.split('-')[0]
    if exe.is_file() and os.access(exe,os.X_OK):
        with tempfile.TemporaryDirectory(prefix='irc-tail-test-') as temp:
            env=os.environ.copy()
            env['HOME']=temp
            env['XDG_RUNTIME_DIR']=temp
            env['ONEKEYIRC_LANG' if exe.name=='onekeyirc' else 'VIPIRC_LANG']=lang
            env['LC_ALL']='C.UTF-8'
            cases=[(['--close-session','0'],keys[-1]),
                   (['--detach-session','0'],keys[-4]),
                   (['--resume','0'],keys[14])]
            for argv,key in cases:
                proc=subprocess.run([str(exe),*argv],env=env,capture_output=True,timeout=15)
                assert proc.returncode==2,(lang,argv,proc.returncode,proc.stderr)
                assert cat[key].encode('utf-8') in proc.stderr,(lang,argv,proc.stderr)
                assert b'--' in proc.stderr,(lang,argv,proc.stderr)
print('background tail localization: 35 keys x 10 locales PASS ('+root.name+')')
