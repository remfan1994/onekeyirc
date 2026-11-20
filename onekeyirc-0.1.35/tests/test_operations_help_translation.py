#!/usr/bin/env python3
"""Check source alignment and technical literals in operations-help drafts.

Mechanical checks only: do not treat a pass as native-speaker proofreading.
"""
import json
import re
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import generate_locale_operations_guides as guides
import audit_help_coverage

EN=json.loads((ROOT/'i18n/en.json').read_text('utf8'))
assert len(EN)>=519
assert len(audit_help_coverage.collect(ROOT))==477
assert sum(line in EN for _,_,line in audit_help_coverage.collect(ROOT))>=170
sections=guides.sections()
assert len(sections)==5
lines=[line for _,group in sections for line in group]
assert len(lines)==23 and len(set(lines))==23
# Check selected protocol/CLI tokens, documentation references and placeholders.
TOKENS=('--background','--no-connect','--check-config','--reconnect',
        '--sessions','--resume','--close-session','/autoconnect','/raw',
        '/help','/mentions','/clear','/bindings','/bind','/unbind',
        'doc/OPENBSD-VPS.txt','SECURITY.md','16 MiB','[NUMBER]',
        'KEY ACTION','GPG','DNS','TLS','IRC','PEM','CA','VPS','RAM',
        'Up/Down','Compose','Command')
def occurrences(text, item):
    # Exact lexical boundaries: /bind and /bindings are different commands.
    if item.startswith('/') or item.startswith('--'):
        return len(re.findall(r'(?<![A-Za-z0-9_-])'+re.escape(item)+
                              r'(?![A-Za-z0-9_-])', text))
    return text.count(item)

def retained(source, translation):
    return all(occurrences(translation,token) >= occurrences(source,token)
               for token in TOKENS)

MUST_RETAIN={
    'id': ('menghentikan catatan baru','bukan menghapus yang lama'),
    'ms': ('menghentikan rekod baharu','bukan memadam rekod lama'),
    'tr': ('yeni kayıtları durdurur','eskileri silmez'),
    'es': ('detiene entradas nuevas','no borra las anteriores'),
    'pt': ('impede novas entradas','não elimina as anteriores'),
    'fr': ('arrête les nouvelles entrées','sans effacer les anciennes'),
    'de': ('stoppt neue Einträge','löscht aber keine alten'),
    'ru': ('останавливает новые записи','не удаляя старые'),
    'it': ('ferma le nuove voci','non cancella quelle vecchie'),
    'ar': ('يمنع السجلات الجديدة','ولا يحذف القديمة')
}
for lang in guides.LANGS:
    translations=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf8'))
    assert len(translations)>=519
    for source in lines:
        text=translations[source]
        assert text!=source and 0<len(text.encode('utf8'))<=256,(lang,source)
        assert not any(c in text for c in ('\n','\x1b','\r'))
        for item in TOKENS:
            assert occurrences(text,item)>=occurrences(source,item), (lang,item,source,text)
    clear=translations[guides.LOG_CLEAR]
    for fragment in MUST_RETAIN[lang]:
        assert fragment in clear,(lang,fragment,clear)
    assert guides.render(lang)==(ROOT/'doc'/f'OPERATIONS-{lang}.txt').read_bytes()

# Mutation check: removing a literal would fail the same rule.
example=next(line for line in lines if '--no-connect' in line)
for lang in guides.LANGS:
    text=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf8'))[example]
    assert retained(example,text)
    assert not retained(example,text.replace('--no-connect','',1))
print('Operations help: 22 new lines + corrected /clear x 10 locales; technical literals and source parity PASS')
