Pacchetto lingua facoltativo 1.0-a - vipirc-1.0-a (it)
Versione alfa pubblica; sono benvenute le revisioni delle traduzioni e le prove della comunità.
Enter: esegui | Esc: chat
Bozza conservata; continua a modificare. Nulla è stato inviato.
2676 chiavi di traduzione per lingua; Revisione di madrelingua ancora da eseguire.

Eseguire apply.sh su sorgenti inglesi r1 puliti prima della compilazione; non sovrapporre i pacchetti.

  tar -xzf vipirc-1.0-a.tar.gz
  tar -xzf vipirc-languages-1.0-a.tar.gz
  VIPIRC_LANG=it sh vipirc-languages-1.0-a/apply.sh vipirc-1.0-a
  cd vipirc-1.0-a
  VIPIRC_LANG=it ./configure && make
  VIPIRC_LANG=it ./vipirc --version

[lang-rtl-1.0-a]  vipirc 1.0-a
