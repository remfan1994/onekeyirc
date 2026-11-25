#!/usr/bin/env python3
"""r41: actual source links, translation catalogs and protected tokens."""
from pathlib import Path
from collections import Counter
import json,re,string,unicodedata
root=Path(__file__).resolve().parents[1]
app='vipirc' if (root/'src/ui.c').is_file() else 'onekeyirc'
rows=json.loads((root/'doc/UI-LOCALIZATION-r41.json').read_text(encoding='utf8'))
assert len(rows)==(29 if app=='vipirc' else 9),(app,len(rows))
keys=json.loads((root/'i18n/en.json').read_text(encoding='utf8'))
assert len(keys)>=(1201 if app=='vipirc' else 948),(app,len(keys))
assert keys==sorted(set(keys))
help_keys=set(json.loads((root/'tools/context_help_keys.json').read_text(encoding='utf8')))
for row in rows:
    source=(root/row['source']).read_text(encoding='utf8')
    k=row['english']
    assert k in keys and k in source,(app,'missing source key',k)
    assert Counter(row['fields'])==Counter(field for _,field,_,_ in string.Formatter().parse(k) if field)
    if row['kind']=='named-fields': assert 'irc_display_render(' in source
    else: assert 'irc_tr('+json.dumps(k,ensure_ascii=True)+')' in source,(app,k)
for language in 'id ms tr es pt fr de ru it ar'.split():
    catalog=json.loads((root/'i18n'/f'{language}.json').read_text(encoding='utf8'))
    assert set(catalog)==set(keys)
    for row in rows:
        k=row['english']; translated=catalog[k]
        assert translated and translated!=k,(app,language,k)
        assert unicodedata.normalize('NFC',translated)==translated,(language,k)
        assert all(unicodedata.category(ch) not in ('Cc','Cf','Cs') for ch in translated),(language,k)
        assert not re.search(r'%([0-9]*\$)?[-+0 #]*\d*[.\d]*[a-zA-Z]',translated),(language,k)
        assert Counter(row['fields'])==Counter(field for _,field,_,_ in string.Formatter().parse(translated) if field),(language,k)
        assert len(translated.encode('utf8'))<=(1024 if k in help_keys else 256),(language,k)
        for cmd in re.findall(r'(?<![\w])/[A-Za-z][\w-]*',k):
            assert cmd in translated,(language,k,'lost IRC command',cmd)
        for literal in ['#channel','nick-or-channel','[original|vim]','nick!user@host']:
            if literal in k: assert literal in translated,(language,k,'lost command literal',literal)
        if k.startswith('Usage: /'):
            for literal in ('[key]','[message]','[on|off|toggle]','[#channel]'):
                if literal in k: assert literal in translated,(language,k,literal)
if app=='vipirc':
    src=(root/'src/ui.c').read_text(encoding='utf8')
    assert 'irc_display_render(shown, sizeof shown,' in src and 'File offer from {nick}. Open Transfers or /dcc to review.' in src
    assert 'strcmp(line, "COMMAND PROMPT")' in src
    assert 'snprintf(term, sizeof term, "COMMAND PROMPT")' in src
    assert 'app_status(a, "%s", irc_tr("Usage: /set autoconnect on|off"))' in src
else:
    src=(root/'src/idle.c').read_text(encoding='utf8')
    assert 'irc_tr("Command screen")' in src
    assert '"%s\\n\\n%s\\n%s\\n%s\\n%s\\n%s\\n%s\\n%s\\n%s\\n"' in src
print('r41 catalog, placeholder, source-link and protected-token checks PASS',app,len(rows))
