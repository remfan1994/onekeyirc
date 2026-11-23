Paquete de idiomas opcional r48: vipirc-0.7.37

Aplícalo solo al código fuente inglés r1 intacto; no lo instales sobre r47 ni otros paquetes.
El resumen agrupa por servidor los servidores, canal 0, vistas de canales unidos o abandonados, conversaciones privadas, mensajes sin leer, menciones, trabajo pendiente y transferencias. Las menciones no se suman a los mensajes sin leer.
 Se completará un mensaje que ya está en curso.
Siguen pendientes la revisión de hablantes competentes y la prueba completa de ncurses de vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r48.tar.gz
  sh vipirc-0.7.37-languages-r48/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=es ./vipirc --help

vipirc 0.7.37 [lang-rtl-r48]

doc/UI-LOCALIZATION-r48.txt
HISTORY/README.es.r47.txt

r48: Aceptar / Rechazar / Cancelar transferencia.
Las transferencias DCC utilizan una conexión directa sin cifrar y revelan al destinatario la dirección de red seleccionada. Recibir una oferta por sí solo no abre archivos ni una conexión.
