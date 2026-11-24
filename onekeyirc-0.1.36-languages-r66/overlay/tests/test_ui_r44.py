#!/usr/bin/env python3
"""r44 source-linked catalog and variable placeholder checks."""
from pathlib import Path
from collections import Counter
import json,re,unicodedata
root=Path(__file__).resolve().parents[1]
keys=json.loads((root/'i18n/en.json').read_text('utf8'))
inv=json.loads((root/'doc/UI-LOCALIZATION-r44.json').read_text('utf8'))
assert len(inv)==11
assert keys==sorted(set(keys)) and len(keys)>=977
langs='id ms tr es pt fr de ru it ar'.split()
help_keys=set(json.loads((root/'tools/context_help_keys.json').read_text('utf8')))
for item in inv:
    key=item['english']
    source=(root/item['source']).read_text('utf8')
    quoted=json.dumps(key,ensure_ascii=False)
    assert key in keys and quoted in source,(item['source'],key)
    assert Counter(item['fields'])==Counter(re.findall(r'\{([a-z]+)\}',key))
    if item['kind']=='named-fields':assert 'irc_display_render' in source
    else:assert 'irc_tr('+quoted+')' in source or (item['source']=='src/ui.c' and 'irc_tr(help[selected])' in source)
for lang in langs:
    values=json.loads((root/'i18n'/f'{lang}.json').read_text('utf8'))
    assert set(values)==set(keys)
    for item in inv:
        key=item['english'];val=values[key]
        assert val and unicodedata.normalize('NFC',val)==val
        assert Counter(re.findall(r'\{([a-z]+)\}',val))==Counter(item['fields']),(lang,key)
        assert not any(unicodedata.category(ch) in {'Cc','Cf','Cs'} for ch in val),(lang,key)
        assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]',val),(lang,key)
        assert len(val.encode('utf8'))<=(1024 if key in help_keys else 256),(lang,key)
        for token in re.findall(r'(?<![\w])/[a-z]+',key):
            assert token in val,(lang,token,key)
        if root.name.startswith('onekeyirc'):
            for token in ('Enter','Escape','Page Up','Page Down','Tab','Up/Down','PgUp/PgDn','Home/End'):
                if token in key:assert token in val,(lang,token,key)
print('r44 source-linked, 10-locale complete catalog PASS',root.name,len(keys))
