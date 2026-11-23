Instrucciones del paquete r26

Este paquete opcional es independiente del código fuente inglés. El lector ncurses y --manual muestran el borrador elegido por VIPIRC_LANG si el archivo existe y es seguro; de lo contrario, se usa el manual inglés integrado. RTL no cambia. Sigue pendiente la revisión de hablantes nativos.

vipirc-0.7.36-english-source-r1.tar.gz
vipirc-0.7.36-languages-r26.tar.gz

    tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.36-languages-r26.tar.gz
    sh vipirc-0.7.36-languages-r26/apply.sh vipirc-0.7.36
    cd vipirc-0.7.36 && ./configure && make
    VIPIRC_LANG=es ./vipirc --manual

--version: vipirc 0.7.36 [lang-rtl-r26]
VIPIRC_MANUAL_DIR=/absolute/path (optional manual directory override)

Aplíquelo sobre una copia nueva del código inglés r1, nunca sobre otro parche.
MANUAL.txt and SECURITY.md in English remain authoritative.
Historical instructions are archived under HISTORY/ and must not be used to install r26.
