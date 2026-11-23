По умолчанию Use signals = no. Ctrl-C и Ctrl-Z отменяют неотправленное сообщение или команду, не завершая и не приостанавливая программу. Выберите yes на панели 0 или в настройках Interface клиента vipirc, чтобы разрешить сигналы терминала. Apply действует сразу; Save также сохраняет [input] use_signals=true|false. При выходе и приостановке исходные настройки терминала восстанавливаются.

vipirc 0.7.37: optional language + Arabic RTL patch r31
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch archive: vipirc-0.7.37-languages-r31.tar.gz

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r31.tar.gz
    sh vipirc-0.7.37-languages-r31/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37 && ./configure && make
    ./vipirc --version

Expected: vipirc 0.7.37 [lang-rtl-r31]
Language: VIPIRC_LANG=ru
See doc/USE-SIGNALS-ru.txt and README.txt.
Do not apply over r27 or older application versions.

Теперь переведены черновики семи из двенадцати/тринадцати разделов безопасности. Авторитетным остаётся английский оригинал; требуется проверка носителями языка со знанием безопасности.
See doc/SECURITY-FULL-ru.txt and doc/SECURITY-COVERAGE-r31.tsv.

Security documentation r31: full section coverage as unreviewed drafts.
See doc/SECURITY-FULL-XX.txt (XX = locale), and use original English SECURITY.md as authority.
ЧЕРНОВИК ПЕРЕВОДА. Проверка носителями языка со знаниями безопасности ещё не выполнена. Английский SECURITY.md остаётся авторитетным источником.
