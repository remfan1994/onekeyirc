Paket bahasa opsional r87 - vipirc-0.7.37 (id)
Enter: jalankan | Esc: obrolan
Draf dipertahankan; lanjutkan penyuntingan. Tidak ada yang dikirim.
2437 keys / 20 new; Peninjauan penutur asli belum dilakukan.

Jalankan apply.sh pada sumber Inggris r1 yang bersih sebelum kompilasi; jangan menumpuk paket.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r87.tar.gz
  sh vipirc-0.7.37-languages-r87/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=id ./vipirc --version

[lang-rtl-r87]  vipirc 0.7.37
