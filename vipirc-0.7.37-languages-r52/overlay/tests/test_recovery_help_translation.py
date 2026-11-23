#!/usr/bin/env python3
"""Exact-source checks for optional r4 session/logging translations.

This is a mechanical check; translation quality requires speakers of the
respective languages. In particular it cannot validate terminal bidi shaping.
"""
import json
import re
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from audit_help_coverage import collect
from generate_locale_practical_guides import INTRO,LANGS

master=json.loads((ROOT/'i18n/en.json').read_text(encoding='utf8'))
source=collect(ROOT)
assert len(source)==477
assert len(master)>=542 and len(set(master))==len(master)
sections=(('command','background',13),('command','catchup',5),('topic','logging',8))
expected=[]
for kind,name,count in sections:
    lines=[s for k,n,s in source if (k,n)==(kind,name)]
    assert len(lines)==count, (name,len(lines))
    expected.append((name,lines))
    assert set(lines)<=set(master),('untranslated section',name)

# Keep syntactically significant tokens. The token patterns use ASCII boundaries,
# so directly adjacent Arabic/Cyrillic prose does not conceal an omitted flag.
protected=re.compile(r'(?<![A-Za-z0-9_-])(?:--[a-z-]+|/[a-z][a-z_-]*|NUMBER|0700|0600|CONFIG_PATH\.log|SECURITY\.md|background_away|Home|End|Escape|Up/Down|Page Up|Page Down)(?![A-Za-z0-9_-])')
def count_tokens(value):
    return {tok:value.count(tok) for tok in set(protected.findall(value))}

for lang in LANGS:
    translations=json.loads((ROOT/'i18n'/f'{lang}.json').read_text(encoding='utf8'))
    guide=(ROOT/'doc'/f'RECOVERY-{lang}.txt').read_text(encoding='utf8')
    assert len(translations)==len(master)
    assert set(translations)==set(master)
    assert guide.startswith(INTRO[lang][0])
    assert 'MANUAL.txt' in guide and 'SECURITY.md' in guide
    assert INTRO[lang][1] in guide and INTRO[lang][2] in guide
    for name,lines in expected:
        assert guide.count('/help '+name)>=1
        for english in lines:
            local=translations[english]
            assert local and local != english,(lang,english)
            assert len(local.encode('utf8'))<=256,(lang,len(local.encode('utf8')),english)
            assert '\n' not in local and '\r' not in local and '\x1b' not in local
            for token,n in count_tokens(english).items():
                assert local.count(token)>=n,(lang,token,english,local)
            assert local in guide,(lang,name,english)
    assert len(guide.encode('utf8'))<18000,lang

# Any guard that permits a missing option is a bad guard.
example=next(s for name,lines in expected for s in lines if '--detach-session' in s)
assert '--detach-session' in count_tokens(example)
assert '--detach-session' not in count_tokens(example.replace('--detach-session','',1))
print('r4 recovery/logging: 26 exact help lines x 10 languages, flags, guides and limits PASS')
