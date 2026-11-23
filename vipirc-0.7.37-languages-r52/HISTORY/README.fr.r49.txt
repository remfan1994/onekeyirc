Pack de langues facultatif r49: vipirc-0.7.37

Appliquez-le uniquement au code source anglais r1 intact ; jamais par-dessus r48 ou un autre pack.
Consulter les chemins des identifiants ou préparer l'authentification
Ouvrir la liste des canaux de ce serveur
Ouvrir les paramètres de connexion du serveur sélectionné
yes = connexion chiffrée et vérifiée ; no = IRC non chiffré.
La relecture par des personnes maîtrisant les langues et le test ncurses complet de vipirc restent nécessaires.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r49.tar.gz
  sh vipirc-0.7.37-languages-r49/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=fr ./vipirc --version

vipirc 0.7.37 [lang-rtl-r49]
doc/COMMAND-LOCALIZATION-r49.txt
HISTORY/README.fr.r48.txt
