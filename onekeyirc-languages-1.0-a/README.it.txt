Pacchetto lingua facoltativo 1.0-a - onekeyirc-1.0-a (it)
Versione alfa pubblica; sono benvenute le revisioni delle traduzioni e le prove della comunità.
Resoconto /summary
È necessario un terminale interattivo compatibile con ANSI/VT
2195 chiavi di traduzione per lingua; Revisione di madrelingua ancora da eseguire.

Eseguire apply.sh su sorgenti inglesi r1 puliti prima della compilazione; non sovrapporre i pacchetti.

  tar -xzf onekeyirc-1.0-a.tar.gz
  tar -xzf onekeyirc-languages-1.0-a.tar.gz
  ONEKEYIRC_LANG=it sh onekeyirc-languages-1.0-a/apply.sh onekeyirc-1.0-a
  cd onekeyirc-1.0-a
  ONEKEYIRC_LANG=it ./configure && make
  ONEKEYIRC_LANG=it ./onekeyirc --version

[lang-rtl-1.0-a]  onekeyirc 1.0-a
