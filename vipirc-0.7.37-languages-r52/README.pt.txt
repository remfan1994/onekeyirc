Pacote opcional de idiomas r52: vipirc-0.7.37

Aplique somente ao código-fonte inglês r1 intacto; não instale sobre r51 nem pacotes anteriores.
Digite o host ou IP do servidor IRC, sem URL ou porta. Para ZNC ou soju use /panel.
Escolha none, file (arquivo privado de texto), gpg (arquivo criptografado) ou plain (valor oculto na configuração).
direct = enviar com um Enter; review = revisar o registro e confirmar com outro Enter. Vale para todos os servidores.
Apelido ou máscara nick!user@host.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r52.tar.gz
  sh vipirc-0.7.37-languages-r52/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=pt ./vipirc --version

vipirc 0.7.37 [lang-rtl-r52]
doc/SETUP-HELP-r52.txt
HISTORY/README.pt.r51.txt
