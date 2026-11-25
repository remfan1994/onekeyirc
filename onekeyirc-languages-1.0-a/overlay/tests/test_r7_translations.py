#!/usr/bin/env python3
"""Source-aligned, byte-bounded check of r7 user help and immutable tokens."""
import json,sys,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from audit_help_coverage import collect
from generate_locale_r7_guides import render
LANGS=('id','ms','tr','es','pt','fr','de','ru','it','ar')
rows=collect(ROOT)
assert len(rows)==477
selections=[('command','list',7),('command','ls',2),('command','manual',1),
            ('command','servers',2),('command','unignore',1),('command','set',18),
            ('topic','aliases',8)]
required={
    'list':('IRC','MASK','LIST','--sort','--limit','/list','--asc/--desc','/list show','/list clear','/ls','/cl'),
    'ls':('IRC','LIST','/server N','/channel N','/pm NICK'),
    'manual':('/help TOPIC',),
    'servers':('1..8',),
    'unignore':('/ignore remove MASK',),
    'aliases':(''),
    'set':('direct','review','output_format compact','[HH:MM:SS]','<nick>',
           'output_format full','queue_notices off','program_messages off',
           'persistent_logging on','0600','0700','16 MiB','off/on',
           'single_session on','--resume','default_interface','/save input',
           'command_suggestions','composer_mode line','fullscreen','part_message',
           'quit_message','/part','/quit','background_away','background_away_message',
           '/background','autoaway_minutes','/away','Common','Contextual',
           '/save output','/panel'),
}
master=json.loads((ROOT/'i18n/en.json').read_text('utf-8'))
assert len(master)>=664 and len(set(master))==len(master)
for lang in LANGS:
    d=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    assert set(master)==set(d),(lang,'keys')
    for kind,name,num in selections:
        lines=[s for k,n,s in rows if k==kind and n==name]
        assert len(lines)==num,(kind,name,len(lines))
        for en in lines:
            is_literal=kind=='topic' and name=='aliases' and en.startswith('/')
            if is_literal:
                local = d.get(en,en)
                assert re.findall(r'/[a-z]+', local)==re.findall(r'/[a-z]+', en),(lang,name,'alias mutated')
                if ' and ' in en:
                    assert local != en, (lang,name,'untranslated conjunction')
                else:
                    assert local == en, (lang,name,'literal mapping changed')
                continue
            assert en in d and d[en]!=en,(lang,name,en)
            tr=d[en]
            assert 0<len(tr.encode('utf-8'))<=256,(lang,name,en)
            if en.startswith('--sort selects channel, users or topic;'):
                assert all(x in tr for x in ('channel','users','topic')),(lang,tr)
            # Only require tokens in the particular English line; this
            # preserves security constraints and exact command syntax.
            for token in required[name]:
                if token and re.search(r'(?<![A-Za-z])'+re.escape(token)+r'(?![A-Za-z])',en):
                    assert token in tr,(lang,name,token,tr)
    for which in ('SETTINGS','CHANNELS'):
        assert (ROOT/'doc'/f'{which}-{lang}.txt').read_bytes()==render(lang,which)
print('r7: 32 translated help sentences, 10 locales, technical syntax and guides PASS')
