#!/usr/bin/env python3
"""Verify complete configuration/status translations and literal IRC syntax."""
import re,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from audit_help_coverage import collect
from generate_locale_practical_guides import LANGS
from generate_locale_config_status_guides import render
ROWS=collect(ROOT)
assert len(ROWS)==477
SECTIONS=(('topic','configuration',27),('command','summary',7),
          ('command','server',7),('command','users',4),('command','watch',2))
# /server includes one help line translated in a previous release.
for kind,name,count in SECTIONS:
    assert len([s for k,n,s in ROWS if k==kind and n==name])==count,(kind,name)
master=json.loads((ROOT/'i18n/en.json').read_text('utf-8'))
assert len(master)>=632
literal=re.compile(r'(?<![A-Za-z0-9_-])(?:\[[a-z]+\]|'
 r'[a-z_]+=(?:[A-Za-z0-9_|.-]*)|<config_path>\.commands|'
 r'\.[a-z]+|/[a-z][a-z0-9_-]*|\#[a-z]+|'
 r'\b(?:IRC|GPG|TLS|CA|MOTD|NAMES|WHOIS|NUMBER|HOST|UTF-8|0700|1\.\.8|1\.\.127)\b)')
for lang in LANGS:
    data=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    assert set(data)==set(master),(lang,'key mismatch')
    for kind,name,_ in SECTIONS:
        for english in [s for k,n,s in ROWS if k==kind and n==name]:
            translated=data[english]
            assert translated and translated != english,(lang,name,english)
            assert len(translated.encode())<=256,(lang,name,english)
            for token in set(literal.findall(english)):
                if translated.count(token)<english.count(token):
                    raise AssertionError(f'{lang} /help {name}: token {token!r} missing in {translated!r}')
    for which in ('CONFIGURATION','STATUS'):
        assert (ROOT/'doc'/f'{which}-{lang}.txt').read_bytes()==render(lang,which)
    # Semantics that cannot be inferred from a term count alone.
    guide=(ROOT/'doc'/f'CONFIGURATION-{lang}.txt').read_text('utf-8')
    assert all(s in guide for s in ('/help tls','/help logging','/help sessions','0700',
      '<config_path>.commands','/autojoin','/autoauth'))
    guide=(ROOT/'doc'/f'STATUS-{lang}.txt').read_text('utf-8')
    assert all(s in guide for s in ('MOTD','NUMBER','NAMES','WHOIS','1..127','/save'))
print('r6 configuration/status: 5 complete sections, 10 locales, syntax and guides PASS')
