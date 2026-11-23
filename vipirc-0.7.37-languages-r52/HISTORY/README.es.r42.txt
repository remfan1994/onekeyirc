Paquete opcional r42 de idiomas y visualización árabe de derecha a izquierda.
vipirc 0.7.37 - r42

Úsalo solo con el código fuente inglés r1 original, sin modificar. No lo instales sobre r41 ni paquetes anteriores. La versión de la aplicación no cambia.

vipirc: se tradujeron más advertencias, errores de conexión, entrada UTF-8, DCC y textos de pantalla.

tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
tar -xzf vipirc-0.7.37-languages-r42.tar.gz
sh vipirc-0.7.37-languages-r42/apply.sh vipirc-0.7.37
cd vipirc-0.7.37
./configure && make
./vipirc --version

VIPIRC_LANG=es ./vipirc --help
vipirc 0.7.37 [lang-rtl-r42]

Comandos IRC, opciones, valores de configuración, usuarios, respuestas del servidor y datos de red permanecen literales. Falta revisión de hablantes competentes; no se verificó la compilación ncurses completa de vipirc.

doc/UI-LOCALIZATION-r42.txt
doc/CLI-LOCALIZATION-r33.txt
doc/SECURITY-FULL-es.txt

NOVEDADES DE r39:
- Se conectaron 45 mensajes visibles de vipirc a las traducciones, además de un aviso de límite de respuesta de onekeyirc.
- Aplicar solo al código fuente inglés r1 correspondiente y sin modificar; no superponer paquetes.

NOVEDADES DE r41: Se traducen la ayuda contextual, las sugerencias de configuración, las etiquetas y los errores. Aplicar solo a fuentes inglesas r1 intactas.

NOVEDADES DE r41: ayuda de comandos y mensajes de estado traducidos. Aplicar solo sobre la fuente inglesa r1 intacta.

NOVEDADES r42: ayuda contextual, estados y encabezados de historial traducidos. Aplicar solo al código inglés r1.
