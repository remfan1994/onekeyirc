Pacchetto linguistico opzionale r51: vipirc-0.7.37

Applicalo solo al sorgente inglese r1 integro; non sovrapporlo a r50 o ad altri pacchetti.
Scegli uno dei numeri di pannello visualizzati.
Inserisci il nome host o l'indirizzo IP del server IRC.
Inserisci direct (un Enter) o review (due Enter).
La creazione di file GPG è disabilitata in --test-mode; non sono stati usati processi ausiliari, keyring o file.
Restano necessari una revisione linguistica competente e il test completo ncurses di vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r51.tar.gz
  sh vipirc-0.7.37-languages-r51/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=it ./vipirc --version

vipirc 0.7.37 [lang-rtl-r51]
doc/SETUP-ERRORS-r51.txt
HISTORY/README.it.r50.txt
