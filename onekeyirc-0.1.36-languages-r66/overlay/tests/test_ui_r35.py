#!/usr/bin/env python3
"""r35: source-linked display and translator-bundle validation."""
from pathlib import Path
import json,unicodedata,string,re
root=Path(__file__).resolve().parents[1]
rows=json.loads((root/'doc/UI-LOCALIZATION-r35.json').read_text('utf8'))
langs=('id','ms','tr','es','pt','fr','de','ru','it','ar')
assert len(rows)==(29 if root.name.startswith('vipirc') else 8)
english=json.loads((root/'i18n/en.json').read_text('utf8'))
assert len(set(english))==len(english)
for row in rows:
    key=row['english'];path=row['source'];kind=row['kind'];fields=row['fields']
    assert key in english
    source=(root/path).read_text('utf8')
    if kind=='named-fields':
        assert key in source and 'irc_display_render(' in source,(key,path)
    elif path.endswith('settings.c'):
        assert 'irc_tr("'+key+'")' in source,(key,path)
    elif path.endswith('app.c'):
        assert 'app_status(a, "%s", irc_tr("'+key+'"))' in source,(key,path)
    else: assert 'notice(idle, irc_tr("'+key+'"))' in source,(key,path)
for lang in langs:
    data=json.loads((root/'i18n'/f'{lang}.json').read_text('utf8'))
    assert set(data)==set(english)
    for row in rows:
        key=row['english'];value=data[key]
        assert value and value!=key,(lang,key)
        assert value==unicodedata.normalize('NFC',value),(lang,key)
        assert len(value.encode('utf8'))<=256,(lang,key)
        assert not any(unicodedata.category(ch) in ('Cc','Cf','Cs') for ch in value),(lang,key)
        fmt=lambda s:[name for _,name,_,_ in string.Formatter().parse(s) if name]
        assert len(fmt(key))==len(fmt(value)) and sorted(fmt(key))==sorted(fmt(value)),(lang,key)
        for token in ('/disconnect','Direct IRC','TLS','UTF-8'):
            if token in key:assert token in value,(lang,key,token)
print('r35 source-linked catalog entries:',len(rows),'x10 locales PASS')
