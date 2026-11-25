#!/usr/bin/env python3
"""Verify locale catalogs and the actual command suggestion display path."""
from pathlib import Path
import json,re,unicodedata
root=Path(__file__).resolve().parents[1]
rev='r48'
inv=json.loads((root/f'doc/COMMAND-LOCALIZATION-{rev}.json').read_text())
keys=json.loads((root/'i18n/en.json').read_text())
assert len(inv)==51 and len({r['command'] for r in inv})==51
assert keys==sorted(set(keys)) and len(keys)>=1063
cmd=(root/'src/commands.c').read_text()
idle=(root/'src/idle.c').read_text()
assert 'entry->spec->name, irc_tr(entry->spec->description)' in idle
assert 'entry->spec->name, entry->spec->description' not in idle
for row in inv:
    assert row['english'] in keys and row['english'] in cmd,(row['command'],row['english'])
for lang in 'id ms tr es pt fr de ru it ar'.split():
    data=json.loads((root/'i18n'/f'{lang}.json').read_text())
    assert set(data)==set(keys)
    for row in inv:
        text=data[row['english']]
        assert text and text!=row['english'],(lang,row['command'])
        assert len(text.encode('utf-8'))<=256
        assert unicodedata.normalize('NFC',text)==text
        assert not any(unicodedata.category(ch) in ('Cc','Cf','Cs') for ch in text)
        assert not re.search(r'%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[a-zA-Z]',text)
        for token in ('/channels','/ql','/servers','MOTD','TEXT','ZNC','soju','Help'):
            if token in row['english']:assert token in text,(lang,row['command'],token)
        if row['command']=='background':
            assert 'onekeyirc'+' --resume' in text
print(rev+': source-linked command suggestions and ten-locale catalogs PASS')
