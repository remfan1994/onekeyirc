r25 comprend les brouillons des 52 sections du manuel complet vipirc. Les cinq derniers chapitres sont traduits, avec un manuel hors ligne assemblé par langue. L’aide ncurses intégrée reste en anglais. Relecture native nécessaire.

Code source anglais d'origine: vipirc-0.7.36-english-source-r1.tar.gz
Pack de langues et correctif RTL facultatif: vipirc-0.7.36-languages-r25.tar.gz

Extrayez les deux archives dans le même répertoire parent.
Appliquez le correctif une seule fois, avant configuration et compilation :
    sh vipirc-0.7.36-languages-r25/apply.sh vipirc-0.7.36

Compilation : cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r25]
Langue : VIPIRC_LANG=fr

Ne l'appliquez pas sur un ancien correctif ; repartez d'une extraction neuve du code anglais r1.
Le code source anglais est inchangé. Les fichiers MANUAL.txt et SECURITY.md anglais restent les références officielles ; les traductions provisoires nécessitent une relecture de locuteurs natifs.
Les anciennes instructions sont conservées dans HISTORY/ ; n'exécutez pas leurs commandes.
