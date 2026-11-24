Pacote de idioma opcional r66 - onekeyirc-0.1.36 (pt).
Visão geral da linha de comando (nomes das opções não mudam):
Extrair o arquivo não ativa as traduções. Execute apply.sh sobre o código-fonte inglês r1 limpo antes de compilar. Não aplique sobre um pacote anterior.

Na vista do registro, / abre Comandos; digitar normalmente abre Compor.
Registro persistente indisponível para este servidor:

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r66.tar.gz
  sh onekeyirc-0.1.36-languages-r66/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=pt ./onekeyirc --version
  ONEKEYIRC_LANG=pt ./onekeyirc --help

Identificador de versão obrigatório: onekeyirc 0.1.36 [lang-rtl-r66]
15 novas chaves e 1658 chaves por idioma; a versão do aplicativo não mudou. Revisão por falantes nativos pendente.

O perfil de servidor salvo não foi encontrado.
Configurações salvas; não foi possível salvar o uso dos comandos:

Esta atualização traduz erros de rede/TLS e estados de conexão; os detalhes do sistema não mudam.

Novo: avisos de desconexão segura, erros SASL e mensagens canceladas. Os comandos do protocolo não mudam.

A desencriptação GPG falhou; desbloqueie a chave fora do cliente e volte a ligar.

A autenticação exige TLS verificado

Novo: 15 avisos de atividade IRC (JOIN/PART/QUIT/NICK/KICK/MODE/INVITE/TOPIC/CTCP) traduzidos. Nomes e mensagens do servidor permanecem intactos.
