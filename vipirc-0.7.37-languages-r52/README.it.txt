Pacchetto linguistico opzionale r52: vipirc-0.7.37

Applicalo solo al sorgente inglese r1 integro; non sovrapporlo a r51 o ad altri pacchetti.
Inserisci host o IP del server IRC, senza URL o porta. Per ZNC o soju usa /panel.
Scegli none, file (file di testo privato), gpg (file cifrato) o plain (valore nascosto nella configurazione).
direct = invia con un Enter; review = controlla il registro e conferma con un altro Enter. Tutti i server.
Soprannome o maschera nick!user@host.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r52.tar.gz
  sh vipirc-0.7.37-languages-r52/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=it ./vipirc --version

vipirc 0.7.37 [lang-rtl-r52]
doc/SETUP-HELP-r52.txt
HISTORY/README.it.r51.txt
