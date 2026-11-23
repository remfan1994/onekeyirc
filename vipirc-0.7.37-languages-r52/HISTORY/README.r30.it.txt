Use signals è impostato su no per impostazione predefinita. Ctrl-C e Ctrl-Z annullano un messaggio o comando non inviato, senza terminare o sospendere il programma. Scegliere yes nel pannello 0 o nelle impostazioni Interface di vipirc per abilitare i segnali del terminale. Apply ha effetto immediato; Save memorizza anche [input] use_signals=true|false. All'uscita o alla sospensione vengono ripristinate le impostazioni originali del terminale.

vipirc 0.7.37: optional language + Arabic RTL patch r30
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch archive: vipirc-0.7.37-languages-r30.tar.gz

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r30.tar.gz
    sh vipirc-0.7.37-languages-r30/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37 && ./configure && make
    ./vipirc --version

Expected: vipirc 0.7.37 [lang-rtl-r30]
Language: VIPIRC_LANG=it
See doc/USE-SIGNALS-it.txt and README.txt.
Do not apply over r27 or older application versions.

Sono ora disponibili bozze tradotte di sette delle dodici/tredici sezioni sulla sicurezza. Fa fede il testo inglese; è necessaria una revisione da parte di esperti madrelingua.
See doc/SECURITY-PARTIAL-it.txt and doc/SECURITY-COVERAGE-r30.tsv.
