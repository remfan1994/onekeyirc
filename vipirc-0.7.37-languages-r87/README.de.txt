Optionales Sprachpaket r87 - vipirc-0.7.37 (de)
Enter: ausführen | Esc: Chat
Entwurf bleibt erhalten; weiter bearbeiten. Nichts gesendet.
2437 keys / 20 new; Muttersprachliche Prüfung steht noch aus.

Vor dem Kompilieren apply.sh auf frische englische r1-Quellen anwenden; Pakete nicht überlagern.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r87.tar.gz
  sh vipirc-0.7.37-languages-r87/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=de ./vipirc --version

[lang-rtl-r87]  vipirc 0.7.37
