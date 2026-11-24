Paket bahasa opsional r85 - onekeyirc-0.1.36 (id)
Laporan /summary
Terminal interaktif yang kompatibel dengan ANSI/VT diperlukan
2010 keys / 2 new; Peninjauan penutur asli belum dilakukan.

Jalankan apply.sh pada sumber Inggris r1 yang bersih sebelum kompilasi; jangan menumpuk paket.

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r85.tar.gz
  sh onekeyirc-0.1.36-languages-r85/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=id ./onekeyirc --version

[lang-rtl-r85]  onekeyirc 0.1.36
