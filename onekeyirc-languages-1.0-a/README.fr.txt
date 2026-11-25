Correctif linguistique facultatif 1.0-a - onekeyirc-1.0-a (fr)
Version alpha publique ; les relectures et les tests de la communauté sont les bienvenus.
Rapport /summary
Un terminal interactif compatible ANSI/VT est requis
2195 clés de traduction par langue ; Relecture par des locuteurs natifs en attente.

Exécutez apply.sh sur la source anglaise r1 intacte avant compilation ; ne superposez pas les correctifs.

  tar -xzf onekeyirc-1.0-a.tar.gz
  tar -xzf onekeyirc-languages-1.0-a.tar.gz
  ONEKEYIRC_LANG=fr sh onekeyirc-languages-1.0-a/apply.sh onekeyirc-1.0-a
  cd onekeyirc-1.0-a
  ONEKEYIRC_LANG=fr ./configure && make
  ONEKEYIRC_LANG=fr ./onekeyirc --version

[lang-rtl-1.0-a]  onekeyirc 1.0-a
