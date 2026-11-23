Optionales Sprachpaket r49: vipirc-0.7.37

Nur auf unveränderten englischen r1-Quellcode anwenden; nicht über r48 oder andere Sprachpakete installieren.
Speicherorte der Zugangsdaten prüfen oder Authentifizierung vorbereiten
Kanalliste dieses Servers öffnen
Verbindungseinstellungen des gewählten Servers öffnen
yes = verifizierte verschlüsselte Verbindung; no = unverschlüsseltes IRC.
Sprachprüfung und vollständiger ncurses-Oberflächentest von vipirc stehen weiterhin aus.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r49.tar.gz
  sh vipirc-0.7.37-languages-r49/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=de ./vipirc --version

vipirc 0.7.37 [lang-rtl-r49]
doc/COMMAND-LOCALIZATION-r49.txt
HISTORY/README.de.r48.txt
