Optionales Sprachpaket r85 - onekeyirc-0.1.36 (de)
/summary-Bericht
Ein interaktives ANSI/VT-kompatibles Terminal ist erforderlich
2010 keys / 2 new; Muttersprachliche Prüfung steht noch aus.

Vor dem Kompilieren apply.sh auf frische englische r1-Quellen anwenden; Pakete nicht überlagern.

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r85.tar.gz
  sh onekeyirc-0.1.36-languages-r85/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=de ./onekeyirc --version

[lang-rtl-r85]  onekeyirc 0.1.36
