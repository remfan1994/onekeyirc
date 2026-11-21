onekeyirc 0.1.35 - Pek Bahasa r9 (ms)
========================================================

--- r9 ---
Status r9 semasa: kesemua 474 baris yang boleh diterjemahkan dalam jadual bantuan arahan/topik tetap telah diterjemahkan. Tiga pemetaan alias yang hanya mengandungi arahan kekal literal; kesemua 477 baris jadual mempunyai entri katalog. Selepas menerapkan pakej, lihat doc/HELP-REFERENCE-ms.txt. MANUAL.txt lengkap, SECURITY.md, serta sebahagian bantuan yang dijana atau khusus klien dan diagnostik masih dalam bahasa Inggeris. Terjemahan masih memerlukan semakan penutur asli. Terapkan r9 hanya pada sumber Inggeris r1 yang baru diekstrak, jangan di atas r8 atau pakej lain.


Pek ini menyediakan terjemahan antara muka, bantuan dan beberapa dokumen. Pek ini bukan program tersendiri.

Gunakan hanya arkib kod sumber English r1 yang dinyatakan di bawah. Arkib lama dengan nombor versi program yang sama, atau sumber yang telah diubah, tidak serasi.
  onekeyirc-0.1.35-english-source-r1.tar.gz

Nyahmampat kedua-dua arkib ke dalam direktori induk yang sama:
  tar -xzf onekeyirc-0.1.35-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.35-languages-r9.tar.gz

Gunakan pek pada sumber English r1 yang masih asal SEBELUM menjalankan configure atau make:
  sh onekeyirc-0.1.35-languages-r9/apply.sh onekeyirc-0.1.35

Konfigurasikan dan bina program:
  cd onekeyirc-0.1.35
  ./configure --prefix=/usr/local
  make

Jalankan dengan antara muka bahasa Melayu:
  ONEKEYIRC_LANG=ms ./onekeyirc

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

ONEKEYIRC_LANG=ar ONEKEYIRC_ARABIC_VISUAL=1 ./onekeyirc
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-ms.txt
