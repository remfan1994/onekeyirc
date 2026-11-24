#!/usr/bin/env python3
"""r36 source-bound catalog checks, no network and no native UI dependency."""
from pathlib import Path
import json,unicodedata,string,re
root=Path(__file__).resolve().parents[1]
entries=json.loads((root/'doc/UI-LOCALIZATION-r36.json').read_text('utf8'))
langs=('id','ms','tr','es','pt','fr','de','ru','it','ar')
assert len(entries)==(58 if root.name.startswith('vipirc') else 9)
english=json.loads((root/'i18n/en.json').read_text('utf8'))
assert len(english)==len(set(english)) and len(english)>=(1006 if root.name.startswith('vipirc') else 913)
source_cache={}
for row in entries:
    key,path,kind=row['english'],row['source'],row['kind']
    assert key in english
    source=source_cache.setdefault(path,(root/path).read_text('utf8'))
    assert key in source,(key,path)
    if kind=='named-fields':
        assert 'irc_display_render(' in source and '"'+key+'"' in source
    elif path.endswith('settings.c') or path.endswith('ui.c'):
        assert 'irc_tr("'+key+'")' in source,(key,path)
    elif path.endswith('app.c') or path.endswith('prompt.c'):
        assert 'app_status(a, "%s", irc_tr("'+key+'"))' in source or ('retained ? irc_tr("'+key+'")' in source),(key,path)
    else:
        assert 'notice(idle, irc_tr("'+key+'"))' in source,(key,path)
for lang in langs:
    catalog=json.loads((root/'i18n'/f'{lang}.json').read_text('utf8'))
    assert set(catalog)==set(english)
    for row in entries:
        key=row['english'];value=catalog[key]
        assert value and value!=key and value==unicodedata.normalize('NFC',value),(lang,key)
        assert len(value.encode('utf8'))<=256,(lang,key)
        assert not any(unicodedata.category(c) in {'Cc','Cf','Cs'} for c in value),(lang,key)
        assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]',value),(lang,key)
        names=lambda s:sorted(x for _,x,_,_ in string.Formatter().parse(s) if x)
        assert names(key)==names(value),(lang,key)
        for token in ('ASCII','UTF-8','TLS','DCC','IRC','CR','LF','/connect','/catchup','persistent_logging','Whois','[y/N]'):
            if token in key:assert token in value,(lang,key,token)
print('r36',root.name,len(entries),'linked strings x ten locales: PASS')
