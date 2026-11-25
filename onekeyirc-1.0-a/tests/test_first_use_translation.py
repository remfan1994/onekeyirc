#!/usr/bin/env python3
"""Check first-use translations, tokens, and source-aligned offline guides."""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from generate_locale_first_use_guides import LANGS, selected, render

EN = json.loads((ROOT / 'i18n/en.json').read_text('utf8'))
TOPICS = selected()
SOURCES = [text for _, lines in TOPICS for text in lines]
assert len(SOURCES) == 28 and len(set(SOURCES)) == 28
assert set(SOURCES).issubset(EN)
# Protect commands, flags, exact placeholder names and byte/pace limits;
# ordinary words, including translated labels, remain free to be localized.
TOKEN = re.compile(
    r'--[A-Za-z0-9-]+|/[a-z][a-z0-9-]+|#[A-Za-z]+'
    r'|\b(?:HOST|NICK|TARGET|TEXT|NUMBER|PID|CA|IRC|UTF-8|Enter|user'
    r'|send_mode|composer_mode|1\.2|64|16384|8|32)\b',
    re.ASCII,
)
for lang in LANGS:
    catalog = json.loads((ROOT / 'i18n' / (lang + '.json')).read_text('utf8'))
    for english in SOURCES:
        value = catalog[english]
        assert value != english and 0 < len(value.encode('utf8')) <= 256, (lang, english)
        assert sorted(TOKEN.findall(english)) == sorted(TOKEN.findall(value)), (lang, english, value)
        assert all(ord(ch) >= 32 for ch in value), (lang, english)
    assert render(lang) == (ROOT / 'doc' / f'FIRST-USE-{lang}.txt').read_bytes()
print('First-use help: 28 complete source lines x 10 languages, tokens and guides PASS')
