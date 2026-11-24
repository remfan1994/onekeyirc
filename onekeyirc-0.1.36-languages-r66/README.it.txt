Patch linguistica facoltativa r66 - onekeyirc-0.1.36 (it).
Panoramica della riga di comando (opzioni invariate):
L'estrazione da sola non attiva le traduzioni. Esegui apply.sh sul sorgente inglese r1 pulito prima della compilazione. Non sovrapporre pacchetti precedenti.

Nella vista log, / apre Comandi; digitare normalmente apre Componi.
Registrazione persistente non disponibile per questo server:

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r66.tar.gz
  sh onekeyirc-0.1.36-languages-r66/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=it ./onekeyirc --version
  ONEKEYIRC_LANG=it ./onekeyirc --help

Indicatore di versione richiesto: onekeyirc 0.1.36 [lang-rtl-r66]
15 nuove chiavi e 1658 chiavi per lingua; la versione dell'applicazione non cambia. Revisione da parte di madrelingua ancora da completare.

Il profilo server salvato non è stato trovato.
Impostazioni salvate; impossibile salvare l’uso dei comandi:

Questo aggiornamento traduce errori di rete/TLS e stati di connessione; i dettagli di sistema restano invariati.

Novità: avvisi di disconnessione sicura, errori SASL e messaggi annullati. I comandi del protocollo restano invariati.

Decifratura GPG non riuscita; sbloccare la chiave fuori dal client e riconnettersi.

L'autenticazione richiede TLS verificato

Novita: tradotti 15 avvisi di attivita IRC (JOIN/PART/QUIT/NICK/KICK/MODE/INVITE/TOPIC/CTCP). Nomi utente e testi del server restano invariati.
