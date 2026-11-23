r23 ergänzt vollständige Übersetzungsentwürfe der Kapitel SETTINGS und FILES AND ENVIRONMENT im vipirc-Handbuch. Damit sind 45 von 52 Abschnitten abgedeckt; muttersprachliche Prüfung steht noch aus.

Unveränderter englischer Quellcode: vipirc-0.7.36-english-source-r1.tar.gz
Optionales Sprachpaket mit RTL-Patch: vipirc-0.7.36-languages-r23.tar.gz

Entpacken Sie beide Archive im selben übergeordneten Verzeichnis.
Wenden Sie den Patch vor der Konfiguration und Übersetzung genau einmal an:
    sh vipirc-0.7.36-languages-r23/apply.sh vipirc-0.7.36

Kompilierung: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r23]
Sprache: VIPIRC_LANG=de

Nicht auf einen älteren Patch aufspielen; frisch entpackten englischen r1-Quellcode verwenden.
Der englische Quellcode bleibt unverändert. Die englischen Dateien MANUAL.txt und SECURITY.md sind weiterhin maßgeblich; Übersetzungsentwürfe benötigen muttersprachliche Prüfung.
Ältere Anweisungen liegen unter HISTORY/; führen Sie deren Befehle nicht aus.
