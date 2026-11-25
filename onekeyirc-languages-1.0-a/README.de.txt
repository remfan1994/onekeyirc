Optionales Sprachpaket 1.0-a - onekeyirc-1.0-a (de)
Oeffentliche Alpha-Version; Sprachpruefung und Plattformtests durch die Community sind willkommen.
/summary-Bericht
Ein interaktives ANSI/VT-kompatibles Terminal ist erforderlich
2195 Übersetzungsschlüssel pro Sprache; Muttersprachliche Prüfung steht noch aus.

Vor dem Kompilieren apply.sh auf frische englische r1-Quellen anwenden; Pakete nicht überlagern.

  tar -xzf onekeyirc-1.0-a.tar.gz
  tar -xzf onekeyirc-languages-1.0-a.tar.gz
  ONEKEYIRC_LANG=de sh onekeyirc-languages-1.0-a/apply.sh onekeyirc-1.0-a
  cd onekeyirc-1.0-a
  ONEKEYIRC_LANG=de ./configure && make
  ONEKEYIRC_LANG=de ./onekeyirc --version

[lang-rtl-1.0-a]  onekeyirc 1.0-a
