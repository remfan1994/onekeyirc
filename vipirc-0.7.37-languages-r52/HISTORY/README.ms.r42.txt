Pek bahasa dan paparan kanan-ke-kiri Arab pilihan r42.
vipirc 0.7.37 - r42

Gunakan hanya dengan sumber Inggeris r1 asal yang belum diubah. Jangan pasang di atas r41 atau pek terdahulu. Nombor versi aplikasi tidak berubah.

vipirc: amaran tambahan, ralat sambungan, input UTF-8, DCC dan petunjuk skrin telah diterjemahkan.

tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
tar -xzf vipirc-0.7.37-languages-r42.tar.gz
sh vipirc-0.7.37-languages-r42/apply.sh vipirc-0.7.37
cd vipirc-0.7.37
./configure && make
./vipirc --version

VIPIRC_LANG=ms ./vipirc --help
vipirc 0.7.37 [lang-rtl-r42]

Arahan IRC, pilihan, konfigurasi, nama pengguna, respons pelayan dan data rangkaian kekal literal. Semakan penutur fasih diperlukan; binaan ncurses penuh vipirc belum disahkan.

doc/UI-LOCALIZATION-r42.txt
doc/CLI-LOCALIZATION-r33.txt
doc/SECURITY-FULL-ms.txt

BAHARU DALAM r39:
- 45 mesej paparan vipirc dipautkan kepada terjemahan; satu notis had respons onekeyirc turut diterjemahkan.
- Gunakan hanya pada sumber Inggeris r1 sepadan yang belum diubah; jangan tindih pakej.

BAHARU DALAM r41: Bantuan konteks, petunjuk tetapan, label senarai dan mesej ralat diterjemahkan. Guna hanya pada sumber Inggeris r1 yang bersih.

BAHARU DALAM r41: bantuan arahan dan mesej status diterjemahkan. Gunakan hanya pada sumber Inggeris r1 yang bersih.

BAHARU r42: bantuan konteks, status dan tajuk sejarah diterjemahkan. Pasang pada sumber Inggeris r1 sahaja.
