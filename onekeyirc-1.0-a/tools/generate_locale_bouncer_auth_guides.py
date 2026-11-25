#!/usr/bin/env python3
"""Generate draft authentication and bouncer reference sheets from live /help.

The English command/topic source is authoritative. Only listed, independently
translated whole lines appear; command/config tokens must be entered verbatim.
"""
import argparse
import json
from pathlib import Path
from audit_help_coverage import collect

ROOT = Path(__file__).resolve().parents[1]
LANGS = ('id','ms','tr','es','pt','fr','de','ru','it','ar')


def sections():
    selected = json.loads((ROOT/'tools/localized-auth-bouncer-lines.json').read_text('utf-8'))
    source = {}
    for kind,name,line in collect(ROOT):
        source.setdefault((kind,name),[]).append(line)
    result=[]
    for spec in selected['sections']:
        kind,name,indices = spec['kind'],spec['name'],spec['indices']
        available=source[kind,name]
        chosen=[available[i] for i in indices]
        result.append((kind,name,chosen))
    all_lines=[s for _,_,lines in result for s in lines]
    if len(all_lines)!=26 or list(dict.fromkeys(all_lines))!=selected['lines']:
        raise ValueError('source help changed; audit and review the translated lines')
    return result


def render(lang):
    translations=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    output=[f'vipirc / onekeyirc - authentication and bouncers ({lang})',
            '=====================================================', '',
            'DRAFT TRANSLATIONS - native-speaker review is outstanding.',
            'Commands, flags, values and config keys must be entered literally.',
            'The English SECURITY.md and MANUAL.txt remain authoritative.', '']
    for kind,name,lines in sections():
        output += [f'/help {name}', '-'*(6+len(name))]
        for text in lines:
            value=translations.get(text)
            if not value or value==text:
                raise ValueError(f'{lang}: untranslated /help {name}: {text}')
            output += [value, '']
    return ('\n'.join(output).rstrip()+'\n').encode('utf-8')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--check',action='store_true')
    args=parser.parse_args()
    for lang in LANGS:
        filename=ROOT/'doc'/f'AUTH-BOUNCERS-{lang}.txt'
        body=render(lang)
        if args.check:
            if not filename.is_file() or filename.read_bytes()!=body:
                raise SystemExit(f'{filename}: stale localized guide')
        else:
            filename.write_bytes(body)
    print('authentication/bouncer help guides: 26 lines x 10 locales PASS')

if __name__=='__main__':main()
