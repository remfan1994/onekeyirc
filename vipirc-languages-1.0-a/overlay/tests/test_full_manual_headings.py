#!/usr/bin/env python3
"""Verify the localized chapter index and safe loader limits (not linguistic review)."""
import csv
import json
import unicodedata
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
DOC = ROOT / 'doc'
LANGS = 'id ms tr es pt fr de ru it ar'.split()
with (DOC / 'FULL-MANUAL-HEADINGS.tsv').open(encoding='utf-8') as fp:
    rows = list(csv.DictReader(fp, delimiter='\t'))
assert len(rows) == 47 * len(LANGS)
reference = None
for lang in LANGS:
    entries = [r for r in rows if r['locale'] == lang]
    assert [int(r['index']) for r in entries] == list(range(47)), lang
    english = [r['english_source_heading'] for r in entries]
    if reference is None:
        reference = english
    assert english == reference, lang
    titles = [r['localized_heading'] for r in entries]
    assert len(set(titles)) == 47, lang
    data = (DOC / f'FULL-MANUAL-{lang}.txt').read_bytes()
    body = data.decode('utf-8')
    lines = body.splitlines()
    assert 0 < len(data) <= 512 * 1024 and len(lines) <= 2048, lang
    assert all(len(line.encode()) <= 8192 for line in lines), lang
    assert all(unicodedata.category(c) not in ('Cc', 'Cf') or c in '\n\t' for c in body), lang
    assert data.endswith(b'\n'), lang
    positions = []
    for i, heading in enumerate(titles):
        assert lines.count(heading) == 1, (lang, heading)
        positions.append(lines.index(heading))
        if i:
            assert lines.count(f'    {i:02d}. {heading}') == 1, (lang, heading)
    assert positions == sorted(positions), lang
    assert lines[positions[0]+2:positions[0]+48] == [f'    {i:02d}. {h}' for i,h in enumerate(titles[1:], 1)], lang
    cat = json.loads((ROOT / 'i18n' / f'{lang}.json').read_text(encoding='utf-8'))
    assert titles[english.index('COMMAND PROMPT')] == cat['COMMAND PROMPT'], lang
    assert 'SOURCE CHAPTER:' not in body and 'EXACT COMMAND/CONTROL EXAMPLES (not translated)' not in body, lang
    assert '/help commands' in body and '/help ls' in body and 'use_signals=true|false' in body, lang
    print(f'{lang}: 47 headings, 46 contents entries, safe UTF-8 and command heading PASS')
print('All ten full-manual indexes passed.')
