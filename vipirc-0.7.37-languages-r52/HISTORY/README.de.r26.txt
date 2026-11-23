Anleitung für Paket r26

Dieses optionale Paket bleibt vom englischen Quellcode getrennt. Der ncurses-Leser und --manual zeigen den durch VIPIRC_LANG gewählten Entwurf, sofern die Datei vorhanden und sicher ist; sonst wird das integrierte englische Handbuch verwendet. RTL ist unverändert. Die Prüfung durch Muttersprachler steht noch aus.

vipirc-0.7.36-english-source-r1.tar.gz
vipirc-0.7.36-languages-r26.tar.gz

    tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.36-languages-r26.tar.gz
    sh vipirc-0.7.36-languages-r26/apply.sh vipirc-0.7.36
    cd vipirc-0.7.36 && ./configure && make
    VIPIRC_LANG=de ./vipirc --manual

--version: vipirc 0.7.36 [lang-rtl-r26]
VIPIRC_MANUAL_DIR=/absolute/path (optional manual directory override)

Nur auf eine frisch entpackte englische r1-Quelle anwenden, nicht auf ältere Patches.
MANUAL.txt and SECURITY.md in English remain authoritative.
Historical instructions are archived under HISTORY/ and must not be used to install r26.
