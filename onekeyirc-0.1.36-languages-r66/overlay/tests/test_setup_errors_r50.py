#!/usr/bin/env python3
from pathlib import Path
import json,unicodedata,re
r=Path(__file__).resolve().parents[1]
entries=json.loads((r/'doc/SETUP-ERRORS-r50.json').read_text())
keys=json.loads((r/'i18n/en.json').read_text())
assert len(entries)==34 and keys==sorted(set(keys)) and len(keys)>=1125
source=(r/'src/idle_setup.c').read_text()
for e in entries:
 k=e['english']; lit=json.dumps(k,ensure_ascii=False)
 if e['index']>=30: assert source.count('irc_tr('+lit+')')==1,k
 else: assert source.count('"%s", irc_tr('+lit+')')==1,k
 assert k in keys
assert not re.search(r'fail\([^\n]*?cap\s*,\s*irc_tr\(',source)
for lang in 'id ms tr es pt fr de ru it ar'.split():
 data=json.loads((r/'i18n'/f'{lang}.json').read_text())
 assert set(data)==set(keys)
 for e in entries:
  value=data[e['english']]
  assert value and len(value.encode())<=256
  assert unicodedata.normalize('NFC',value)==value
  assert not any(unicodedata.category(c) in ('Cc','Cf','Cs') for c in value)
  assert not re.search(r'%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[a-zA-Z]',value)
print('r50: 34 setup errors x 10 locales and source-linked display paths PASS')
