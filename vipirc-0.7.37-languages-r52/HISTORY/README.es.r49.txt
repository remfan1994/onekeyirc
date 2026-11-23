Paquete de idiomas opcional r49: vipirc-0.7.37

Aplícalo solo al código fuente inglés r1 intacto; no lo instales sobre r48 ni otros paquetes.
Consulta rutas de credenciales o prepara la autenticación
Abre la lista de canales de este servidor
Abre los ajustes de conexión del servidor seleccionado
yes = conexión cifrada y verificada; no = IRC sin cifrar.
Siguen pendientes la revisión de hablantes competentes y la prueba completa de ncurses de vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r49.tar.gz
  sh vipirc-0.7.37-languages-r49/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=es ./vipirc --version

vipirc 0.7.37 [lang-rtl-r49]
doc/COMMAND-LOCALIZATION-r49.txt
HISTORY/README.es.r48.txt
