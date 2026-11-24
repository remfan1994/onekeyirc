#!/usr/bin/env python3
"""r33 ncurses display source links; safe to run without curses dev libs."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
lang=('id','ms','tr','es','pt','fr','de','ru','it','ar')
source={name:(root/'src'/name).read_text() for name in ('ui.c','settings.c','prompt.c')}
ui=[
    'Key name (type the name, not the key itself):',
    'Examples: F7, Ctrl-K, Alt-x, PageUp, Enter',
    'Enter: choose action | Backspace: edit | Esc: table',
    'Up/Down: select | Home/End: first/last',
    'Enter: stage binding | Esc: key name',
    'a:Add  Enter:Edit  d:Remove  p:Apply  s:Save  Esc:Back',
    'Custom bindings only. Add a key and action, then Apply or Save. Esc discards staged edits.',
    'Cannot allocate Settings; draft preserved.',
    'Cannot allocate input buffer.',
]
settings=['Settings closed.','Unapplied settings discarded.']
prompt=['No matching command.','No matching command history.']
for fn in source:
    assert '#include "l10n.h"' in source[fn],fn
for key in ui:
    assert 'irc_tr("'+key+'")' in source['ui.c'],key
for key in settings:
    assert '"'+key+'"' in source['settings.c'],key
assert 'irc_tr(changed ? "Unapplied settings discarded." : "Settings closed.")' in source['settings.c']
for key in prompt:
    assert 'irc_tr("'+key+'")' in source['prompt.c'],key
for key in ui[6:]+prompt:
    assert 'app_status(a, "%s", irc_tr("'+key+'"));' in source['ui.c']+source['prompt.c'],key
keys=set(json.loads((root/'i18n/en.json').read_text()))
for key in ui+settings+prompt:assert key in keys,key
for code in lang:
    cat=json.loads((root/'i18n'/f'{code}.json').read_text())
    for key in ui+settings+prompt:
        assert cat[key] and len(cat[key].encode())<=256,(code,key)
print('r33 vipirc UI source links and catalogs: 13 messages x 10 locales PASS')
