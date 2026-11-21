onekeyirc 0.1.35 - Pack de langues r9 (fr)
========================================================

--- r9 ---
État actuel de r9 : les 474 lignes traduisibles des tables fixes d'aide sur les commandes et les sujets sont traduites. Trois lignes contenant uniquement des correspondances d'alias de commandes restent littérales ; les 477 lignes des tables ont des entrées dans le catalogue. Après application du paquet, consultez doc/HELP-REFERENCE-fr.txt. MANUAL.txt, SECURITY.md et une partie de l'aide et des diagnostics générés ou propres à chaque client restent en anglais. Les traductions nécessitent encore une relecture par des locuteurs natifs. Appliquez r9 uniquement à une copie neuve des sources anglaises r1, jamais par-dessus r8 ou un autre paquet.


Ce pack fournit des traductions de l’interface, de l’aide et de certains documents. Ce n’est pas un programme autonome.

Utilisez uniquement l’archive source English r1 indiquée ci-dessous. Une ancienne archive portant le même numéro de version, ou des sources déjà modifiées, ne sont pas compatibles.
  onekeyirc-0.1.35-english-source-r1.tar.gz

Extrayez les deux archives dans le même répertoire parent :
  tar -xzf onekeyirc-0.1.35-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.35-languages-r9.tar.gz

Appliquez le pack aux sources English r1 intactes AVANT de lancer configure ou make :
  sh onekeyirc-0.1.35-languages-r9/apply.sh onekeyirc-0.1.35

Configurez et compilez le programme :
  cd onekeyirc-0.1.35
  ./configure --prefix=/usr/local
  make

Lancez le programme avec l’interface en français :
  ONEKEYIRC_LANG=fr ./onekeyirc

Les commandes IRC, les options de lancement et les clés de configuration ne sont pas traduites. Saisissez-les exactement comme indiqué.

Ces traductions sont provisoires : une partie de l’aide et les manuels complets restent en anglais. Consultez MANUAL.txt et SECURITY.md en anglais pour les instructions de sécurité exactes. Utilisez un terminal UTF-8.

Nouveau en r4 : /help background, /help catchup et /help logging sont entièrement traduits. RECOVERY-fr.txt explique la reprise des sessions et les limites des journaux.

PACK-SHA256SUMS contient les sommes de contrôle. apply.sh vérifie la compatibilité des sources. Ne copiez pas les fichiers manuellement sur des sources modifiées.


PACK-SHA256SUMS / README.txt / MANUAL.txt / SECURITY.md

Nouveau dans r5 : /help autocommands, /help dcc, /help bindings, /help bind, /help unbind, /help transfers et /help hotkeys complets. Voir doc/WORKFLOWS-fr.txt.

Nouveau dans r6 : /help configuration, /help summary, /help server, /help users et /help watch sont complets. Consultez doc/CONFIGURATION-fr.txt et doc/STATUS-fr.txt après installation.

Nouveau dans r7 : 32 explications d'aide supplémentaires pour /help set, /help list, la navigation locale et les alias. Après installation, lisez doc/SETTINGS-fr.txt et doc/CHANNELS-fr.txt. Couverture : 384/477 ; 93 lignes restent en anglais.

--- r8 ---
L'affichage visuel de l'arabe est expérimental et limité à l'interface minimaliste. Installez la bibliothèque système FriBidi puis activez la variable ci-dessous. Ne l'activez pas si le terminal façonne déjà l'arabe. L'éditeur, le texte enregistré et l'interface ncurses complète ne changent pas. Sans FriBidi, l'ordre Unicode d'origine reste intact.

ONEKEYIRC_LANG=ar ONEKEYIRC_ARABIC_VISUAL=1 ./onekeyirc
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-fr.txt
