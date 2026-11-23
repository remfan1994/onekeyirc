Pacchetto linguistico opzionale r47: vipirc-0.7.37

Applicalo solo al sorgente inglese r1 integro; non sovrapporlo a r46 o ad altri pacchetti.
Il riepilogo raggruppa per server i server, il canale 0, le viste dei canali attivi o lasciati, le conversazioni private, i non letti, le menzioni, le attività in sospeso e i trasferimenti. Le menzioni non si sommano ai non letti.
 Un messaggio già in trasmissione verrà completato.
Restano necessari una revisione linguistica competente e il test completo ncurses di vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r47.tar.gz
  sh vipirc-0.7.37-languages-r47/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=it ./vipirc --help

vipirc 0.7.37 [lang-rtl-r47]

doc/UI-LOCALIZATION-r47.txt
HISTORY/README.it.r46.txt

r47: Connessione diretta non cifrata. Accept rivela il tuo IP al peer.

r47: Tradotte la guida del browser dei file e le informazioni sull’autenticazione.
