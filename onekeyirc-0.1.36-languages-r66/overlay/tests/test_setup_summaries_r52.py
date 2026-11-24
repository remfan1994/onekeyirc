from pathlib import Path
import json,re,unicodedata
p=Path(__file__).resolve().parents[1]
rev='r52'
data=json.loads((p/f'doc/SETUP-SUMMARIES-{rev}.json').read_text())
keys=json.loads((p/'i18n/en.json').read_text())
src=(p/'src/idle_setup.c').read_text()
assert len(data)==26 and len(keys)>=1166 and keys==sorted(set(keys))
assert '#include "display_l10n.h"' in src
assert src.count('irc_display_render(')>=3
for x in data:
 assert x['english'] in keys and json.dumps(x['english']) in src,x['english']
for lang in 'id ms tr es pt fr de ru it ar'.split():
 cat=json.loads((p/'i18n'/f'{lang}.json').read_text())
 assert set(cat)==set(keys)
 for x in data:
  k=x['english'];v=cat[k]; assert v and v!=k and len(v.encode())<=256,(lang,k)
  assert set(re.findall(r'\{[a-z]+\}',k))==set(re.findall(r'\{[a-z]+\}',v)),(lang,k)
  assert unicodedata.normalize('NFC',v)==v
  assert not any(unicodedata.category(c) in ('Cc','Cf','Cs') for c in v)
assert 'if (!irc_display_render(message, cap, pattern, fields, 1u))' in src
assert 'if (!irc_display_render(message, cap,' in src
print('PASS: '+rev+' guided setup summaries, ten locales, literal substitution source links')
