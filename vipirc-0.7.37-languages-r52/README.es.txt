Paquete de idiomas opcional r52: vipirc-0.7.37

Aplícalo solo al código fuente inglés r1 intacto; no lo instales sobre r51 ni otros paquetes.
Introduce el nombre de host o la IP del servidor IRC, sin URL ni puerto. Para ZNC o soju usa /panel.
Elige none, file (archivo privado de texto), gpg (archivo cifrado) o plain (valor oculto en la configuración).
direct = enviar con un Enter; review = revisar el registro y confirmar con otro Enter. Se aplica a todos los servidores.
Un apodo o una máscara nick!user@host.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r52.tar.gz
  sh vipirc-0.7.37-languages-r52/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=es ./vipirc --version

vipirc 0.7.37 [lang-rtl-r52]
doc/SETUP-HELP-r52.txt
HISTORY/README.es.r51.txt
