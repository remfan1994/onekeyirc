#!/usr/bin/env python3
"""Check r30 source-linked security drafts, counts, control safety, and literal tokens."""
from pathlib import Path
import csv
import hashlib
import json
import re
import unicodedata
R=Path(__file__).resolve().parent.parent
D=R/'doc'
L=['en','id','ms','tr','es','pt','fr','de','ru','it','ar']
NEW={'PROTOCOL':'Protocol, terminal input and bounded state',
'SESSIONS':'Managed sessions and SSH loss','BOUNCER':'ZNC, soju and playback'}
REQUIRED={
 'PROTOCOL':['IRC','CTCP','UTF-8','LIST','LINELEN','64','1.2','32','4096','8192','8 MiB','50..5000','2048','512 KiB','RSS','CPU','ignore','[input] use_signals=false','Ctrl-C/Ctrl-Z','Use signals=yes','Apply','Save','Ctrl-D','SIGKILL','bracketed paste','Escape','500 ms','1000 ms'],
 'SESSIONS':['Background','Unix','64','PID','TERM','--resume','single_session','IRC','--detach-session NUMBER','--close-session NUMBER','RAM','ncurses','SSH'],
 'BOUNCER':['TLS','ZNC','soju','SASL PLAIN','JOIN','away','/away','/back','/znc','/soju','CTCP','DCC','16','64','UTC','echo-message','draft/chathistory','znc.in/playback','doc/BOUNCERS.txt']}
x=re.split(r'(?m)^## ([^\n]+)\n',(R/'SECURITY.md').read_text(encoding='utf8'))
source=dict(zip(x[1::2],[v.strip() for v in x[2::2]]))
meta=json.loads((D/'SECURITY-SOURCE-r30.json').read_text(encoding='utf8'))
assert not meta['native_speaker_review']
assert len(meta['sections'])==len(source)
assert sum(v['status']=='draft in 10 languages' for v in meta['sections'])==10

def safe(s):
 assert unicodedata.normalize('NFC',s)==s, 'NFC'
 assert not any(unicodedata.category(c) in ('Cc','Cf','Cs','Co','Cn') and c not in '\n\t' for c in s),'unsafe control'

def h(s):return hashlib.sha256(s.encode()).hexdigest()
files=0
for slug,title in NEW.items():
 raw=source[title]
 ep=[' '.join(p.splitlines()) for p in raw.split('\n\n')]
 for lang in L:
  f=D/f'SECURITY-SECTION-{slug}-{lang}.txt';t=f.read_text(encoding='utf8');safe(t)
  assert f'Source: SECURITY.md, section "{title}"' in t
  assert f'English section SHA-256: {h(raw)}' in t
  assert f'Paragraphs: {len(ep)}' in t
  assert 'fluent-speaker security review required' in t
  p=t.split('\n\n',1)[1].strip().split('\n\n')
  assert len(p)==len(ep),(f,'paragraphs')
  if lang=='en':assert p==ep,(f,'English reference changed')
  else:
   body=' '.join(p)
   assert body != ' '.join(ep),(f,'English copied')
   assert all(x in body for x in REQUIRED[slug]),(f,'technical literal',[x for x in REQUIRED[slug] if x not in body])
  files+=1
for lang in L:
 p=D/f'SECURITY-PARTIAL-{lang}.txt';t=p.read_text(encoding='utf8');safe(t)
 assert 'PARTIAL SECURITY DOCUMENTATION: ten of' in t
 assert len(re.findall(r'(?m)^## ',t))==10,(p,'section count')
 for slug,title in NEW.items():assert f'## {title}\nSource SHA-256: {h(source[title])}' in t
with (D/'SECURITY-COVERAGE-r30.tsv').open(encoding='utf8') as f: rows=list(csv.DictReader(f,delimiter='\t'))
assert len(rows)==len(source)
for row in rows:
 assert row['sha256']==h(source[row['section']]),row['section']
 assert (row['status']=='draft in 10 languages') == (row['section'] not in ('DCC file transfers','Offline checks and evidence','Full-interface review and file browsing'))
make=(R/'Makefile.in').read_text(encoding='utf8')
for slug in NEW:
 for lang in L:assert f'doc/SECURITY-SECTION-{slug}-{lang}.txt' in make
for file in ('doc/SECURITY-SOURCE-r30.json','doc/SECURITY-COVERAGE-r30.tsv','doc/SECURITY-TRANSLATION-NOTES-r30.txt'):
 assert file in make
print('OK r30:',files,'new source-linked sections and',len(L),'assembled partial documents')
