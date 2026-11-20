#!/usr/bin/env python3
"""Generate short, source-aligned draft operations references in ten languages.

These are extracts, NOT complete translated security manuals.
"""
import argparse
import json
from pathlib import Path
from audit_help_coverage import collect

ROOT = Path(__file__).resolve().parents[1]
LANGS = ('id', 'ms', 'tr', 'es', 'pt', 'fr', 'de', 'ru', 'it', 'ar')
LOG_CLEAR = ('/clear clears memory only. Existing log files survive quit, --close-session '
             'and configuration saves. Turning logging off stops new records without deleting old ones.')


def sections():
    specs = json.loads((ROOT/'tools/localized-operations-lines.json').read_text('utf8'))['groups']
    source = {}
    for kind, name, line in collect(ROOT):
        source.setdefault((kind,name), set()).add(line)
    output = []
    for spec in specs:
        kind, name, lines = spec['kind'], spec['name'], spec['lines']
        if kind != 'topic' or not name or not isinstance(lines,list) or not lines:
            raise ValueError('invalid operation help selection')
        if len(lines)!=len(set(lines)) or any(s not in source.get((kind,name),set()) for s in lines):
            raise ValueError('changed source help: '+name)
        output.append((name, lines))
    if sum(len(lines) for _,lines in output)!=22 or len(output)!=4:
        raise ValueError('operations selection changed: review the lines and tests')
    if LOG_CLEAR not in source.get(('topic','logging'),set()):
        raise ValueError('logging /clear source line changed')
    output.append(('logging', [LOG_CLEAR]))
    return output


def render(lang):
    translations=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf8'))
    lines=[f'vipirc / onekeyirc - selected operations help ({lang})',
           '====================================================', '',
           'DRAFT - selected help excerpts only; not a complete security manual.',
           'Commands, numeric limits and file names must be entered literally.',
           'Authoritative reference: English MANUAL.txt and SECURITY.md.', '']
    for name, sources in sections():
        lines.extend(('/help '+name,'-'*(6+len(name))))
        for original in sources:
            value=translations.get(original)
            if not value or value==original:
                raise ValueError(f'{lang}: untranslated help for /help {name}: {original}')
            lines.extend((value,''))
    return ('\n'.join(lines).rstrip()+'\n').encode('utf8')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args=parser.parse_args()
    for lang in LANGS:
        path=ROOT/'doc'/f'OPERATIONS-{lang}.txt'
        generated=render(lang)
        if args.check:
            if not path.is_file() or path.read_bytes()!=generated:
                raise SystemExit(str(path)+': guide out of date')
        else:path.write_bytes(generated)
    print('Selected operations help: 23 source lines x 10 draft language guides PASS')

if __name__=='__main__':main()
