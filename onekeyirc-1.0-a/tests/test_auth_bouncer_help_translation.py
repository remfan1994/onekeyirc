#!/usr/bin/env python3
"""Guard syntax/security literals in reviewed-scope draft help translations.

This is machine verification of technical literal preservation, not proof of
natural-language accuracy. Native-speaker review is still required.
"""
import collections
import json
import re
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import audit_help_coverage as audit
import generate_locale_bouncer_auth_guides as guide

rows=audit.collect(ROOT)
english=json.loads((ROOT/'i18n/en.json').read_text('utf-8'))
assert len(english)>=519
assert len(rows)==477
assert sum(line in english for _,_,line in rows)>=170
sections=guide.sections()
lines=list(dict.fromkeys(s for _,_,group in sections for s in group))
assert len(lines)==25
# Preserve spellings for protocol commands, auth/CA file options, credential
# sizes, permissions, service names and references. We never translate syntax.
TOKEN = re.compile(r'(?:doc/BOUNCERS\.txt|examples/|/help [a-z]+|'
                   r'(?<![A-Za-z0-9_])/[a-z][a-z0-9_-]*|--[a-z][a-z0-9_-]*|'
                   r'\b(?:password=|server_password=|COMMAND|BouncerServ|Help|'
                   r'ZNC|SASL|PASS|TLS|IRC|GPG|CA|PEM|MiB|I/O|FIFO|'
                   r'0600|0400|1024|503|16|pinentry)\b|'
                   r'\*status|\*module)',re.ASCII)
# Use a special check for options ending with '=', which are not word tokens.
SPECIAL=('password=','server_password=','doc/BOUNCERS.txt','examples/','/help tls','/help bouncers','*status','*module')
for lang in guide.LANGS:
    translations=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    assert len(translations)>=519
    for original in lines:
        local=translations[original]
        assert local and local!=original,(lang,original)
        assert len(local.encode('utf-8'))<=256,(lang,original,len(local.encode('utf-8')))
        assert all(ord(ch)>=32 for ch in local),(lang,original)
        for tok,n in collections.Counter(TOKEN.findall(original)).items():
            # Repeated compound tokens such as '/help tls' overlap '/help'.
            assert local.count(tok)>=n,(lang,tok,original,local)
        for tok in SPECIAL:
            if tok in original:assert tok in local,(lang,tok,original,local)
    assert guide.render(lang)==(ROOT/'doc'/f'AUTH-BOUNCERS-{lang}.txt').read_bytes(),lang
print('Authentication/bouncer help: 25 unique strings / 26 source lines x 10 locales; literal syntax and guides PASS')
