#!/usr/bin/env python3
"""Generate concise offline command guides from exact running-help translations."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LANGS = ('id','ms','tr','es','pt','fr','de','ru','it','ar')
SECTIONS = json.loads((ROOT/'tools/localized-command-lines.json').read_text('ascii'))


def render(lang):
    translations = json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    source = (ROOT/'src/idle_commands.c').read_text('utf-8')
    lines = ['vipirc / onekeyirc - command guide (' + lang + ')',
             '=======================================', '',
             'Commands, argument names and saved configuration keys remain literal.',
             'For complete reference and security details see MANUAL.txt and SECURITY.md.', '']
    for name, phrases in SECTIONS.items():
        lines.extend((name, '-'*len(name)))
        for english in phrases:
            if english not in source:
                raise ValueError('stale help source for '+name+': '+english)
            if english not in translations:
                raise ValueError('missing translation: '+lang+' '+english)
            lines.extend((translations[english], ''))
    return ('\n'.join(lines).rstrip()+'\n').encode('utf-8')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--check',action='store_true')
    args=parser.parse_args()
    for lang in LANGS:
        path=ROOT/'doc'/f'COMMANDS-{lang}.txt'
        expected=render(lang)
        if args.check:
            if not path.is_file() or path.read_bytes()!=expected:
                raise SystemExit('stale command guide: '+str(path))
        else: path.write_bytes(expected)
    print('Localized command guides: 10 languages PASS')


if __name__=='__main__':main()
