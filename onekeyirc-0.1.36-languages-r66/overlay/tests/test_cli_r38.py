#!/usr/bin/env python3
"""r38 optional CLI localization: source, catalog and runtime checks.

Safe offline tests only: no IRC network, no logins, no file writes except the
interpreter's cache and subprocess pipes. English protocol option spellings
and exit statuses are retained.
"""
import json, os, re, subprocess, sys, unicodedata
from pathlib import Path

root=Path(__file__).resolve().parents[1]
langs=('id','ms','tr','es','pt','fr','de','ru','it','ar')
messages=json.loads((root/'i18n/en.json').read_text('utf-8'))
original_count=880
assert len(messages)==len(set(messages)) and len(messages)>=926
source=(root/'src/main.c').read_text('utf-8')
# A translation is text, not a printf format. Named slots must appear once.
import string
placeholders={
    '{option} is too long': {'option'},
    'cannot start in background: saved-server catalog: {error}': {'error'},
    'cannot start in background: saved server {number}: {error}': {'number','error'},
}
for lang in langs:
    cat=json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))
    for key, required_slots in placeholders.items():
        actual=[name for _,name,_,_ in string.Formatter().parse(cat[key]) if name]
        assert len(actual)==len(required_slots) and set(actual)==required_slots,(lang,key,actual)
        assert not any('%' in x for x in (cat[key],))

assert 'cli_diagnostic(FILE *out, const char *english)' in source
assert 'cli_localized_intro(FILE *out)' in source
assert len(re.findall(r'\[lang-rtl-r[0-9]+\]',source))==1
for word in ('invalid option; use --help','interactive operation requires a terminal; use --help or --manual', 'Configuration is valid.', 'Created'):
    assert f'irc_tr("{word}")' in source or f'cli_diagnostic(stderr, "{word}")' in source,word
required=[
'input is too long',
'input contains an unsupported control character',
'cannot reserve standard input/output descriptors',
'invalid option; use --help',
'unexpected operand; use --help',
'--resume, --sessions, --detach-session and --close-session are separate operations; do not combine them with connection or configuration options',
'port must be 1..65535',
'--background requires an existing configuration or an explicit --server; configure and check it first',
'interactive operation requires a terminal; use --help or --manual',
'configuration file does not exist; use --init-config first',
'Configuration is valid.',
'Choose a valid IRC nickname (ASCII letters, digits, or IRC nickname symbols).',
'Created',
'Translated command-line overview (English option list follows):',
'Connect with --server HOST; use --no-connect to start without a network connection.',
'Choose verified TLS with --tls; --plain sends IRC without encryption.',
'Use --check-config to check settings; it does not test login or TLS.',
'Use --sessions to list jobs; --resume JOB resumes a detached job.',
'Use signals is off by default; Ctrl-C/Ctrl-Z cancel unsent text. Use /quit to exit.',
'Run --manual for the full manual in your selected language.',
'Command and option spellings in the English reference below are literal.',
]
for key in required:
    assert key in messages,key
literal=re.compile(r'(?<![A-Za-z0-9])(?:--[a-z][a-z0-9-]*|/[a-z][a-z0-9_-]*)(?![A-Za-z0-9_-])', re.I)
for lang in langs:
    catalog=json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))
    assert set(catalog)==set(messages)
    for key in required:
        v=catalog[key]
        assert isinstance(v,str) and v and v != key,(lang,key)
        assert len(v.encode('utf-8'))<=256,(lang,key)
        assert v==unicodedata.normalize('NFC',v),(lang,key)
        assert not any(unicodedata.category(c) in ('Cc','Cf','Cs') for c in v),(lang,key)
        for token in literal.findall(key): assert token in v,(lang,key,token)
    bin_path=root/('onekeyirc' if (root/'src/main.c').read_text('utf-8').find('#define PROGRAM_NAME "onekeyirc"')>=0 and (root/'onekeyirc').exists() else 'vipirc')

# Runtime integration: choose the executable that exists in this source tree.
exe=root/('onekeyirc' if (root/'onekeyirc').is_file() else 'vipirc')
if exe.is_file() and os.access(exe,os.X_OK):
    app=exe.name
    variable='ONEKEYIRC_LANG' if app=='onekeyirc' else 'VIPIRC_LANG'
    for lang in ('en',)+langs:
        env=os.environ.copy()
        env.update({'LC_ALL':'C.UTF-8', 'LANG':'C.UTF-8', 'ONEKEYIRC_LANG':'', 'VIPIRC_LANG':'', variable:lang})
        p=subprocess.run([str(exe),'--help'],env=env,capture_output=True,text=True,timeout=8)
        assert p.returncode==0,(app,lang,p.stderr)
        assert '--plain' in p.stdout and '--manual' in p.stdout
        title='Command-line overview (option spellings remain unchanged):'
        if lang=='en': assert not title in p.stdout and p.stdout.startswith('Usage:'), (app,lang)
        else:
            expected=json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))[title]
            assert p.stdout.startswith(expected+'\n'),(app,lang,p.stdout[:100])
            assert json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))['Usage: ']+app in p.stdout
            assert p.stdout.count('--no-connect')>=2,(app,lang)
        bad=subprocess.run([str(exe),'--not-a-valid-option'],env=env,capture_output=True,text=True,timeout=8)
        assert bad.returncode==2,(app,lang,bad.returncode)
        key='invalid option; use --help'
        expected=key if lang=='en' else json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))[key]
        assert bad.stderr.strip()==app+': '+expected,(app,lang,bad.stderr)
        operand=subprocess.run([str(exe),'unexpected-argument'],env=env,capture_output=True,text=True,timeout=8)
        key='unexpected operand; use --help'
        expected=key if lang=='en' else json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))[key]
        assert operand.returncode==2 and operand.stderr.strip()==app+': '+expected,(app,lang,operand.stderr)
        once=subprocess.run([str(exe),'--resume','--resume'],env=env,capture_output=True,text=True,timeout=8)
        key='specify --resume only once'
        expected=key if lang=='en' else json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))[key]
        assert once.returncode==2 and once.stderr.strip()==app+': '+expected,(app,lang,once.stderr)
        # A dynamic diagnostic must preserve the literal option spelling.
        # In particular, translation text never becomes a printf format.
        arg='a'*400
        oversize=subprocess.run([str(exe),'--nick',arg],env=env,capture_output=True,text=True,timeout=8)
        expected_key='{option} is too long'
        translated=expected_key if lang=='en' else json.loads((root/'i18n'/f'{lang}.json').read_text('utf-8'))[expected_key]
        expected=app+': '+translated.replace('{option}','nick')+'\n'
        assert oversize.returncode!=0 and oversize.stderr==expected,(app,lang,oversize.stderr,expected)
    print('r38 executable CLI smoke test:',app, '11 locales x 5 paths PASS')
else:
    print('r38 source/catalog tests passed; no compiled executable found, runtime tests SKIPPED')
print('r38 ten-language source/catalog safety:',len(messages),'keys PASS')
