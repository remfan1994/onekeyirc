Paquete de idiomas opcional r47: vipirc-0.7.37

Aplícalo solo al código fuente inglés r1 intacto; no lo instales sobre r46 ni otros paquetes.
El resumen agrupa por servidor los servidores, canal 0, vistas de canales unidos o abandonados, conversaciones privadas, mensajes sin leer, menciones, trabajo pendiente y transferencias. Las menciones no se suman a los mensajes sin leer.
 Se completará un mensaje que ya está en curso.
Siguen pendientes la revisión de hablantes competentes y la prueba completa de ncurses de vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r47.tar.gz
  sh vipirc-0.7.37-languages-r47/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=es ./vipirc --help

vipirc 0.7.37 [lang-rtl-r47]

doc/UI-LOCALIZATION-r47.txt
HISTORY/README.es.r46.txt

r47: Conexión directa sin cifrar. Aceptar revela tu dirección IP al otro usuario.

r47: Se tradujeron la ayuda del explorador de archivos y los estados de autenticación.
