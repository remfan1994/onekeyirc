Paquete de idiomas opcional r50: vipirc-0.7.37

Aplícalo solo al código fuente inglés r1 intacto; no lo instales sobre r49 ni otros paquetes.
El archivo de credenciales nuevo debe diferir de los de configuración y sus auxiliares; usa el directorio de credenciales propuesto.
El cifrado GPG sigue en curso; espera o retrocede para cancelarlo.
Usa la huella completa de la clave GPG local: 40 o 64 dígitos hexadecimales sin espacios.
Introduce yes para TLS verificado o no para IRC sin cifrar.
Siguen pendientes la revisión de hablantes competentes y la prueba completa de ncurses de vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r50.tar.gz
  sh vipirc-0.7.37-languages-r50/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=es ./vipirc --version

vipirc 0.7.37 [lang-rtl-r50]
doc/SETUP-VALIDATION-r50.txt
HISTORY/README.es.r49.txt
