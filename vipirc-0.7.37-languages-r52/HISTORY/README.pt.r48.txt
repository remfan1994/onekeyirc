Pacote opcional de idiomas r48: vipirc-0.7.37

Aplique somente ao código-fonte inglês r1 intacto; não instale sobre r47 nem pacotes anteriores.
O resumo agrupa por servidor os servidores, canal 0, canais ativos e abandonados, conversas privadas, mensagens não lidas, menções, tarefas pendentes e transferências. Menções não são somadas ao total de não lidas.
 Uma mensagem em transmissão será concluída.
Ainda faltam revisão por falantes fluentes e teste completo da interface ncurses do vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r48.tar.gz
  sh vipirc-0.7.37-languages-r48/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=pt ./vipirc --help

vipirc 0.7.37 [lang-rtl-r48]

doc/UI-LOCALIZATION-r48.txt
HISTORY/README.pt.r47.txt

r48: Aceitar / Rejeitar / Cancelar transferência.
Transferências DCC usam conexão direta sem criptografia e revelam ao outro participante o endereço de rede escolhido. Receber uma oferta por si só não abre arquivos nem conexões.
