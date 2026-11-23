r18 добавляет полные черновые переводы разделов FILE TRANSFERS, INPUT EDITING и MOUSE CONTROLS для vipirc. Для onekeyirc выделена справка /dcc из ранее переведённого минималистичного руководства. Команды вводятся без изменений. Требуется редактура носителями языка.

English source: vipirc-0.7.36-english-source-r1.tar.gz
Optional patch: vipirc-0.7.36-languages-r18.tar.gz
Extract both archives into the same parent directory.
Apply once BEFORE configure/build:
    sh vipirc-0.7.36-languages-r18/apply.sh vipirc-0.7.36
Build: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r18]
Locale: VIPIRC_LANG=ru
English security authority: MANUAL.txt and SECURITY.md.
Complete historical READMEs are in HISTORY/, not current install instructions.
