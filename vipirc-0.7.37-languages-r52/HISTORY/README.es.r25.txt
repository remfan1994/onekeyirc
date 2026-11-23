r25 incluye borradores de las 52 secciones del manual completo de vipirc. Se tradujeron los cinco capítulos restantes y se ensambló un manual sin conexión por idioma. La ayuda ncurses interna sigue en inglés. Falta revisión nativa.

Código fuente original en inglés: vipirc-0.7.36-english-source-r1.tar.gz
Paquete de idiomas y parche RTL opcional: vipirc-0.7.36-languages-r25.tar.gz

Extraiga ambos archivos en el mismo directorio principal.
Aplique el parche una sola vez antes de configurar y compilar:
    sh vipirc-0.7.36-languages-r25/apply.sh vipirc-0.7.36

Compilación: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r25]
Idioma: VIPIRC_LANG=es

No lo instale sobre un parche anterior; utilice una extracción nueva del código inglés r1.
El código fuente inglés no cambia. MANUAL.txt y SECURITY.md en inglés siguen siendo la referencia principal; las traducciones requieren revisión por hablantes nativos.
Las instrucciones anteriores están en HISTORY/; no ejecute sus comandos.
