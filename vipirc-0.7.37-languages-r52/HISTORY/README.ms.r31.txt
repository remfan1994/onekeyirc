Use signals = no secara lalai. Ctrl-C dan Ctrl-Z membatalkan mesej atau arahan yang belum dihantar, bukan menamatkan aplikasi. Pilih yes dalam panel 0 (atau tetapan Interface vipirc) untuk mengaktifkan isyarat terminal. Apply berkuat kuasa serta-merta; Save turut menyimpan [input] use_signals=true|false. Tetapan terminal asal dipulihkan apabila keluar atau digantung.

vipirc 0.7.37: optional language + Arabic RTL patch r31
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch archive: vipirc-0.7.37-languages-r31.tar.gz

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r31.tar.gz
    sh vipirc-0.7.37-languages-r31/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37 && ./configure && make
    ./vipirc --version

Expected: vipirc 0.7.37 [lang-rtl-r31]
Language: VIPIRC_LANG=ms
See doc/USE-SIGNALS-ms.txt and README.txt.
Do not apply over r27 or older application versions.

Dokumentasi keselamatan kini mempunyai draf tujuh daripada dua belas/tiga belas bahagian. Teks Inggeris masih menjadi rujukan utama; semakan penutur mahir diperlukan.
See doc/SECURITY-FULL-ms.txt and doc/SECURITY-COVERAGE-r31.tsv.

Security documentation r31: full section coverage as unreviewed drafts.
See doc/SECURITY-FULL-XX.txt (XX = locale), and use original English SECURITY.md as authority.
DRAF TERJEMAHAN. Belum disemak penutur fasih yang memahami keselamatan. SECURITY.md bahasa Inggeris kekal sebagai rujukan berwibawa.
