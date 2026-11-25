#!/usr/bin/env python3
"""Check full translated help sections and exact operational syntax.

This is a machine-checkable technical review boundary, not linguistic QA.
"""
import collections
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_help_coverage
import generate_locale_practical_guides as guides

LANGS = guides.LANGS
baseline_english = json.loads((ROOT/'i18n/en.json').read_text('utf-8'))
assert len(baseline_english) >= 519
assert len(set(baseline_english)) == len(baseline_english)
lines = audit_help_coverage.collect(ROOT)
source_keys = {x[2] for x in lines}
section_lines = guides.section_texts()
assert len(section_lines) == 10
assert sum(len(v) for _,v in section_lines) == 89
for (kind,topic),value in section_lines:
    assert value, (kind,topic)
    assert set(value) <= set(baseline_english), (kind,topic)
    assert set(value) <= source_keys, (kind,topic)
# No command/control/filename mutation. Labels and explanations may translate.
TOKEN = re.compile(
    r'doc/[A-Za-z0-9_-]+\.txt|tests/README\.md|[A-Za-z0-9_-]+\.(?:md|txt)|'
    r'--[a-z][a-z0-9_-]*|(?<![A-Za-z0-9/])/[a-z][a-z0-9_-]*|'
    r'\b(?:PID|TLS|SASL|PASS|IRC|SSH|UTC|CTCP|DCC|ZNC|NickServ|SIGCHLD|Unix|'
    r'Quickstart|Compose|Escape|Up|COMMAND)\b|\b[1-4]\b', re.ASCII)

for lang in LANGS:
    catalog = json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    assert set(catalog) == set(baseline_english), lang
    for (kind,topic),values in section_lines:
        for original in values:
            local = catalog[original]
            assert local and local != original, (lang, kind, topic, original)
            assert 0<len(local.encode('utf-8'))<=256, (lang,kind,topic,original)
            required = collections.Counter(TOKEN.findall(original))
            found = collections.Counter(TOKEN.findall(local))
            assert not required - found, (lang,kind,topic,required-found,original)
    expected = guides.render(lang)
    assert expected == (ROOT/'doc'/f'PRACTICAL-GUIDE-{lang}.txt').read_bytes(), lang
    assert len(expected) > 8000, (lang,len(expected))

with_catalog = sum(text in baseline_english for _,_,text in lines)
assert with_catalog >= 239, with_catalog
print(f'Practical help: 10 fully translated sections, 89 explanation lines, '
      f'{len(LANGS)} locales; command/option/file tokens preserved; {with_catalog}/{len(lines)} overall lines covered: PASS')
