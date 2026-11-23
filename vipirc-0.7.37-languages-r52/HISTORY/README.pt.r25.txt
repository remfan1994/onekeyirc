r25 inclui rascunhos das 52 secções do manual completo de vipirc. Foram traduzidos os cinco capítulos restantes e criado um manual offline por língua. A ajuda ncurses integrada continua em inglês. Falta revisão nativa.

Código-fonte original em inglês: vipirc-0.7.36-english-source-r1.tar.gz
Pacote de idiomas e patch RTL opcional: vipirc-0.7.36-languages-r25.tar.gz

Extraia os dois arquivos no mesmo diretório pai.
Aplique o patch uma única vez, antes de configurar e compilar:
    sh vipirc-0.7.36-languages-r25/apply.sh vipirc-0.7.36

Compilação: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r25]
Idioma: VIPIRC_LANG=pt

Não instale sobre um patch anterior; use uma extração nova do código-fonte inglês r1.
O código-fonte inglês não foi alterado. MANUAL.txt e SECURITY.md em inglês permanecem as referências principais; os rascunhos traduzidos ainda precisam de revisão por falantes nativos.
Instruções antigas estão em HISTORY/; não execute os comandos de lá.
