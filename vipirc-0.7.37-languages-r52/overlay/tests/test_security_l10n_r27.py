#!/usr/bin/env python3
"""Verify r27 source-linked security translations. No third-party packages."""
from pathlib import Path
import sys, re, hashlib, unicodedata, json, csv
ROOT=Path(__file__).resolve().parent.parent
SRC=ROOT/'SECURITY.md'
DOC=ROOT/'doc'
LANGS=['en','id','ms','tr','es','pt','fr','de','ru','it','ar']
SLUGS={
 'TRUST':'Trust boundary',
 'TLS':'TLS, credentials and endpoint identity',
 'FILES':'Configuration and private files',
 'CHILDREN':'Child processes and synchronous work',
}
REQUIRED={
 'TRUST':['IRC','C/TLS','curses','GPG','/dev/null','umask 0077','Linux','pledge/unveil','seccomp','configure','libc','PIE','RELRO'],
 'TLS':['TLS','1.2','SASL','PASS','OpenSSL 3.5.7','CVE-2026-35189','3.5.9','https://openssl-library.org/news/secadv/20260929.txt','ca_file','PEM','16 MiB','AUX','CRL','--server','SASL PLAIN','soju','ZNC','GPG'],
 'FILES':['INI','64 KiB','0600','0400','0700','--init-config','SASL','1024','PASS','503','ZNC','GPG','TLS','5','16 KiB','40/64','argv','Apply only','Connect once','Save','inode','chmod'],
 'CHILDREN':['DNS','GPG','SIGCHLD=SIG_IGN','SA_NOCLDWAIT','O_NONBLOCK','FIFO'],
}

def sections():
    x=re.split(r'(?m)^## ([^\n]+)\n',SRC.read_text(encoding='utf8'))
    return {name: body.strip() for name,body in zip(x[1::2],x[2::2])}


def clean(data):
    assert data==unicodedata.normalize('NFC',data), 'not NFC'
    assert not any(unicodedata.category(c) in ('Cc','Cf','Cs','Co','Cn') and c not in ('\n','\t') for c in data), 'control/format character'
    assert '\r' not in data and '\x1b' not in data


def main():
    source=sections()
    meta=json.loads((DOC/'SECURITY-SOURCE-r27.json').read_text(encoding='utf8'))
    assert meta['native_speaker_review'] is False
    assert len(meta['sections'])==len(source)
    count=0
    for slug,title in SLUGS.items():
        raw=source[title]
        expected_hash=hashlib.sha256(raw.encode()).hexdigest()
        expected_paras=[' '.join(p.splitlines()) for p in raw.split('\n\n')]
        for lang in LANGS:
            file=DOC/f'SECURITY-SECTION-{slug}-{lang}.txt'
            text=file.read_text(encoding='utf8');clean(text)
            assert f'English section SHA-256: {expected_hash}' in text,(file,'hash')
            assert f'Paragraphs: {len(expected_paras)}' in text,(file,'paragraph count')
            assert f'Source: SECURITY.md, section "{title}"' in text
            body=text.split('\n\n',1)[1].strip()
            parts=body.split('\n\n')
            assert len(parts)==len(expected_paras),(file,'incomplete')
            if lang=='en':
                assert parts==expected_paras,(file,'English text changed')
            else:
                assert all(token in body for token in REQUIRED[slug]),(file,'missing technical literal')
            count+=1
    for lang in LANGS:
        file=DOC/f'SECURITY-PARTIAL-{lang}.txt'
        content=file.read_text(encoding='utf8');clean(content)
        assert 'PARTIAL SECURITY DOCUMENTATION' in content
        for slug,title in SLUGS.items():
            assert f'## {title}' in content
            assert hashlib.sha256(source[title].encode()).hexdigest() in content
        assert len(re.findall(r'(?m)^## ',content))>=4
    with (DOC/'SECURITY-COVERAGE-r27.tsv').open(encoding='utf8') as f:
        rows=list(csv.DictReader(f,delimiter='\t'))
    assert len(rows)==len(source)
    assert sum(row['status']=='draft in 10 languages' for row in rows)==4
    assert all(row['sha256']==hashlib.sha256(source[row['section']].encode()).hexdigest() for row in rows)
    print(f'OK: {count} complete source-linked section files; {len(LANGS)} partial docs; {len(rows)} source sections')

if __name__=='__main__':
    main()
