#!/usr/bin/env python3
"""Check the complete fixed help tables, literal syntax, exports and C output."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from audit_help_coverage import LANGS, collect, literal_alias
from generate_help_reference import render

parser = argparse.ArgumentParser()
parser.add_argument('--binary', help='compiled test_localized_help_output fixture')
args = parser.parse_args()
rows = collect(ROOT)
master = json.loads((ROOT/'i18n/en.json').read_text('utf-8'))
assert len(rows) == 477
assert len(master) == len(set(master)) == 757
assert len({(kind, name) for kind, name, _ in rows}) == 78
assert sum(literal_alias(*row) for row in rows) == 3
command = re.compile(r'(?<![\w/])/[a-z][a-z_-]*')
new_sections = {('command', 'panel'), ('topic', 'about'), ('topic', 'aliases'),
                ('topic', 'deployment'), ('topic', 'history'), ('topic', 'keys')}
protected = ('0700', 'SIGCHLD', 'PID', '2048', '512 KiB', '16 MiB',
             '64 KiB', '<config_path>.commands', '500 ms', '1000 ms',
             'force_terminal_signals=false', '[input]', 'true', 'fg',
             'Ctrl-D', 'CR/LF/Tab', 'GPG', 'IRC', 'www.bloodguiltcurse.net',
             'www.thatiam.vip', 'remfan1994', 'IALA')
for lang in LANGS:
    catalog = json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    assert set(catalog) == set(master), (lang, 'catalog parity')
    actual = None
    if args.binary:
        output = subprocess.run([str(Path(args.binary).resolve()), lang],
                                check=True, capture_output=True, timeout=20).stdout
        actual = set(output.decode('utf-8').splitlines())
    for kind, name, source in rows:
        value = catalog[source]
        if literal_alias(kind, name, source):
            assert value == source, (lang, 'changed literal aliases', source)
        else:
            assert value != source, (lang, 'untranslated prose', source)
        assert 0 < len(value.encode('utf-8')) <= 1024, (lang, 'byte bound', source)
        if (kind, name) in new_sections:
            assert command.findall(value) == command.findall(source), (lang, 'command spelling/order', source)
            for token in protected:
                if token in source:
                    assert token in value, (lang, 'missing technical token', token, source)
        if actual is not None:
            assert value in actual, (lang, 'runtime help truncated or omitted a line', source)
    path = ROOT/'doc'/f'HELP-REFERENCE-{lang}.txt'
    assert path.read_bytes() == render(lang), (lang, 'stale offline reference')
print('Help: 474 translated lines + 3 literal mappings x 10 locales PASS')
if args.binary:
    print('Runtime command reader: all 4770 complete UTF-8 lines PASS')
