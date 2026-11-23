Pack de langues facultatif r43: vipirc-0.7.37

Appliquez-le uniquement au code source anglais r1 intact ; jamais par-dessus r42 ou un autre pack.
Nouvelles traductions : état GPG, liste des canaux, aide des paramètres et des transferts DCC, lecteur de réponses.
La relecture par des personnes maîtrisant les langues et le test ncurses complet de vipirc restent nécessaires.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r43.tar.gz
  sh vipirc-0.7.37-languages-r43/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=fr ./vipirc --help

vipirc 0.7.37 [lang-rtl-r43]

doc/UI-LOCALIZATION-r43.txt
HISTORY/README.fr.r42.txt
