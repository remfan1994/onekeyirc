Optionaler Sprachpatch r66 - onekeyirc-0.1.36 (de).
Kommandozeilenübersicht (Optionsnamen bleiben unverändert):
Das bloße Entpacken aktiviert keine Übersetzungen. Vor dem Kompilieren apply.sh auf einer frischen englischen r1-Quelle ausführen. Nicht über ein altes Sprachpaket installieren.

In der Protokollansicht öffnet / den Befehl; normales Tippen öffnet Verfassen.
Dauerhafte Protokollierung für diesen Server nicht verfügbar:

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r66.tar.gz
  sh onekeyirc-0.1.36-languages-r66/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=de ./onekeyirc --version
  ONEKEYIRC_LANG=de ./onekeyirc --help

Erforderliche Versionskennung: onekeyirc 0.1.36 [lang-rtl-r66]
15 neue Schlüssel, insgesamt 1658 pro Sprache; die Anwendungsversion bleibt unverändert. Muttersprachliche Prüfung steht noch aus.

Das gespeicherte Serverprofil wurde nicht gefunden.
Einstellungen gespeichert; Befehlsnutzung konnte nicht gespeichert werden:

Dieses Update übersetzt Netzwerk-/TLS-Fehler und Verbindungszustände; Systemdetails bleiben unverändert.

Neu: sichere Trennmeldungen, SASL-Fehler und verworfene Nachrichten. Protokollbefehle bleiben unverändert.

GPG-Entschlüsselung fehlgeschlagen; Schlüssel außerhalb des Clients entsperren und neu verbinden.

Die Authentifizierung erfordert verifiziertes TLS

Neu: 15 IRC-Ereignismeldungen (JOIN/PART/QUIT/NICK/KICK/MODE/INVITE/TOPIC/CTCP) uebersetzt. Nicknamen und Servertexte bleiben unveraendert.
