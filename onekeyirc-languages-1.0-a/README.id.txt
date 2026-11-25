Paket bahasa opsional 1.0-a - onekeyirc-1.0-a (id)
Rilis alfa publik; komunitas diundang untuk meninjau terjemahan dan menguji platform.
Laporan /summary
Terminal interaktif yang kompatibel dengan ANSI/VT diperlukan
2195 kunci terjemahan per bahasa; Peninjauan penutur asli belum dilakukan.

Jalankan apply.sh pada sumber Inggris r1 yang bersih sebelum kompilasi; jangan menumpuk paket.

  tar -xzf onekeyirc-1.0-a.tar.gz
  tar -xzf onekeyirc-languages-1.0-a.tar.gz
  ONEKEYIRC_LANG=id sh onekeyirc-languages-1.0-a/apply.sh onekeyirc-1.0-a
  cd onekeyirc-1.0-a
  ONEKEYIRC_LANG=id ./configure && make
  ONEKEYIRC_LANG=id ./onekeyirc --version

[lang-rtl-1.0-a]  onekeyirc 1.0-a
