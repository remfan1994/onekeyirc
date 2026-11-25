#!/usr/bin/env python3
"""Generate draft offline startup/messages/limits guides from runtime source.

These are whole-source-line catalog entries, not independent translations of
security manuals. Topic names and IRC command syntax are intentionally literal.
"""
import argparse
import json
from pathlib import Path
from audit_help_coverage import collect

ROOT = Path(__file__).resolve().parents[1]
LANGS = ('id', 'ms', 'tr', 'es', 'pt', 'fr', 'de', 'ru', 'it', 'ar')
TOPICS = ('startup', 'messages', 'limits')
EXPECTED_LENGTHS = (16, 6, 6)


def selected():
    records = collect(ROOT)
    topics = [(name, [s for kind, topic, s in records
                      if kind == 'topic' and topic == name]) for name in TOPICS]
    assert tuple(len(lines) for _, lines in topics) == EXPECTED_LENGTHS
    assert len(set(text for _, lines in topics for text in lines)) == sum(EXPECTED_LENGTHS)
    return topics


def render(lang):
    catalog = json.loads((ROOT / 'i18n' / (lang + '.json')).read_text('utf8'))
    lines = [f'vipirc / onekeyirc: first-use help ({lang})',
             '============================================', '',
             'DRAFT TRANSLATIONS - complete excerpts of three interactive help topics.',
             'Command names and option tokens are entered literally.',
             'For authoritative security details, read the English SECURITY.md and MANUAL.txt.', '']
    for topic, sources in selected():
        lines += ['/help ' + topic, '-' * (6 + len(topic))]
        for source in sources:
            translation = catalog.get(source)
            if not translation or translation == source:
                raise ValueError(f'{lang}: untranslated first-use source line: {source}')
            lines.extend([translation, ''])
    return ('\n'.join(lines).rstrip() + '\n').encode('utf8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for lang in LANGS:
        path = ROOT / 'doc' / f'FIRST-USE-{lang}.txt'
        value = render(lang)
        if args.check:
            if not path.is_file() or path.read_bytes() != value:
                raise SystemExit(f'stale translated first-use guide: {path}')
        else:
            path.write_bytes(value)
    print('First-use guides: 3 complete help topics x 10 languages PASS')


if __name__ == '__main__':
    main()
