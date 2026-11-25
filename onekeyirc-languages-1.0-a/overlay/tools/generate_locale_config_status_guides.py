#!/usr/bin/env python3
"""Create localized configuration and status help guides from the source catalog."""
import argparse
import json
from pathlib import Path
from audit_help_coverage import collect
from generate_locale_practical_guides import INTRO, LANGS
ROOT=Path(__file__).resolve().parents[1]
TITLES={
 'id':('Konfigurasi','Server, ringkasan, pengguna, dan kata pantau'),
 'ms':('Konfigurasi','Pelayan, ringkasan, pengguna dan kata pantau'),
 'tr':('Yapılandırma','Sunucular, özet, kullanıcılar ve izlenen sözcükler'),
 'es':('Configuración','Servidores, resumen, usuarios y palabras vigiladas'),
 'pt':('Configuração','Servidores, resumo, usuários e palavras monitoradas'),
 'fr':('Configuration','Serveurs, résumé, utilisateurs et mots surveillés'),
 'de':('Konfiguration','Server, Übersicht, Benutzer und überwachte Wörter'),
 'ru':('Конфигурация','Серверы, сводка, пользователи и слова отслеживания'),
 'it':('Configurazione','Server, riepilogo, utenti e parole monitorate'),
 'ar':('الإعدادات','الخوادم والخلاصة والمستخدمون والكلمات المراقبة'),
}
TOPICS={
 'CONFIGURATION':(('topic','configuration'),),
 'STATUS':(('command','summary'),('command','server'),('command','users'),('command','watch')),
}
def render(lang,which):
    catalog=json.loads((ROOT/'i18n'/f'{lang}.json').read_text('utf-8'))
    title,disclaimer,security=INTRO[lang]
    title += ' - ' + TITLES[lang][0 if which=='CONFIGURATION' else 1]
    out=[title,'='*54,'',disclaimer,security,'']
    rows=collect(ROOT)
    for kind,name in TOPICS[which]:
        entries=[s for k,n,s in rows if (k,n)==(kind,name)]
        if not entries: raise ValueError('missing source help: '+name)
        heading='/help '+name
        out += [heading,'-'*len(heading),'']
        for en in entries:
            value=catalog.get(en)
            if not value or value==en:
                raise ValueError(f'{lang}: missing {heading}: {en}')
            out += [value,'']
    return ('\n'.join(out).rstrip()+'\n').encode('utf-8')

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--check',action='store_true')
    args=ap.parse_args()
    for which in TOPICS:
        for lang in LANGS:
            path=ROOT/'doc'/f'{which}-{lang}.txt'
            output=render(lang,which)
            if args.check:
                if not path.is_file() or path.read_bytes()!=output: raise SystemExit('stale '+str(path))
            else: path.write_bytes(output)
    print('r6 configuration/status guides: 5 complete help sections x 10 languages PASS')
if __name__=='__main__': main()
