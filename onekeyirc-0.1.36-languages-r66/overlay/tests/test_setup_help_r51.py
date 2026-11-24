from pathlib import Path
import json,re,unicodedata
p=Path(__file__).resolve().parents[1]
data=json.loads((p/'doc/SETUP-HELP-r51.json').read_text())
source=(p/'src/idle_setup.c').read_text()
keys=json.loads((p/'i18n/en.json').read_text())
assert len(data)==27 and len(keys)>=1141 and keys==sorted(set(keys))
for e in data:
    if e['index'] in (2,3):
        assert source.count(json.dumps(e['english']))==1 and 'irc_tr(s->quick ?' in source
    else:
        assert source.count('irc_tr('+json.dumps(e['english'])+')')==1,e['english']
    assert e['english'] in keys
assert '"%s %s",' in source
assert 'irc_tr(s->quick ?' in source
for locale in 'id ms tr es pt fr de ru it ar'.split():
    trans=json.loads((p/'i18n'/f'{locale}.json').read_text())
    assert set(trans)==set(keys)
    for e in data:
        value=trans[e['english']]
        assert value and len(value.encode('utf8'))<=256 and unicodedata.normalize('NFC',value)==value
        assert not any(unicodedata.category(c) in ('Cc','Cf','Cs') for c in value)
        assert not re.search(r'%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[A-Za-z]',value)
print('PASS: r51 27 source-linked help strings in ten locales')
