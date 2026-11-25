#!/usr/bin/env python3
"""Ensure translated display prose retains literal CLI options and IRC commands.

Set inclusion (not counts): grammatical repetition may vary. ASCII token
matching supports adjacent Arabic conjunctions. German noun compounds such
as /summary-Bericht retain the literal command followed by a hyphen.
Exclude /message in the byte-budget label: it is a unit, not an IRC command.
"""
from pathlib import Path
import json
import re
from collections import Counter
ROOT=Path(__file__).resolve().parents[1]
LANGS=('id','ms','tr','es','pt','fr','de','ru','it','ar')
FLAG=re.compile(r'--[a-z][a-z0-9-]*',re.ASCII)
CMD=re.compile(r'(?<![A-Za-z0-9_/])/([a-z][a-z0-9_-]*)',re.ASCII)
COMMANDS=set(re.findall(r'\{\{?"([a-z][a-z0-9_-]+)", "/',(ROOT/'src/idle_commands.c').read_text()))
COMMANDS.update(re.findall(r'\{"([a-z][a-z0-9_-]+)", "/',(ROOT/'src/commands.c').read_text()))
COMMANDS.update(('autocommand','channellist','querylist','serverlist','conn','disc','detach','exit'))
assert len(COMMANDS)>40
BUDGET='{bytes} UTF-8 bytes | {budget}/message | row {row} col {col}'
# Known slash commands in the source catalog are preserved except this
# exact display unit. Protocol names and placeholders have separate guards.
def required(k):
    tokens=set(FLAG.findall(k))|{'/'+t for t in CMD.findall(k) if t in COMMANDS}
    if k==BUDGET: tokens.discard('/message')
    return tokens

def present(token, value):
    # A following hyphen may be prose punctuation or a German compound.
    # Reject a longer CLI option or ASCII identifier sharing this prefix.
    end=r'(?![A-Za-z0-9_-])' if token.startswith('--') else r'(?![A-Za-z0-9_])'
    return bool(re.search(re.escape(token)+end,value,re.ASCII))

def main():
    master=json.loads((ROOT/'i18n/en.json').read_text('utf-8'))
    failures=[]; checked=0
    for lang in LANGS:
        data=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
        for k in master:
            req=required(k)
            if not req: continue
            checked+=1
            missing=sorted(t for t in req if not present(t,data[k]))
            if missing: failures.append((lang,k,missing))
    for f in failures: print(*f,sep=' | ')
    if failures: raise SystemExit(f'{len(failures)} literal-token failures')
    print(f'CLI/IRC literal tokens: {checked} locale/key pairs PASS')
if __name__=='__main__':main()
