r20, vipirc tam kılavuzundan 14 bölümü Türkçeye ekler. Komutlar ve yollar aynen korunur; anadili Türkçe olan uzman incelemesi gereklidir.

English source: vipirc-0.7.36-english-source-r1.tar.gz
Optional patch: vipirc-0.7.36-languages-r20.tar.gz
Extract both archives into the same parent directory.
Apply ONCE before configure/build:
    sh vipirc-0.7.36-languages-r20/apply.sh vipirc-0.7.36
Build: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r20]
Locale: VIPIRC_LANG=tr
Original English MANUAL.txt and SECURITY.md remain authoritative.
Historical READMEs are in HISTORY/; do not use their old commands.
