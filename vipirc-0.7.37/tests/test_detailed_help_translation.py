#!/usr/bin/env python3
"""Detailed-help localization: topics match runtime source and preserve syntax."""
import json
import re
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import generate_locale_topic_guides as guide
TOKEN = re.compile(r'(?<![A-Za-z_])(?:/[A-Za-z_]+|--[a-z][a-z-]+|\bNUMBER\b|\bUSER\b|\bPATH\b)')
source = (ROOT / 'src' / 'idle_commands.c').read_text('utf-8')
english = json.loads((ROOT / 'i18n' / 'en.json').read_text('utf-8'))
keys = [line for group in guide.SECTIONS.values() for line in group]
assert len(keys) == 23 and len(set(keys)) == len(keys)
for key in keys:
    assert key in english and key in source, f'unconnected topic help: {key}'
for lang in guide.LANGS:
    catalog=json.loads((ROOT / 'i18n' / (lang+'.json')).read_text('utf-8'))
    assert len(catalog) >= 519
    for key in keys:
        translated = catalog[key]
        assert translated and translated != key, (lang, key)
        assert sorted(TOKEN.findall(key)) == sorted(TOKEN.findall(translated)), (lang, key)
        assert len(translated.encode('utf-8'))<=256, (lang,key)
        assert not any(ord(ch) < 32 for ch in translated), (lang,key)
    assert guide.render(lang) == (ROOT/'doc'/('TOPICS-'+lang+'.txt')).read_bytes(), lang
print('detailed topic help: 23 source lines x 10 languages, commands preserved, generated guides PASS')
