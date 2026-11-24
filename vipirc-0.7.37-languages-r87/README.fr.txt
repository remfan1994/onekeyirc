Correctif linguistique facultatif r87 - vipirc-0.7.37 (fr)
Enter : exécuter | Esc : discussion
Brouillon conservé ; continuez la modification. Rien n'a été envoyé.
2437 keys / 20 new; Relecture par des locuteurs natifs en attente.

Exécutez apply.sh sur la source anglaise r1 intacte avant compilation ; ne superposez pas les correctifs.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r87.tar.gz
  sh vipirc-0.7.37-languages-r87/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=fr ./vipirc --version

[lang-rtl-r87]  vipirc 0.7.37
