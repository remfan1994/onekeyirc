#!/usr/bin/env python3
"""r40: source-linked translations, per-locale named fields and safety."""
from pathlib import Path
from collections import Counter
import json
import re
import string
import unicodedata
root=Path(__file__).resolve().parents[1]
app='vipirc' if (root.name.startswith('vipirc') or root.parent.name.startswith('vipirc')) else 'onekeyirc'
rows=json.loads((root/'doc/UI-LOCALIZATION-r40.json').read_text(encoding='utf8'))
assert len(rows)==(34 if app=='vipirc' else 12)
keys=json.loads((root/'i18n/en.json').read_text(encoding='utf8'))
assert len(keys)>=(1172 if app=='vipirc' else 939),(app,len(keys))
assert keys==sorted(set(keys))
help_keys=set()
path=root/'tools/context_help_keys.json'
if path.exists(): help_keys.update(json.loads(path.read_text(encoding='utf8')))
for r in rows:
    key=r['english']
    source=(root/r['source']).read_text(encoding='utf8')
    assert key in source and key in keys,(app,r['source'],key)
    assert Counter(r['fields'])==Counter(x for _,x,_,_ in string.Formatter().parse(key) if x)
    if r['kind']=='indexed-help':
        assert 'irc_tr(help[selected])' in source
    elif r['kind']=='named-fields':
        assert 'irc_display_render(' in source
    else:
        assert 'irc_tr('+json.dumps(key,ensure_ascii=True)+')' in source,(app,r['source'],key)
for lang in ('id','ms','tr','es','pt','fr','de','ru','it','ar'):
    cat=json.loads((root/'i18n'/f'{lang}.json').read_text(encoding='utf8'))
    assert set(cat)==set(keys),(app,lang,'key set')
    for r in rows:
        k=r['english'];t=cat[k]
        assert t and t != k,(app,lang,k)
        assert unicodedata.normalize('NFC',t)==t,(app,lang,k)
        assert not any(unicodedata.category(c) in ('Cc','Cf','Cs') for c in t),(app,lang,k)
        assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]',t),(app,lang,k)
        assert Counter(r['fields'])==Counter(x for _,x,_,_ in string.Formatter().parse(t) if x),(app,lang,k)
        maxbytes=1024 if k in help_keys else 256
        assert len(t.encode('utf8'))<=maxbytes,(app,lang,k,len(t.encode('utf8')))
        for command in re.findall(r'(?<![\w])/[A-Za-z][\w-]*',k):
            assert command in t,(app,lang,k,'missing command',command)
        for literal in ('nick!user@host','UTF-8','127','32'):
            if literal in k: assert literal in t,(app,lang,k,'missing literal',literal)
# Preserve the internal help marker, which is a data search key, not a label.
if app=='vipirc':
    ui=(root/'src/ui.c').read_text(encoding='utf8')
    assert 'strcmp(line, "COMMAND PROMPT")' in ui
    assert 'snprintf(term, sizeof term, "COMMAND PROMPT")' in ui
print('r40 source links, catalogs, field integrity and protected tokens PASS',app,len(rows))
