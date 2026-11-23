Optionales Sprachpaket r50: vipirc-0.7.37

Nur auf unveränderten englischen r1-Quellcode anwenden; nicht über r49 oder andere Sprachpakete installieren.
Neue Zugangsdaten-Datei darf keine Konfigurations- oder Nebendatei sein; vorgeschlagenen Zugangsdatenordner wählen.
GPG-Verschlüsselung läuft noch; warten oder zurückgehen und abbrechen.
Vollständigen lokalen GPG-Fingerabdruck eingeben: 40 oder 64 Hexadezimalziffern ohne Leerzeichen.
yes für verifiziertes TLS, no für unverschlüsseltes IRC eingeben.
Sprachprüfung und vollständiger ncurses-Oberflächentest von vipirc stehen weiterhin aus.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r50.tar.gz
  sh vipirc-0.7.37-languages-r50/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=de ./vipirc --version

vipirc 0.7.37 [lang-rtl-r50]
doc/SETUP-VALIDATION-r50.txt
HISTORY/README.de.r49.txt
