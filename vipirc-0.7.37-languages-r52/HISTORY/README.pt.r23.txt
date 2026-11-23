O r23 acrescenta traduções integrais dos capítulos SETTINGS e FILES AND ENVIRONMENT do manual do vipirc. Há rascunhos de 45 das 52 seções; ainda é necessária revisão por falantes nativos.

Código-fonte original em inglês: vipirc-0.7.36-english-source-r1.tar.gz
Pacote de idiomas e patch RTL opcional: vipirc-0.7.36-languages-r23.tar.gz

Extraia os dois arquivos no mesmo diretório pai.
Aplique o patch uma única vez, antes de configurar e compilar:
    sh vipirc-0.7.36-languages-r23/apply.sh vipirc-0.7.36

Compilação: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r23]
Idioma: VIPIRC_LANG=pt

Não instale sobre um patch anterior; use uma extração nova do código-fonte inglês r1.
O código-fonte inglês não foi alterado. MANUAL.txt e SECURITY.md em inglês permanecem as referências principais; os rascunhos traduzidos ainda precisam de revisão por falantes nativos.
Instruções antigas estão em HISTORY/; não execute os comandos de lá.
