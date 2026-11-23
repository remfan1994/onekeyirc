Pacchetto linguistico opzionale r50: vipirc-0.7.37

Applicalo solo al sorgente inglese r1 integro; non sovrapporlo a r49 o ad altri pacchetti.
Il nuovo file delle credenziali deve differire dai file di configurazione e accessori; usa la cartella privata proposta.
La cifratura GPG è ancora in corso; attendi o torna indietro per annullarla.
Usa l'impronta completa della chiave GPG locale: 40 o 64 cifre esadecimali senza spazi.
Inserisci yes per TLS verificato o no per IRC non cifrato.
Restano necessari una revisione linguistica competente e il test completo ncurses di vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r50.tar.gz
  sh vipirc-0.7.37-languages-r50/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=it ./vipirc --version

vipirc 0.7.37 [lang-rtl-r50]
doc/SETUP-VALIDATION-r50.txt
HISTORY/README.it.r49.txt
