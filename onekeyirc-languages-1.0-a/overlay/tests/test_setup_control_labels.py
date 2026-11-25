#!/usr/bin/env python3
"""Keep setup and contextual help synchronized with actual localized buttons."""
import json, pathlib, re
root=pathlib.Path(__file__).resolve().parents[1]
controls=json.loads((root/'tests/setup-control-labels.json').read_text())
for lang in 'id ms tr es pt fr de ru it ar'.split():
    d=json.loads((root/'i18n'/f'{lang}.json').read_text())
    for key, labels in controls.items():
        for label in labels:
            assert ('«'+d[label]+'»') in d[key], (lang,key,label)
            assert not re.search(r'(?<![A-Za-z])'+label+r'(?![A-Za-z])',d[key]), (lang,key,label)
    print(lang, 'localized setup/control references: PASS')
