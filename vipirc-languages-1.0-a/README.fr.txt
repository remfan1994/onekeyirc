Correctif linguistique facultatif 1.0-a - vipirc-1.0-a (fr)
Version alpha publique ; les relectures et les tests de la communauté sont les bienvenus.
Enter : exécuter | Esc : discussion
Brouillon conservé ; continuez la modification. Rien n'a été envoyé.
2676 clés de traduction par langue ; Relecture par des locuteurs natifs en attente.

Exécutez apply.sh sur la source anglaise r1 intacte avant compilation ; ne superposez pas les correctifs.

  tar -xzf vipirc-1.0-a.tar.gz
  tar -xzf vipirc-languages-1.0-a.tar.gz
  VIPIRC_LANG=fr sh vipirc-languages-1.0-a/apply.sh vipirc-1.0-a
  cd vipirc-1.0-a
  VIPIRC_LANG=fr ./configure && make
  VIPIRC_LANG=fr ./vipirc --version

[lang-rtl-1.0-a]  vipirc 1.0-a
