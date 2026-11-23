Use signals steht standardmäßig auf no. Ctrl-C und Ctrl-Z brechen eine ungesendete Nachricht oder einen Befehl ab, statt das Programm zu beenden oder anzuhalten. Wählen Sie yes in Panel 0 oder den Interface-Einstellungen von vipirc, um Terminalsignale zuzulassen. Apply wirkt sofort; Save speichert zusätzlich [input] use_signals=true|false. Beim Beenden oder Anhalten werden die ursprünglichen Terminaleinstellungen wiederhergestellt.

vipirc 0.7.37: optional language + Arabic RTL patch r28
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch archive: vipirc-0.7.37-languages-r28.tar.gz

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r28.tar.gz
    sh vipirc-0.7.37-languages-r28/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37 && ./configure && make
    ./vipirc --version

Expected: vipirc 0.7.37 [lang-rtl-r28]
Language: VIPIRC_LANG=de
See doc/USE-SIGNALS-de.txt and README.txt.
Do not apply over r27 or older application versions.
