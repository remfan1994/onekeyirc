#!/usr/bin/env python3
"""Check r22 full-manual companion translations against the shipped English text.

This is technical-literal and source-integrity verification, not linguistic review.
"""
import hashlib
import sys
import unicodedata
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
DOCS=ROOT/'doc'
SOURCE=(ROOT/'MANUAL.txt').read_text('utf-8').splitlines()
LOCALES='id ms tr es pt fr de ru it ar'.split()
CHAPTERS={
 'MINIMALIST-CATCHUP':('MINIMALIST CATCH-UP AFTER RESUME',242,280,[
  '/catchup','vipirc -i','/set persistent_logging on','/save output',
  'Up/Down','PageUp/PageDown','Home','End','Escape']),
 'KEY-BINDINGS':('CUSTOM KEY BINDINGS',782,815,[
  '/bindings keys','/bindings actions','/bind F7 summary','/bind Alt-c command',
  '/unbind F7','/unbind all','/save input','F1..F12','Ctrl-A..Z',
  'Alt-a..z/0..9','Ctrl-H/Backspace','Ctrl-I/Tab','Ctrl-J/Ctrl-M/Enter',
  'Ctrl-[/Escape','[hotkeys]','KEY=action','64','doc/HOTKEYS.txt',
  'compose, command, bindings, help, manual, summary, servers, channels, queries, unread, mentions, transfers, settings, quickstart, next, prev, clearinput, cancel, background, quit']),
 'SERVERS-UNREAD':('SERVERS AND UNREAD ACTIVITY',969,1018,[
  '0..7','F2','/servers','Open    New    Connect    Disconnect    Close    Away    Back',
  'MOTD','DCC','TLS6697','login/default','999+','new N !M','F5','/unread',
  'Server/Channel/Query','/mentions clear']),
 'BOUNCER-CONNECTIONS':('BOUNCER CONNECTIONS',1692,1747,[
  '/panel','/connect','[bouncer]','type=none|znc|soju','username','network',
  'client','automatic_away','63','ASCII','auth=none','auth_user','SASL',
  'username/network','username@client/network','503','PASS','auth=sasl',
  'username/network@client','NickServ','/autojoin','/join','/part','/autoauth',
  'automatic_away=true','/away','/back','/znc ListNetworks',
  '/soju network status','/msg *status','/msg *module','/msg BouncerServ',
  'UTC','CTCP/DCC','doc/BOUNCERS.txt','examples/bouncer-znc.ini',
  'examples/bouncer-soju.ini'])
}
errors=[]
for slug,(heading,first,last,tokens) in CHAPTERS.items():
 source='\n'.join(SOURCE[first-1:last])
 if not source.startswith(heading+'\n'):
  errors.append((slug,'English source boundaries changed'))
 digest=hashlib.sha256(source.encode()).hexdigest()
 base='FULLMANUAL-'+slug+'-'
 ref=(DOCS/(base+'en.txt')).read_text('utf-8')
 if digest not in ref or source.rstrip() not in ref:
  errors.append((slug,'English reference does not match source'))
 for lang in LOCALES:
  path=DOCS/(base+lang+'.txt')
  if not path.is_file():errors.append((slug,lang,'missing'));continue
  data=path.read_text('utf-8')
  if 'English source SHA-256: '+digest not in data:
   errors.append((slug,lang,'source checksum mismatch'))
  if 'NATIVE REVIEW REQUIRED' not in data:
   errors.append((slug,lang,'draft review disclaimer missing'))
  for token in tokens:
   if token not in data:errors.append((slug,lang,'technical literal '+token))
  for ch in data:
   if unicodedata.category(ch) in ('Cc','Cf') and ch not in '\n\t':
    errors.append((slug,lang,'invalid Unicode control '+repr(ch)))
 if not errors:print('PASS',slug,'English SHA256',digest[:16],'+ ten translations')
print('Total checked:',len(CHAPTERS)*len(LOCALES),'localized chapters;',len(errors),'errors')
for error in errors:print('ERROR:',*error)
sys.exit(1 if errors else 0)
