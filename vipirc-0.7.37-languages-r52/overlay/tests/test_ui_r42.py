#!/usr/bin/env python3
"""r42: source-linked entries, protected commands, placeholders and UTF-8 bounds."""
import json, re, string, unicodedata
from collections import Counter
from pathlib import Path
root=Path(__file__).resolve().parents[1]
app='vipirc' if 'vipirc-0.7.37' in str(root) else 'onekeyirc'
rows=json.loads((root/'doc/UI-LOCALIZATION-r42.json').read_text(encoding='utf8'))
keys=json.loads((root/'i18n/en.json').read_text(encoding='utf8'))
assert len(rows)==(20 if app=='vipirc' else 10),len(rows)
assert len(keys)>=(1221 if app=='vipirc' else 958),len(keys)
assert keys==sorted(set(keys))
help_keys=set(json.loads((root/'tools/context_help_keys.json').read_text(encoding='utf8')))
assert all(row['english'] in help_keys for row in rows)
for row in rows:
    source=(root/row['source']).read_text(encoding='utf8')
    key=row['english']
    assert key in keys
    assert key in source,(app,'unlinked English key',key)
    assert Counter(row['fields'])==Counter(x for x in re.findall(r'\{([a-z]+)\}',key))
    if row['kind']=='named-fields':
        assert 'irc_display_render(' in source
    else:
        assert 'irc_tr('+json.dumps(key,ensure_ascii=True)+')' in source,(app,key)
for lang in 'id ms tr es pt fr de ru it ar'.split():
    cat=json.loads((root/'i18n'/f'{lang}.json').read_text(encoding='utf8'))
    assert set(cat)==set(keys),(lang,'missing catalog keys')
    for row in rows:
        key=row['english']; val=cat[key]
        assert isinstance(val,str) and val and len(val.encode('utf8'))<=1024,(lang,key)
        assert unicodedata.normalize('NFC',val)==val,(lang,key)
        assert all(unicodedata.category(c) not in ('Cc','Cf','Cs') for c in val),(lang,key)
        assert Counter(re.findall(r'\{([a-z]+)\}',key))==Counter(re.findall(r'\{([a-z]+)\}',val)),(lang,key)
        assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]',val),(lang,key)
        for cmd in re.findall(r'(?<![\w])/[a-z]+',key):
            assert cmd in val,(lang,key,'command token lost',cmd)
        if 'nickname|query-number' in key:assert 'nickname|query-number' in val,(lang,key)
# Ensure translation was applied to whole away status in prior r41 (no partial suffix).
if app=='vipirc':
    source=(root/'src/app.c').read_text(encoding='utf8')
    assert 'app_status(a, "%s", *rest ? irc_tr("Setting away status; waiting for server confirmation.")' in source
    assert 'app_status(a, "%s; waiting for server confirmation."' not in source
else:
    source=(root/'src/idle.c').read_text(encoding='utf8')
    assert 'irc_tr("Watch log")' in source
    assert 'irc_display_render(heading, sizeof heading, "Catch-up from your most recent sent message | S{number} {target}"' in source
print('r42 source-link, locale, placeholder and protocol-token checks PASS',app,len(rows))
