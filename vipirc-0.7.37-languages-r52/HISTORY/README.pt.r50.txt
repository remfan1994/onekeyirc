Pacote opcional de idiomas r50: vipirc-0.7.37

Aplique somente ao código-fonte inglês r1 intacto; não instale sobre r49 nem pacotes anteriores.
O novo arquivo de credenciais deve diferir dos arquivos de configuração e auxiliares; use a pasta privada de credenciais oferecida.
A criptografia GPG continua em execução; aguarde ou volte para cancelar.
Use a impressão digital completa da chave GPG local: 40 ou 64 dígitos hexadecimais sem espaços.
Digite yes para TLS verificado ou no para IRC sem criptografia.
Ainda faltam revisão por falantes fluentes e teste completo da interface ncurses do vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r50.tar.gz
  sh vipirc-0.7.37-languages-r50/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=pt ./vipirc --version

vipirc 0.7.37 [lang-rtl-r50]
doc/SETUP-VALIDATION-r50.txt
HISTORY/README.pt.r49.txt
