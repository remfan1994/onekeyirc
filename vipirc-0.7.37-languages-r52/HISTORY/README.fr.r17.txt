r17 : les sections de vipirc sur l'arrière-plan, la récupération SSH, la fermeture des sessions et la mise à jour des processus actifs sont traduites en entier. Voir doc/SESSION-MANAGEMENT-fr.txt. Le manuel ncurses et SECURITY.md ne sont pas entièrement traduits ; RTL est inchangé.
--version: vipirc 0.7.36 [lang-rtl-r17]
Apply: sh vipirc-0.7.36-languages-r17/apply.sh vipirc-0.7.36

=== r16 and earlier notes (historical) ===
r16 : Ajout d’extraits traduits pour débuter avec l’interface complète (vipirc uniquement) et d’explications approfondies sur les limites de sécurité. MANUAL.txt et SECURITY.md ne sont pas traduits intégralement. Le fonctionnement RTL ne change pas par rapport à r15.
doc/NCURSES-START-fr.txt
doc/SECURITY-BOUNDARIES-fr.txt
--version: vipirc 0.7.36 [lang-rtl-r16]
Aplicar/Apply: sh vipirc-0.7.36-languages-r16/apply.sh vipirc-0.7.36

=== Prior r15 and earlier notes (historical, not current installation instructions) ===
OPTIONAL LANGUAGE + RTL PATCH r15 (fr)

r15 conserve les sources anglaises originales séparées du correctif facultatif pour dix langues et le RTL arabe. La vérification de sécurité couvre les contrôles de direction Unicode, la validation SHA-256 et le rejet des liens symboliques. Appliquer uniquement aux sources r1 fraîchement extraites, avant compilation. Voir doc/LOCALIZATION-SECURITY-fr.txt ; --version affiche [lang-rtl-r15].

-- Earlier release history --
vipirc 0.7.36 - Pack de langues r14 (fr)
========================================================

--- r14 ---
r14 conserve les sources anglaises d'origine separees du paquet facultatif contenant les traductions ET le correctif RTL arabe. Apres application avant compilation, --version affiche [lang-rtl-r14]. La version de base ne change pas. Appliquez ce paquet uniquement aux sources anglaises r1 nouvellement extraites.


--- r12 ---
r12 affine le mode d’affichage arabe facultatif. Chaque rafraîchissement de l’éditeur utilise une disposition commune au texte et au curseur, qui restent ainsi cohérents lors du retour à l’ordre logique si une allocation mémoire échoue. En mode visuel ncurses, les marques combinatoires restent avec leur lettre de base dans la cellule inférieure droite du terminal. Un déplacement vertical au-delà de la première ou dernière ligne conserve le point d’insertion. Réutiliser la disposition des paragraphes réduit le travail pendant le défilement. Les contrôles préalables évitent les conversions inutiles ou indisponibles et conservent l’affichage logique. Brouillons, messages et journaux restent en UTF-8 dans l’ordre logique. Le périmètre de traduction ne change pas ; le manuel ncurses complet et SECURITY.md restent en anglais. Les essais réels des terminaux et polices sur OpenBSD/Crostini et une relecture arabe compétente restent nécessaires. Appliquez r12 aux sources English r1 fraîchement extraites, jamais sur r11.


--- r11 ---
r11 ajoute en option la mise en forme et le retour à la ligne de l’arabe par paragraphe, avec édition visuelle, déplacement du curseur et placement à la souris dans onekeyirc, vipirc -i et vipirc --ncurses. Textes enregistrés, messages envoyés et journaux restent en UTF-8 dans l’ordre logique. FriBidi système et une locale UTF-8 sont nécessaires. Activez ONEKEYIRC_ARABIC_VISUAL=1 ou VIPIRC_ARABIC_VISUAL=1 indépendamment de la langue. Désactivez cette option si le terminal gère déjà le texte bidirectionnel. Home/End rejoignent les extrémités du document ; Backspace/Delete modifient les groupes logiques de caractères. Tests des polices sur OpenBSD/Crostini réels et relecture arabe restent à faire. Les 844 entrées du catalogue par langue et les manuels minimalistes complets sont conservés. Le manuel ncurses complet et SECURITY.md restent en anglais. Versions des applications inchangées. Appliquez r11 aux sources English r1 fraîchement extraites, jamais sur r10.


--- r10 ---
r10 traduit l'aide générée propre à chaque client et fournit les manuels complets localisés de l'interface minimaliste dans doc/MINIMAL-MANUAL-fr.txt. onekeyirc --manual et vipirc -i --manual respectent désormais la langue d'affichage choisie, écrivent du texte UTF-8 dans l'ordre logique et ne nécessitent aucun terminal. Le manuel de l'interface complète ncurses de vipirc et SECURITY.md restent en anglais ; d'autres éléments d'interface, messages de diagnostic et l'édition de texte de droite à gauche restent à terminer. Les traductions nécessitent encore une relecture par des locuteurs natifs. Appliquez r10 uniquement à une copie neuve des sources anglaises r1 ; n'appliquez jamais r10 par-dessus r9 ou un autre paquet de langues.


--- r9 ---
État actuel de r9 : les 474 lignes traduisibles des tables fixes d'aide sur les commandes et les sujets sont traduites. Trois lignes contenant uniquement des correspondances d'alias de commandes restent littérales ; les 477 lignes des tables ont des entrées dans le catalogue. Après application du paquet, consultez doc/HELP-REFERENCE-fr.txt. MANUAL.txt, SECURITY.md et une partie de l'aide et des diagnostics générés ou propres à chaque client restent en anglais. Les traductions nécessitent encore une relecture par des locuteurs natifs. Appliquez r9 uniquement à une copie neuve des sources anglaises r1, jamais par-dessus r8 ou un autre paquet.


Ce pack fournit des traductions de l’interface, de l’aide et de certains documents. Ce n’est pas un programme autonome.

Utilisez uniquement l’archive source English r1 indiquée ci-dessous. Une ancienne archive portant le même numéro de version, ou des sources déjà modifiées, ne sont pas compatibles.
  vipirc-0.7.36-english-source-r1.tar.gz

Extrayez les deux archives dans le même répertoire parent :
  tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.36-languages-r15.tar.gz

Appliquez le pack aux sources English r1 intactes AVANT de lancer configure ou make :
  sh vipirc-0.7.36-languages-r15/apply.sh vipirc-0.7.36

Configurez et compilez le programme :
  cd vipirc-0.7.36
  ./configure --prefix=/usr/local
  make

Lancez le programme avec l’interface en français :
  VIPIRC_LANG=fr ./vipirc

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

VIPIRC_LANG=ar VIPIRC_ARABIC_VISUAL=1 ./vipirc -i
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-fr.txt
