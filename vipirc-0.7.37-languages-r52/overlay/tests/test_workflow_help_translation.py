#!/usr/bin/env python3
"""Source-aligned checks of 49 new whole-line draft translations."""
import json, re, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from audit_help_coverage import collect
from generate_locale_practical_guides import LANGS, INTRO
from generate_locale_workflow_guides import render,SECTIONS
ROWS=collect(ROOT)
assert len(ROWS)==477
SPEC={
 'command/autocommands':list(range(10)),
 'command/dcc':list(range(10)),
 'command/bindings':list(range(7)),
 'topic/hotkeys':list(range(3,12)),
 'command/bind':list(range(1,6)),
 'command/unbind':[0,1],
 'command/transfers':[0],
 'topic/configuration':[6,11,12,23,24],
}
new=[]
for group,ids in SPEC.items():
    kind,name=group.split('/')
    candidates=[s for k,n,s in ROWS if k==kind and n==name]
    for index in ids: new.append(candidates[index])
assert len(new)==49 and len(set(new))==49
master=json.loads((ROOT/'i18n/en.json').read_text('utf-8'))
assert len(master)>=591 and set(new)<=set(master)
# Technical command spellings, placeholders, limits, and syntax remain unmodified.
TOKEN=re.compile(r'(?<![A-Za-z0-9_-])(?:/[a-z][a-z0-9_-]*|--[a-z-]+|-[a-z]+|'
 r'KEY=action|\[hotkeys\]|\[autocommands\]|password=|server_password=|'
 r'command=|delay_seconds|interval_seconds|NICK|PATH|DIRECTORY|NUMBER|'
 r'Ctrl-[A-Z\[]|Alt-[a-zA-Z0-9]|F1\.\.F12|[0-9]+\.\.[0-9]+|'
 r'GPG|SASL|TLS|UTF-8|IPv[46]|NAT|IRC|0600|0400)(?![A-Za-z0-9_-])')
for lang in LANGS:
    data=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    assert set(data)==set(master),(lang,'catalog keys')
    for english in new:
        local=data[english]
        assert local!=english and local and len(local.encode('utf-8'))<=256,(lang,english)
        assert not any(ord(ch)<32 for ch in local),(lang,english)
        tokens=TOKEN.findall(english)
        for token in set(tokens):
            assert local.count(token)>=tokens.count(token),(lang,token,english,local)
    path=ROOT/'doc'/f'WORKFLOWS-{lang}.txt'
    assert path.read_bytes()==render(lang),(lang,'guide mismatch')
    assert all(h in path.read_text('utf-8') for h in ('/help autocommands','/help hotkeys','/help dcc'))
# Reject removal of a genuinely protected placeholder.
example=next(x for x in new if '/dcc accept NUMBER' in x)
assert 'NUMBER' in TOKEN.findall(example)
print('r5 workflow translations: 49 source lines x 10 languages, literal tokens and guides PASS')
