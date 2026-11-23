Optionales Sprachpaket r52: vipirc-0.7.37

Nur auf unveränderten englischen r1-Quellcode anwenden; nicht über r51 oder andere Sprachpakete installieren.
IRC-Serverhostname oder IP-Adresse ohne URL oder Port eingeben. Für ZNC oder soju /panel verwenden.
none, file (private Textdatei), gpg (verschlüsselte Datei) oder plain (verdeckter Wert in der Konfiguration) wählen.
direct = mit Enter senden; review = Protokoll prüfen und mit erneutem Enter bestätigen. Für alle Server.
Nickname oder Muster nick!user@host.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r52.tar.gz
  sh vipirc-0.7.37-languages-r52/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=de ./vipirc --version

vipirc 0.7.37 [lang-rtl-r52]
doc/SETUP-HELP-r52.txt
HISTORY/README.de.r51.txt
