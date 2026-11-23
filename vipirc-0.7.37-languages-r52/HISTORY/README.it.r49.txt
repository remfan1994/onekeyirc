Pacchetto linguistico opzionale r49: vipirc-0.7.37

Applicalo solo al sorgente inglese r1 integro; non sovrapporlo a r48 o ad altri pacchetti.
Controlla i percorsi delle credenziali o prepara l'autenticazione
Apri l'elenco dei canali di questo server
Apri le impostazioni di connessione del server selezionato
yes = connessione cifrata verificata; no = IRC non cifrato.
Restano necessari una revisione linguistica competente e il test completo ncurses di vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r49.tar.gz
  sh vipirc-0.7.37-languages-r49/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=it ./vipirc --version

vipirc 0.7.37 [lang-rtl-r49]
doc/COMMAND-LOCALIZATION-r49.txt
HISTORY/README.it.r48.txt
