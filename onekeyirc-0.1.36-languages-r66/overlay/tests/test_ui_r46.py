#!/usr/bin/env python3
from pathlib import Path
import json,re,unicodedata
root=Path(__file__).resolve().parents[1]
inv=json.loads((root/'doc/UI-LOCALIZATION-r46.json').read_text())
keys=json.loads((root/'i18n/en.json').read_text())
assert keys==sorted(set(keys))
assert len(inv)==4 and len(keys)>=997
for item in inv:
    k=item['english']
    src=(root/item['source']).read_text()
    literal=json.dumps(k,ensure_ascii=True)
    assert k in keys and literal in src,k
    if item['source']=='src/transfers.c':
        assert 'irc_tr('+literal+')' in src,k
    else:
        assert 'display_text(c,' in src and 'emit(c, "%s", display_text(c,' in src,k
for lang in 'id ms tr es pt fr de ru it ar'.split():
    catalog=json.loads((root/'i18n'/f'{lang}.json').read_text())
    assert set(catalog)==set(keys)
    for item in inv:
        k=item['english'];val=catalog[k]
        assert val and unicodedata.normalize('NFC',val)==val
        assert not any(unicodedata.category(c) in ('Cc','Cf','Cs') for c in val)
        assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]',val)
        assert len(val.encode())<=256,(lang,k)
        for token in re.findall(r'(?<![\w])/[a-z]+',k):assert token in val,(lang,k,token)
        for token in ('UTF-8','DCC','IPv4','IRC','Accept','Type path','NAT','LIST'):
            if re.search(r'(?<![A-Za-z])'+re.escape(token)+r'(?![A-Za-z])',k): assert token in val,(lang,k,token)
print('r46 source links and locale integrity PASS',root.name,len(inv),len(keys))
