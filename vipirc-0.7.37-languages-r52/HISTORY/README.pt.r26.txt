Instruções do pacote r26

Este pacote opcional é separado do código-fonte inglês. O leitor ncurses e --manual mostram o rascunho escolhido por VIPIRC_LANG se o ficheiro estiver disponível e for seguro; caso contrário, utilizam o manual inglês incorporado. RTL não mudou. Ainda é necessária revisão por falantes nativos.

vipirc-0.7.36-english-source-r1.tar.gz
vipirc-0.7.36-languages-r26.tar.gz

    tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.36-languages-r26.tar.gz
    sh vipirc-0.7.36-languages-r26/apply.sh vipirc-0.7.36
    cd vipirc-0.7.36 && ./configure && make
    VIPIRC_LANG=pt ./vipirc --manual

--version: vipirc 0.7.36 [lang-rtl-r26]
VIPIRC_MANUAL_DIR=/absolute/path (optional manual directory override)

Aplique a uma cópia nova do código inglês r1, nunca sobre um patch anterior.
MANUAL.txt and SECURITY.md in English remain authoritative.
Historical instructions are archived under HISTORY/ and must not be used to install r26.
