#!/usr/bin/env python3
"""Verify all profile messages are lookupable and an actual client failure translates."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
app=root.name.split('-')[0]
keys=json.loads((root/'tests/client-profile-translation-keys.json').read_text('utf8'))
assert len(keys)==19
src=(root/'src/client.c').read_text('utf8')
assert '#include "l10n.h"' in src
assert 'irc_tr(message)' in src
for key in keys:
    assert json.dumps(key,ensure_ascii=True) in src, key
assert 'app_disconnect(slot->app, irc_tr("Disconnected by user"))' in src
assert '"%s %.380s", irc_tr("Settings saved; command usage could not be saved:")' in src
assert 'irc_tr(committed ?' in src
assert '"%s %.350s", irc_tr("Saved default server is unavailable:")' in src
with tempfile.TemporaryDirectory() as d:
    binary=str(Path(d)/'client-profile-live')
    subprocess.run(['cc','-std=c11','-D_POSIX_C_SOURCE=200809L',
        '-O1','-ffunction-sections','-fdata-sections','-Wall','-Wextra',
        '-Werror','-I','src','-Wl,--gc-sections',
        'tests/test_client_profile_lookup.c','src/client.c',
        'src/config.c','src/autocommands.c','src/protocol.c',
        '-o',binary],cwd=root,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    for lang in ('id','ms','tr','es','pt','fr','de','ru','it','ar','C'):
        env=os.environ.copy()
        env['VIPIRC_LANG']=''
        env['ONEKEYIRC_LANG']=''
        env[f'{app.upper()}_LANG']=lang
        output=subprocess.run([binary],env=env,text=True,capture_output=True,
                              check=True,timeout=10).stdout.splitlines()
        catalog={k:k for k in keys} if lang=='C' else json.loads((root/'i18n'/f'{lang}.json').read_text('utf8'))
        expected=[catalog[k] for k in keys]+[catalog['Invalid client or primary configuration path.']]
        assert output==expected,(lang,output[-1],expected[-1])
print(app, '19 client.c profile displays and live failure x 10 locales + English fallback: PASS')
