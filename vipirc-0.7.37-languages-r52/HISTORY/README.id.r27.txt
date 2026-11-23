Empat bagian SECURITY.md telah diterjemahkan sebagai draf. Dokumen keamanan Inggris tetap menjadi acuan resmi. Periksa perintah dan nilai keamanan terhadap naskah Inggris; tinjauan penutur mahir masih diperlukan.

vipirc 0.7.36: optional language + RTL patch r27
English source: vipirc-0.7.36-english-source-r1.tar.gz
Patch: vipirc-0.7.36-languages-r27.tar.gz

    tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.36-languages-r27.tar.gz
    sh vipirc-0.7.36-languages-r27/apply.sh vipirc-0.7.36
    cd vipirc-0.7.36 && ./configure && make
    ./vipirc --version    # vipirc 0.7.36 [lang-rtl-r27]

Language: VIPIRC_LANG=id
Draft security chapters: doc/SECURITY-PARTIAL-id.txt
Apply only to a fresh English r1 source tree, not over an earlier patch.
English SECURITY.md remains authoritative. Earlier instructions: HISTORY/.
