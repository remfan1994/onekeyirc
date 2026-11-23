#!/usr/bin/env python3
"""Check setup error lookup source links and locale safety."""
from pathlib import Path
import json,re,unicodedata
root=Path(__file__).resolve().parents[1]
rev='r50'
inv=json.loads((root/f'doc/SETUP-VALIDATION-{rev}.json').read_text())
assert len(inv)==28
keys=json.loads((root/'i18n/en.json').read_text())
assert keys==sorted(set(keys)) and len(keys)>=1479
code=(root/'src/idle_setup.c').read_text()
for row in inv:
    key=row['english']
    assert key in keys
    literal=json.dumps(key,ensure_ascii=False)
    assert code.count('"%s", irc_tr('+literal+')')==row['source_occurrences'],key
    assert code.count('fail(')>=28
    assert not re.search(r'fail\([^\n]*?cap\s*,\s*irc_tr\(',code)
for lang in 'id ms tr es pt fr de ru it ar'.split():
    data=json.loads((root/'i18n'/f'{lang}.json').read_text())
    assert set(data)==set(keys)
    for row in inv:
        k=row['english']; val=data[k]
        assert val and val!=k
        assert len(val.encode('utf-8'))<=256
        assert unicodedata.normalize('NFC',val)==val
        assert not any(unicodedata.category(c) in ('Cc','Cf','Cs') for c in val)
        assert not re.search(r'%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[a-zA-Z]',val)
print(rev+': 28 setup validation notices x ten locales / source links PASS')
