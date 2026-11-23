vipirc dispose de brouillons traduits de 43 des 52 sections du manuel. Les commandes restent littérales ; relecture native requise.

English source: vipirc-0.7.36-english-source-r1.tar.gz
Optional patch: vipirc-0.7.36-languages-r22.tar.gz
Extract both archives into the same parent directory.
Apply ONCE before configure/build:
    sh vipirc-0.7.36-languages-r22/apply.sh vipirc-0.7.36
Build: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r22]
Locale: VIPIRC_LANG=fr
Original English MANUAL.txt and SECURITY.md remain authoritative.
Historical READMEs are in HISTORY/; do not use their old commands.

