Optionales Sprachpaket 1.0-a - vipirc-1.0-a (de)
Oeffentliche Alpha-Version; Sprachpruefung und Plattformtests durch die Community sind willkommen.
Enter: ausführen | Esc: Chat
Entwurf bleibt erhalten; weiter bearbeiten. Nichts gesendet.
2676 Übersetzungsschlüssel pro Sprache; Muttersprachliche Prüfung steht noch aus.

Vor dem Kompilieren apply.sh auf frische englische r1-Quellen anwenden; Pakete nicht überlagern.

  tar -xzf vipirc-1.0-a.tar.gz
  tar -xzf vipirc-languages-1.0-a.tar.gz
  VIPIRC_LANG=de sh vipirc-languages-1.0-a/apply.sh vipirc-1.0-a
  cd vipirc-1.0-a
  VIPIRC_LANG=de ./configure && make
  VIPIRC_LANG=de ./vipirc --version

[lang-rtl-1.0-a]  vipirc 1.0-a
