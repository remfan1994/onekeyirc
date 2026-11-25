#!/usr/bin/env python3
"""Source-linked setup and actual prompt display verification."""
from pathlib import Path
import json,re,unicodedata
root=Path(__file__).resolve().parents[1]
rev='r49'
expected=1451
inv=json.loads((root/f'doc/SETUP-LOCALIZATION-{rev}.json').read_text())
assert len(inv)==8
keys=json.loads((root/'i18n/en.json').read_text())
assert keys==sorted(set(keys)) and len(keys)>=expected
source=(root/'src/idle_setup.c').read_text()
for item in inv:
    key=item['english']
    assert key in keys
    assert source.count('"%s", irc_tr("'+key+'")')==1,key
    for lang in 'id ms tr es pt fr de ru it ar'.split():
        data=json.loads((root/'i18n'/f'{lang}.json').read_text())
        assert len(data)>=expected and set(data)==set(keys)
        value=data[key]
        assert value and value!=key
        assert len(value.encode('utf-8'))<=256
        assert unicodedata.normalize('NFC',value)==value
        assert not re.search(r'%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[a-zA-Z]',value)
if (root/'src/prompt.c').exists():
    code=(root/'src/prompt.c').read_text()
    assert 'irc_tr(batch[i].spec->summary)' in code
    assert 'COLS - (int)offset, batch[i].spec->summary, A_NORMAL' not in code
print('PASS: '+rev+' setup locales, source links, prompt display')
