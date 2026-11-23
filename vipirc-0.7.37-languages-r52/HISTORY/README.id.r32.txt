Patch r32 menerjemahkan pesan kesalahan awal dan ringkasan --help. Perintah IRC dan opsi tetap berbahasa Inggris. Semua dokumen SECURITY.md sudah dicakup sebagai draf; teks Inggris tetap rujukan utama.

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
Language: VIPIRC_LANG=id
Use --help for a localized introduction and English option list.
See doc/CLI-LOCALIZATION-r32.txt and doc/SECURITY-FULL-id.txt.
Do not layer patches or alter the English source version.
