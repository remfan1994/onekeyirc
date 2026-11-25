Pek bahasa pilihan 1.0-a - vipirc-1.0-a (ms)
Keluaran alfa awam; komuniti dialu-alukan untuk menyemak terjemahan dan menguji platform.
Enter: jalankan | Esc: sembang
Draf dikekalkan; teruskan penyuntingan. Tiada apa-apa dihantar.
2676 kunci terjemahan bagi setiap bahasa; Semakan penutur asli belum dilakukan.

Jalankan apply.sh pada sumber Inggeris r1 yang bersih sebelum kompilasi; jangan tindihkan pek.

  tar -xzf vipirc-1.0-a.tar.gz
  tar -xzf vipirc-languages-1.0-a.tar.gz
  VIPIRC_LANG=ms sh vipirc-languages-1.0-a/apply.sh vipirc-1.0-a
  cd vipirc-1.0-a
  VIPIRC_LANG=ms ./configure && make
  VIPIRC_LANG=ms ./vipirc --version

[lang-rtl-1.0-a]  vipirc 1.0-a
