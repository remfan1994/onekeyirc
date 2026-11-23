#!/usr/bin/env python3
from pathlib import Path
import json,re,unicodedata
root=Path(__file__).resolve().parents[1]
inv=json.loads((root/'doc/UI-LOCALIZATION-r47.json').read_text())
keys=json.loads((root/'i18n/en.json').read_text())
assert keys==sorted(set(keys))
assert len(inv)==48 and len(keys)>=1358
for item in inv:
    key=item['english']
    src=(root/item['source']).read_text()
    assert key in keys and key in src,(item['source'],key)
    if item['source']=='src/idle_commands.c':
        assert 'display_text(c, '+json.dumps(key)+')' in src,key
    elif item['source']=='src/transfers.c':
        assert 'irc_tr(' in src
    else:
        assert item['source']=='src/filepicker.c'
        assert 'irc_tr(filepicker_error(t->picker))' in (root/'src/transfers.c').read_text()
for lang in 'id ms tr es pt fr de ru it ar'.split():
    catalog=json.loads((root/'i18n'/f'{lang}.json').read_text())
    assert set(catalog)==set(keys)
    for item in inv:
        key=item['english'];value=catalog[key]
        assert value and value!=key
        assert unicodedata.normalize('NFC',value)==value
        assert len(value.encode('utf-8'))<=256,(lang,key)
        assert not any(unicodedata.category(c) in ('Cc','Cf','Cs') for c in value)
        assert not re.search(r'%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[a-zA-Z]',value)
        for token in ('IPv4','Type path','SASL','/disconnect','/connect','/save','/join','/part','/channels'):
            if token in key:assert token in value,(lang,key,token)
print('r47 source-backed translations PASS:',root.name,len(inv),'new; catalog',len(keys),'x 10 locales')
