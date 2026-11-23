Pacote opcional r42 de idiomas e apresentação árabe da direita para a esquerda.
vipirc 0.7.37 - r42

Use somente com o código-fonte inglês r1 original e intacto. Não instale sobre r41 nem pacotes anteriores. A versão do aplicativo não muda.

vipirc: traduzidos novos avisos, erros de conexão, entrada UTF-8, DCC e textos da interface.

tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
tar -xzf vipirc-0.7.37-languages-r42.tar.gz
sh vipirc-0.7.37-languages-r42/apply.sh vipirc-0.7.37
cd vipirc-0.7.37
./configure && make
./vipirc --version

VIPIRC_LANG=pt ./vipirc --help
vipirc 0.7.37 [lang-rtl-r42]

Comandos IRC, opções, valores da configuração, usuários, respostas do servidor e dados da rede continuam literais. Falta revisão de falantes fluentes; a compilação ncurses completa de vipirc não foi verificada.

doc/UI-LOCALIZATION-r42.txt
doc/CLI-LOCALIZATION-r33.txt
doc/SECURITY-FULL-pt.txt

NOVIDADES DA r39:
- Foram integradas traduções a 45 mensagens visíveis do vipirc e a um aviso de limite de resposta do onekeyirc.
- Aplique somente ao código-fonte inglês r1 correspondente e intacto; não sobreponha pacotes.

NOVIDADES DO r41: Ajuda contextual, dicas de configuração, rótulos e erros foram traduzidos. Aplique apenas ao código-fonte inglês r1 intacto.

NOVO NO r41: ajuda de comandos e mensagens de estado traduzidas. Aplique somente ao codigo-fonte ingles r1 intacto.

NOVIDADES r42: ajuda contextual, status e títulos do histórico traduzidos. Aplicar apenas à fonte inglesa r1.
