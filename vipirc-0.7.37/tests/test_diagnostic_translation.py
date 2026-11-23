#!/usr/bin/env python3
"""Static safe-display wiring of selected diagnostics, with preserved tokens."""
import json
import re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'src/idle_commands.c').read_text('utf-8')
english=json.loads((ROOT/'i18n/en.json').read_text('utf-8'))
keys=json.loads((ROOT/'tools/localized-diagnostic-lines.json').read_text('ascii'))
assert len(keys)==15 and len(set(keys))==15
protected=re.compile(r'(?<![A-Za-z0-9_])/[A-Za-z_]+|\b(?:NICK|GPG|SASL|IRC|1|65535|63)\b|#channel|irc\.example\.org', re.ASCII)
for key in keys:
    assert key in english
    assert 'display_text(c, "'+key+'")' in source, 'missing display-only lookup: '+key
    assert source.count('display_text(c, "'+key+'")')>=1
for lang in ('id','ms','tr','es','pt','fr','de','ru','it','ar'):
    catalog=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    for key in keys:
        text=catalog[key]
        assert text and text!=key and len(text.encode('utf-8'))<=256,(lang,key)
        assert sorted(protected.findall(text))==sorted(protected.findall(key)),(lang,key,text)
print('Live diagnostic display lookup: 15 strings x 10 languages, literal tokens retained PASS')
