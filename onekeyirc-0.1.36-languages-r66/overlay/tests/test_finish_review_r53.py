from pathlib import Path
import json,unicodedata
root=Path(__file__).resolve().parents[1]
rev='r53'
expected=1210
records=json.loads((root/f'doc/FINISH-REVIEW-{rev}.json').read_text())
assert len(records)==44
keys=json.loads((root/'i18n/en.json').read_text())
assert len(keys)>=expected and keys==sorted(set(keys))
source=(root/'src/idle_setup.c').read_text()
for entry in records:
 key=entry['english']
 assert key in keys and json.dumps(key) in source, key
for lang in 'id ms tr es pt fr de ru it ar'.split():
 cat=json.loads((root/'i18n'/f'{lang}.json').read_text())
 assert set(cat)==set(keys)
 for entry in records:
  value=cat[entry['english']]
  assert value and len(value.encode('utf-8'))<=256
  assert unicodedata.normalize('NFC',value)==value
  assert not any(unicodedata.category(c) in ('Cc','Cf','Cs') for c in value)
  if entry['index']==40:assert '/autoauth status' in value
assert 'irc_tr("Encrypted credential created.")' not in source or 'Encrypted credential created.' in keys
assert 'irc_tr("1 Save | 2 Apply without saving")' in source
assert 'irc_tr("1 Save and connect | 2 Save only | 3 Connect once | 4 Apply without saving")' in source
assert '"%s %s %s"' in source
print('PASS: '+rev+' 44 source-linked Finish review entries across ten locales')
