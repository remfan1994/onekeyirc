r17: Se han traducido íntegramente los capítulos de vipirc sobre trabajo en segundo plano, recuperación SSH, cierre de sesiones y actualización de procesos. Consulte doc/SESSION-MANAGEMENT-es.txt. El manual ncurses completo y SECURITY.md siguen sin traducción íntegra; RTL no ha cambiado.
--version: vipirc 0.7.36 [lang-rtl-r17]
Apply: sh vipirc-0.7.36-languages-r17/apply.sh vipirc-0.7.36

=== r16 and earlier notes (historical) ===
r16: Se añaden extractos traducidos para empezar con la interfaz completa (solo vipirc) y explicaciones ampliadas del modelo de seguridad. MANUAL.txt y SECURITY.md todavía no están traducidos íntegramente. El funcionamiento RTL no cambia respecto a r15.
doc/NCURSES-START-es.txt
doc/SECURITY-BOUNDARIES-es.txt
--version: vipirc 0.7.36 [lang-rtl-r16]
Aplicar/Apply: sh vipirc-0.7.36-languages-r16/apply.sh vipirc-0.7.36

=== Prior r15 and earlier notes (historical, not current installation instructions) ===
OPTIONAL LANGUAGE + RTL PATCH r15 (es)

r15 mantiene separado el código inglés original del parche opcional para diez idiomas y RTL árabe. La revisión de seguridad cubre controles de dirección Unicode, verificación SHA-256 y rechazo de enlaces simbólicos. Aplique el parche solamente a una copia r1 nueva antes de compilar. Consulte doc/LOCALIZATION-SECURITY-es.txt; --version muestra [lang-rtl-r15].

-- Earlier release history --
vipirc 0.7.36 - Paquete de idiomas r14 (es)
========================================================

--- r14 ---
r14 mantiene el codigo fuente ingles original separado del paquete opcional con traducciones Y el parche RTL arabe. Si se aplica antes de compilar, --version muestra [lang-rtl-r14]. La version base de la aplicacion no cambia. Aplique el paquete solo a una copia nueva de la fuente inglesa r1.


--- r12 ---
r12 mejora el modo opcional de visualización del árabe. Cada redibujado del editor usa una única disposición para texto y cursor, de modo que ambos siguen coordinados al volver al orden lógico si falla una reserva de memoria. En el modo visual de ncurses, las marcas combinantes se conservan con su letra base en la celda inferior derecha del terminal. El movimiento vertical más allá de la primera o última fila conserva el punto de inserción. Reutilizar la disposición de los párrafos reduce el trabajo al desplazarse. Las comprobaciones previas omiten conversiones innecesarias o no disponibles y mantienen la visualización lógica. Borradores, mensajes y registros siguen en UTF-8 y orden lógico. El alcance de la traducción no cambia; el manual completo de ncurses y SECURITY.md siguen en inglés. Quedan pendientes las pruebas reales de terminales y fuentes en OpenBSD/Crostini y la revisión por hablantes de árabe competentes. Aplique r12 solo a fuentes English r1 recién extraídas, nunca sobre r11.


--- r11 ---
r11 añade formas contextuales de letras árabes y ajuste de líneas por párrafos, de forma opcional, con edición visual, movimiento del cursor y posicionamiento con el ratón en onekeyirc, vipirc -i y vipirc --ncurses. Los textos guardados, mensajes enviados y registros conservan UTF-8 en orden lógico. Requiere FriBidi del sistema y una configuración regional UTF-8. Active ONEKEYIRC_ARABIC_VISUAL=1 o VIPIRC_ARABIC_VISUAL=1, independientemente del idioma. Déjelo desactivado si la terminal ya organiza texto bidireccional. Home/End van a los extremos del documento; Backspace/Delete editan grupos lógicos de caracteres. Faltan pruebas de fuentes en terminales OpenBSD/Crostini reales y revisión lingüística del árabe. Se conservan las 844 entradas del catálogo por idioma y los manuales minimalistas completos. El manual ncurses completo y SECURITY.md siguen en inglés. Las versiones de las aplicaciones no cambian. Aplique r11 sobre fuentes English r1 recién extraídas, nunca sobre r10.


--- r10 ---
r10 traduce la ayuda generada específica de cada cliente e incluye los manuales completos localizados de la interfaz minimalista en doc/MINIMAL-MANUAL-es.txt. Tanto onekeyirc --manual como vipirc -i --manual respetan ahora el idioma de visualización seleccionado, escriben UTF-8 en orden lógico y no necesitan una terminal. El manual de la interfaz completa ncurses de vipirc y SECURITY.md siguen en inglés; aún quedan por terminar otras partes de la interfaz, mensajes de diagnóstico y la edición de texto de derecha a izquierda. Las traducciones aún necesitan revisión por hablantes nativos. Aplique r10 solo sobre una copia nueva del código fuente inglés r1; nunca aplique r10 encima de r9 ni de otro paquete de idiomas.


--- r9 ---
Estado actual de r9: están traducidas las 474 líneas traducibles de las tablas fijas de ayuda de comandos y temas. Tres líneas que solo relacionan alias de comandos se conservan literalmente; las 477 líneas de las tablas tienen entradas en el catálogo. Tras aplicar el paquete, consulte doc/HELP-REFERENCE-es.txt. MANUAL.txt, SECURITY.md y parte de la ayuda y los diagnósticos generados o específicos de cada cliente siguen en inglés. Las traducciones aún necesitan revisión por hablantes nativos. Aplique r9 únicamente sobre una copia nueva del código fuente inglés r1, nunca sobre r8 ni otro paquete.


Este paquete agrega traducciones de la interfaz, la ayuda y algunos documentos. No es un programa independiente.

Use solo el archivo de fuentes English r1 indicado abajo. Un archivo antiguo con el mismo número de versión, o fuentes ya modificadas, no es compatible.
  vipirc-0.7.36-english-source-r1.tar.gz

Extraiga los dos archivos en el mismo directorio principal:
  tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.36-languages-r15.tar.gz

Aplique el paquete a las fuentes English r1 sin modificar ANTES de ejecutar configure o make:
  sh vipirc-0.7.36-languages-r15/apply.sh vipirc-0.7.36

Configure y compile el programa:
  cd vipirc-0.7.36
  ./configure --prefix=/usr/local
  make

Inicie el programa con la interfaz en español:
  VIPIRC_LANG=es ./vipirc

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

VIPIRC_LANG=ar VIPIRC_ARABIC_VISUAL=1 ./vipirc -i
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-es.txt
