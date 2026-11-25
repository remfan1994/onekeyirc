#!/usr/bin/env python3
"""Generate concise user command guides from the verified in-program text."""
import argparse,json
from pathlib import Path
from audit_help_coverage import collect
from generate_locale_practical_guides import INTRO, LANGS
ROOT=Path(__file__).resolve().parents[1]
TOPICS=('clear','ignore','join','part','topic','quit')
def render(lang):
    strings=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf8'))
    title,disclaimer,security=INTRO[lang]
    sections=[title+' - '+strings['Help'], '='*50, '', disclaimer,security,'']
    rows=collect(ROOT)
    for topic in TOPICS:
        arr=[v for kind,n,v in rows if kind=='command' and n==topic]
        if not arr:raise RuntimeError(topic)
        sections.extend(['/help '+topic,'-'*len(topic),''])
        for text in arr:
            translated=strings.get(text,text)
            if translated==text:raise RuntimeError(f'{lang} /help {topic} not translated: {text}')
            sections.extend([translated,''])
    return ('\n'.join(sections).rstrip()+'\n').encode('utf8')
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--check',action='store_true');a=parser.parse_args()
    for lang in LANGS:
        file=ROOT/'doc'/f'CHANNEL-COMMANDS-{lang}.txt'
        b=render(lang)
        if a.check:
            if file.read_bytes()!=b:raise RuntimeError('stale '+str(file))
        else:file.write_bytes(b)
    print('r8 command guides: 6 commands x 10 locales PASS')
if __name__=='__main__':main()
