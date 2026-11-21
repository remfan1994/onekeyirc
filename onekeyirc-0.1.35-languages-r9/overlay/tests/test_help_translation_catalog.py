#!/usr/bin/env python3
"""Check localized help preserves all command and configuration literals."""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT/'tools'))
from audit_help_coverage import collect
LONG_HELP_KEYS = {text for _, _, text in collect(ROOT)}
EN = json.loads((ROOT/'i18n/en.json').read_text(encoding='utf-8'))
COMMAND = re.compile(r'(?<![A-Za-z0-9_])/[A-Za-z_]+')
PROTECTED = ('COMMAND', 'TOPIC', 'NICK', 'TEXT', 'SSH', 'TLS', 'UTF-8',
             'irc.example.org', '#channel', 'VIPIRC_LANG=id', 'VIPIRC_LANG=tr')
HELP_KEYS = [key for key in EN if ' /help ' in key or key.startswith(('Example: VIPIRC_LANG',
                'First connection:', 'Private chat:', 'Language selection and UTF-8'))]
assert len(HELP_KEYS) >= 17, f'localized help too small: {len(HELP_KEYS)}'
for lang in ('id','ms','tr','es','pt','fr','de','ru','it','ar'):
    t=json.loads((ROOT/'i18n'/f'{lang}.json').read_text(encoding='utf-8'))
    for key in HELP_KEYS:
        value = t[key]
        assert COMMAND.findall(value) == COMMAND.findall(key), (lang,key,value)
        for token in PROTECTED:
            if token in key: assert token in value, (lang,key,token)
        assert len(value.encode('utf-8')) <= (1024 if key in LONG_HELP_KEYS else 256)
    assert t['Read help topics: /help ls.'] != 'Read help topics: /help ls.', lang
print(f'help literal safety: {len(HELP_KEYS)} strings in 10 locales PASS')
