Le correctif r32 traduit les erreurs de démarrage et le résumé de --help. Les commandes et les options IRC restent en anglais. Tout SECURITY.md est couvert par des brouillons ; le texte anglais fait autorité.

vipirc 0.7.37 - optional language and Arabic RTL patch r32
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch: vipirc-0.7.37-languages-r32.tar.gz
    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r32.tar.gz
    sh vipirc-0.7.37-languages-r32/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37
    ./configure && make
    ./vipirc --version
Expected: vipirc 0.7.37 [lang-rtl-r32]
Language: VIPIRC_LANG=fr
Use --help for a localized introduction and English option list.
See doc/CLI-LOCALIZATION-r32.txt and doc/SECURITY-FULL-fr.txt.
Do not layer patches or alter the English source version.
