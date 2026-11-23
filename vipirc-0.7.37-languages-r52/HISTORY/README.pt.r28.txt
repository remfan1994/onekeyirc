Use signals começa em no. Ctrl-C e Ctrl-Z cancelam uma mensagem ou comando por enviar, sem terminar nem suspender a aplicação. Selecione yes no painel 0 ou nas definições Interface do vipirc para ativar os sinais do terminal. Apply aplica imediatamente; Save também guarda [input] use_signals=true|false. As definições originais do terminal são repostas ao sair ou suspender.

vipirc 0.7.37: optional language + Arabic RTL patch r28
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch archive: vipirc-0.7.37-languages-r28.tar.gz

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r28.tar.gz
    sh vipirc-0.7.37-languages-r28/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37 && ./configure && make
    ./vipirc --version

Expected: vipirc 0.7.37 [lang-rtl-r28]
Language: VIPIRC_LANG=pt
See doc/USE-SIGNALS-pt.txt and README.txt.
Do not apply over r27 or older application versions.
