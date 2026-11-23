По умолчанию Use signals = no. Ctrl-C и Ctrl-Z отменяют неотправленное сообщение или команду, не завершая и не приостанавливая программу. Выберите yes на панели 0 или в настройках Interface клиента vipirc, чтобы разрешить сигналы терминала. Apply действует сразу; Save также сохраняет [input] use_signals=true|false. При выходе и приостановке исходные настройки терминала восстанавливаются.

vipirc 0.7.37: optional language + Arabic RTL patch r28
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch archive: vipirc-0.7.37-languages-r28.tar.gz

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r28.tar.gz
    sh vipirc-0.7.37-languages-r28/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37 && ./configure && make
    ./vipirc --version

Expected: vipirc 0.7.37 [lang-rtl-r28]
Language: VIPIRC_LANG=ru
See doc/USE-SIGNALS-ru.txt and README.txt.
Do not apply over r27 or older application versions.
