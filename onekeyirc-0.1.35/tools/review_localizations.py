#!/usr/bin/env python3
"""Export localization CSV worksheets without contacting a network service.

The catalog values are draft translations. The unchanged flag means that the
English and target strings match byte-for-byte, not that translation is bad:
protocol acronyms, brand names and other fixed tokens may be identical.
"""
import argparse
import csv
import json
from pathlib import Path
import sys

LANGS = ('id', 'ms', 'tr', 'es', 'pt', 'fr', 'de', 'ru', 'it', 'ar')
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument('--lang', choices=LANGS, help='export one language')
    selection.add_argument('--all', action='store_true', help='export a parallel CSV')
    parser.add_argument('--unchanged-only', action='store_true',
                        help='show source-equal terms (only valid with --lang)')
    args = parser.parse_args()
    if args.unchanged_only and not args.lang:
        parser.error('--unchanged-only requires --lang')
    english = json.loads((ROOT/'i18n/en.json').read_text(encoding='utf-8'))
    languages = LANGS if args.all else (args.lang,)
    catalogs = {lang: json.loads((ROOT/'i18n'/(lang+'.json')).read_text(encoding='utf-8'))
                for lang in languages}
    if not isinstance(english, list) or len(set(english)) != len(english):
        raise SystemExit('invalid English catalog')
    for lang, values in catalogs.items():
        if not isinstance(values, dict) or set(values) != set(english):
            raise SystemExit(lang+': catalog keys differ from English')
    output = csv.writer(sys.stdout, lineterminator='\n')
    if args.all:
        output.writerow(('English',)+LANGS)
        for key in english:
            output.writerow((key,)+tuple(catalogs[lang][key] for lang in LANGS))
    else:
        output.writerow(('English', args.lang, 'Unchanged from English'))
        for key in english:
            value = catalogs[args.lang][key]
            if args.unchanged_only and key != value:
                continue
            output.writerow((key, value, 'yes' if key == value else 'no'))


if __name__ == '__main__':
    main()
