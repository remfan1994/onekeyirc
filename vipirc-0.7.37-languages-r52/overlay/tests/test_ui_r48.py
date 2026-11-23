#!/usr/bin/env python3
"""Regression: 35 localized DCC display strings, no button geometry drift."""
from pathlib import Path
import json
import re
import sys
import unicodedata
root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root.parents[0]))
# The release inventory is also useful to language reviewers.
keys = json.loads((root / 'i18n/en.json').read_text())
inv = json.loads((root / 'doc/UI-LOCALIZATION-r48.json').read_text())
assert keys == sorted(set(keys)) and len(keys) >= 1393
assert len(inv) == 35
source = (root / 'src/transfers.c').read_text()
header = (root / 'src/transfer_ui_l10n.h').read_text()
for k in inv:
    assert k['english'] in keys
    assert k['english'] in source or k['english'] in header or k['english'] in (root / 'src/dcc.c').read_text(), k['english']
assert 'irc_transfer_action_layout(translated, count, COLS' in source
assert 'irc_transfer_label_cells(labels[i]) + 2' in header
assert '(int)strlen(labels[i]) + 2' not in source
assert 't->action_x[i] >= 0' in source
assert 'irc_tr(filepicker_error(t->picker))' in source  # fixed errors, not dynamic errno
assert 'irc_display_render' in source
assert 'config_wipe' in source and 'dcc_' in source
for lang in 'id ms tr es pt fr de ru it ar'.split():
    d = json.loads((root / ('i18n/' + lang + '.json')).read_text())
    assert set(d) == set(keys)
    for item in inv:
        key = item['english']
        value = d[key]
        assert value
        # Some English labels are identical in Indonesian, Italian or Portuguese.
        # Repeated delimiters in format templates are intentionally unchanged.
        assert len(value.encode('utf-8')) <= 256
        assert unicodedata.normalize('NFC', value) == value
        assert not any(unicodedata.category(c) in ('Cc', 'Cf', 'Cs') for c in value)
        assert sorted(re.findall(r'\{([a-z]+)\}', key)) == sorted(re.findall(r'\{([a-z]+)\}', value)), (lang, key)
        assert not re.search(r'%[-+0-9.]*[a-zA-Z]', value), (lang, key)
print('r48: DCC source links and full ten-language catalog integrity PASS')
