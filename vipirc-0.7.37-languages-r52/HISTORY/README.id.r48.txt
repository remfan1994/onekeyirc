Paket bahasa opsional r48: vipirc-0.7.37

Terapkan hanya pada kode sumber Inggris r1 yang masih asli; jangan timpa r47 atau paket sebelumnya.
Ringkasan mengelompokkan server, kanal 0, tampilan kanal yang diikuti atau ditinggalkan, percakapan privat, pesan belum dibaca, penyebutan, tugas tertunda, dan transfer menurut server. Penyebutan tidak ditambahkan ke jumlah belum dibaca.
 Satu pesan yang sedang dikirim akan diselesaikan.
Tinjauan penutur fasih dan pengujian penuh antarmuka ncurses vipirc masih diperlukan.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r48.tar.gz
  sh vipirc-0.7.37-languages-r48/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=id ./vipirc --help

vipirc 0.7.37 [lang-rtl-r48]

doc/UI-LOCALIZATION-r48.txt
HISTORY/README.id.r47.txt

r48: Terima / Tolak / Batalkan transfer.
Transfer DCC memakai koneksi langsung tanpa enkripsi dan memperlihatkan alamat jaringan yang dipilih kepada rekan. Menerima tawaran saja tidak membuka berkas atau koneksi.
