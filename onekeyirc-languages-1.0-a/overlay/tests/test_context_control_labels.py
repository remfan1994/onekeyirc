#!/usr/bin/env python3
"""Audit dynamic setup/menu lookup keys and contextual control references."""
import json, pathlib, re
root=pathlib.Path(__file__).resolve().parents[1]
keys=set(json.loads((root/'i18n/en.json').read_text()))
setup=(root/'src/idle_setup.c').read_text()
questions=re.search(r'questions\[\]\s*=\s*\{(.*?)\};',setup,re.S).group(1)
assert set(re.findall(r'"([^"]+)"',questions)) <= keys
assert 'irc_tr(commands_suggestion_mode_label(c->command_suggestions))' in setup
if (root/'src/ui.c').exists():
    ui=(root/'src/ui.c').read_text()
    for m in re.finditer(r'const\s+char\s*\*\s*(?:const\s*)?(?:choices|entries)\[\]\s*=\s*\{([^{}]+)\}',ui):
        labels=set(re.findall(r'"([^"]+)"',m.group(1)))
        if 'Whois' in labels:
            assert labels <= keys, labels-keys
            assert 'Unignore' in labels
controls=json.loads((root/'tests/context-control-labels.json').read_text())
for lang in 'id ms tr es pt fr de ru it ar'.split():
    d=json.loads((root/'i18n'/f'{lang}.json').read_text())
    for k, labels in controls.items():
        for label in labels:
            assert '«'+d[label]+'»' in d[k], (lang,k,label,d[k])
    assert d['Use signals']==d['Use signals:'].rstrip(':： ').strip()
    if 'Whois' in keys:
        assert all(d[k]!=k for k in ('Whois','Ignore','Unignore'))
    print(lang, 'dynamic labels and contextual references: PASS')
