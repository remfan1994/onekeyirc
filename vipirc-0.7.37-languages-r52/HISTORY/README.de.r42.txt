Optionales r42-Sprachpaket mit arabischer Rechts-nach-links-Darstellung.
vipirc 0.7.37 - r42

Nur mit unveränderten englischen Originalquellen r1 verwenden. Nicht über r41 oder ältere Pakete installieren. Die Anwendungsversion bleibt unverändert.

vipirc: weitere Warnungen, Verbindungsfehler, UTF-8-Eingaben, DCC- und Bildschirmhinweise übersetzt.

tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
tar -xzf vipirc-0.7.37-languages-r42.tar.gz
sh vipirc-0.7.37-languages-r42/apply.sh vipirc-0.7.37
cd vipirc-0.7.37
./configure && make
./vipirc --version

VIPIRC_LANG=de ./vipirc --help
vipirc 0.7.37 [lang-rtl-r42]

IRC-Befehle, Optionsnamen, Konfigurationswerte, Nutzernamen, Serverantworten und Netzwerkdaten bleiben wörtlich erhalten. Sprachprüfung steht aus; der vollständige ncurses-Build von vipirc wurde nicht überprüft.

doc/UI-LOCALIZATION-r42.txt
doc/CLI-LOCALIZATION-r33.txt
doc/SECURITY-FULL-de.txt

NEU IN r39:
- 45 sichtbare vipirc-Meldungen sind mit Übersetzungen verbunden; außerdem ein Antwortgrößenhinweis von onekeyirc.
- Nur auf die passende, unveränderte englische r1-Quelle anwenden; Sprachpakete nicht übereinander installieren.

NEU IN r41: Kontexthilfe, Einstellungshinweise, Listenbeschriftungen und Fehlermeldungen übersetzt. Nur auf unveränderten englischen r1-Quellcode anwenden.

NEU IN r41: Befehlshilfe und Statusmeldungen uebersetzt. Nur auf unveraenderte englische r1-Quellen anwenden.

NEU IN r42: Kontexthilfe, Status und Verlaufstitel übersetzt. Nur auf unveränderten englischen r1-Quellcode anwenden.
