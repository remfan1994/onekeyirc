#!/usr/bin/env python3
"""Export all fixed command/topic help with original syntax and examples.

This is a localized table reference, not a replacement for the full manuals:
frontend-specific generated additions are intentionally outside this export.
"""
import argparse
import json
from pathlib import Path
from audit_help_coverage import LANGS, literal_alias, records

ROOT = Path(__file__).resolve().parents[1]


def render(lang, root=ROOT):
    catalog = json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))
    notes = json.loads((root/'tools/help_reference_notes.json').read_text('utf-8'))
    source = (root/'src/idle_commands.c').read_text('utf-8')
    start = 'static const Command catalog[] = {'
    commands = dict(records(source, start, '\n};', 3))
    syntax = dict(records(source, start, '\n};', 1))
    descriptions = dict(records(source, start, '\n};', 2))
    examples = dict(records(source, start, '\n};', 4))
    topics = dict(records(source, 'static const Topic topics[] = {', '\n};', 1))
    output = ['vipirc / onekeyirc - '+catalog['Help'], '='*50, '',
              notes[lang]['reference'], '', 'r9 / English source r1', '']
    for group, entries in (('topic', topics), ('command', commands)):
        for name, text in entries.items():
            # /help /ls disambiguates the local-list command from the index.
            heading = '/help '+('/' if group == 'command' else '')+name
            output.extend([heading, '-'*len(heading), ''])
            if group == 'command':
                output.extend([catalog.get(descriptions[name], descriptions[name]),
                               catalog['Usage:']+' '+syntax[name], ''])
            for line in text.splitlines():
                if not line.strip():
                    output.append('')
                    continue
                translated = catalog.get(line)
                if not translated or (translated == line and not literal_alias(group, name, line)):
                    raise ValueError(f'{lang}: untranslated {group}/{name}: {line}')
                output.extend([translated, ''])
            if group == 'command':
                output.extend([catalog['Examples:'], examples[name], ''])
    return ('\n'.join(output).rstrip()+'\n').encode('utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for lang in LANGS:
        path = ROOT/'doc'/f'HELP-REFERENCE-{lang}.txt'
        content = render(lang)
        if args.check:
            if not path.exists() or path.read_bytes() != content:
                raise SystemExit('stale localized help reference: '+str(path))
        else:
            path.write_bytes(content)
    print('Fixed-table help references: 10 locales PASS')


if __name__ == '__main__':
    main()
