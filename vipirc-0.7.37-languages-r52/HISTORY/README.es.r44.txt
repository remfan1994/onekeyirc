Paquete de idiomas opcional r44: vipirc-0.7.37

Aplícalo solo al código fuente inglés r1 intacto; no lo instales sobre r43 ni otros paquetes.
Estas listas muestran vistas abiertas del servidor seleccionado. Up/Down elige; Left/Right escoge Open, New, Part o Close; Enter ejecuta. El canal 0 es el servidor. /cl y /channels muestran vistas locales; /list [MASK] [OPTIONS] abre el directorio de red; /ql muestra conversaciones privadas.
Siguen pendientes la revisión de hablantes competentes y la prueba completa de ncurses de vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r44.tar.gz
  sh vipirc-0.7.37-languages-r44/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=es ./vipirc --help

vipirc 0.7.37 [lang-rtl-r44]

doc/UI-LOCALIZATION-r44.txt
HISTORY/README.es.r43.txt
