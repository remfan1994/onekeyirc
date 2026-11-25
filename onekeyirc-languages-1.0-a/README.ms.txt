Pek bahasa pilihan 1.0-a - onekeyirc-1.0-a (ms)
Keluaran alfa awam; komuniti dialu-alukan untuk menyemak terjemahan dan menguji platform.
Laporan /summary
Terminal interaktif yang serasi dengan ANSI/VT diperlukan
2195 kunci terjemahan bagi setiap bahasa; Semakan penutur asli belum dilakukan.

Jalankan apply.sh pada sumber Inggeris r1 yang bersih sebelum kompilasi; jangan tindihkan pek.

  tar -xzf onekeyirc-1.0-a.tar.gz
  tar -xzf onekeyirc-languages-1.0-a.tar.gz
  ONEKEYIRC_LANG=ms sh onekeyirc-languages-1.0-a/apply.sh onekeyirc-1.0-a
  cd onekeyirc-1.0-a
  ONEKEYIRC_LANG=ms ./configure && make
  ONEKEYIRC_LANG=ms ./onekeyirc --version

[lang-rtl-1.0-a]  onekeyirc 1.0-a
