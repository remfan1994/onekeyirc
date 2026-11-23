Pack de langues facultatif r52: vipirc-0.7.37

Appliquez-le uniquement au code source anglais r1 intact ; jamais par-dessus r51 ou un autre pack.
Saisissez le nom d'hôte ou l'adresse IP du serveur IRC, sans URL ni port. Pour ZNC ou soju, utilisez /panel.
Choisissez none, file (fichier texte privé), gpg (fichier chiffré) ou plain (valeur masquée dans la configuration).
direct = envoyer avec Enter ; review = vérifier le journal puis confirmer avec un second Enter. Tous les serveurs.
Un pseudonyme ou un masque nick!user@host.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r52.tar.gz
  sh vipirc-0.7.37-languages-r52/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=fr ./vipirc --version

vipirc 0.7.37 [lang-rtl-r52]
doc/SETUP-HELP-r52.txt
HISTORY/README.fr.r51.txt
