Correctif linguistique facultatif r85 - onekeyirc-0.1.36 (fr)
Rapport /summary
Un terminal interactif compatible ANSI/VT est requis
2010 keys / 2 new; Relecture par des locuteurs natifs en attente.

Exécutez apply.sh sur la source anglaise r1 intacte avant compilation ; ne superposez pas les correctifs.

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r85.tar.gz
  sh onekeyirc-0.1.36-languages-r85/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=fr ./onekeyirc --version

[lang-rtl-r85]  onekeyirc 0.1.36
