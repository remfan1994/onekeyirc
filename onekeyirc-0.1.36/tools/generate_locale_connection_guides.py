#!/usr/bin/env python3
"""Generate connection, TLS and recovery guides from live /help translations.

A maintenance aid, not an independent translation source. The English
command and topic catalog in src/idle_commands.c remains authoritative.
Each translated guide uses the exact text displayed by the running client.
"""
import argparse
import json
from pathlib import Path
from audit_help_coverage import collect

ROOT = Path(__file__).resolve().parents[1]
LANGS = ('id', 'ms', 'tr', 'es', 'pt', 'fr', 'de', 'ru', 'it', 'ar')
GROUPS = (
    ('command', 'connect', 11),
    ('command', 'autoauth', 8),
    ('command', 'autojoin', 6),
    ('command', 'saveconf', 7),
    ('topic', 'tls', 9),
    ('topic', 'reconnect', 11),
)
TOTAL = sum(group[2] for group in GROUPS)
assert TOTAL == 52


def sections():
    rows = collect(ROOT)
    master = set(json.loads((ROOT / 'i18n/en.json').read_text('utf-8')))
    output = []
    for kind, name, expected in GROUPS:
        lines = [line for k, n, line in rows
                 if (k, n) == (kind, name) and line in master]
        if len(lines) != expected or len(set(lines)) != len(lines):
            raise ValueError(f'{name}: help/source changed; recheck guide coverage')
        output.append((name, lines))
    return output


def render(lang):
    strings = json.loads((ROOT / 'i18n' / (lang + '.json')).read_text('utf-8'))
    out = [f'vipirc / onekeyirc - connection help ({lang})',
           '============================================', '',
           'Translation draft. Commands, options and config keys must be entered literally.',
           'For full security details read the authoritative English SECURITY.md and MANUAL.txt.', '']
    for name, lines in sections():
        out.extend(('/help ' + name, '-' * (6 + len(name))))
        for line in lines:
            translated = strings.get(line)
            if not translated or translated == line:
                raise ValueError(f'{lang}: untranslated connection-help line: {line}')
            out.append(translated)
            out.append('')
    return ('\n'.join(out).rstrip() + '\n').encode('utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for lang in LANGS:
        path = ROOT / 'doc' / f'CONNECTION-{lang}.txt'
        content = render(lang)
        if args.check:
            if not path.is_file() or path.read_bytes() != content:
                raise SystemExit(f'{path}: translation guide out of date')
        else:
            path.write_bytes(content)
    print(f'connection help guides: {TOTAL} source lines, 10 languages PASS')


if __name__ == '__main__':
    main()
