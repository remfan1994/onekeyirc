r23 aggiunge le traduzioni complete dei capitoli SETTINGS e FILES AND ENVIRONMENT del manuale vipirc. Le bozze coprono 45 sezioni su 52; serve ancora la revisione di madrelingua.

Codice sorgente inglese originale: vipirc-0.7.36-english-source-r1.tar.gz
Pacchetto di lingue e patch RTL facoltativa: vipirc-0.7.36-languages-r23.tar.gz

Estrarre entrambi gli archivi nella stessa directory superiore.
Applicare la patch una sola volta, prima della configurazione e della compilazione:
    sh vipirc-0.7.36-languages-r23/apply.sh vipirc-0.7.36

Compilazione: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r23]
Lingua: VIPIRC_LANG=it

Non applicare sopra una patch precedente; usare una nuova estrazione del sorgente inglese r1.
Il sorgente inglese rimane invariato. I file MANUAL.txt e SECURITY.md inglesi sono i riferimenti principali; le traduzioni preliminari richiedono revisione da madrelingua.
Le istruzioni obsolete sono conservate in HISTORY/; non eseguirne i comandi.
