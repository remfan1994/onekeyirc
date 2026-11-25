#!/usr/bin/env python3
"""Locale option descriptions in --help; never alter accepted option names."""
import json
import os
from pathlib import Path
import re
import subprocess
import unicodedata

root=Path(__file__).resolve().parents[1]
prog=root.name.split('-',1)[0]
assert prog in ('onekeyirc','vipirc')
langs=('id','ms','tr','es','pt','fr','de','ru','it','ar')
keys=json.loads((root/'tests/cli-option-keys.json').read_text('ascii'))
english=json.loads((root/'i18n/en.json').read_text('utf-8'))
source=(root/'src/main.c').read_text('utf-8')
assert len(keys)==26 and len(set(keys))==26
assert set(keys).issubset(english)
assert source.count('cli_usage_options(out, options, sizeof options / sizeof options[0]);')==2
assert source.count('fputs(irc_tr("Usage: "), out);')==2
assert source.count('irc_tr("A small IRC client for Unix-like terminals.")')==2
assert source.count('irc_tr("Command-line overview (option spellings remain unchanged):")')==1
version=re.search(r'^#define (?:ONEKEY_VERSION|VIPIRC_VERSION) "([^"]+)"$', (root/'src/app.h').read_text('utf-8'), re.M)
assert version and source.count('[lang-rtl-'+version[1]+']')==1
option_rows=re.findall(r'^\s*\{"(  -[^"]*|      --[^"]*)", "([^"]*)"\},$',source,re.M)
assert len(option_rows)>=45,len(option_rows)
for syntax,key in option_rows:
    assert key in keys and syntax.startswith('  ')
for lang in langs:
    local=json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))
    assert set(local)==set(english)
    for key in keys:
        assert local[key] and local[key]!=key,(lang,key)
        assert len(local[key].encode('utf-8'))<=256,(lang,key)
        assert unicodedata.normalize('NFC',local[key])==local[key],(lang,key)
        assert not any(unicodedata.category(c) in ('Cf','Cc','Cs') for c in local[key]),(lang,key)
    exe=root/prog
    if exe.is_file():
        env=os.environ.copy()
        env['VIPIRC_LANG']=env['ONEKEYIRC_LANG']=''
        env[prog.upper()+'_LANG']=lang
        result=subprocess.run([str(exe),'--help'],env=env,check=True,capture_output=True,text=True,timeout=10)
        lines=result.stdout.splitlines()
        assert lines[0]==local['Command-line overview (option spellings remain unchanged):'],(prog,lang)
        assert any(line.startswith(local['Usage: ']+prog) for line in lines),(prog,lang)
        for syntax,key in option_rows[:23]:
            if prog=='onekeyirc' and '--ncurses' in syntax:continue
            assert syntax+local[key] in lines,(prog,lang,syntax,key)
        for label in ('--server HOST','--tls','--plain','--ca-file FILE','--no-connect','--resume [JOB]','--detach-session N','--close-session N'):
            assert any(label in line for line in lines),(prog,lang,label)
print(prog,'26 translated CLI keys x 10 languages: PASS')
