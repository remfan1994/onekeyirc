r25 enthält Entwürfe aller 52 Abschnitte des vollständigen vipirc-Handbuchs. Die letzten fünf Kapitel sind übersetzt; je Sprache liegt ein Offline-Gesamthandbuch bei. Die eingebaute ncurses-Hilfe bleibt englisch. Muttersprachliche Prüfung fehlt.

Unveränderter englischer Quellcode: vipirc-0.7.36-english-source-r1.tar.gz
Optionales Sprachpaket mit RTL-Patch: vipirc-0.7.36-languages-r25.tar.gz

Entpacken Sie beide Archive im selben übergeordneten Verzeichnis.
Wenden Sie den Patch vor der Konfiguration und Übersetzung genau einmal an:
    sh vipirc-0.7.36-languages-r25/apply.sh vipirc-0.7.36

Kompilierung: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r25]
Sprache: VIPIRC_LANG=de

Nicht auf einen älteren Patch aufspielen; frisch entpackten englischen r1-Quellcode verwenden.
Der englische Quellcode bleibt unverändert. Die englischen Dateien MANUAL.txt und SECURITY.md sind weiterhin maßgeblich; Übersetzungsentwürfe benötigen muttersprachliche Prüfung.
Ältere Anweisungen liegen unter HISTORY/; führen Sie deren Befehle nicht aus.
