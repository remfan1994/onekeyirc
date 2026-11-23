#!/usr/bin/env python3
"""Protect literal commands in newly translated command help and UI errors."""
import json
import re
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import audit_help_coverage as audit
import generate_locale_command_guides as guides

LANGS=guides.LANGS
SECTIONS=guides.SECTIONS
master=json.loads((ROOT/'i18n/en.json').read_text('utf-8'))
lines=[phrase for group in SECTIONS.values() for phrase in group]
assert len(lines)==21 and len(set(lines))==len(lines)
assert set(lines).issubset(master)
source=(ROOT/'src/idle_commands.c').read_text('utf-8')
actual={text for _,_,text in audit.collect()}
assert set(lines[:-1]).issubset(actual), 'command help text no longer matches the runtime'
assert lines[-1] not in actual, 'diagnostic accidentally treated as command-topic help'
assert 'display_text(c, "'+lines[-1]+'")' in source, 'diagnostic not wired to translation callback'
protected=re.compile(r'(?<![A-Za-z0-9_])/[A-Za-z_]+|\b(?:NICK|MESSAGE|PORT|COMMAND|TLS|IRC|ASCII|63|6697|0)\b',re.ASCII)
for lang in LANGS:
    catalog=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    for english in lines:
        translated=catalog[english]
        assert translated != english and 0 < len(translated.encode('utf-8'))<=256,(lang,english)
        assert sorted(protected.findall(translated))==sorted(protected.findall(english)),(lang,english,translated)
        assert all(ord(ch)>=32 for ch in translated),(lang,english)
    assert guides.render(lang)==(ROOT/'doc'/f'COMMANDS-{lang}.txt').read_bytes(),lang
translated_count=sum(text in master for text in actual)
assert translated_count>=43, ('lost help coverage',translated_count)
print('Command help: 20 exact help lines + 1 diagnostic in 10 languages; all literal syntax preserved, guides match PASS')
