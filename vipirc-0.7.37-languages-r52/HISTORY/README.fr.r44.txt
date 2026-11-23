Pack de langues facultatif r44: vipirc-0.7.37

Appliquez-le uniquement au code source anglais r1 intact ; jamais par-dessus r43 ou un autre pack.
Ces listes montrent les vues déjà ouvertes du serveur choisi. Up/Down choisit ; Left/Right sélectionne Open, New, Part ou Close ; Enter exécute. Le canal 0 est le serveur. /cl et /channels affichent les vues locales ; /list [MASK] [OPTIONS] ouvre le répertoire réseau ; /ql montre les conversations privées.
La relecture par des personnes maîtrisant les langues et le test ncurses complet de vipirc restent nécessaires.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r44.tar.gz
  sh vipirc-0.7.37-languages-r44/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=fr ./vipirc --help

vipirc 0.7.37 [lang-rtl-r44]

doc/UI-LOCALIZATION-r44.txt
HISTORY/README.fr.r43.txt
