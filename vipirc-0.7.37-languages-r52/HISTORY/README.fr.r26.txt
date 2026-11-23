Instructions du paquet r26

Ce paquet facultatif est séparé du code source anglais. Le lecteur ncurses et --manual affichent le brouillon choisi par VIPIRC_LANG si le fichier est présent et sûr ; sinon, le manuel anglais intégré est utilisé. RTL reste inchangé. Une relecture par des locuteurs natifs reste nécessaire.

vipirc-0.7.36-english-source-r1.tar.gz
vipirc-0.7.36-languages-r26.tar.gz

    tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.36-languages-r26.tar.gz
    sh vipirc-0.7.36-languages-r26/apply.sh vipirc-0.7.36
    cd vipirc-0.7.36 && ./configure && make
    VIPIRC_LANG=fr ./vipirc --manual

--version: vipirc 0.7.36 [lang-rtl-r26]
VIPIRC_MANUAL_DIR=/absolute/path (optional manual directory override)

Appliquez-le à une extraction neuve du code anglais r1, jamais sur un ancien patch.
MANUAL.txt and SECURITY.md in English remain authoritative.
Historical instructions are archived under HISTORY/ and must not be used to install r26.
