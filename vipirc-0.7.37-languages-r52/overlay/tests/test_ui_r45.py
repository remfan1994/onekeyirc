#!/usr/bin/env python3
"""r45: source-linked localized strings, integrity and safe named fields."""
from pathlib import Path
from collections import Counter
import json,re,unicodedata
root=Path(__file__).resolve().parents[1]
inv=json.loads((root/'doc/UI-LOCALIZATION-r45.json').read_text('utf8'))
keys=json.loads((root/'i18n/en.json').read_text('utf8'))
assert len(inv)==17
assert keys==sorted(set(keys)) and len(keys)>=1280
help_keys=set(json.loads((root/'tools/context_help_keys.json').read_text('utf8')))
for item in inv:
    key=item['english']
    src=(root/item['source']).read_text('utf8')
    assert key in keys and json.dumps(key,ensure_ascii=True) in src,key
    assert Counter(item['fields'])==Counter(re.findall(r'\{([a-z]+)\}',key)),key
    if item['kind']=='named-fields': assert 'irc_display_render' in src
    else: assert 'irc_tr('+json.dumps(key,ensure_ascii=True)+')' in src,key
for lang in 'id ms tr es pt fr de ru it ar'.split():
    values=json.loads((root/'i18n'/f'{lang}.json').read_text('utf8'))
    assert set(values)==set(keys)
    for item in inv:
        key=item['english'];val=values[key]
        assert val and unicodedata.normalize('NFC',val)==val,(lang,key)
        assert Counter(re.findall(r'\{([a-z]+)\}',val))==Counter(item['fields']),(lang,key)
        assert not any(unicodedata.category(ch) in ('Cc','Cf','Cs') for ch in val),(lang,key)
        assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]',val),(lang,key)
        assert len(val.encode('utf8'))<=(1024 if key in help_keys else 256),(lang,key)
        for token in re.findall(r'(?<![\w])/[a-z]+',key): assert token in val,(lang,key,token)
        if root.name.startswith('onekeyirc'):
            for token in ('Enter','Escape','PgUp/PgDn','Home','End','Up/Down'):
                if token in key: assert token in val,(lang,key,token)
print('r45 source-linked catalogs and field integrity PASS',root.name,len(keys))
