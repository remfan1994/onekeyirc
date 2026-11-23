r17: Foram traduzidas integralmente as secções de vipirc sobre processos em segundo plano, recuperação SSH, fecho de sessões e atualização de processos ativos. Consulte doc/SESSION-MANAGEMENT-pt.txt. O manual ncurses completo e SECURITY.md continuam sem tradução integral; RTL não mudou.
--version: vipirc 0.7.36 [lang-rtl-r17]
Apply: sh vipirc-0.7.36-languages-r17/apply.sh vipirc-0.7.36

=== r16 and earlier notes (historical) ===
r16: Foram incluídos trechos traduzidos de introdução à interface completa (apenas vipirc) e explicações ampliadas do modelo de segurança. MANUAL.txt e SECURITY.md ainda não têm tradução integral. O funcionamento RTL não mudou desde r15.
doc/NCURSES-START-pt.txt
doc/SECURITY-BOUNDARIES-pt.txt
--version: vipirc 0.7.36 [lang-rtl-r16]
Aplicar/Apply: sh vipirc-0.7.36-languages-r16/apply.sh vipirc-0.7.36

=== Prior r15 and earlier notes (historical, not current installation instructions) ===
OPTIONAL LANGUAGE + RTL PATCH r15 (pt)

r15 mantém o código-fonte inglês original separado do patch opcional para dez idiomas e RTL árabe. A revisão de segurança abrange controles de direção Unicode, verificação SHA-256 e rejeição de links simbólicos. Aplique somente ao código r1 recém-extraído antes de compilar. Consulte doc/LOCALIZATION-SECURITY-pt.txt; --version mostra [lang-rtl-r15].

-- Earlier release history --
vipirc 0.7.36 - Pacote de idiomas r14 (pt)
========================================================

--- r14 ---
r14 mantem o codigo-fonte ingles original separado do pacote opcional com traducoes E a correcao RTL arabe. Aplicado antes da compilacao, --version mostra [lang-rtl-r14]. A versao base da aplicacao nao muda. Aplique apenas a uma copia nova do codigo ingles r1.


--- r12 ---
r12 aperfeiçoa o modo opcional de exibição do árabe. Cada atualização do editor usa uma única disposição para texto e cursor, mantendo ambos coerentes quando uma falha de alocação de memória exige o retorno à ordem lógica. No modo visual do ncurses, as marcas combinantes permanecem junto à letra base na célula inferior direita do terminal. O movimento vertical além da primeira ou última linha preserva a posição de inserção. Reutilizar a disposição dos parágrafos reduz o trabalho durante a rolagem. As verificações prévias evitam conversões desnecessárias ou indisponíveis e mantêm a exibição lógica. Rascunhos, mensagens e registros continuam em UTF-8 e ordem lógica. O escopo da tradução não muda; o manual completo do ncurses e SECURITY.md continuam em inglês. Ainda são necessários testes reais de terminais e fontes no OpenBSD/Crostini e revisão por falantes proficientes de árabe. Aplique r12 apenas às fontes English r1 recém-extraídas, nunca sobre r11.


--- r11 ---
r11 acrescenta formas contextuais das letras árabes e quebra de linhas por parágrafo, de forma opcional, com edição visual, movimento do cursor e posicionamento pelo mouse em onekeyirc, vipirc -i e vipirc --ncurses. Textos armazenados, mensagens enviadas e registros mantêm UTF-8 em ordem lógica. Requer FriBidi do sistema e localidade UTF-8. Ative ONEKEYIRC_ARABIC_VISUAL=1 ou VIPIRC_ARABIC_VISUAL=1, independentemente do idioma. Deixe desativado se o terminal já organiza texto bidirecional. Home/End levam aos extremos do documento; Backspace/Delete editam grupos lógicos de caracteres. Faltam testes de fontes em terminais OpenBSD/Crostini reais e revisão linguística do árabe. Preservam-se as 844 entradas do catálogo por idioma e os manuais minimalistas completos. O manual ncurses completo e SECURITY.md permanecem em inglês. As versões dos aplicativos não mudam. Aplique r11 apenas ao código-fonte English r1 recém-extraído, nunca sobre r10.


--- r10 ---
r10 traduz a ajuda gerada específica de cada cliente e inclui os manuais completos localizados da interface minimalista em doc/MINIMAL-MANUAL-pt.txt. Tanto onekeyirc --manual quanto vipirc -i --manual agora respeitam o idioma de exibição selecionado, escrevem UTF-8 em ordem lógica e não precisam de um terminal. O manual da interface completa ncurses do vipirc e SECURITY.md permanecem em inglês; outras partes da interface, mensagens de diagnóstico e a edição de texto da direita para a esquerda ainda precisam ser concluídas. As traduções ainda precisam de revisão por falantes nativos. Aplique r10 somente sobre uma cópia nova do código-fonte inglês r1; nunca aplique r10 sobre r9 ou outro pacote de idiomas.


--- r9 ---
Estado atual do r9: todas as 474 linhas traduzíveis das tabelas fixas de ajuda de comandos e tópicos estão traduzidas. Três linhas que apenas relacionam aliases de comandos permanecem literais; todas as 477 linhas das tabelas têm entradas no catálogo. Após aplicar o pacote, consulte doc/HELP-REFERENCE-pt.txt. MANUAL.txt, SECURITY.md e parte da ajuda e dos diagnósticos gerados ou específicos de cada cliente permanecem em inglês. As traduções ainda precisam de revisão por falantes nativos. Aplique r9 somente sobre uma cópia nova do código-fonte inglês r1, nunca sobre r8 ou outro pacote.


Este pacote adiciona traduções da interface, da ajuda e de alguns documentos. Não é um programa independente.

Use apenas o arquivo de código-fonte English r1 indicado abaixo. Um arquivo antigo com a mesma versão, ou fontes já modificados, não é compatível.
  vipirc-0.7.36-english-source-r1.tar.gz

Extraia os dois arquivos para o mesmo diretório pai:
  tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.36-languages-r15.tar.gz

Aplique o pacote ao código-fonte English r1 intacto ANTES de executar configure ou make:
  sh vipirc-0.7.36-languages-r15/apply.sh vipirc-0.7.36

Configure e compile o programa:
  cd vipirc-0.7.36
  ./configure --prefix=/usr/local
  make

Inicie o programa com a interface em português:
  VIPIRC_LANG=pt ./vipirc

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

VIPIRC_LANG=ar VIPIRC_ARABIC_VISUAL=1 ./vipirc -i
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-pt.txt
