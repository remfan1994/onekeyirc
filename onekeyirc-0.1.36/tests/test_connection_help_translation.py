#!/usr/bin/env python3
"""Check connection/TLS/reconnect help literal syntax and guide coverage.

These checks prevent silent changes to commands, numerical values and
options. They do not certify semantic correctness in any natural language.
"""
import collections
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_help_coverage
import generate_locale_connection_guides as guides

# Source authoritative; no duplicate string spreadsheet to drift out of date.
rows = audit_help_coverage.collect(ROOT)
english = set(json.loads((ROOT / 'i18n/en.json').read_text('utf-8')))
assert len(english) >= 519
assert len(rows) == 477
assert sum(s in english for _, _, s in rows) >= 170
sections = guides.sections()
assert sum(len(lines) for _, lines in sections) == 52

# Require all command and argument literals that occur in the English line.
# Bracketed "list" and "status" words occur in ordinary descriptions too,
# so those are checked specifically where they denote command arguments.
TOKENS = re.compile(
    r'(?<![A-Za-z0-9_])(?:'
    r'/[a-z][a-z0-9_-]*|--[a-z][a-z0-9_-]*|-[a-z][a-z0-9_-]*|'
    r'\+PORT|irc\.example\.org|ca_file=|'
    r'\b(?:PATH|NUMBER|NICK|FILE|PORT|MOTD|PEM|GPG|DNS|ERROR|TLS|IRC|CA)\b|'
    r'\b[0-9]+\b)', re.ASCII)

for lang in guides.LANGS:
    catalog = json.loads((ROOT / 'i18n' / f'{lang}.json').read_text('utf-8'))
    for name, lines in sections:
        for phrase in lines:
            value = catalog[phrase]
            assert value and value != phrase, (lang, name, phrase)
            assert len(value.encode('utf-8')) <= 256, (lang, name, phrase)
            assert '\n' not in value and '\r' not in value, (lang, name)
            expected = collections.Counter(TOKENS.findall(phrase))
            for literal, count in expected.items():
                assert value.count(literal) >= count, (lang, name, literal, value)
            if ' or status,' in phrase:
                assert 'status' in value, (lang, name)
            if ' or list,' in phrase:
                assert 'list' in value, (lang, name)
            if '/autoconnect on first' in phrase:
                assert '/autoconnect on' in value, (lang, name)
    assert guides.render(lang) == (ROOT / 'doc' / f'CONNECTION-{lang}.txt').read_bytes(), lang

print('Connection help: 52 translated lines x 10 locales, exact token preservation and guides PASS')
