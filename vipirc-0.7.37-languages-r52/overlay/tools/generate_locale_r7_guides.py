#!/usr/bin/env python3
"""Generate r7 settings and channel-directory help from runtime translations."""
import argparse,json
from pathlib import Path
from audit_help_coverage import collect
from generate_locale_practical_guides import INTRO,LANGS
ROOT=Path(__file__).resolve().parents[1]
PARTS={
    "SETTINGS":(("command","set"),),
    "CHANNELS":(("command","list"),("command","ls"),
                ("command","manual"),("command","servers"),
                ("command","unignore"),("topic","aliases")),
}
def render(lang,which):
    d=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    title,disclaimer,security=INTRO[lang]
    title=title+' - '+d['Settings' if which=='SETTINGS' else 'Channels']
    out=[title,'='*55,'',disclaimer,security,'']
    rows=collect(ROOT)
    for kind,name in PARTS[which]:
        arr=[s for k,n,s in rows if (k,n)==(kind,name)]
        if not arr:raise ValueError((kind,name))
        heading='/help '+name
        out.extend([heading,'-'*len(heading),''])
        for en in arr:
            trans=d.get(en,en)
            # Pure alias command mappings MUST remain unchanged for accuracy.
            is_literal=kind=='topic' and name=='aliases' and en.startswith('/') and ' and ' not in en
            if not is_literal and trans==en:
                raise ValueError(f'{lang} /help {name} not translated: {en}')
            out.extend([trans,''])
    return ('\n'.join(out).rstrip()+'\n').encode('utf-8')
def main():
    p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');a=p.parse_args()
    for lang in LANGS:
        for part in PARTS:
            path=ROOT/'doc'/f'{part}-{lang}.txt'
            data=render(lang,part)
            if a.check:
                if not path.exists() or path.read_bytes()!=data:raise SystemExit('stale '+str(path))
            else:path.write_bytes(data)
    print('r7 guides: 2 areas x 10 locales PASS')
if __name__=='__main__':main()
