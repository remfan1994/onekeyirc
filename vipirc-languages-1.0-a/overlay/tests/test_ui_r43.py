#!/usr/bin/env python3
"""r43 source-linked, full-catalog, placeholder and control-code audit."""
from pathlib import Path
from collections import Counter
import json,re,unicodedata
root=Path(__file__).resolve().parents[1]
keys=json.loads((root/'i18n/en.json').read_text('utf8'))
rows=json.loads((root/'doc/UI-LOCALIZATION-r43.json').read_text('utf8'))
assert keys==sorted(set(keys))
assert len(rows)==(19 if (root/'src/ui.c').is_file() else 8)
assert len(keys)>=(1240 if (root/'src/ui.c').is_file() else 966)
help_keys=set(json.loads((root/'tools/context_help_keys.json').read_text('utf8')))
for item in rows:
    key=item['english']
    src=(root/item['source']).read_text('utf8')
    assert key in keys and key in src,(item['source'],key)
    assert Counter(item['fields'])==Counter(re.findall(r'\{([a-z]+)\}',key)),key
    if item['kind']=='named-fields':assert 'irc_display_render' in src
    else: assert 'irc_tr('+json.dumps(key,ensure_ascii=True)+')' in src or 'irc_tr('+json.dumps(key,ensure_ascii=False)+')' in src,key
for lang in 'id ms tr es pt fr de ru it ar'.split():
    catalog=json.loads((root/'i18n'/f'{lang}.json').read_text('utf8'))
    assert set(catalog)==set(keys)
    for item in rows:
        key=item['english'];val=catalog[key]
        assert unicodedata.normalize('NFC',val)==val
        assert val and len(val.encode('utf8'))<=(1024 if key in help_keys else 256),(lang,key,len(val.encode('utf8')))
        assert Counter(re.findall(r'\{([a-z]+)\}',key))==Counter(re.findall(r'\{([a-z]+)\}',val)),(lang,key)
        assert all(unicodedata.category(ch) not in ('Cc','Cf','Cs') for ch in val),(lang,key)
        assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]',val),(lang,key)
        for cmd in re.findall(r'(?<![\w])/[a-z]+',key):assert cmd in val,(lang,key,cmd)
    if not (root/'src/ui.c').is_file():
        for item in rows:
            key=item['english']; val=catalog[key]
            for token in ('Up','Down','Page Up','Page Down','Home','End','Escape','Slash','Space'):
                if token in key:assert token in val,(lang,key,token)
print('r43 full source-linked catalogs and literal fields PASS:',root.name,len(keys),'terms per locale')
