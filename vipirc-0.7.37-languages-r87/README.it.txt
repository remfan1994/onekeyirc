Pacchetto lingua facoltativo r87 - vipirc-0.7.37 (it)
Enter: esegui | Esc: chat
Bozza conservata; continua a modificare. Nulla è stato inviato.
2437 keys / 20 new; Revisione di madrelingua ancora da eseguire.

Eseguire apply.sh su sorgenti inglesi r1 puliti prima della compilazione; non sovrapporre i pacchetti.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r87.tar.gz
  sh vipirc-0.7.37-languages-r87/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=it ./vipirc --version

[lang-rtl-r87]  vipirc 0.7.37
