r19 menambahkan terjemahan lima bab manual vipirc (panjang pesan, log dan privasi, profil server, TLS, serta batas keamanan/sumber daya) dan bab batas kepercayaan onekeyirc. Semua bagian ini masih draf untuk ditinjau penutur fasih.

English source: vipirc-0.7.36-english-source-r1.tar.gz
Optional patch: vipirc-0.7.36-languages-r19.tar.gz
Extract both archives into the same parent directory.
Apply ONCE before configure/build:
    sh vipirc-0.7.36-languages-r19/apply.sh vipirc-0.7.36
Build: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r19]
Locale: vipirc_LANG=0.7.36
Original English MANUAL.txt and SECURITY.md remain authoritative.
Historical READMEs are in HISTORY/; do not use their old commands.
