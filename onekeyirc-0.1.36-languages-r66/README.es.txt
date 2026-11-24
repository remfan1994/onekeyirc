Parche de idioma opcional r66 - onekeyirc-0.1.36 (es).
Resumen de línea de comandos (los nombres de opciones no cambian):
Extraer el archivo no activa las traducciones. Ejecuta apply.sh sobre el código inglés r1 limpio antes de compilar. No lo instales encima de otro paquete.

En el registro, / abre Comandos; escribir normalmente abre Redactar.
El registro persistente no está disponible para este servidor:

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r66.tar.gz
  sh onekeyirc-0.1.36-languages-r66/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=es ./onekeyirc --version
  ONEKEYIRC_LANG=es ./onekeyirc --help

Identificador de versión obligatorio: onekeyirc 0.1.36 [lang-rtl-r66]
31 claves nuevas y 1658 claves por idioma; la versión de la aplicación no cambia. Falta la revisión de hablantes nativos.

No se encuentra el perfil de servidor guardado.
Se guardó la configuración, pero no el uso de comandos:

Esta actualización traduce errores de red/TLS y estados de conexión; los detalles del sistema no cambian.

Novedad: avisos de desconexión segura, errores SASL y mensajes cancelados. Los comandos del protocolo no cambian.

Falló el descifrado GPG; desbloquee la clave fuera del cliente y vuelva a conectar.

La autenticación requiere TLS verificado

Nuevo: 15 avisos de actividad IRC (JOIN/PART/QUIT/NICK/KICK/MODE/INVITE/TOPIC/CTCP) traducidos. Los nombres y el texto del servidor permanecen intactos.
