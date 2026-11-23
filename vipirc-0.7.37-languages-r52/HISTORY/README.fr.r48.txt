Pack de langues facultatif r48: vipirc-0.7.37

Appliquez-le uniquement au code source anglais r1 intact ; jamais par-dessus r47 ou un autre pack.
Le résumé regroupe par serveur les serveurs, le canal 0, les canaux rejoints ou quittés, les conversations privées, les non-lus, les mentions, les opérations en attente et les transferts. Les mentions ne sont pas ajoutées au total des non-lus.
 Un message déjà en cours d'envoi sera terminé.
La relecture par des personnes maîtrisant les langues et le test ncurses complet de vipirc restent nécessaires.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r48.tar.gz
  sh vipirc-0.7.37-languages-r48/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=fr ./vipirc --help

vipirc 0.7.37 [lang-rtl-r48]

doc/UI-LOCALIZATION-r48.txt
HISTORY/README.fr.r47.txt

r48: Accepter / Refuser / Annuler le transfert.
Les transferts DCC utilisent une connexion directe non chiffrée et révèlent au correspondant l'adresse réseau choisie. Recevoir une offre n'ouvre à lui seul ni fichier ni connexion.
