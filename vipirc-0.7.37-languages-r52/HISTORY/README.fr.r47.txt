Pack de langues facultatif r47: vipirc-0.7.37

Appliquez-le uniquement au code source anglais r1 intact ; jamais par-dessus r46 ou un autre pack.
Le résumé regroupe par serveur les serveurs, le canal 0, les canaux rejoints ou quittés, les conversations privées, les non-lus, les mentions, les opérations en attente et les transferts. Les mentions ne sont pas ajoutées au total des non-lus.
 Un message déjà en cours d'envoi sera terminé.
La relecture par des personnes maîtrisant les langues et le test ncurses complet de vipirc restent nécessaires.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r47.tar.gz
  sh vipirc-0.7.37-languages-r47/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=fr ./vipirc --help

vipirc 0.7.37 [lang-rtl-r47]

doc/UI-LOCALIZATION-r47.txt
HISTORY/README.fr.r46.txt

r47: Connexion directe non chiffrée. Accepter révèle votre adresse IP à l'autre personne.

r47 : aide de l’explorateur de fichiers et états d’authentification traduits.
