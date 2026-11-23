SECURITY.md dosyasının dört bölümü çeviri taslağı olarak eklendi. İngilizce güvenlik belgesi yetkili kaynaktır. Komutları ve güvenlik ayarlarını İngilizce kaynakla karşılaştırın; akıcı konuşan uzmanların incelemesi hâlâ gereklidir.

vipirc 0.7.36: optional language + RTL patch r27
English source: vipirc-0.7.36-english-source-r1.tar.gz
Patch: vipirc-0.7.36-languages-r27.tar.gz

    tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.36-languages-r27.tar.gz
    sh vipirc-0.7.36-languages-r27/apply.sh vipirc-0.7.36
    cd vipirc-0.7.36 && ./configure && make
    ./vipirc --version    # vipirc 0.7.36 [lang-rtl-r27]

Language: VIPIRC_LANG=tr
Draft security chapters: doc/SECURITY-PARTIAL-tr.txt
Apply only to a fresh English r1 source tree, not over an earlier patch.
English SECURITY.md remains authoritative. Earlier instructions: HISTORY/.
