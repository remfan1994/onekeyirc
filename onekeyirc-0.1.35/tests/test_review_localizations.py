#!/usr/bin/env python3
"""Ensure locale review CSV stays deterministic and doesn't omit master keys."""
import csv
import io
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
master = json.loads((root/'i18n/en.json').read_text(encoding='utf-8'))
program = root/'tools/review_localizations.py'


def run(*options):
    proc = subprocess.run([sys.executable, str(program), *options],
                          cwd=root, check=True, capture_output=True, text=True,
                          encoding='utf-8')
    return list(csv.reader(io.StringIO(proc.stdout)))

for lang in ('id', 'ar', 'fr'):
    rows=run('--lang',lang)
    assert rows[0] == ['English', lang, 'Unchanged from English']
    assert [row[0] for row in rows[1:]] == master
    assert all(len(row)==3 and row[-1] in ('yes','no') for row in rows[1:])
    unchanged=run('--lang',lang,'--unchanged-only')
    assert [row for row in rows[1:] if row[-1]=='yes'] == unchanged[1:]
all_languages=run('--all')
assert all_languages[0] == ['English','id','ms','tr','es','pt','fr','de','ru','it','ar']
assert [row[0] for row in all_languages[1:]] == master
print('review CSV: individual, source-equal and parallel exports PASS')
