#!/usr/bin/env python3
"""Audit whole-line translations of command and topic help, without executing C.

The source array is the authority. This deliberately does not count UI labels,
examples, protocol literals, or CLI startup diagnostics as translated prose.
The report is a translator work queue, not a claim of native-speaker review.
"""
import argparse
import ast
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LANGS = ('id', 'ms', 'tr', 'es', 'pt', 'fr', 'de', 'ru', 'it', 'ar')
STRING = re.compile(r'"(?:\\.|[^"\\])*"', re.S)
ENTRY = re.compile(r'^\s*\{\{?"([a-z][a-z0-9_-]+)"', re.M)


def c_groups(chunk):
    """Join adjacent C string literals, respecting commas between fields."""
    matches = list(STRING.finditer(chunk))
    groups = []
    for index, match in enumerate(matches):
        value = ast.literal_eval(match.group())
        if index == 0 or ',' in chunk[matches[index - 1].end():match.start()]:
            groups.append(value)
        else:
            groups[-1] += value
    return groups


def records(source, start, end, offset):
    block = source.split(start, 1)[1].split(end, 1)[0]
    matches = list(ENTRY.finditer(block))
    if not matches:
        raise ValueError('no help entries found in ' + start)
    for i, match in enumerate(matches):
        next_start = matches[i + 1].start() if i + 1 < len(matches) else len(block)
        parts = c_groups(block[match.start():next_start])
        if len(parts) <= offset:
            raise ValueError('invalid command/topic C string structure: ' + match.group(1))
        if parts[0] != match.group(1):
            raise ValueError('entry name mismatch: ' + match.group(1))
        yield parts[0], parts[offset]


def collect(root=ROOT):
    src = (root/'src/idle_commands.c').read_text('utf-8')
    commands = dict(records(src, 'static const Command catalog[] = {', '\n};', 3))
    topics = dict(records(src, 'static const Topic topics[] = {', '\n};', 1))
    result = []
    for group, entries in (('command', commands), ('topic', topics)):
        for name, value in entries.items():
            for line in value.splitlines():
                if line.strip():
                    result.append((group, name, line))
    return result


def report(root=ROOT):
    rows = collect(root)
    keys = set(json.loads((root/'i18n/en.json').read_text('utf-8')))
    catalogs = {lang: json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8')) for lang in LANGS}
    lines = ['INTERACTIVE HELP LOCALIZATION COVERAGE',
             '======================================',
             '',
             'Exact whole-line translations in command/topic help only.',
             'Does not measure translation quality, screen layout, source comments, or manuals.',
             'Examples and IRC protocol commands stay literal and are excluded.',
             '']
    unique = list(dict.fromkeys((group, name, s) for group, name, s in rows))
    covered = sum(text in keys for _, _, text in unique)
    lines.extend([f'Help lines: {len(unique)}',
                  f'With catalog keys: {covered}',
                  f'Without catalog keys: {len(unique)-covered}',
                  '',
                  'Per-locale entries that differ from source English:',
                  '(Count is not a native-speaker quality rating.)'])
    for lang in LANGS:
        translated = sum(text in keys and catalogs[lang][text] != text
                         for _, _, text in unique)
        lines.append(f'  {lang}: {translated}/{len(unique)}')
    lines.append('')
    groups = sorted(set((group, name) for group, name, _ in unique))
    for group, name in groups:
        entries = [text for g, n, text in unique if (g, n) == (group, name)]
        coverage = sum(text in keys for text in entries)
        lines.append(f'{group} /{name}: {coverage}/{len(entries)} catalog lines')
        for text in entries:
            if text not in keys:
                lines.append('  EN ONLY: ' + text)
            else:
                unchanged = ','.join(lang for lang in LANGS if catalogs[lang][text] == text)
                if unchanged:
                    lines.append('  SOURCE-EQUAL ['+unchanged+']: '+text)
        lines.append('')
    return '\n'.join(lines).rstrip() + '\n'


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--check', action='store_true')
    args = p.parse_args()
    path = ROOT/'doc/HELP-LOCALIZATION-COVERAGE.txt'
    data = report()
    if args.check:
        if not path.exists() or path.read_text('utf-8') != data:
            raise SystemExit('help-translation audit stale; run python3 tools/audit_help_coverage.py')
    else:
        path.write_text(data, encoding='utf-8')
    print('\n'.join(data.splitlines()[7:10]) + '\nHelp-translation audit: PASS')


if __name__ == '__main__':
    main()
