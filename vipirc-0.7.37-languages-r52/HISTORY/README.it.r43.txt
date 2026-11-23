Pacchetto linguistico opzionale r43: vipirc-0.7.37

Applicalo solo al sorgente inglese r1 integro; non sovrapporlo a r42 o ad altri pacchetti.
Nuove traduzioni: stato GPG, elenco dei canali, guida alle impostazioni e ai trasferimenti DCC e lettore delle risposte.
Restano necessari una revisione linguistica competente e il test completo ncurses di vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r43.tar.gz
  sh vipirc-0.7.37-languages-r43/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=it ./vipirc --help

vipirc 0.7.37 [lang-rtl-r43]

doc/UI-LOCALIZATION-r43.txt
HISTORY/README.it.r42.txt
