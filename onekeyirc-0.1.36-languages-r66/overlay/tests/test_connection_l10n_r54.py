from pathlib import Path
import json,re,unicodedata
root=Path(__file__).resolve().parents[1]
REV='r54'
data=json.loads((root/f'doc/CONNECTION-L10N-{REV}.json').read_text())
keys=json.loads((root/'i18n/en.json').read_text())
src=(root/'src/app.c').read_text()
assert len(data['new_keys'])==29 and len(keys)==1330 and keys==sorted(set(keys))
assert src.count('irc_display_render(')>=5
assert 'disconnect_retry(a, m->nparams ? m->params[m->nparams - 1] : "Server ERROR", a->registered)' in src
assert 'app_outgoing(a, 0, "PING :' in src
assert 'irc_tr("Required SASL negotiation timed out")' in src
for key in data['new_keys']:
 assert key in keys and key in src,key
for lang in 'id ms tr es pt fr de ru it ar'.split():
 cat=json.loads((root/'i18n'/f'{lang}.json').read_text())
 assert set(cat)==set(keys)
 for key in data['new_keys']:
  v=cat[key]
  assert v and v!=key and len(v.encode('utf-8'))<=256,(lang,key)
  assert set(re.findall(r'\{[a-z]+\}',key))==set(re.findall(r'\{[a-z]+\}',v)),(lang,key)
  assert unicodedata.normalize('NFC',v)==v,(lang,key)
  assert not any(unicodedata.category(c) in {'Cc','Cf','Cs'} for c in v),(lang,key)
  assert not re.search(r'%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[a-zA-Z]',v),(lang,key)
print('PASS: connection display path and 29 keys x 10 locale catalogs')
