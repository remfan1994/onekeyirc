r25 contiene bozze di tutte le 52 sezioni del manuale completo di vipirc. Sono stati tradotti gli ultimi cinque capitoli e assemblato un manuale offline per lingua. La guida ncurses integrata resta inglese. Serve revisione madrelingua.

Codice sorgente inglese originale: vipirc-0.7.36-english-source-r1.tar.gz
Pacchetto di lingue e patch RTL facoltativa: vipirc-0.7.36-languages-r25.tar.gz

Estrarre entrambi gli archivi nella stessa directory superiore.
Applicare la patch una sola volta, prima della configurazione e della compilazione:
    sh vipirc-0.7.36-languages-r25/apply.sh vipirc-0.7.36

Compilazione: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r25]
Lingua: VIPIRC_LANG=it

Non applicare sopra una patch precedente; usare una nuova estrazione del sorgente inglese r1.
Il sorgente inglese rimane invariato. I file MANUAL.txt e SECURITY.md inglesi sono i riferimenti principali; le traduzioni preliminari richiedono revisione da madrelingua.
Le istruzioni obsolete sono conservate in HISTORY/; non eseguirne i comandi.
