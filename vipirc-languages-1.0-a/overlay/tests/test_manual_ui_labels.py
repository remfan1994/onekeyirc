#!/usr/bin/env python3
"""Check the localized manual's explicit UI action strips."""
import json
from pathlib import Path
root = Path(__file__).resolve().parents[1]
rows = [['Open', 'New', 'Connect', 'Disconnect', 'Close', 'Away', 'Back'], ['Open', 'Join', 'Part', 'Close', 'Back'], ['Open', 'New', 'Close', 'Back'], ['Accept', 'Reject', 'Send...', 'Cancel transfer', 'Back']]
for lang in 'id ms tr es pt fr de ru it ar'.split():
    cat = json.loads((root/'i18n'/f'{lang}.json').read_text(encoding='utf-8'))
    manual = (root/'doc'/f'FULL-MANUAL-{lang}.txt').read_text(encoding='utf-8')
    lines = [x.strip() for x in manual.splitlines()]
    for row in rows:
        expected = '    '.join(cat[k] for k in row)
        assert lines.count(expected) == 1, (lang, row, expected)
    print(lang, 'manual UI action strips: PASS')
