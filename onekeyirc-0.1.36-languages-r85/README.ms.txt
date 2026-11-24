Pek bahasa pilihan r85 - onekeyirc-0.1.36 (ms)
Laporan /summary
Terminal interaktif yang serasi dengan ANSI/VT diperlukan
2010 keys / 2 new; Semakan penutur asli belum dilakukan.

Jalankan apply.sh pada sumber Inggeris r1 yang bersih sebelum kompilasi; jangan tindihkan pek.

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r85.tar.gz
  sh onekeyirc-0.1.36-languages-r85/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=ms ./onekeyirc --version

[lang-rtl-r85]  onekeyirc 0.1.36
