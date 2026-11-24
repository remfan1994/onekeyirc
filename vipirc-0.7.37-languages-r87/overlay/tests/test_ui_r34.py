#!/usr/bin/env python3
"""Independent source-path and localization regressions for r34.
Not a substitute for full terminal/functional or native speaker tests.
"""
from pathlib import Path
import json, re, string, unicodedata
root=Path(__file__).resolve().parents[1]
langs=('id','ms','tr','es','pt','fr','de','ru','it','ar')
# Bound source to catalogs. Avoid accepting a catalog-only translation.
source=(root/'src/ui.c').read_text('utf8')
source_one=(root/'src/idle.c').read_text('utf8')
head=(root/'src/display_l10n.h').read_text('utf8') if (root/'src/display_l10n.h').exists() else ''
project=root.name
vip_fixed=(
'Welcome to vipirc',
'(Press a printable key, Enter, Tab, or an arrow to begin.)',
'Cannot update terminal signals; saved preference takes effect on next terminal attachment.',
'Returned to vipirc.',
"Could not open this server's settings. Choose it in Settings.",
'Key binding table closed. Unapplied edits discarded.',
'The table is full. Edit or remove a binding first.',
'Binding staged. Apply changes this session; Save also writes the primary config.',
'Cannot stage that binding; choose a supported key and action.',
'Paste ignored in the key binding table; type key names explicitly.',
'That key already has a binding. Edit its existing row or choose another key.',
'Select an action with arrows, then Enter stages the binding.',
'Removal staged. Apply or Save to use the changed table.',
'Search cleared.',
'No matches. Press / to change the search.',
' Wrapped.',
'Out of memory; could not open manual search.',
'Search is limited to 256 UTF-8 bytes.',
'Unsupported control character in search.',
'Server limit reached (8). Close a secondary server first.',
)
keys=(
'Returned to {view}.',
'Could not apply/save bindings: {error}',
'Match {current} of {total}.{wrap}',
'Connecting server {number}.',
'Server {number} disconnected. Accepted direct transfers continue.',
)
ones=(
'A stream line could not be retained (allocation failed).',
'Completed command would exceed the input limit; draft retained.',
'Removed the selected command from recent history.',
"Reset the selected command's usage count.",
'This suggestion comes from the command catalog.',
'Catch-up is available in channel and query conversations.',
'Destination disconnected; draft retained.',
'Destination changed or channel was left; draft retained.',
'Finish or cancel the current draft before changing editors.',
'ASCII terminal: non-ASCII input rejected; use a UTF-8 LC_CTYPE.',
'Invalid UTF-8 input ignored.',
'Incomplete UTF-8 input ignored.',
'Paste interrupted by terminal change; review the retained draft before submitting.',
)
if project.startswith('vipirc'):
    required=vip_fixed+keys
    assert '#include "display_l10n.h"' in source
    assert 'app_status(a, "%s", label)' in source
    for key in vip_fixed:
        assert f'irc_tr("{key}")' in source, key
    for key in keys:
        assert key in source, key
    assert 'for (size_t i = 0; i < count; ++i)' in head
    assert 'if (length >= cap - used)' in head
else:
    required=ones
    for key in ones:
        assert f'notice(idle, irc_tr("{key}"))' in source_one, key
    assert 'notice(idle, irc_tr("No matching suggestions."))' in source_one
keys_en=json.loads((root/'i18n/en.json').read_text('utf8'))
assert len(set(keys_en))==len(keys_en) and set(required)<=set(keys_en)
for lang in langs:
    cat=json.loads((root/'i18n'/f'{lang}.json').read_text('utf8'))
    assert set(cat)==set(keys_en)
    for key in required:
        value=cat[key]
        assert value and value != key, (lang,key)
        assert value==unicodedata.normalize('NFC',value), (lang,key)
        assert len(value.encode('utf8'))<=256, (lang,key)
        assert not any(unicodedata.category(ch) in {'Cc','Cf','Cs'} for ch in value), (lang,key)
        if key in keys:
            fmt=lambda s: [x for _,x,_,_ in string.Formatter().parse(s) if x]
            original,localized=fmt(key),fmt(value)
            assert len(original)==len(localized) and sorted(original)==sorted(localized), (lang,key,localized)
        if key in ones:
            for protected in ('UTF-8','LC_CTYPE'):
                if protected in key: assert protected in value, (lang,key)
print('r34',project,'source hooks:',len(required),'translated keys x 10 locales PASS')
