Pack de langues facultatif r50: vipirc-0.7.37

Appliquez-le uniquement au code source anglais r1 intact ; jamais par-dessus r49 ou un autre pack.
Le nouveau fichier d'identifiants doit différer des fichiers de configuration et annexes ; utilisez le répertoire privé proposé.
Le chiffrement GPG est en cours ; patientez ou revenez pour l'annuler.
Utilisez l'empreinte complète de la clé GPG locale : 40 ou 64 chiffres hexadécimaux sans espaces.
Saisissez yes pour TLS vérifié ou no pour IRC non chiffré.
La relecture par des personnes maîtrisant les langues et le test ncurses complet de vipirc restent nécessaires.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r50.tar.gz
  sh vipirc-0.7.37-languages-r50/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=fr ./vipirc --version

vipirc 0.7.37 [lang-rtl-r50]
doc/SETUP-VALIDATION-r50.txt
HISTORY/README.fr.r49.txt
