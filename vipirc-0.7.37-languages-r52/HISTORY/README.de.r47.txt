Optionales Sprachpaket r47: vipirc-0.7.37

Nur auf unveränderten englischen r1-Quellcode anwenden; nicht über r46 oder andere Sprachpakete installieren.
Die Übersicht gruppiert Server, Kanal 0, beigetretene und verlassene Kanalansichten, Privatgespräche, ungelesene Nachrichten, Erwähnungen, ausstehende Arbeit und Dateiübertragungen nach Server. Erwähnungen werden nicht zu den Ungelesenen addiert.
 Eine bereits gesendete Nachricht wird noch abgeschlossen.
Sprachprüfung und vollständiger ncurses-Oberflächentest von vipirc stehen weiterhin aus.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r47.tar.gz
  sh vipirc-0.7.37-languages-r47/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=de ./vipirc --help

vipirc 0.7.37 [lang-rtl-r47]

doc/UI-LOCALIZATION-r47.txt
HISTORY/README.de.r46.txt

r47: Unverschlüsselte Direktverbindung. Accept verrät Ihre IP-Adresse an die Gegenstelle.

r47: Hilfe zum Dateibrowser und Authentifizierungsstatus übersetzt.
