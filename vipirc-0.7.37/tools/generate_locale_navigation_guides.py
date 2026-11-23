#!/usr/bin/env python3
"""Build offline navigation help guides from the live command-help catalog.

Source text in src/idle_commands.c stays authoritative. Each guide mirrors the
same translations the program displays; syntax tokens remain in English.
"""
import argparse
import json
from pathlib import Path
from audit_help_coverage import collect

ROOT = Path(__file__).resolve().parents[1]
LANGS = ('id','ms','tr','es','pt','fr','de','ru','it','ar')
COMMANDS = ('channel','channels','cl','close','disconnect','help','me',
            'mentions','msg','next','nick','pm','prev','ql','queries',
            'settings','sl','unread','view','views')


def sections():
    grouped = {name: [] for name in COMMANDS}
    for kind, name, line in collect():
        if kind == 'command' and name in grouped:
            grouped[name].append(line)
    count = sum(len(lines) for lines in grouped.values())
    if count != 40 or any(not lines for lines in grouped.values()):
        raise ValueError('navigation guide source changed: review the selected commands')
    return grouped


def render(lang):
    data = json.loads((ROOT/'i18n'/f'{lang}.json').read_text(encoding='utf-8'))
    output = [f'vipirc / onekeyirc - navigation help ({lang})',
              '==========================================', '',
              'IRC commands and uppercase arguments are entered literally.',
              'For full English security and protocol details see MANUAL.txt.', '']
    for name, lines in sections().items():
        output.extend((f'/{name}', '-'*(1+len(name))))
        for line in lines:
            if line not in data or not data[line]:
                raise ValueError(f'{lang}: missing navigation help: {line}')
            output.append(data[line])
        output.append('')
    return ('\n'.join(output).rstrip()+'\n').encode('utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for lang in LANGS:
        target=ROOT/'doc'/f'NAVIGATION-{lang}.txt'
        data=render(lang)
        if args.check:
            if not target.is_file() or target.read_bytes()!=data:
                raise SystemExit('stale translated navigation guide: '+str(target))
        else:
            target.write_bytes(data)
    print('navigation help guides: 40 lines, 10 languages PASS')

if __name__=='__main__':
    main()
