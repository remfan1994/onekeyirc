#!/usr/bin/env python3
"""Source-linked and catalog security checks for background status translation."""
import json
import os
import re
import subprocess
import tempfile
import unicodedata
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
LANGS=('id','ms','tr','es','pt','fr','de','ru','it','ar')
keys=json.loads((ROOT/'tests/background-localization-keys.json').read_text('utf-8'))
en=json.loads((ROOT/'i18n/en.json').read_text('utf-8'))
source=(ROOT/'src/background.c').read_text('utf-8')
assert len(keys)==30 and len(set(keys))==30
assert set(keys).issubset(en)
assert '#include "l10n.h"' in source
assert 'irc_tr(message)' in source
assert 'fputs(irc_tr(english), stderr)' in source
assert 'fputs(irc_tr(state), stdout)' in source
for key in keys:
    assert json.dumps(key,ensure_ascii=False) in source or ('"'+key+'"') in source,repr(key)
assert 'BG_WIRE_PROGRAM "1keyirc"' in source
assert 'memcpy(request.magic, "VIPIRCD1", 8)' in source
assert 'memcpy(request.magic, "VIPIRCC1", 8)' in source
assert 'SO_PEERCRED' in source and 'SCM_RIGHTS' in source
assert 'fputs(irc_tr(english), stderr)' in source
for lang in LANGS:
    cat=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    assert set(cat)==set(en),(lang,len(cat),len(en))
    for key in keys:
        value=cat[key]
        assert value and isinstance(value,str),(lang,key)
        assert all(unicodedata.category(c) not in {'Cc','Cf','Cs'} for c in value),(lang,key)
        assert unicodedata.normalize('NFC', value)==value,(lang,key)
        assert '\n' not in value and '\r' not in value and '\x1b' not in value,(lang,key)
        assert len(value.encode('utf-8'))<=256,(lang,key)
        assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]',value),(lang,key)
        if '--sessions' in key: assert '--sessions' in value,(lang,key)
        if 'SIGCHLD' in key: assert 'SIGCHLD' in value,(lang,key)
        if '64' in key: assert '64' in value,(lang,key)
        assert value != key,(lang,key)

# Verify a real displayed path for each locale if a built CLI is available.
exe=ROOT/ROOT.name.split('-')[0]
if exe.is_file() and os.access(exe,os.X_OK):
    with tempfile.TemporaryDirectory() as home:
        # Private registry location; do not touch any of the user's jobs.
        for lang in LANGS:
            env=os.environ.copy()
            env['HOME']=home
            env['XDG_RUNTIME_DIR']=home
            env['ONEKEYIRC_LANG' if exe.name=='onekeyirc' else 'VIPIRC_LANG']=lang
            p=subprocess.run([str(exe),'--sessions'],env=env,capture_output=True,text=True,timeout=10)
            cat=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
            assert p.returncode==0,(lang,p.stderr)
            assert cat['background jobs:'] in p.stdout,(lang,p.stdout)
            assert cat['No background jobs.'] in p.stdout,(lang,p.stdout)
            assert cat['Resume a listed detached job with:'] in p.stdout,(lang,p.stdout)
            assert '--resume N' in p.stdout and '--detach-session N' in p.stdout,(lang,p.stdout)
print(f"background localization: {len(keys)} source paths x {len(LANGS)} locales PASS ({ROOT.name})")
