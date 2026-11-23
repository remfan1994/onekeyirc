Use signals está desactivado (no) por defecto. Ctrl-C y Ctrl-Z cancelan un mensaje o comando sin enviar; no cierran ni suspenden la aplicación. Seleccione yes en el panel 0 o en Interface de vipirc para activar las señales del terminal. Apply surte efecto inmediatamente; Save también guarda [input] use_signals=true|false. Al salir o suspenderse, se restauran los ajustes originales del terminal.

vipirc 0.7.37: optional language + Arabic RTL patch r30
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch archive: vipirc-0.7.37-languages-r30.tar.gz

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r30.tar.gz
    sh vipirc-0.7.37-languages-r30/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37 && ./configure && make
    ./vipirc --version

Expected: vipirc 0.7.37 [lang-rtl-r30]
Language: VIPIRC_LANG=es
See doc/USE-SIGNALS-es.txt and README.txt.
Do not apply over r27 or older application versions.

Ahora hay borradores traducidos de siete de las doce/trece secciones de seguridad. El inglés sigue siendo la referencia autorizada; falta revisión por expertos nativos.
See doc/SECURITY-PARTIAL-es.txt and doc/SECURITY-COVERAGE-r30.tsv.
