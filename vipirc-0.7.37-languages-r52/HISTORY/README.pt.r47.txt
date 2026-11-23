Pacote opcional de idiomas r47: vipirc-0.7.37

Aplique somente ao código-fonte inglês r1 intacto; não instale sobre r46 nem pacotes anteriores.
O resumo agrupa por servidor os servidores, canal 0, canais ativos e abandonados, conversas privadas, mensagens não lidas, menções, tarefas pendentes e transferências. Menções não são somadas ao total de não lidas.
 Uma mensagem em transmissão será concluída.
Ainda faltam revisão por falantes fluentes e teste completo da interface ncurses do vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r47.tar.gz
  sh vipirc-0.7.37-languages-r47/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=pt ./vipirc --help

vipirc 0.7.37 [lang-rtl-r47]

doc/UI-LOCALIZATION-r47.txt
HISTORY/README.pt.r46.txt

r47: Conexão direta sem criptografia. Aceitar revela seu IP ao outro usuário.

r47: Traduzidas as instruções do navegador de arquivos e os estados de autenticação.
