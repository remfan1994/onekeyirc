r23 ajoute les traductions intégrales des chapitres SETTINGS et FILES AND ENVIRONMENT du manuel vipirc. Les brouillons couvrent 45 sections sur 52 ; une révision par des locuteurs natifs reste nécessaire.

Code source anglais d'origine: vipirc-0.7.36-english-source-r1.tar.gz
Pack de langues et correctif RTL facultatif: vipirc-0.7.36-languages-r23.tar.gz

Extrayez les deux archives dans le même répertoire parent.
Appliquez le correctif une seule fois, avant configuration et compilation :
    sh vipirc-0.7.36-languages-r23/apply.sh vipirc-0.7.36

Compilation : cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r23]
Langue : VIPIRC_LANG=fr

Ne l'appliquez pas sur un ancien correctif ; repartez d'une extraction neuve du code anglais r1.
Le code source anglais est inchangé. Les fichiers MANUAL.txt et SECURITY.md anglais restent les références officielles ; les traductions provisoires nécessitent une relecture de locuteurs natifs.
Les anciennes instructions sont conservées dans HISTORY/ ; n'exécutez pas leurs commandes.
