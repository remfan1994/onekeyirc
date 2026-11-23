Arahan pakej r26

Pakej pilihan ini mengekalkan sumber Inggeris secara berasingan. Pembaca manual ncurses dan --manual kini menggunakan draf terjemahan mengikut VIPIRC_LANG jika fail tersedia dan selamat. Jika tidak, manual Inggeris terbina digunakan. RTL tidak berubah. Semakan penutur jati masih diperlukan.

vipirc-0.7.36-english-source-r1.tar.gz
vipirc-0.7.36-languages-r26.tar.gz

    tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.36-languages-r26.tar.gz
    sh vipirc-0.7.36-languages-r26/apply.sh vipirc-0.7.36
    cd vipirc-0.7.36 && ./configure && make
    VIPIRC_LANG=ms ./vipirc --manual

--version: vipirc 0.7.36 [lang-rtl-r26]
VIPIRC_MANUAL_DIR=/absolute/path (optional manual directory override)

Gunakan sumber Inggeris r1 yang baharu; jangan tampal atas pakej terdahulu.
MANUAL.txt and SECURITY.md in English remain authoritative.
Historical instructions are archived under HISTORY/ and must not be used to install r26.
