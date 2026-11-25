#!/usr/bin/env python3
"""Revalidate every translated catalog against executable-control injections.

Run from any working directory. No network or terminal is used.
"""
from pathlib import Path
import json, re, unicodedata

root = Path(__file__).resolve().parents[1]
i18n = root / 'i18n'
keys = json.loads((i18n / 'en.json').read_text('utf-8'))
assert len(keys) == len(set(keys)) >= 844
literal = re.compile(r'(?<![A-Za-z0-9])(?:--[a-z][a-z0-9-]*|/[a-z][a-z0-9_-]*|[a-z][a-z0-9_-]*\.(?:md|txt|ini|log))(?![A-Za-z0-9_-])', re.I)
for lang in ('id', 'ms', 'tr', 'es', 'pt', 'fr', 'de', 'ru', 'it', 'ar'):
    obj = json.loads((i18n / f'{lang}.json').read_text('utf-8'))
    assert set(obj) == set(keys), lang
    for source, value in obj.items():
        assert value and isinstance(value, str)
        assert not any(unicodedata.category(c) in ('Cc','Cf','Cs') for c in value), (lang, source)
        assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]', value), (lang, source)
        for token in literal.findall(source):
            assert token in value, (lang, source, token)
print(f'catalog security: 10 x {len(keys)} entries; no controls, format directives, or altered command/file tokens PASS')
