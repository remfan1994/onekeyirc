Pacchetto linguistico opzionale r44: vipirc-0.7.37

Applicalo solo al sorgente inglese r1 integro; non sovrapporlo a r43 o ad altri pacchetti.
Questi elenchi mostrano viste aperte sul server selezionato. Up/Down sceglie; Left/Right seleziona Open, New, Part o Close; Enter esegue. Il canale 0 è il server. /cl e /channels mostrano viste locali; /list [MASK] [OPTIONS] apre la directory della rete; /ql mostra le conversazioni private.
Restano necessari una revisione linguistica competente e il test completo ncurses di vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r44.tar.gz
  sh vipirc-0.7.37-languages-r44/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=it ./vipirc --help

vipirc 0.7.37 [lang-rtl-r44]

doc/UI-LOCALIZATION-r44.txt
HISTORY/README.it.r43.txt
