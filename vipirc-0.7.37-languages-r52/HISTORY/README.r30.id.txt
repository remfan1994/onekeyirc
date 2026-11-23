Secara bawaan Use signals = no. Ctrl-C dan Ctrl-Z membatalkan pesan atau perintah yang belum dikirim, bukan menghentikan aplikasi. Pilih yes pada panel 0 (atau pengaturan Interface vipirc) untuk mengaktifkan sinyal terminal. Apply berlaku segera; Save juga menyimpan [input] use_signals=true|false. Pengaturan terminal asal dipulihkan saat keluar atau ditangguhkan.

vipirc 0.7.37: optional language + Arabic RTL patch r30
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch archive: vipirc-0.7.37-languages-r30.tar.gz

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r30.tar.gz
    sh vipirc-0.7.37-languages-r30/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37 && ./configure && make
    ./vipirc --version

Expected: vipirc 0.7.37 [lang-rtl-r30]
Language: VIPIRC_LANG=id
See doc/USE-SIGNALS-id.txt and README.txt.
Do not apply over r27 or older application versions.

Dokumentasi keamanan sekarang memiliki draf tujuh bagian dari dua belas/tiga belas. Bahasa Inggris tetap menjadi rujukan utama; tinjauan penutur ahli masih diperlukan.
See doc/SECURITY-PARTIAL-id.txt and doc/SECURITY-COVERAGE-r30.tsv.
