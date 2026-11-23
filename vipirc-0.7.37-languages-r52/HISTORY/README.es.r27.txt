Se han traducido cuatro secciones de SECURITY.md como borradores. El documento de seguridad en inglés sigue siendo la referencia autorizada. Compare los comandos y límites con el original; todavía se necesita una revisión por hablantes competentes.

vipirc 0.7.36: optional language + RTL patch r27
English source: vipirc-0.7.36-english-source-r1.tar.gz
Patch: vipirc-0.7.36-languages-r27.tar.gz

    tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.36-languages-r27.tar.gz
    sh vipirc-0.7.36-languages-r27/apply.sh vipirc-0.7.36
    cd vipirc-0.7.36 && ./configure && make
    ./vipirc --version    # vipirc 0.7.36 [lang-rtl-r27]

Language: VIPIRC_LANG=es
Draft security chapters: doc/SECURITY-PARTIAL-es.txt
Apply only to a fresh English r1 source tree, not over an earlier patch.
English SECURITY.md remains authoritative. Earlier instructions: HISTORY/.
