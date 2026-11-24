#!/usr/bin/env python3
"""Ensure new r38 catalog keys are actually used by source display paths."""
from pathlib import Path
import json, unicodedata, string, re
root=Path(__file__).resolve().parents[1]
vip=root.name.startswith('vipirc')
rows=json.loads((root/'doc/UI-LOCALIZATION-r38.json').read_text('utf8'))
assert len(rows)==(47 if vip else 11)
keys=json.loads((root/'i18n/en.json').read_text('utf8'))
assert len(keys)>=(1095 if vip else 926) and len(keys)==len(set(keys))
links={}
for row in rows:
    source=links.setdefault(row['source'],(root/row['source']).read_text('utf8'))
    key=row['english']
    assert key in keys and key in source,(row['source'],key)
    assert row['fields']==[name for _,name,_,_ in string.Formatter().parse(key) if name]
    if row['kind']=='named-fields':
        assert 'irc_display_render(' in source
        assert 'const IrcDisplayField fields[]' in source
        assert row['fields']
    else:
        assert 'irc_tr('+json.dumps(key,ensure_ascii=False)+')' in source,(row['source'],key)
# Three existing r37 translations are already wired to bounded ncurses rendering.
if vip:
    ui=(root/'src/ui.c').read_text('utf8')
    assert 'irc_tr("Command exceeds the input byte limit.")' in ui
    for existing in ('Connecting server {number}.', 'Server {number} disconnected. Accepted direct transfers continue.', 'Could not apply/save bindings: {error}'):
        assert existing in ui and existing in keys
for lang in ('id','ms','tr','es','pt','fr','de','ru','it','ar'):
    cat=json.loads((root/'i18n'/f'{lang}.json').read_text('utf8'))
    assert set(cat)==set(keys),(lang,len(cat))
    for row in rows:
        key=row['english'];value=cat[key]
        assert value and value!=key and value==unicodedata.normalize('NFC',value),(lang,key)
        assert len(value.encode('utf8'))<=256,(lang,key)
        assert not any(unicodedata.category(ch) in ('Cc','Cf','Cs') for ch in value),(lang,key)
        assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]',value),(lang,key)
        assert sorted(name for _,name,_,_ in string.Formatter().parse(value) if name)==sorted(row['fields']),(lang,key)
print('r38 source links, '+root.name+', 10 locales PASS')
