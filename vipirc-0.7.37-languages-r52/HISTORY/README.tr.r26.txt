r26 paketi talimatları

Bu isteğe bağlı paket, özgün İngilizce kaynak kodundan ayrıdır. ncurses kılavuz okuyucusu ve --manual, VIPIRC_LANG ile seçilen taslağı dosya mevcut ve güvenliyse açar; aksi halde yerleşik İngilizce kılavuz kullanılır. RTL değişmedi. Ana dili konuşanların incelemesi hâlâ gerekli.

vipirc-0.7.36-english-source-r1.tar.gz
vipirc-0.7.36-languages-r26.tar.gz

    tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.36-languages-r26.tar.gz
    sh vipirc-0.7.36-languages-r26/apply.sh vipirc-0.7.36
    cd vipirc-0.7.36 && ./configure && make
    VIPIRC_LANG=tr ./vipirc --manual

--version: vipirc 0.7.36 [lang-rtl-r26]
VIPIRC_MANUAL_DIR=/absolute/path (optional manual directory override)

Yeni çıkarılmış İngilizce r1 kaynak koduna uygulayın; önceki yamaların üzerine uygulamayın.
MANUAL.txt and SECURITY.md in English remain authoritative.
Historical instructions are archived under HISTORY/ and must not be used to install r26.
