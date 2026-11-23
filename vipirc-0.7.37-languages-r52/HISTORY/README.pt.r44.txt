Pacote opcional de idiomas r44: vipirc-0.7.37

Aplique somente ao código-fonte inglês r1 intacto; não instale sobre r43 nem pacotes anteriores.
As listas mostram vistas abertas no servidor selecionado. Up/Down seleciona; Left/Right escolhe Open, New, Part ou Close; Enter executa. Canal 0 é o servidor. /cl e /channels mostram vistas locais; /list [MASK] [OPTIONS] abre o diretório da rede; /ql mostra conversas privadas.
Ainda faltam revisão por falantes fluentes e teste completo da interface ncurses do vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r44.tar.gz
  sh vipirc-0.7.37-languages-r44/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=pt ./vipirc --help

vipirc 0.7.37 [lang-rtl-r44]

doc/UI-LOCALIZATION-r44.txt
HISTORY/README.pt.r43.txt
