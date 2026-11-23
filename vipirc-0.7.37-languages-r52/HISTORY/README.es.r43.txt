Paquete de idiomas opcional r43: vipirc-0.7.37

Aplícalo solo al código fuente inglés r1 intacto; no lo instales sobre r42 ni otros paquetes.
Nuevas traducciones: estado de GPG, lista de canales, ayuda de configuración y transferencias DCC, y lector de respuestas.
Siguen pendientes la revisión de hablantes competentes y la prueba completa de ncurses de vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r43.tar.gz
  sh vipirc-0.7.37-languages-r43/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=es ./vipirc --help

vipirc 0.7.37 [lang-rtl-r43]

doc/UI-LOCALIZATION-r43.txt
HISTORY/README.es.r42.txt
