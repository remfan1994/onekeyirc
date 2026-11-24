#!/usr/bin/env python3
"""r37 source-bound display and format safety checks; standalone in release tree."""
from pathlib import Path
import json,unicodedata,re,string
root=Path(__file__).resolve().parents[1]
rows=json.loads((root/'doc/UI-LOCALIZATION-r37.json').read_text('utf8'))
vip=root.name.startswith('vipirc')
assert len(rows)==(42 if vip else 2)
expected=1048 if vip else 915
langs=('id','ms','tr','es','pt','fr','de','ru','it','ar')
master=json.loads((root/'i18n/en.json').read_text('utf8'))
assert len(master)>=expected and len(master)==len(set(master))
cache={}
for row in rows:
    key=row['english'];kind=row['kind'];path=row['source']
    source=cache.setdefault(path,(root/path).read_text('utf8'))
    assert key in master and key in source,(path,key)
    literal='irc_tr('+json.dumps(key,ensure_ascii=False)+')'
    if kind in ('fixed-status','context-help'):
        assert literal in source,(path,key)
        assert 'app_status' in source or kind=='context-help'
    elif kind=='named-fields':
        assert 'irc_display_render(' in source,(path,key)
        assert 'const IrcDisplayField values[]' in source
        assert set(row['fields']) and len(set(row['fields']))==len(row['fields'])
    else:raise ValueError(kind)
    if kind=='context-help':
        keys=json.loads((root/'tools/context_help_keys.json').read_text('utf8'))
        assert key in keys
    if key.startswith('Automatic command '):
        assert 'app_log(a, 0, "Automatic command %zu was not sent:' in source
# No source changes outside display statuses are tested by this script.
for lang in langs:
    cat=json.loads((root/'i18n'/f'{lang}.json').read_text('utf8'))
    assert set(cat)==set(master)
    for row in rows:
        key=row['english'];val=cat[key];kind=row['kind']
        assert val and val!=key and val==unicodedata.normalize('NFC',val),(lang,key)
        assert not any(unicodedata.category(ch) in ('Cc','Cf','Cs') for ch in val),(lang,key)
        assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]',val),(lang,key)
        assert len(val.encode('utf8')) <= (1024 if kind=='context-help' else 256),(lang,key)
        fields=lambda s:sorted(x for _,x,_,_ in string.Formatter().parse(s) if x)
        assert fields(key)==fields(val),(lang,key)
        for token in ('/help','/connect','LIST','TLS','QUIT','IRC','UTF-8','F7','Ctrl-K','Alt-x','PageUp',
                      'F1..F12','Ctrl-A..Z','Alt-a..z/0..9','Esc','hjkl'):
            if token in key:assert token in val,(lang,key,token)
if not vip:
    assert '#include "display_l10n.h"' in cache['src/idle.c']
print('r37',root.name,len(rows),'source-linked entries across 10 locales: PASS')
