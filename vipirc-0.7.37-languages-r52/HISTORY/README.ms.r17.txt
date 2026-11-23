r17: Bab penuh tentang kerja latar belakang, pemulihan SSH, penutupan sesi dan naik taraf proses telah diterjemahkan untuk vipirc. Lihat doc/SESSION-MANAGEMENT-ms.txt. Manual ncurses penuh dan SECURITY.md belum diterjemahkan sepenuhnya; RTL tidak berubah.
--version: vipirc 0.7.36 [lang-rtl-r17]
Apply: sh vipirc-0.7.36-languages-r17/apply.sh vipirc-0.7.36

=== r16 and earlier notes (historical) ===
r16: Petikan panduan antara muka penuh (vipirc sahaja) dan penerangan sempadan keselamatan yang diperluas telah ditambah. MANUAL.txt dan SECURITY.md belum diterjemah sepenuhnya. Kelakuan RTL tidak berubah daripada r15.
doc/NCURSES-START-ms.txt
doc/SECURITY-BOUNDARIES-ms.txt
--version: vipirc 0.7.36 [lang-rtl-r16]
Aplicar/Apply: sh vipirc-0.7.36-languages-r16/apply.sh vipirc-0.7.36

=== Prior r15 and earlier notes (historical, not current installation instructions) ===
OPTIONAL LANGUAGE + RTL PATCH r15 (ms)

Pek r15 mengekalkan sumber Inggeris asal berasingan daripada tampalan pilihan untuk sepuluh bahasa dan paparan RTL Arab. Pemeriksaan keselamatan meliputi kawalan arah Unicode, SHA-256 dan penolakan pautan simbolik. Gunakan hanya pada sumber r1 baharu sebelum kompilasi. Rujuk doc/LOCALIZATION-SECURITY-ms.txt; --version memaparkan [lang-rtl-r15].

-- Earlier release history --
vipirc 0.7.36 - Pek Bahasa r14 (ms)
========================================================

--- r14 ---
Keluaran r14 mengekalkan sumber Inggeris asal berasingan daripada pek pilihan yang mengandungi terjemahan DAN tampalan RTL Arab. Setelah digunakan sebelum kompilasi, --version memaparkan tanda [lang-rtl-r14]. Versi aplikasi asas tidak berubah. Gunakan hanya pada sumber Inggeris r1 yang baru diekstrak.


--- r12 ---
r12 memperhalus mod paparan Arab pilihan. Setiap lukisan semula editor menggunakan satu susun atur bersama untuk teks dan kursor, supaya kedua-duanya kekal sepadan apabila kegagalan peruntukan memori menyebabkan paparan kembali ke susunan logik. Dalam mod visual ncurses, tanda gabungan kekal bersama huruf asas di sel penjuru kanan bawah terminal. Gerakan menegak melepasi baris pertama atau terakhir mengekalkan kedudukan sisipan. Penggunaan semula susun atur perenggan mengurangkan kerja semasa menatal. Semakan awal melangkau penukaran yang tidak diperlukan atau tidak tersedia dan mengekalkan paparan logik. Draf, mesej dan log kekal dalam UTF-8 mengikut susunan logik. Skop terjemahan tidak berubah; manual penuh ncurses dan SECURITY.md masih dalam bahasa Inggeris. Ujian terminal dan fon OpenBSD/Crostini sebenar serta semakan oleh penutur Arab yang mahir masih diperlukan. Gunakan r12 hanya pada sumber English r1 yang baru diekstrak, bukan di atas r11.


--- r11 ---
r11 menambah pembentukan huruf dan pembalutan baris Arab mengikut perenggan secara pilihan, bersama penyuntingan visual, pergerakan kursor dan penempatan menggunakan tetikus dalam onekeyirc, vipirc -i serta vipirc --ncurses. Teks tersimpan, mesej dihantar dan log kekal sebagai UTF-8 dalam urutan logik. Gunakan pustaka FriBidi sistem dan locale UTF-8; aktifkan ONEKEYIRC_ARABIC_VISUAL=1 atau VIPIRC_ARABIC_VISUAL=1 tanpa bergantung pada bahasa antara muka. Biarkan pilihan ini dimatikan jika terminal sudah mengatur teks dua arah. Home/End menuju awal/akhir dokumen; Backspace/Delete menyunting kelompok aksara logik. Ujian terminal dan fon pada OpenBSD/Crostini sebenar serta semakan bahasa Arab oleh penutur asli masih belum dilakukan. Kesemua 844 entri katalog setiap bahasa dan manual minimalis lengkap dikekalkan. Manual ncurses penuh dan SECURITY.md masih dalam bahasa Inggeris. Versi aplikasi tidak berubah. Gunakan r11 hanya pada sumber English r1 yang baru diekstrak, bukan di atas r10.


--- r10 ---
r10 menterjemahkan bantuan terjana khusus bagi setiap klien dan menyediakan manual minimalis lengkap yang diterjemahkan di doc/MINIMAL-MANUAL-ms.txt. onekeyirc --manual dan vipirc -i --manual mengikut bahasa paparan serta menulis UTF-8 dalam susunan logik tanpa memerlukan terminal. Manual antara muka ncurses penuh vipirc, SECURITY.md, baki antara muka dan diagnostik, serta penyunting kanan-ke-kiri belum selesai. Terapkan r10 hanya pada sumber Inggeris r1 yang baru diekstrak, jangan sekali-kali di atas r9.


--- r9 ---
Status r9 semasa: kesemua 474 baris yang boleh diterjemahkan dalam jadual bantuan arahan/topik tetap telah diterjemahkan. Tiga pemetaan alias yang hanya mengandungi arahan kekal literal; kesemua 477 baris jadual mempunyai entri katalog. Selepas menerapkan pakej, lihat doc/HELP-REFERENCE-ms.txt. MANUAL.txt lengkap, SECURITY.md, serta sebahagian bantuan yang dijana atau khusus klien dan diagnostik masih dalam bahasa Inggeris. Terjemahan masih memerlukan semakan penutur asli. Terapkan r9 hanya pada sumber Inggeris r1 yang baru diekstrak, jangan di atas r8 atau pakej lain.


Pek ini menyediakan terjemahan antara muka, bantuan dan beberapa dokumen. Pek ini bukan program tersendiri.

Gunakan hanya arkib kod sumber English r1 yang dinyatakan di bawah. Arkib lama dengan nombor versi program yang sama, atau sumber yang telah diubah, tidak serasi.
  vipirc-0.7.36-english-source-r1.tar.gz

Nyahmampat kedua-dua arkib ke dalam direktori induk yang sama:
  tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.36-languages-r15.tar.gz

Gunakan pek pada sumber English r1 yang masih asal SEBELUM menjalankan configure atau make:
  sh vipirc-0.7.36-languages-r15/apply.sh vipirc-0.7.36

Konfigurasikan dan bina program:
  cd vipirc-0.7.36
  ./configure --prefix=/usr/local
  make

Jalankan dengan antara muka bahasa Melayu:
  VIPIRC_LANG=ms ./vipirc

Arahan IRC, pilihan baris perintah dan nama tetapan tidak diterjemahkan. Taip tepat seperti yang ditunjukkan.

Terjemahan ini masih draf; sebahagian bantuan dan manual penuh kekal dalam bahasa Inggeris. Rujuk MANUAL.txt dan SECURITY.md dalam bahasa Inggeris untuk maklumat keselamatan yang tepat. Gunakan terminal UTF-8.

Baharu dalam r4: bantuan /help background, /help catchup dan /help logging kini diterjemah sepenuhnya. Panduan RECOVERY-ms.txt menerangkan pemulihan sesi dan had penyimpanan log.

PACK-SHA256SUMS mengandungi semak jumlah fail. Skrip apply.sh menyemak keserasian sumber. Jangan salin fail secara manual ke atas sumber yang telah diubah.


PACK-SHA256SUMS / README.txt / MANUAL.txt / SECURITY.md

Baharu pada r5: /help autocommands, /help dcc, /help bindings, /help bind, /help unbind, /help transfers dan /help hotkeys kini lengkap. Lihat doc/WORKFLOWS-ms.txt.

Baharu dalam r6: /help configuration, /help summary, /help server, /help users dan /help watch kini lengkap. Baca doc/CONFIGURATION-ms.txt dan doc/STATUS-ms.txt selepas pemasangan.

Baharu dalam r7: 32 penerangan bantuan lagi untuk /help set, /help list, navigasi saluran setempat dan alias arahan. Lihat doc/SETTINGS-ms.txt serta doc/CHANNELS-ms.txt selepas pemasangan. Liputan bantuan: 384/477; 93 masih dalam bahasa Inggeris.

--- r8 ---
Paparan Arab visual ialah percubaan untuk antaramuka minimal sahaja. Pasang pustaka FriBidi sistem dan aktifkan pemboleh ubah di bawah. Jangan aktifkan jika terminal sudah membentuk tulisan Arab. Penyunting, mesej tersimpan dan ncurses penuh tidak diubah. Tanpa FriBidi, teks kekal dalam susunan Unicode asal.

VIPIRC_LANG=ar VIPIRC_ARABIC_VISUAL=1 ./vipirc -i
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-ms.txt
