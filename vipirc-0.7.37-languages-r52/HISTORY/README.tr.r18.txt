r18, vipirc için FILE TRANSFERS, INPUT EDITING ve MOUSE CONTROLS bölümlerinin tam taslak çevirilerini ekler. onekeyirc için önceden çevrilmiş minimalist kılavuzdan bağımsız bir /dcc başvurusu sunulur. Komutları aynen yazın. Akıcı konuşanların incelemesi gerekir.

English source: vipirc-0.7.36-english-source-r1.tar.gz
Optional patch: vipirc-0.7.36-languages-r18.tar.gz
Extract both archives into the same parent directory.
Apply once BEFORE configure/build:
    sh vipirc-0.7.36-languages-r18/apply.sh vipirc-0.7.36
Build: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r18]
Locale: VIPIRC_LANG=tr
English security authority: MANUAL.txt and SECURITY.md.
Complete historical READMEs are in HISTORY/, not current install instructions.
