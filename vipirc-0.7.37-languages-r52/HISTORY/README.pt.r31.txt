Use signals começa em no. Ctrl-C e Ctrl-Z cancelam uma mensagem ou comando por enviar, sem terminar nem suspender a aplicação. Selecione yes no painel 0 ou nas definições Interface do vipirc para ativar os sinais do terminal. Apply aplica imediatamente; Save também guarda [input] use_signals=true|false. As definições originais do terminal são repostas ao sair ou suspender.

vipirc 0.7.37: optional language + Arabic RTL patch r31
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch archive: vipirc-0.7.37-languages-r31.tar.gz

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r31.tar.gz
    sh vipirc-0.7.37-languages-r31/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37 && ./configure && make
    ./vipirc --version

Expected: vipirc 0.7.37 [lang-rtl-r31]
Language: VIPIRC_LANG=pt
See doc/USE-SIGNALS-pt.txt and README.txt.
Do not apply over r27 or older application versions.

Existem agora rascunhos traduzidos de sete das doze/treze secções de segurança. O inglês continua a ser a referência oficial; falta revisão por falantes especializados.
See doc/SECURITY-FULL-pt.txt and doc/SECURITY-COVERAGE-r31.tsv.

Security documentation r31: full section coverage as unreviewed drafts.
See doc/SECURITY-FULL-XX.txt (XX = locale), and use original English SECURITY.md as authority.
RASCUNHO DE TRADUÇÃO. Falta revisão por falantes fluentes com conhecimentos de segurança. O SECURITY.md inglês continua a ser a referência autorizada.
