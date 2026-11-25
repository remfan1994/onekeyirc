Paquete de idioma opcional 1.0-a - onekeyirc-1.0-a (es)
Versión alfa pública; se invita a la comunidad a revisar las traducciones y probar las plataformas.
Informe de /summary
Se requiere un terminal interactivo compatible con ANSI/VT
2195 claves de traducción por idioma; Pendiente de revisión por hablantes nativos.

Ejecute apply.sh sobre el código fuente inglés r1 limpio antes de compilar; no superponga paquetes.

  tar -xzf onekeyirc-1.0-a.tar.gz
  tar -xzf onekeyirc-languages-1.0-a.tar.gz
  ONEKEYIRC_LANG=es sh onekeyirc-languages-1.0-a/apply.sh onekeyirc-1.0-a
  cd onekeyirc-1.0-a
  ONEKEYIRC_LANG=es ./configure && make
  ONEKEYIRC_LANG=es ./onekeyirc --version

[lang-rtl-1.0-a]  onekeyirc 1.0-a
