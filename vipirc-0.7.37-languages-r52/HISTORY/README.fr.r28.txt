Use signals est réglé sur no par défaut. Ctrl-C et Ctrl-Z annulent un message ou une commande non envoyés sans quitter ni suspendre le programme. Choisissez yes dans le panneau 0 ou les paramètres Interface de vipirc pour activer les signaux du terminal. Apply agit immédiatement ; Save enregistre aussi [input] use_signals=true|false. Les paramètres initiaux du terminal sont rétablis à la sortie ou lors de la suspension.

vipirc 0.7.37: optional language + Arabic RTL patch r28
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch archive: vipirc-0.7.37-languages-r28.tar.gz

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r28.tar.gz
    sh vipirc-0.7.37-languages-r28/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37 && ./configure && make
    ./vipirc --version

Expected: vipirc 0.7.37 [lang-rtl-r28]
Language: VIPIRC_LANG=fr
See doc/USE-SIGNALS-fr.txt and README.txt.
Do not apply over r27 or older application versions.
