Instruksi paket r26

Paket opsional ini menjaga sumber Inggris tetap terpisah. Pembaca manual ncurses dan --manual kini memakai draf terjemahan sesuai VIPIRC_LANG bila berkas aman dan tersedia. Jika tidak, manual Inggris bawaan digunakan. RTL tidak berubah. Tinjauan penutur asli tetap diperlukan.

vipirc-0.7.36-english-source-r1.tar.gz
vipirc-0.7.36-languages-r26.tar.gz

    tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.36-languages-r26.tar.gz
    sh vipirc-0.7.36-languages-r26/apply.sh vipirc-0.7.36
    cd vipirc-0.7.36 && ./configure && make
    VIPIRC_LANG=id ./vipirc --manual

--version: vipirc 0.7.36 [lang-rtl-r26]
VIPIRC_MANUAL_DIR=/absolute/path (optional manual directory override)

Gunakan sumber Inggris r1 baru; jangan menimpa paket sebelumnya.
MANUAL.txt and SECURITY.md in English remain authoritative.
Historical instructions are archived under HISTORY/ and must not be used to install r26.
