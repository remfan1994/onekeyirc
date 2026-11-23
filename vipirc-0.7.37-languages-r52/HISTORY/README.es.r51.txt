Paquete de idiomas opcional r51: vipirc-0.7.37

Aplícalo solo al código fuente inglés r1 intacto; no lo instales sobre r50 ni otros paquetes.
Elige uno de los números de panel mostrados.
Introduce el nombre de host o la dirección IP de un servidor IRC.
Introduce direct (un Enter) o review (dos Enter).
La creación de archivos GPG está desactivada en --test-mode; no se accedió a procesos auxiliares, keyring ni archivos.
Siguen pendientes la revisión de hablantes competentes y la prueba completa de ncurses de vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r51.tar.gz
  sh vipirc-0.7.37-languages-r51/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=es ./vipirc --version

vipirc 0.7.37 [lang-rtl-r51]
doc/SETUP-ERRORS-r51.txt
HISTORY/README.es.r50.txt
