#!/usr/bin/env python3
"""r39: source-linked, UTF-8, catalog, and dynamic-field regression checks."""
from pathlib import Path
import json, re, string, unicodedata
root=Path(__file__).resolve().parents[1]
program='vipirc' if (root.name.startswith('vipirc') or root.parent.name.startswith('vipirc')) else 'onekeyirc'
rows=json.loads((root/'doc/UI-LOCALIZATION-r39.json').read_text(encoding='utf8'))
assert len(rows)==(45 if program=='vipirc' else 1)
master=json.loads((root/'i18n/en.json').read_text(encoding='utf8'))
assert len(master)>=(1138 if program=='vipirc' else 927),len(master)
assert master==sorted(set(master))
for row in rows:
 source=(root/row['source']).read_text(encoding='utf8')
 key=row['english']
 assert key in master and key in source,(row['source'],key)
 assert row['fields']==[name for _,name,_,_ in string.Formatter().parse(key) if name]
 if row['kind']=='named-fields':
  assert 'irc_display_render(' in source and 'const IrcDisplayField fields[]' in source
 else:
  assert 'irc_tr('+json.dumps(key,ensure_ascii=False)+')' in source,(row['source'],key)
for lang in ('id','ms','tr','es','pt','fr','de','ru','it','ar'):
 cat=json.loads((root/'i18n'/f'{lang}.json').read_text(encoding='utf8'))
 assert set(cat)==set(master),(lang,'catalog mismatch')
 for row in rows:
  key=row['english']; value=cat[key]
  assert value and value!=key and unicodedata.normalize('NFC',value)==value,(lang,key)
  assert 0<len(value.encode('utf8'))<=256,(lang,key)
  assert not any(unicodedata.category(c) in ('Cc','Cf','Cs') for c in value),(lang,key)
  assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]',value),(lang,key)
  assert [n for _,n,_,_ in string.Formatter().parse(value) if n]==row['fields'],(lang,key)
print('r39 source links and 10 locale catalogs PASS:', program, len(rows))
