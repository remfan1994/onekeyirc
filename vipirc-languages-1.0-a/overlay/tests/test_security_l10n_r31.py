#!/usr/bin/env python3
"""Check full r31 SECURITY.md translation draft coverage and source linkage.

Technical completeness checks are not semantic or native-speaker verification.
"""
from pathlib import Path
import csv
import hashlib
import json
import re
import unicodedata

BASE=Path(__file__).resolve().parent.parent
DOC=BASE/'doc'
L=['en','id','ms','tr','es','pt','fr','de','ru','it','ar']
TITLE={
'TRUST':'Trust boundary',
'TLS':'TLS, credentials and endpoint identity',
'FILES':'Configuration and private files',
'CHILDREN':'Child processes and synchronous work',
'PROTOCOL':'Protocol, terminal input and bounded state',
'LOG':'Local history, saved logs and privacy',
'AUTO':'Automatic commands and cancellation',
'SESSIONS':'Managed sessions and SSH loss',
'BOUNCER':'ZNC, soju and playback',
'DCC':'DCC file transfers',
'OFFLINE':'Offline checks and evidence',
'FULL':'Full-interface review and file browsing',
'KEYS':'User-defined key bindings',
}
NEEDED={
'DCC':['DCC SEND','IPv4','TLS','IRC','Accept/send','SSH','symlink','0600','inode','4294967295','DCC CHAT','resume','IPv6','NAT/firewall','/cancel'],
'OFFLINE':['--help','--version','--manual','--check-config','--test-mode','IRC/DCC','CA','GPG','Background','IPC','vipirc 0.7.34 / onekeyirc 0.1.33','doc/STABILITY-REVIEW-0.7.34-0.1.33.txt','doc/STABILITY-PASS-RESULTS.json','doc/SECURITY-REVIEW-0.7.33-0.1.32.txt','doc/SECURITY-PASS-RESULTS.json','ZNC/soju','IRCNow','ncurses','AF_UNIX','OpenBSD/LibreSSL'],
'FULL':['ncurses','Accept/Offer','ID','inode','cwd','64','4096','512','XDG_DATA_HOME/vipirc/downloads','HOME/.local/share/vipirc/downloads','IPv4','IRC','Whois/Query/Ignore','curses'],
}

def digest(s):return hashlib.sha256(s.encode('utf8')).hexdigest()
def normalized_paras(s):return [' '.join(p.splitlines()) for p in s.split('\n\n')]
def safe(s):
 assert unicodedata.normalize('NFC',s)==s,'not NFC'
 assert not any(unicodedata.category(c) in ('Cc','Cf','Cs','Co','Cn') and c not in '\n\t' for c in s),'invisible control'
parts=re.split(r'(?m)^## ([^\n]+)\n',(BASE/'SECURITY.md').read_text(encoding='utf8'))
source={title:body.strip() for title,body in zip(parts[1::2],parts[2::2])}
assert len(source) in (12,13)
meta=json.loads((DOC/'SECURITY-SOURCE-r31.json').read_text())
assert meta['release']=='r31'
assert not meta['native_speaker_review']
assert meta['english_source_authoritative']
assert len(meta['sections'])==len(source)
assert all(v['status']=='draft in 10 languages' for v in meta['sections'])
assert set(meta['locales'])==set(L[1:])
with (DOC/'SECURITY-COVERAGE-r31.tsv').open(encoding='utf8') as f:
 rows=list(csv.DictReader(f,delimiter='\t'))
assert len(rows)==len(source)
for r in rows:
 assert r['sha256']==digest(source[r['section']])
 assert r['status']=='draft in 10 languages'
 assert int(r['paragraphs'])==len(normalized_paras(source[r['section']]))
 assert int(r['english_words'])==len(source[r['section']].split())

make=(BASE/'Makefile.in').read_text(encoding='utf8')
checks=0
for title,raw in source.items():
 slug=next(k for k,v in TITLE.items() if v==title)
 en=normalized_paras(raw)
 for lang in L:
  f=DOC/f'SECURITY-SECTION-{slug}-{lang}.txt'
  t=f.read_text(encoding='utf8');safe(t)
  assert f'Source: SECURITY.md, section "{title}"' in t,(f,'section name')
  assert f'English section SHA-256: {digest(raw)}' in t,(f,'source hash')
  assert f'Paragraphs: {len(en)}' in t,(f,'paragraph metadata')
  p=t.split('\n\n',1)[1].strip().split('\n\n')
  assert len(p)==len(en),(f,'paragraph coverage')
  if lang=='en':assert p==en,(f,'English reference changed')
  else:
   assert all(len(q)>20 for q in p),(f,'suspiciously short text')
   assert p!=en,(f,'not translated')
   if slug in NEEDED:
    body=' '.join(p)
    miss=[k for k in NEEDED[slug] if k not in body]
    assert not miss,(f,'missing technical tokens',miss)
  checks+=1
  if slug in NEEDED:assert f'doc/SECURITY-SECTION-{slug}-{lang}.txt' in make,f
for lang in L:
 f=DOC/f'SECURITY-FULL-{lang}.txt';t=f.read_text(encoding='utf8');safe(t)
 assert f'doc/SECURITY-FULL-{lang}.txt' in make
 assert 'English SECURITY.md' in t or 'SECURITY.md' in t
 assert len(re.findall(r'(?m)^## ',t))==len(source),(f,'missing headers')
 for title,raw in source.items():
  assert f'English source section: {title}\nSource SHA-256: {digest(raw)}\n' in t,(f,title,'missing source link')
 assert 'review' in t.lower() or 'revis' in t.lower() or 'перевод' in t.lower() or 'مراجعة' in t
 # Exact normalized source paragraphs must appear in order in the English edition.
 if lang=='en':
  start=0
  for raw in source.values():
   v='\n\n'.join(normalized_paras(raw))
   index=t.find(v,start)
   assert index>=start,(f,'English reference not complete')
   start=index+len(v)
assert 'tests/test_security_l10n_r31.py' in make
print(f'OK r31: {len(source)} sections in {len(L)} languages; {checks} section files; complete drafts, not reviewed translations')
