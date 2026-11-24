#!/usr/bin/env python3
"""Validate translation/source parity and safe UTF-8 catalogs, without networking."""
from pathlib import Path
import json
import re
import subprocess
import sys
import unicodedata

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT/'tools'))
from audit_help_coverage import collect
HELP_KEYS = {text for _, _, text in collect(ROOT)}
HELP_KEYS.update(json.loads((ROOT/'tools/generated_manual_keys.json').read_text('utf-8')))
HELP_KEYS.update(json.loads((ROOT/'tools/manual_paragraph_keys.json').read_text('utf-8')))
HELP_KEYS.update(json.loads((ROOT/'tools/context_help_keys.json').read_text('utf-8')))
LANGS = ('id','ms','tr','es','pt','fr','de','ru','it','ar')
EN = json.loads((ROOT/'i18n/en.json').read_text(encoding='utf-8'))
assert isinstance(EN,list) and len(EN)>=519 and len(set(EN))==len(EN), 'master catalog missing or duplicate keys'
assert all(isinstance(k,str) and k and k.isascii() for k in EN), 'invalid master keys'
# These dynamic source labels are not string literals inside irc_tr() and
# therefore require explicit catalog coverage (choices are display-only).
DYNAMIC_DISPLAY_KEYS = {
    'Alphabetical', 'Commonly used', 'Recent history',
    'Most used with this configuration', 'Contextual',
    'Alphabetic', 'Common', 'History', 'Most used',
    'Channel', 'Query',
}
assert DYNAMIC_DISPLAY_KEYS <= set(EN), 'dynamic display vocabulary incomplete'

for lang in LANGS:
    p=ROOT/'i18n'/f'{lang}.json'
    data=json.loads(p.read_text(encoding='utf-8'))
    assert set(data)==set(EN), f'{lang}: mismatched master keys'
    assert len(data)==len(EN), f'{lang}: catalog entries missing'
    for key,val in data.items():
        limit = 1024 if key in HELP_KEYS else 256
        assert isinstance(val,str) and 0<len(val.encode('utf-8'))<=limit, f'{lang}: invalid {key}'
        assert all(unicodedata.category(ch) not in {'Cc','Cf','Cs'} for ch in val), f'{lang}: control in {key}'
        assert '\x1b' not in val, f'{lang}: terminal escape in {key}'
        assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]',val), f'{lang}: format in {key}'

for p in (ROOT/'src').glob('*.c'):
    text=p.read_text(encoding='utf-8')
    literals=re.findall(r'irc_tr\(\s*"([^"\n]+)"\s*\)',text)
    for label in literals:
        assert label in EN, f'{p.name}: missing catalog term {label}'

subprocess.run([sys.executable,str(ROOT/'tools/generate_localizations.py'),'--check'],cwd=ROOT,check=True)
print(f'locale catalogs: {len(LANGS)} languages x {len(EN)} terms, source coverage and generated header PASS')
