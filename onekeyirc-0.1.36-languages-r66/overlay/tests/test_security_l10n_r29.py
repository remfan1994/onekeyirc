#!/usr/bin/env python3
"""Validate r29 security drafts against original English SECURITY.md."""
from pathlib import Path
import hashlib, json, re, csv, unicodedata
R=Path(__file__).resolve().parent.parent
D=R/'doc'; L=['en','id','ms','tr','es','pt','fr','de','ru','it','ar']
M={'TRUST':'Trust boundary','TLS':'TLS, credentials and endpoint identity',
 'FILES':'Configuration and private files','CHILDREN':'Child processes and synchronous work',
 'LOG':'Local history, saved logs and privacy','AUTO':'Automatic commands and cancellation',
 'KEYS':'User-defined key bindings'}
REQ={'LOG':['64','64 KiB','<config_path>.commands','96','<config_path>.log','0700','0600','O_NOFOLLOW','O_NONBLOCK','FIFO','16 MiB','SASL/PASS'],
 'AUTO':['16','511','UTF-8','WHOIS','SASL','0..300','2..60','TLS','/cancel'],
 'KEYS':['64','Save','Function/Alt','doc/HOTKEYS.txt']}
def safe(s):
 assert s==unicodedata.normalize('NFC',s)
 assert not any(unicodedata.category(c) in ('Cc','Cf','Cs','Co','Cn') and c not in '\n\t' for c in s)
x=re.split(r'(?m)^## ([^\n]+)\n',(R/'SECURITY.md').read_text(encoding='utf8'))
source={a:b.strip() for a,b in zip(x[1::2],x[2::2])}
meta=json.loads((D/'SECURITY-SOURCE-r29.json').read_text(encoding='utf8'))
assert meta['native_speaker_review'] is False and len(meta['sections'])==len(source)
assert sum(x['status']=='draft in 10 languages' for x in meta['sections'])==7
count=0
for slug,title in M.items():
 raw=source[title]; h=hashlib.sha256(raw.encode()).hexdigest()
 ep=[' '.join(p.splitlines()) for p in raw.split('\n\n')]
 for lang in L:
  f=D/f'SECURITY-SECTION-{slug}-{lang}.txt';t=f.read_text(encoding='utf8');safe(t)
  assert f'English section SHA-256: {h}' in t,(f,'hash')
  assert f'Paragraphs: {len(ep)}' in t,(f,'paragraphs')
  assert f'Source: SECURITY.md, section "{title}"' in t
  body=t.split('\n\n',1)[1].strip(); p=body.split('\n\n')
  assert len(p)==len(ep),(f,'incomplete')
  assert p==ep if lang=='en' else all(token in body for token in REQ.get(slug,[])),(f,'source or technical tokens')
  count+=1
for lang in L:
 f=D/f'SECURITY-PARTIAL-{lang}.txt';t=f.read_text(encoding='utf8');safe(t)
 assert 'PARTIAL SECURITY DOCUMENTATION' in t
 assert len(re.findall(r'(?m)^## ',t))>=7,(f,'sections')
 for slug,title in M.items():
  assert f'## {title}\nSource SHA-256: {hashlib.sha256(source[title].encode()).hexdigest()}' in t
with (D/'SECURITY-COVERAGE-r29.tsv').open(encoding='utf8') as f:rows=list(csv.DictReader(f,delimiter='\t'))
assert len(rows)==len(source)
for row in rows:
 assert row['sha256']==hashlib.sha256(source[row['section']].encode()).hexdigest()
 assert (row['status']=='draft in 10 languages') == (row['section'] in M.values())
make=(R/'Makefile.in').read_text(encoding='utf8')
for slug in M:
 for lang in L:
  assert f'doc/SECURITY-SECTION-{slug}-{lang}.txt' in make
print('OK:',count,'section files,',len(L),'partial security documents,',len(source),'original sections')
