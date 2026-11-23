#!/usr/bin/env python3
"""Generate translated keyboard, automatic-command and DCC help extracts."""
import argparse
import json
from pathlib import Path
from audit_help_coverage import collect
from generate_locale_practical_guides import INTRO, LANGS
ROOT=Path(__file__).resolve().parents[1]
TITLES={"ar": "\u0627\u062e\u062a\u0635\u0627\u0631\u0627\u062a \u0627\u0644\u0645\u0641\u0627\u062a\u064a\u062d \u0648\u0627\u0644\u0623\u0648\u0627\u0645\u0631 \u0627\u0644\u0622\u0644\u064a\u0629 \u0648\u0646\u0642\u0644 DCC", "de": "Tastenk\u00fcrzel, automatische Befehle und DCC-\u00dcbertragungen", "es": "Atajos, comandos autom\u00e1ticos y transferencias DCC", "fr": "Raccourcis, commandes automatiques et transferts DCC", "id": "Pintasan, perintah otomatis, dan transfer DCC", "it": "Scorciatoie, comandi automatici e trasferimenti DCC", "ms": "Pintasan, arahan automatik dan pemindahan DCC", "pt": "Atalhos, comandos autom\u00e1ticos e transfer\u00eancias DCC", "ru": "\u0413\u043e\u0440\u044f\u0447\u0438\u0435 \u043a\u043b\u0430\u0432\u0438\u0448\u0438, \u0430\u0432\u0442\u043e\u043a\u043e\u043c\u0430\u043d\u0434\u044b \u0438 \u043f\u0435\u0440\u0435\u0434\u0430\u0447\u0438 DCC", "tr": "K\u0131sayollar, otomatik komutlar ve DCC aktar\u0131mlar\u0131"}
SECTIONS=(('command','autocommands'),('command','bind'),('command','bindings'),
          ('command','unbind'),('topic','hotkeys'),('command','dcc'),('command','transfers'))

def render(lang):
    lang_data=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    title, disclaimer, security=INTRO[lang]
    out=[title+' - '+TITLES[lang], '='*54,'',disclaimer,security,'']
    rows=collect(ROOT)
    for kind,name in SECTIONS:
        source=[s for k,n,s in rows if (k,n)==(kind,name)]
        if not source: raise ValueError(name)
        heading='/help '+name
        out.extend([heading,'-'*len(heading),''])
        for english in source:
            translated=lang_data.get(english)
            if not translated or translated==english:
                raise ValueError(f'{lang} incomplete {kind}/{name}: {english}')
            out.extend([translated,''])
    return ('\n'.join(out).rstrip()+'\n').encode('utf-8')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--check',action='store_true')
    args=parser.parse_args()
    for lang in LANGS:
        path=ROOT/'doc'/f'WORKFLOWS-{lang}.txt'
        output=render(lang)
        if args.check:
            if not path.exists() or path.read_bytes()!=output:
                raise SystemExit('stale '+str(path))
        else: path.write_bytes(output)
    print('r5 workflow guides: 7 complete help sections x 10 languages PASS')

if __name__=='__main__':main()
