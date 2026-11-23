r24 aggiunge bozze di traduzione complete per MINIMALIST INTERFACE e GLOBAL HEADER AND CHAT CONTROLS. Sono coperti 47 dei 52 capitoli del manuale vipirc; è ancora necessaria la revisione di parlanti madrelingua.

Codice sorgente inglese originale: vipirc-0.7.36-english-source-r1.tar.gz
Pacchetto di lingue e patch RTL facoltativa: vipirc-0.7.36-languages-r24.tar.gz

Estrarre entrambi gli archivi nella stessa directory superiore.
Applicare la patch una sola volta, prima della configurazione e della compilazione:
    sh vipirc-0.7.36-languages-r24/apply.sh vipirc-0.7.36

Compilazione: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r24]
Lingua: VIPIRC_LANG=it

Non applicare sopra una patch precedente; usare una nuova estrazione del sorgente inglese r1.
Il sorgente inglese rimane invariato. I file MANUAL.txt e SECURITY.md inglesi sono i riferimenti principali; le traduzioni preliminari richiedono revisione da madrelingua.
Le istruzioni obsolete sono conservate in HISTORY/; non eseguirne i comandi.
