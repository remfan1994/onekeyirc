#!/usr/bin/env python3
"""Derive short offline language guides from the same reviewed runtime catalog."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LANGS = ('id','ms','tr','es','pt','fr','de','ru','it','ar')
HELP_LINES = (
    'Choose the display language before starting the client.',
    'Example: VIPIRC_LANG=id onekeyirc',
    'Example: VIPIRC_LANG=tr vipirc -i',
    'Available codes: id ms tr es pt fr de ru it ar.',
    'Use a UTF-8 terminal, font and installed UTF-8 locale.',
    'IRC commands, help topics and configuration keys stay in English.',
    'Arabic text requires terminal shaping and bidirectional support.',
    'Read help topics: /help ls.',
    'Command list: /help commands. Details: /help COMMAND.',
    'First connection: /connect irc.example.org, then /join #channel.',
    'Private chat: /query NICK or /msg NICK TEXT.',
    'Guided setup: /quickstart or /panel.',
    'Save settings: /saveconf. Saving does not enable autoconnect.',
    'Switch views: /servers, /channels, /ql.',
    'Startup options: /help autoconnect, /help autojoin, /help autoauth.',
    'Recover SSH sessions: /help sessions.',
    'Bouncer instructions: /help bouncers.',
    'Settings and hotkeys: /help configuration, /help hotkeys.',
    'Problems and TLS security: /help troubleshooting, /help security, /help tls.',
    'History and logs: /help history, /help logging.',
    'Full reference: /help all or /manual.',
    'Restart the program to change the language of an existing worker.',
)

def render(lang):
    catalog = json.loads((ROOT/'i18n'/f'{lang}.json').read_text(encoding='utf-8'))
    missing = [key for key in HELP_LINES if key not in catalog]
    if missing: raise ValueError(f'{lang}: missing {missing}')
    return ('vipirc / onekeyirc - '+catalog['command help']+'\n'
            '========================================\n\n'
            + '\n\n'.join(catalog[key] for key in HELP_LINES)+'\n')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for locale in LANGS:
        path = ROOT/'doc'/f'START-{locale}.txt'
        data = render(locale).encode('utf-8')
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                raise SystemExit(f'Stale translated guide: {path}')
        else:
            path.write_bytes(data)
    print(f'offline guides: {len(LANGS)} UTF-8 locales PASS')
