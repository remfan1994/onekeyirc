Paket bahasa dan tampilan kanan-ke-kiri Arab opsional r42.
vipirc 0.7.37 - r42

Gunakan hanya bersama sumber Inggris r1 asli yang belum diubah. Jangan pasang di atas r41 atau paket sebelumnya. Nomor versi aplikasi tetap sama.

vipirc: tambahan peringatan, galat koneksi, input UTF-8, DCC, dan petunjuk layar diterjemahkan.

tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
tar -xzf vipirc-0.7.37-languages-r42.tar.gz
sh vipirc-0.7.37-languages-r42/apply.sh vipirc-0.7.37
cd vipirc-0.7.37
./configure && make
./vipirc --version

VIPIRC_LANG=id ./vipirc --help
vipirc 0.7.37 [lang-rtl-r42]

Perintah IRC, nama opsi, konfigurasi, nama pengguna, respons server, dan data jaringan tetap literal. Terjemahan memerlukan tinjauan penutur fasih; pembangunan ncurses penuh vipirc belum diverifikasi.

doc/UI-LOCALIZATION-r42.txt
doc/CLI-LOCALIZATION-r33.txt
doc/SECURITY-FULL-id.txt

BARU DI r39:
- 45 pesan tampilan vipirc kini terhubung ke terjemahan; satu peringatan batas respons onekeyirc juga diterjemahkan.
- Terapkan hanya pada sumber Inggris r1 yang cocok dan belum diubah; jangan menumpuk paket.

BARU DALAM r41: Bantuan konteks, petunjuk pengaturan, label daftar, dan pesan kesalahan diterjemahkan. Terapkan hanya pada sumber Inggris r1 yang bersih.

BARU DI r41: bantuan perintah dan pesan status diterjemahkan. Terapkan hanya pada sumber Inggris r1 yang bersih.

BARU r42: bantuan konteks, status, dan judul riwayat diterjemahkan. Pasang hanya pada sumber Inggris r1.
