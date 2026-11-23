r17: Sono state tradotte integralmente le sezioni di vipirc su background, ripristino SSH, chiusura delle sessioni e aggiornamento dei processi attivi. Vedere doc/SESSION-MANAGEMENT-it.txt. Il manuale ncurses completo e SECURITY.md non sono ancora tradotti integralmente; RTL resta invariato.
--version: vipirc 0.7.36 [lang-rtl-r17]
Apply: sh vipirc-0.7.36-languages-r17/apply.sh vipirc-0.7.36

=== r16 and earlier notes (historical) ===
r16: Aggiunti estratti tradotti per iniziare con l’interfaccia completa (solo vipirc) e spiegazioni più dettagliate del modello di sicurezza. MANUAL.txt e SECURITY.md non sono ancora tradotti integralmente. Il comportamento RTL resta invariato rispetto a r15.
doc/NCURSES-START-it.txt
doc/SECURITY-BOUNDARIES-it.txt
--version: vipirc 0.7.36 [lang-rtl-r16]
Aplicar/Apply: sh vipirc-0.7.36-languages-r16/apply.sh vipirc-0.7.36

=== Prior r15 and earlier notes (historical, not current installation instructions) ===
OPTIONAL LANGUAGE + RTL PATCH r15 (it)

r15 mantiene separate le sorgenti inglesi originali dalla patch facoltativa per dieci lingue e RTL arabo. La revisione della sicurezza include controlli della direzione Unicode, verifica SHA-256 e rifiuto dei collegamenti simbolici. Applicare soltanto a una nuova copia r1 prima della compilazione. Vedere doc/LOCALIZATION-SECURITY-it.txt; --version mostra [lang-rtl-r15].

-- Earlier release history --
vipirc 0.7.36 - Pacchetto lingue r14 (it)
========================================================

--- r14 ---
r14 mantiene separati i sorgenti inglesi originali dal pacchetto facoltativo con traduzioni E patch RTL arabo. Se applicato prima della compilazione, --version mostra [lang-rtl-r14]. La versione base del programma non cambia. Applicare solo ai sorgenti inglesi r1 appena estratti.


--- r12 ---
r12 perfeziona la modalità facoltativa di visualizzazione dell’arabo. Ogni aggiornamento dell’editor usa una sola disposizione condivisa per testo e cursore, che restano coerenti quando un errore di allocazione della memoria richiede il ritorno all’ordine logico. Nella modalità visiva di ncurses, i segni combinanti restano con la lettera di base nella cella inferiore destra del terminale. Il movimento verticale oltre la prima o l’ultima riga conserva il punto di inserimento. Riutilizzare la disposizione dei paragrafi riduce il lavoro durante lo scorrimento. I controlli preliminari evitano conversioni inutili o non disponibili e mantengono la visualizzazione logica. Bozze, messaggi e registri restano in UTF-8 e ordine logico. L’ambito della traduzione non cambia; il manuale ncurses completo e SECURITY.md restano in inglese. Servono ancora prove reali di terminali e caratteri su OpenBSD/Crostini e una revisione da parte di parlanti arabi competenti. Applicare r12 solo ai sorgenti English r1 appena estratti, mai sopra r11.


--- r11 ---
r11 aggiunge il supporto opzionale alle forme contestuali arabe e all’andata a capo per paragrafo, con modifica visiva, movimento del cursore e posizionamento tramite mouse in onekeyirc, vipirc -i e vipirc --ncurses. Testi salvati, messaggi inviati e registri mantengono UTF-8 in ordine logico. Servono FriBidi di sistema e una locale UTF-8. Attivare ONEKEYIRC_ARABIC_VISUAL=1 o VIPIRC_ARABIC_VISUAL=1 indipendentemente dalla lingua. Lasciare disattivato se il terminale gestisce già il testo bidirezionale. Home/End raggiungono gli estremi del documento; Backspace/Delete modificano gruppi logici di caratteri. Restano da verificare i caratteri su terminali OpenBSD/Crostini reali e da revisionare l’arabo. Sono conservate le 844 voci del catalogo per lingua e i manuali minimalisti completi. Il manuale ncurses completo e SECURITY.md restano in inglese. Versioni delle applicazioni invariate. Applicare r11 ai sorgenti English r1 appena estratti, mai sopra r10.


--- r10 ---
r10 traduce la guida generata specifica dei singoli client e include i manuali completi localizzati dell'interfaccia minimalista in doc/MINIMAL-MANUAL-it.txt. onekeyirc --manual e vipirc -i --manual ora rispettano la lingua di visualizzazione scelta, scrivono testo UTF-8 in ordine logico e non richiedono un terminale. Il manuale dell'interfaccia completa ncurses di vipirc e SECURITY.md restano in inglese; altre parti dell'interfaccia, messaggi diagnostici e l'editing del testo da destra a sinistra restano da completare. Le traduzioni richiedono ancora una revisione madrelingua. Applica r10 soltanto a una copia nuova dei sorgenti inglesi r1; non applicare mai r10 sopra r9 o un altro pacchetto di lingue.


--- r9 ---
Stato attuale di r9: sono tradotte tutte le 474 righe traducibili delle tabelle fisse della guida a comandi e argomenti. Tre righe contenenti soltanto corrispondenze tra alias di comandi restano letterali; tutte le 477 righe delle tabelle hanno voci nel catalogo. Dopo l'applicazione del pacchetto, consulta doc/HELP-REFERENCE-it.txt. MANUAL.txt, SECURITY.md e parte della guida e dei messaggi diagnostici generati o specifici dei singoli client restano in inglese. Le traduzioni richiedono ancora una revisione madrelingua. Applica r9 soltanto a una copia nuova dei sorgenti inglesi r1, mai sopra r8 o un altro pacchetto.


Questo pacchetto aggiunge traduzioni dell’interfaccia, della guida e di alcuni documenti. Non è un programma autonomo.

Usare soltanto l’archivio sorgente English r1 indicato sotto. Un vecchio archivio con la stessa versione, o sorgenti già modificati, non sono compatibili.
  vipirc-0.7.36-english-source-r1.tar.gz

Estrarre entrambi gli archivi nella stessa directory superiore:
  tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.36-languages-r15.tar.gz

Applicare il pacchetto ai sorgenti English r1 intatti PRIMA di eseguire configure o make:
  sh vipirc-0.7.36-languages-r15/apply.sh vipirc-0.7.36

Configurare e compilare il programma:
  cd vipirc-0.7.36
  ./configure --prefix=/usr/local
  make

Avviare il programma con l’interfaccia in italiano:
  VIPIRC_LANG=it ./vipirc

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

VIPIRC_LANG=ar VIPIRC_ARABIC_VISUAL=1 ./vipirc -i
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-it.txt
