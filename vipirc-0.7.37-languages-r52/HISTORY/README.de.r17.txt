r17: Die vipirc-Abschnitte zu Hintergrundbetrieb, SSH-Wiederherstellung, Sitzungsschließung und Worker-Updates wurden vollständig übersetzt. Siehe doc/SESSION-MANAGEMENT-de.txt. Das vollständige ncurses-Handbuch und SECURITY.md sind noch nicht vollständig übersetzt; RTL blieb unverändert.
--version: vipirc 0.7.36 [lang-rtl-r17]
Apply: sh vipirc-0.7.36-languages-r17/apply.sh vipirc-0.7.36

=== r16 and earlier notes (historical) ===
r16: Übersetzte Einstiegsauszüge für die Vollbildoberfläche (nur vipirc) und erweiterte Erläuterungen des Sicherheitsmodells. MANUAL.txt und SECURITY.md sind weiterhin nicht vollständig übersetzt. Die RTL-Funktion bleibt gegenüber r15 unverändert.
doc/NCURSES-START-de.txt
doc/SECURITY-BOUNDARIES-de.txt
--version: vipirc 0.7.36 [lang-rtl-r16]
Aplicar/Apply: sh vipirc-0.7.36-languages-r16/apply.sh vipirc-0.7.36

=== Prior r15 and earlier notes (historical, not current installation instructions) ===
OPTIONAL LANGUAGE + RTL PATCH r15 (de)

r15 trennt die englischen Originalquellen weiterhin vom optionalen Patch für zehn Sprachen und arabisches RTL. Die Sicherheitsprüfung umfasst Unicode-Richtungssteuerzeichen, SHA-256-Prüfung und das Ablehnen symbolischer Links. Nur auf frisch entpackte r1-Quellen vor dem Kompilieren anwenden. Siehe doc/LOCALIZATION-SECURITY-de.txt; --version zeigt [lang-rtl-r15].

-- Earlier release history --
vipirc 0.7.36 - Sprachpaket r14 (de)
========================================================

--- r14 ---
r14 haelt die urspruenglichen englischen Quellen getrennt vom optionalen Paket mit Uebersetzungen UND arabischem RTL-Patch. Nach dem Anwenden vor dem Kompilieren zeigt --version den Zusatz [lang-rtl-r14]. Die Basisversion bleibt unveraendert. Nur auf frisch entpackte englische r1-Quellen anwenden.


--- r12 ---
r12 verfeinert die optionale arabische Darstellung. Bei jedem Neuzeichnen nutzt der Editor ein gemeinsames Layout für Text und Cursor. Dadurch bleiben beide auch beim Rückfall auf die logische Darstellung nach einem Speicherfehler aufeinander abgestimmt. Im visuellen ncurses-Modus bleiben kombinierende Zeichen in der rechten unteren Terminalzelle beim Grundbuchstaben. Vertikale Bewegung über die erste oder letzte Zeile hinaus erhält die Einfügeposition. Wiederverwendete Absatzlayouts verringern den Aufwand beim Scrollen. Vorabprüfungen überspringen unnötige oder nicht verfügbare Umwandlungen und erhalten die logische Darstellung. Entwürfe, Nachrichten und Protokolle bleiben UTF-8 in logischer Reihenfolge. Der Übersetzungsumfang bleibt unverändert; das vollständige ncurses-Handbuch und SECURITY.md bleiben englisch. Tests mit echten OpenBSD/Crostini-Terminals und Schriftarten sowie eine fachkundige arabische Sprachprüfung stehen weiterhin aus. r12 nur auf frisch entpackte English-r1-Quellen anwenden, niemals über r11.


--- r11 ---
r11 ergänzt die optionale absatzweise kontextabhängige Formung und den Zeilenumbruch arabischer Schrift sowie visuelle Texteingabe, Cursorbewegung und Positionierung per Maus in onekeyirc, vipirc -i und vipirc --ncurses. Gespeicherte Texte, gesendete Nachrichten und Protokolle bleiben UTF-8 in logischer Reihenfolge. Voraussetzung sind die Systembibliothek FriBidi und eine UTF-8-Locale. Aktivieren Sie ONEKEYIRC_ARABIC_VISUAL=1 oder VIPIRC_ARABIC_VISUAL=1 unabhängig von der Oberflächensprache. Lassen Sie dies ausgeschaltet, wenn Ihr Terminal bidirektionalen Text bereits anordnet. Home/End führen zum Dokumentanfang beziehungsweise Dokumentende; Backspace/Delete bearbeiten logische Zeichengruppen. Prüfungen der Schriftdarstellung auf echten OpenBSD/Crostini-Terminals und die muttersprachliche Prüfung des Arabischen stehen noch aus. Alle 844 Katalogeinträge je Sprache und die vollständigen minimalistischen Handbücher bleiben erhalten. Das vollständige ncurses-Handbuch und SECURITY.md sind weiterhin Englisch. Die Anwendungsversionen ändern sich nicht. Wenden Sie r11 ausschließlich auf frisch entpackte Quellen von English r1 an, niemals auf bereits mit r10 gepatchte Quellen.


--- r10 ---
r10 übersetzt die erzeugte clientspezifische Hilfe und liefert vollständige übersetzte Handbücher der minimalistischen Oberfläche unter doc/MINIMAL-MANUAL-de.txt. onekeyirc --manual und vipirc -i --manual beachten die Anzeigesprache und geben logisches UTF-8 ohne erforderliches Terminal aus. Das vollständige vipirc-ncurses-Handbuch, SECURITY.md, verbleibende Oberflächen-/Diagnosemeldungen und der Rechts-nach-links-Editor sind noch nicht fertig. r10 nur auf frisch entpackte englische Quellen r1 anwenden, niemals über r9.


--- r9 ---
Aktueller Stand r9: Alle 474 übersetzbaren Zeilen der festen Befehls-/Themenhilfetabellen sind übersetzt. Drei reine Befehls-Aliaszuordnungen bleiben wörtlich; alle 477 Tabellenzeilen haben Katalogeinträge. Nach dem Anwenden siehe doc/HELP-REFERENCE-de.txt. Das vollständige MANUAL.txt, SECURITY.md sowie einige erzeugte/clientabhängige Hilfen und Diagnosemeldungen bleiben englisch. Die Übersetzungen benötigen weiterhin muttersprachliche Prüfung. r9 nur auf frisch entpackte englische Quellen r1 anwenden, niemals über r8 oder ein anderes Paket.


Dieses Paket enthält Übersetzungen der Oberfläche, der Hilfe und einiger Dokumente. Es ist kein eigenständiges Programm.

Verwenden Sie nur das unten genannte English-r1-Quellarchiv. Ein älteres Archiv mit derselben Programmversion oder bereits veränderter Quellcode ist nicht kompatibel.
  vipirc-0.7.36-english-source-r1.tar.gz

Entpacken Sie beide Archive in dasselbe übergeordnete Verzeichnis:
  tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.36-languages-r15.tar.gz

Wenden Sie das Paket VOR configure oder make auf den unveränderten English-r1-Quellcode an:
  sh vipirc-0.7.36-languages-r15/apply.sh vipirc-0.7.36

Konfigurieren und kompilieren Sie das Programm:
  cd vipirc-0.7.36
  ./configure --prefix=/usr/local
  make

Starten Sie das Programm mit deutscher Oberfläche:
  VIPIRC_LANG=de ./vipirc

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

VIPIRC_LANG=ar VIPIRC_ARABIC_VISUAL=1 ./vipirc -i
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-de.txt
