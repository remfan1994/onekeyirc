Pack de langues facultatif r51: vipirc-0.7.37

Appliquez-le uniquement au code source anglais r1 intact ; jamais par-dessus r50 ou un autre pack.
Choisissez un des numéros de panneau affichés.
Saisissez le nom d'hôte ou l'adresse IP du serveur IRC.
Saisissez direct (un Enter) ou review (deux Enter).
La création de fichiers GPG est désactivée en --test-mode ; aucun assistant, keyring ou fichier n'a été consulté.
La relecture par des personnes maîtrisant les langues et le test ncurses complet de vipirc restent nécessaires.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r51.tar.gz
  sh vipirc-0.7.37-languages-r51/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=fr ./vipirc --version

vipirc 0.7.37 [lang-rtl-r51]
doc/SETUP-ERRORS-r51.txt
HISTORY/README.fr.r50.txt
