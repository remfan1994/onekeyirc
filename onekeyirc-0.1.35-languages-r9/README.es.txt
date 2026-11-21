onekeyirc 0.1.35 - Paquete de idiomas r9 (es)
========================================================

--- r9 ---
Estado actual de r9: están traducidas las 474 líneas traducibles de las tablas fijas de ayuda de comandos y temas. Tres líneas que solo relacionan alias de comandos se conservan literalmente; las 477 líneas de las tablas tienen entradas en el catálogo. Tras aplicar el paquete, consulte doc/HELP-REFERENCE-es.txt. MANUAL.txt, SECURITY.md y parte de la ayuda y los diagnósticos generados o específicos de cada cliente siguen en inglés. Las traducciones aún necesitan revisión por hablantes nativos. Aplique r9 únicamente sobre una copia nueva del código fuente inglés r1, nunca sobre r8 ni otro paquete.


Este paquete agrega traducciones de la interfaz, la ayuda y algunos documentos. No es un programa independiente.

Use solo el archivo de fuentes English r1 indicado abajo. Un archivo antiguo con el mismo número de versión, o fuentes ya modificadas, no es compatible.
  onekeyirc-0.1.35-english-source-r1.tar.gz

Extraiga los dos archivos en el mismo directorio principal:
  tar -xzf onekeyirc-0.1.35-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.35-languages-r9.tar.gz

Aplique el paquete a las fuentes English r1 sin modificar ANTES de ejecutar configure o make:
  sh onekeyirc-0.1.35-languages-r9/apply.sh onekeyirc-0.1.35

Configure y compile el programa:
  cd onekeyirc-0.1.35
  ./configure --prefix=/usr/local
  make

Inicie el programa con la interfaz en español:
  ONEKEYIRC_LANG=es ./onekeyirc

Los comandos IRC, las opciones de consola y las claves de configuración no se traducen. Escríbalos exactamente como aparecen.

Las traducciones son borradores: parte de la ayuda y los manuales completos siguen en inglés. Consulte MANUAL.txt y SECURITY.md en inglés para instrucciones de seguridad precisas. Use una terminal UTF-8.

Nuevo en r4: se completaron las traducciones de /help background, /help catchup y /help logging. RECOVERY-es.txt explica la recuperación de sesiones y los límites del registro.

PACK-SHA256SUMS contiene las sumas de comprobación. apply.sh verifica la compatibilidad de las fuentes. No copie archivos manualmente sobre fuentes modificadas.


PACK-SHA256SUMS / README.txt / MANUAL.txt / SECURITY.md

Nuevo en r5: se completaron /help autocommands, /help dcc, /help bindings, /help bind, /help unbind, /help transfers y /help hotkeys. Consulte doc/WORKFLOWS-es.txt.

Nuevo en r6: se completaron /help configuration, /help summary, /help server, /help users y /help watch. Consulte doc/CONFIGURATION-es.txt y doc/STATUS-es.txt tras instalar.

Nuevo en r7: 32 explicaciones de ayuda sobre /help set, /help list, navegación local y alias. Tras instalar, consulte doc/SETTINGS-es.txt y doc/CHANNELS-es.txt. Cobertura: 384/477; quedan 93 explicaciones en inglés.

--- r8 ---
La visualización árabe es experimental y solo funciona en la interfaz minimalista. Instale la biblioteca FriBidi del sistema y active la variable indicada. No la active si el terminal ya compone árabe. El editor, el texto guardado y la interfaz ncurses completa no cambian. Sin FriBidi se conserva el orden Unicode original.

ONEKEYIRC_LANG=ar ONEKEYIRC_ARABIC_VISUAL=1 ./onekeyirc
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-es.txt
