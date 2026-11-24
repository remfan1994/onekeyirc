#!/usr/bin/env python3
"""Check user-facing navigation help for integrity and syntax preservation."""
import json
import re
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import generate_locale_navigation_guides as guides
from audit_help_coverage import collect

source=json.loads((ROOT/'i18n/en.json').read_text('utf-8'))
groups=guides.sections()
keys=[line for texts in groups.values() for line in texts]
assert len(keys)==40 and len(set(keys))==40
assert set(keys).issubset(source)
# ASCII-only word boundaries: Arabic and other scripts can directly precede a
# literal /command. Do not turn these English protocol tokens into new words.
PROTECTED=re.compile(r'(?<![A-Za-z0-9_])/[a-z][a-z0-9_-]*|(?<![A-Za-z0-9_])#[A-Z]+|\b(?:NUMBER|NICK|MOTD|TLS|GPG|IRC|ASCII|UTF-8|0|1)\b')
for lang in guides.LANGS:
    catalog=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    for key in keys:
        value=catalog[key]
        assert value and value!=key,(lang,key)
        assert sorted(PROTECTED.findall(key))==sorted(PROTECTED.findall(value)),(lang,key,value)
        assert len(value.encode('utf-8'))<=256,(lang,key)
        assert all(ord(ch)>=32 for ch in value),(lang,key)
    assert guides.render(lang)==(ROOT/'doc'/f'NAVIGATION-{lang}.txt').read_bytes(),lang
assert len({line for _,_,line in collect() if line in source})>=82
print('navigation help: 40 source explanations x 10 languages; exact tokens and generated guides PASS')
