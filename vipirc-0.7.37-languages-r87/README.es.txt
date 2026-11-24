Paquete de idioma opcional r87 - vipirc-0.7.37 (es)
Enter: ejecutar | Esc: chat
Borrador conservado; siga editando. No se envió nada.
2437 keys / 20 new; Pendiente de revisión por hablantes nativos.

Ejecute apply.sh sobre el código fuente inglés r1 limpio antes de compilar; no superponga paquetes.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r87.tar.gz
  sh vipirc-0.7.37-languages-r87/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=es ./vipirc --version

[lang-rtl-r87]  vipirc 0.7.37
