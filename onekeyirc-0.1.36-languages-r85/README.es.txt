Paquete de idioma opcional r85 - onekeyirc-0.1.36 (es)
Informe de /summary
Se requiere un terminal interactivo compatible con ANSI/VT
2010 keys / 2 new; Pendiente de revisión por hablantes nativos.

Ejecute apply.sh sobre el código fuente inglés r1 limpio antes de compilar; no superponga paquetes.

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r85.tar.gz
  sh onekeyirc-0.1.36-languages-r85/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=es ./onekeyirc --version

[lang-rtl-r85]  onekeyirc 0.1.36
