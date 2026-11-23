r19 ergänzt fünf vollständig übersetzte vipirc-Handbuchkapitel (Nachrichtenlänge, Protokollierung/Datenschutz, Serverprofile, TLS-Vertrauen und Sicherheitsgrenzen) sowie das onekeyirc-Kapitel zur Vertrauensgrenze. Die Entwürfe benötigen noch sprachliche Fachprüfung.

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
