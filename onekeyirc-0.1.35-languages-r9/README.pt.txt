onekeyirc 0.1.35 - Pacote de idiomas r9 (pt)
========================================================

--- r9 ---
Estado atual do r9: todas as 474 linhas traduzíveis das tabelas fixas de ajuda de comandos e tópicos estão traduzidas. Três linhas que apenas relacionam aliases de comandos permanecem literais; todas as 477 linhas das tabelas têm entradas no catálogo. Após aplicar o pacote, consulte doc/HELP-REFERENCE-pt.txt. MANUAL.txt, SECURITY.md e parte da ajuda e dos diagnósticos gerados ou específicos de cada cliente permanecem em inglês. As traduções ainda precisam de revisão por falantes nativos. Aplique r9 somente sobre uma cópia nova do código-fonte inglês r1, nunca sobre r8 ou outro pacote.


Este pacote adiciona traduções da interface, da ajuda e de alguns documentos. Não é um programa independente.

Use apenas o arquivo de código-fonte English r1 indicado abaixo. Um arquivo antigo com a mesma versão, ou fontes já modificados, não é compatível.
  onekeyirc-0.1.35-english-source-r1.tar.gz

Extraia os dois arquivos para o mesmo diretório pai:
  tar -xzf onekeyirc-0.1.35-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.35-languages-r9.tar.gz

Aplique o pacote ao código-fonte English r1 intacto ANTES de executar configure ou make:
  sh onekeyirc-0.1.35-languages-r9/apply.sh onekeyirc-0.1.35

Configure e compile o programa:
  cd onekeyirc-0.1.35
  ./configure --prefix=/usr/local
  make

Inicie o programa com a interface em português:
  ONEKEYIRC_LANG=pt ./onekeyirc

Os comandos IRC, as opções de linha de comando e as chaves de configuração não são traduzidos. Digite-os exatamente como mostrados.

As traduções ainda são rascunhos; parte da ajuda e os manuais completos continuam em inglês. Consulte MANUAL.txt e SECURITY.md em inglês para instruções de segurança precisas. Use um terminal UTF-8.

Novo na r4: /help background, /help catchup e /help logging estão inteiramente traduzidos. RECOVERY-pt.txt explica a recuperação de sessões e os limites dos registos.

PACK-SHA256SUMS contém os hashes dos arquivos. apply.sh verifica a compatibilidade do código-fonte. Não copie arquivos manualmente sobre fontes modificados.


PACK-SHA256SUMS / README.txt / MANUAL.txt / SECURITY.md

Novo na r5: /help autocommands, /help dcc, /help bindings, /help bind, /help unbind, /help transfers e /help hotkeys completos. Veja doc/WORKFLOWS-pt.txt.

Novo no r6: /help configuration, /help summary, /help server, /help users e /help watch estão completos. Leia doc/CONFIGURATION-pt.txt e doc/STATUS-pt.txt após instalar.

Novo no r7: mais 32 explicações de ajuda sobre /help set, /help list, navegação local e aliases. Após instalar, veja doc/SETTINGS-pt.txt e doc/CHANNELS-pt.txt. Cobertura: 384/477; restam 93 explicações em inglês.

--- r8 ---
A exibição visual em árabe é experimental e exclusiva da interface minimalista. Instale a biblioteca FriBidi do sistema e ative a variável abaixo. Não ative se o terminal já compõe árabe. O editor, o texto salvo e a interface ncurses completa não mudam. Sem FriBidi, permanece a ordem Unicode original.

ONEKEYIRC_LANG=ar ONEKEYIRC_ARABIC_VISUAL=1 ./onekeyirc
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-pt.txt
