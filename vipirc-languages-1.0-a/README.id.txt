Paket bahasa opsional 1.0-a - vipirc-1.0-a (id)
Rilis alfa publik; komunitas diundang untuk meninjau terjemahan dan menguji platform.
Enter: jalankan | Esc: obrolan
Draf dipertahankan; lanjutkan penyuntingan. Tidak ada yang dikirim.
2676 kunci terjemahan per bahasa; Peninjauan penutur asli belum dilakukan.

Jalankan apply.sh pada sumber Inggris r1 yang bersih sebelum kompilasi; jangan menumpuk paket.

  tar -xzf vipirc-1.0-a.tar.gz
  tar -xzf vipirc-languages-1.0-a.tar.gz
  VIPIRC_LANG=id sh vipirc-languages-1.0-a/apply.sh vipirc-1.0-a
  cd vipirc-1.0-a
  VIPIRC_LANG=id ./configure && make
  VIPIRC_LANG=id ./vipirc --version

[lang-rtl-1.0-a]  vipirc 1.0-a
