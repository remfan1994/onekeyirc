onekeyirc 0.1.35 - Paket Bahasa r9 (id)
========================================================

--- r9 ---
Status r9 saat ini: semua 474 baris yang dapat diterjemahkan dalam tabel bantuan perintah/topik tetap telah diterjemahkan. Tiga pemetaan alias yang hanya berisi perintah tetap literal; semua 477 baris tabel memiliki entri katalog. Setelah menerapkan paket, lihat doc/HELP-REFERENCE-id.txt. MANUAL.txt lengkap, SECURITY.md, serta sebagian bantuan yang dihasilkan atau khusus klien dan diagnostik masih berbahasa Inggris. Terjemahan masih perlu ditinjau penutur asli. Terapkan r9 hanya pada sumber Inggris r1 yang baru diekstrak, jangan di atas r8 atau paket lain.


Paket ini menyediakan terjemahan antarmuka, bantuan, dan beberapa dokumen. Paket ini bukan program mandiri.

Gunakan hanya arsip sumber English r1 yang disebut di bawah. Arsip lama dengan nomor versi program yang sama, atau sumber yang telah diubah, tidak kompatibel.
  onekeyirc-0.1.35-english-source-r1.tar.gz

Ekstrak kedua arsip ke direktori induk yang sama:
  tar -xzf onekeyirc-0.1.35-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.35-languages-r9.tar.gz

Terapkan paket pada sumber English r1 yang masih bersih SEBELUM menjalankan configure atau make:
  sh onekeyirc-0.1.35-languages-r9/apply.sh onekeyirc-0.1.35

Konfigurasikan dan kompilasi program:
  cd onekeyirc-0.1.35
  ./configure --prefix=/usr/local
  make

Jalankan dengan antarmuka bahasa Indonesia:
  ONEKEYIRC_LANG=id ./onekeyirc

Perintah IRC, opsi baris perintah, dan nama pengaturan tidak diterjemahkan. Ketik persis seperti yang tertulis.

Terjemahan ini masih berupa draf; sebagian bantuan dan manual lengkap tetap dalam bahasa Inggris. Periksa MANUAL.txt dan SECURITY.md berbahasa Inggris untuk petunjuk keamanan yang tepat. Gunakan terminal UTF-8.

Baru pada r4: bantuan /help background, /help catchup, dan /help logging kini diterjemahkan lengkap. Panduan RECOVERY-id.txt mencakup pemulihan sesi dan batas penyimpanan log.

PACK-SHA256SUMS berisi checksum berkas. Skrip apply.sh memeriksa kecocokan sumber. Jangan menyalin berkas secara manual ke sumber yang telah diubah.


PACK-SHA256SUMS / README.txt / MANUAL.txt / SECURITY.md

Baru pada r5: /help autocommands, /help dcc, /help bindings, /help bind, /help unbind, /help transfers, dan /help hotkeys kini lengkap. Lihat doc/WORKFLOWS-id.txt.

Baru di r6: /help configuration, /help summary, /help server, /help users, dan /help watch kini lengkap. Panduan doc/CONFIGURATION-id.txt dan doc/STATUS-id.txt tersedia setelah pemasangan.

Baru pada r7: 32 penjelasan bantuan tambahan tentang /help set, /help list, navigasi kanal lokal, dan alias perintah. Lihat doc/SETTINGS-id.txt dan doc/CHANNELS-id.txt setelah memasang paket. Cakupan bantuan: 384/477; 93 masih berbahasa Inggris.

--- r8 ---
Tampilan Arab visual bersifat eksperimental dan hanya untuk antarmuka minimal. Pasang pustaka FriBidi pada sistem, lalu aktifkan variabel di bawah. Jika terminal sudah membentuk Arab, jangan aktifkan. Editor, pesan tersimpan, dan antarmuka ncurses penuh tidak diubah. Jika FriBidi tidak ada, teks tetap dalam urutan Unicode asli.

ONEKEYIRC_LANG=ar ONEKEYIRC_ARABIC_VISUAL=1 ./onekeyirc
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-id.txt
