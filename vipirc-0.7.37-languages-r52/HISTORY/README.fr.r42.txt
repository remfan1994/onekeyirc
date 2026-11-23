Paquet facultatif r42 de langues et d'affichage arabe de droite à gauche.
vipirc 0.7.37 - r42

À appliquer uniquement aux sources anglaises r1 originales et intactes. Ne pas installer sur r41 ou un ancien paquet. La version de l'application ne change pas.

vipirc : nouveaux avertissements, erreurs de connexion, saisie UTF-8, DCC et messages d'interface traduits.

tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
tar -xzf vipirc-0.7.37-languages-r42.tar.gz
sh vipirc-0.7.37-languages-r42/apply.sh vipirc-0.7.37
cd vipirc-0.7.37
./configure && make
./vipirc --version

VIPIRC_LANG=fr ./vipirc --help
vipirc 0.7.37 [lang-rtl-r42]

Commandes IRC, options, configuration, pseudos, réponses du serveur et données réseau restent littéraux. Une révision linguistique est nécessaire ; la compilation ncurses complète de vipirc n'est pas vérifiée.

doc/UI-LOCALIZATION-r42.txt
doc/CLI-LOCALIZATION-r33.txt
doc/SECURITY-FULL-fr.txt

NOUVEAUTÉS DE r39 :
- 45 messages affichés par vipirc sont désormais reliés aux traductions, ainsi qu'un avis de limite de réponse d'onekeyirc.
- À appliquer uniquement aux sources anglaises r1 correspondantes et intactes ; ne superposez pas les correctifs.

NOUVEAUTES r41 : Aide contextuelle, conseils de réglage, étiquettes et erreurs traduits. Appliquer uniquement à la source anglaise r1 intacte.

NOUVEAU EN r41 : aide aux commandes et messages d'etat traduits. Appliquer uniquement aux sources anglaises r1 intactes.

NOUVEAU r42 : aide contextuelle, statuts et titres d'historique traduits. Appliquer seulement sur la source anglaise r1.
