Correctif linguistique facultatif r66 - onekeyirc-0.1.36 (fr).
Présentation de la ligne de commande (noms d'options inchangés) :
L'extraction seule n'active pas les traductions. Exécutez apply.sh sur le code source anglais r1 intact avant compilation. Ne superposez pas les packs.

Dans le journal, / ouvre Commande ; la saisie ordinaire ouvre Rédaction.
Journalisation persistante indisponible pour ce serveur :

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r66.tar.gz
  sh onekeyirc-0.1.36-languages-r66/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=fr ./onekeyirc --version
  ONEKEYIRC_LANG=fr ./onekeyirc --help

Marqueur de version requis : onekeyirc 0.1.36 [lang-rtl-r66]
15 nouvelles clés et 1658 clés par langue ; la version de l'application ne change pas. Relecture par des locuteurs natifs en attente.

Le profil de serveur enregistré est introuvable.
Paramètres enregistrés ; impossible d’enregistrer l’usage des commandes :

Cette mise à jour traduit les erreurs réseau/TLS et les états de connexion ; les détails du système ne changent pas.

Nouveau : déconnexions de sécurité, erreurs SASL et messages annulés. Les commandes du protocole restent inchangées.

Échec du déchiffrement GPG ; déverrouillez la clé hors du client, puis reconnectez-vous.

L'authentification exige une connexion TLS vérifiée

Nouveau : 15 avis d'activite IRC (JOIN/PART/QUIT/NICK/KICK/MODE/INVITE/TOPIC/CTCP) traduits. Les pseudonymes et les textes du serveur restent inchanges.
