#!/usr/bin/env python3
"""Generate ten concise localized practical guides from the exact runtime help.

Only sections whose every help explanation has a translation are included.
A guide is not a replacement for MANUAL.txt, SECURITY.md or source comments.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from audit_help_coverage import collect

ROOT = Path(__file__).resolve().parents[1]
LANGS = ('id', 'ms', 'tr', 'es', 'pt', 'fr', 'de', 'ru', 'it', 'ar')
SECTIONS = (
    ('topic', 'startup'), ('topic', 'messages'), ('topic', 'limits'),
    ('command', 'quickstart'), ('command', 'save'),
    ('topic', 'documentation'), ('topic', 'security'),
    ('topic', 'sessions'), ('topic', 'troubleshooting'), ('topic', 'bouncers'),
)
# The technical cross-references and section identifiers remain literal.
INTRO = {
    'id': ('Panduan praktis vipirc / onekeyirc',
           'DRAF TERJEMAHAN. Perintah dan opsi harus diketik persis seperti tercetak.',
           'Untuk rincian keamanan resmi, baca MANUAL.txt dan SECURITY.md berbahasa Inggris.'),
    'ms': ('Panduan praktikal vipirc / onekeyirc',
           'DRAF TERJEMAHAN. Taip arahan dan pilihan tepat seperti yang ditunjukkan.',
           'Untuk syarat keselamatan penuh, rujuk MANUAL.txt dan SECURITY.md dalam bahasa Inggeris.'),
    'tr': ('vipirc / onekeyirc pratik kilavuzu',
           'TASLAK CEVIRI. Komutlari ve secenekleri gosterildigi gibi aynen yazin.',
           'Kesin guvenlik sinirlari icin Ingilizce MANUAL.txt ve SECURITY.md dosyalarini okuyun.'),
    'es': ('Guía práctica de vipirc / onekeyirc',
           'TRADUCCIÓN PRELIMINAR. Escriba comandos y opciones exactamente como aparecen.',
           'Para los detalles de seguridad, consulte MANUAL.txt y SECURITY.md en inglés.'),
    'pt': ('Guia prático de vipirc / onekeyirc',
           'TRADUÇÃO PRELIMINAR. Escreva os comandos e opções exatamente como aparecem.',
           'Para os detalhes de segurança, consulte MANUAL.txt e SECURITY.md em inglês.'),
    'fr': ('Guide pratique vipirc / onekeyirc',
           'TRADUCTION PROVISOIRE. Tapez les commandes et options exactement comme indiqué.',
           'Pour la sécurité, consultez MANUAL.txt et SECURITY.md en anglais.'),
    'de': ('Praxisleitfaden vipirc / onekeyirc',
           'ENTWURFSÜBERSETZUNG. Befehle und Optionen genauso wie gezeigt eingeben.',
           'Für Sicherheitsdetails die englischen Dateien MANUAL.txt und SECURITY.md lesen.'),
    'ru': ('Практическое руководство vipirc / onekeyirc',
           'ЧЕРНОВОЙ ПЕРЕВОД. Пишите команды и параметры точно как показано.',
           'Точные правила безопасности - в английских MANUAL.txt и SECURITY.md.'),
    'it': ('Guida pratica a vipirc / onekeyirc',
           'TRADUZIONE PRELIMINARE. Digita comandi e opzioni esattamente come mostrati.',
           'Per i dettagli di sicurezza, consulta MANUAL.txt e SECURITY.md in inglese.'),
    'ar': ('دليل عملي لـ vipirc / onekeyirc',
           'ترجمة أولية. اكتب الأوامر والخيارات كما هي تماما.',
           'للتفاصيل الأمنية الكاملة راجع MANUAL.txt وSECURITY.md بالإنجليزية.'),
}

def section_texts():
    src = collect(ROOT)
    return [((kind, name), [s for k,n,s in src if (k,n)==(kind,name)])
            for kind,name in SECTIONS]

def render(lang):
    catalog = json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    title, disclaimer, reference = INTRO[lang]
    output = [title, '='*len(title), '', disclaimer, reference, '']
    for (kind, name), lines in section_texts():
        if not lines:
            raise ValueError(f'no source help for {kind}/{name}')
        heading=f'/help {name}'
        output.extend([heading, '-'*len(heading), ''])
        for source in lines:
            value=catalog.get(source)
            if not value or value==source:
                raise ValueError(f'{lang}: missing translation for {kind}/{name}: {source}')
            output += [value, '']
    return ('\n'.join(output).rstrip()+'\n').encode('utf-8')

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--check',action='store_true')
    a=p.parse_args()
    for lang in LANGS:
        path=ROOT/'doc'/f'PRACTICAL-GUIDE-{lang}.txt'
        result=render(lang)
        if a.check:
            if not path.is_file() or path.read_bytes()!=result:
                raise SystemExit(f'outdated localized guide {path}')
        else:
            path.write_bytes(result)
    count=sum(len(group) for _,group in section_texts())
    print(f'Practical guides: {count} source help lines x 10 draft languages PASS')

if __name__=='__main__':
    main()
