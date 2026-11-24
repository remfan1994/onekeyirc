Pacchetto lingua facoltativo r85 - onekeyirc-0.1.36 (it)
Resoconto /summary
È necessario un terminale interattivo compatibile con ANSI/VT
2010 keys / 2 new; Revisione di madrelingua ancora da eseguire.

Eseguire apply.sh su sorgenti inglesi r1 puliti prima della compilazione; non sovrapporre i pacchetti.

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r85.tar.gz
  sh onekeyirc-0.1.36-languages-r85/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=it ./onekeyirc --version

[lang-rtl-r85]  onekeyirc 0.1.36
