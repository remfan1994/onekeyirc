onekeyirc 0.1.35 - Pacchetto lingue r9 (it)
========================================================

--- r9 ---
Stato attuale di r9: sono tradotte tutte le 474 righe traducibili delle tabelle fisse della guida a comandi e argomenti. Tre righe contenenti soltanto corrispondenze tra alias di comandi restano letterali; tutte le 477 righe delle tabelle hanno voci nel catalogo. Dopo l'applicazione del pacchetto, consulta doc/HELP-REFERENCE-it.txt. MANUAL.txt, SECURITY.md e parte della guida e dei messaggi diagnostici generati o specifici dei singoli client restano in inglese. Le traduzioni richiedono ancora una revisione madrelingua. Applica r9 soltanto a una copia nuova dei sorgenti inglesi r1, mai sopra r8 o un altro pacchetto.


Questo pacchetto aggiunge traduzioni dell’interfaccia, della guida e di alcuni documenti. Non è un programma autonomo.

Usare soltanto l’archivio sorgente English r1 indicato sotto. Un vecchio archivio con la stessa versione, o sorgenti già modificati, non sono compatibili.
  onekeyirc-0.1.35-english-source-r1.tar.gz

Estrarre entrambi gli archivi nella stessa directory superiore:
  tar -xzf onekeyirc-0.1.35-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.35-languages-r9.tar.gz

Applicare il pacchetto ai sorgenti English r1 intatti PRIMA di eseguire configure o make:
  sh onekeyirc-0.1.35-languages-r9/apply.sh onekeyirc-0.1.35

Configurare e compilare il programma:
  cd onekeyirc-0.1.35
  ./configure --prefix=/usr/local
  make

Avviare il programma con l’interfaccia in italiano:
  ONEKEYIRC_LANG=it ./onekeyirc

I comandi IRC, le opzioni da riga di comando e le chiavi di configurazione non vengono tradotti. Digitarli esattamente come mostrato.

Le traduzioni sono ancora bozze; parte della guida e i manuali completi restano in inglese. Consultare MANUAL.txt e SECURITY.md in inglese per le istruzioni di sicurezza esatte. Usare un terminale UTF-8.

Novità r4: /help background, /help catchup e /help logging sono completamente tradotti. RECOVERY-it.txt spiega il recupero delle sessioni e i limiti dei registri.

PACK-SHA256SUMS contiene le somme di controllo. apply.sh verifica la compatibilità dei sorgenti. Non copiare i file manualmente sopra sorgenti modificati.


PACK-SHA256SUMS / README.txt / MANUAL.txt / SECURITY.md

Nuovo in r5: completati /help autocommands, /help dcc, /help bindings, /help bind, /help unbind, /help transfers e /help hotkeys. Vedi doc/WORKFLOWS-it.txt.

Novità di r6: /help configuration, /help summary, /help server, /help users e /help watch sono completi. Leggere doc/CONFIGURATION-it.txt e doc/STATUS-it.txt dopo l’installazione.

Novità in r7: altre 32 spiegazioni per /help set, /help list, navigazione locale e alias. Dopo l'installazione, consultare doc/SETTINGS-it.txt e doc/CHANNELS-it.txt. Copertura: 384/477; restano 93 spiegazioni in inglese.

--- r8 ---
La resa visiva dell'arabo è sperimentale e vale solo per l'interfaccia minimalista. Installa la libreria di sistema FriBidi e abilita la variabile sotto. Non usarla se il terminale forma già l'arabo. Editor, dati salvati e interfaccia ncurses completa restano invariati. Senza FriBidi rimane l'ordine Unicode originale.

ONEKEYIRC_LANG=ar ONEKEYIRC_ARABIC_VISUAL=1 ./onekeyirc
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-it.txt
