Pacchetto opzionale r42 per le lingue e la visualizzazione araba da destra a sinistra.
vipirc 0.7.37 - r42

Usare solo con il codice sorgente inglese r1 originale e integro. Non installare sopra r41 o pacchetti precedenti. La versione dell'applicazione resta invariata.

vipirc: tradotti altri avvisi, errori di connessione, input UTF-8, messaggi DCC e dell'interfaccia.

tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
tar -xzf vipirc-0.7.37-languages-r42.tar.gz
sh vipirc-0.7.37-languages-r42/apply.sh vipirc-0.7.37
cd vipirc-0.7.37
./configure && make
./vipirc --version

VIPIRC_LANG=it ./vipirc --help
vipirc 0.7.37 [lang-rtl-r42]

Comandi IRC, opzioni, configurazione, nomi utente, risposte del server e dati di rete restano letterali. Serve una revisione linguistica; la compilazione ncurses completa di vipirc non è stata verificata.

doc/UI-LOCALIZATION-r42.txt
doc/CLI-LOCALIZATION-r33.txt
doc/SECURITY-FULL-it.txt

NOVITÀ IN r39:
- Collegate alle traduzioni 45 stringhe visualizzate da vipirc e un avviso di limite della risposta di onekeyirc.
- Applicare solo al codice sorgente inglese r1 corrispondente e intatto; non sovrapporre le patch.

NOVITA IN r41: Tradotti la guida contestuale, i suggerimenti delle impostazioni, le etichette e gli errori. Applicare solo al sorgente inglese r1 originale.

NOVITA r41: tradotti guida ai comandi e messaggi di stato. Applicare solo al codice sorgente inglese r1 intatto.

NOVITÀ r42: guida contestuale, stati e titoli della cronologia tradotti. Applicare solo alla fonte inglese r1.
