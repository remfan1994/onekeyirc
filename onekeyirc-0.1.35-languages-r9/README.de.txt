onekeyirc 0.1.35 - Sprachpaket r9 (de)
========================================================

--- r9 ---
Aktueller Stand r9: Alle 474 übersetzbaren Zeilen der festen Befehls-/Themenhilfetabellen sind übersetzt. Drei reine Befehls-Aliaszuordnungen bleiben wörtlich; alle 477 Tabellenzeilen haben Katalogeinträge. Nach dem Anwenden siehe doc/HELP-REFERENCE-de.txt. Das vollständige MANUAL.txt, SECURITY.md sowie einige erzeugte/clientabhängige Hilfen und Diagnosemeldungen bleiben englisch. Die Übersetzungen benötigen weiterhin muttersprachliche Prüfung. r9 nur auf frisch entpackte englische Quellen r1 anwenden, niemals über r8 oder ein anderes Paket.


Dieses Paket enthält Übersetzungen der Oberfläche, der Hilfe und einiger Dokumente. Es ist kein eigenständiges Programm.

Verwenden Sie nur das unten genannte English-r1-Quellarchiv. Ein älteres Archiv mit derselben Programmversion oder bereits veränderter Quellcode ist nicht kompatibel.
  onekeyirc-0.1.35-english-source-r1.tar.gz

Entpacken Sie beide Archive in dasselbe übergeordnete Verzeichnis:
  tar -xzf onekeyirc-0.1.35-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.35-languages-r9.tar.gz

Wenden Sie das Paket VOR configure oder make auf den unveränderten English-r1-Quellcode an:
  sh onekeyirc-0.1.35-languages-r9/apply.sh onekeyirc-0.1.35

Konfigurieren und kompilieren Sie das Programm:
  cd onekeyirc-0.1.35
  ./configure --prefix=/usr/local
  make

Starten Sie das Programm mit deutscher Oberfläche:
  ONEKEYIRC_LANG=de ./onekeyirc

IRC-Befehle, Kommandozeilenoptionen und Konfigurationsschlüssel werden nicht übersetzt. Geben Sie sie genau wie gezeigt ein.

Die Übersetzungen sind noch Entwürfe; Teile der Hilfe und die vollständigen Handbücher bleiben auf Englisch. Für verbindliche Sicherheitshinweise lesen Sie MANUAL.txt und SECURITY.md auf Englisch. Verwenden Sie ein UTF-8-Terminal.

Neu in r4: /help background, /help catchup und /help logging sind vollständig übersetzt. RECOVERY-de.txt erklärt Sitzungswiederherstellung und Protokollgrenzen.

PACK-SHA256SUMS enthält die Prüfsummen. apply.sh prüft die Kompatibilität der Quellen. Kopieren Sie keine Dateien manuell über veränderten Quellcode.


PACK-SHA256SUMS / README.txt / MANUAL.txt / SECURITY.md

Neu in r5: /help autocommands, /help dcc, /help bindings, /help bind, /help unbind, /help transfers und /help hotkeys sind vollstaendig. Siehe doc/WORKFLOWS-de.txt.

Neu in r6: /help configuration, /help summary, /help server, /help users und /help watch sind vollständig. Siehe doc/CONFIGURATION-de.txt und doc/STATUS-de.txt nach der Installation.

Neu in r7: 32 weitere Hilfeerklärungen zu /help set, /help list, lokaler Kanalnavigation und Aliasnamen. Nach Installation doc/SETTINGS-de.txt und doc/CHANNELS-de.txt lesen. Abdeckung: 384/477; 93 Erklärungen bleiben englisch.

--- r8 ---
Die visuelle arabische Anzeige ist experimentell und gilt nur für die minimalistische Oberfläche. Installieren Sie die systemweite FriBidi-Bibliothek und aktivieren Sie die Variable unten. Bei Terminals mit eigener Arabisch-Darstellung nicht aktivieren. Editor, gespeicherte Texte und vollständiges ncurses bleiben unverändert. Ohne FriBidi bleibt die Unicode-Reihenfolge erhalten.

ONEKEYIRC_LANG=ar ONEKEYIRC_ARABIC_VISUAL=1 ./onekeyirc
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-de.txt
