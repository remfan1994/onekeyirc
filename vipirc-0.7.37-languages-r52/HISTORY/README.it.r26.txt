Istruzioni del pacchetto r26

Questo pacchetto facoltativo resta separato dal codice sorgente inglese. Il lettore ncurses e --manual mostrano la bozza scelta tramite VIPIRC_LANG, se il file è disponibile e sicuro; altrimenti usano il manuale inglese incorporato. RTL è invariato. Serve ancora una revisione madrelingua.

vipirc-0.7.36-english-source-r1.tar.gz
vipirc-0.7.36-languages-r26.tar.gz

    tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.36-languages-r26.tar.gz
    sh vipirc-0.7.36-languages-r26/apply.sh vipirc-0.7.36
    cd vipirc-0.7.36 && ./configure && make
    VIPIRC_LANG=it ./vipirc --manual

--version: vipirc 0.7.36 [lang-rtl-r26]
VIPIRC_MANUAL_DIR=/absolute/path (optional manual directory override)

Applicare solo a una copia appena estratta del sorgente inglese r1, mai sopra una patch precedente.
MANUAL.txt and SECURITY.md in English remain authoritative.
Historical instructions are archived under HISTORY/ and must not be used to install r26.
