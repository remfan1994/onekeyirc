#!/usr/bin/env python3
"""Source-aligned quality checks for the 15 r8 command explanations."""
import json,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from audit_help_coverage import collect
from generate_locale_r8_guides import render,LANGS
NAMES=('clear','ignore','join','part','topic','quit')
expected={'clear':3,'ignore':3,'join':3,'part':3,'topic':2,'quit':2}
keys=json.loads((ROOT/'i18n/en.json').read_text('utf8'))
assert len(keys)>=679 and len(set(keys))==len(keys)
for lang in LANGS:
    data=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf8'))
    assert set(data)==set(keys),(lang,'catalog keys')
    for name in NAMES:
        sentences=[s for kind,n,s in collect(ROOT) if kind=='command' and n==name]
        assert len(sentences)==expected[name]
        for original in sentences:
            if original not in keys:raise AssertionError((lang,name,'missing help',original))
            translated=data[original]
            assert translated and len(translated.encode('utf8'))<=256
            assert translated!=original,(lang,name,'source-equal')
            tokens=['nick!user@host','ASCII','PART','TEXT','MESSAGE','IRC QUIT',
                    'quit_message','part_message','/save','/autojoin','/close',
                    'Up','Page Up','Home','Down','Page Down','End','Escape']
            for token in tokens:
                if token in original and token not in translated:
                    raise AssertionError((lang,name,'technical token changed',token,translated))
    assert (ROOT/'doc'/f'CHANNEL-COMMANDS-{lang}.txt').read_bytes()==render(lang)
print('r8: 15 new complete help lines x 10 locales, command syntax and guides PASS')
