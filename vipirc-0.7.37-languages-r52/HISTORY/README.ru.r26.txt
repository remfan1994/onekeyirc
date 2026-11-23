Инструкция пакета r26

Необязательный пакет отделён от исходного английского кода. Просмотрщик ncurses и --manual открывают черновик, выбранный через VIPIRC_LANG, если файл доступен и безопасен; иначе используется встроенное английское руководство. RTL не изменён. Требуется проверка носителями языка.

vipirc-0.7.36-english-source-r1.tar.gz
vipirc-0.7.36-languages-r26.tar.gz

    tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.36-languages-r26.tar.gz
    sh vipirc-0.7.36-languages-r26/apply.sh vipirc-0.7.36
    cd vipirc-0.7.36 && ./configure && make
    VIPIRC_LANG=ru ./vipirc --manual

--version: vipirc 0.7.36 [lang-rtl-r26]
VIPIRC_MANUAL_DIR=/absolute/path (optional manual directory override)

Применяйте к свежей копии английских исходников r1, а не поверх прежней заплаты.
MANUAL.txt and SECURITY.md in English remain authoritative.
Historical instructions are archived under HISTORY/ and must not be used to install r26.
