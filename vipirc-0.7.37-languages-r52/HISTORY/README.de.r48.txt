Optionales Sprachpaket r48: vipirc-0.7.37

Nur auf unveränderten englischen r1-Quellcode anwenden; nicht über r47 oder andere Sprachpakete installieren.
Die Übersicht gruppiert Server, Kanal 0, beigetretene und verlassene Kanalansichten, Privatgespräche, ungelesene Nachrichten, Erwähnungen, ausstehende Arbeit und Dateiübertragungen nach Server. Erwähnungen werden nicht zu den Ungelesenen addiert.
 Eine bereits gesendete Nachricht wird noch abgeschlossen.
Sprachprüfung und vollständiger ncurses-Oberflächentest von vipirc stehen weiterhin aus.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r48.tar.gz
  sh vipirc-0.7.37-languages-r48/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=de ./vipirc --help

vipirc 0.7.37 [lang-rtl-r48]

doc/UI-LOCALIZATION-r48.txt
HISTORY/README.de.r47.txt

r48: Annehmen / Ablehnen / Übertragung abbrechen.
DCC-Transfers nutzen eine direkte, unverschlüsselte Verbindung und legen dem Gegenüber die ausgewählte Netzwerkadresse offen. Der Empfang eines Angebots allein öffnet weder Datei noch Verbindung.
