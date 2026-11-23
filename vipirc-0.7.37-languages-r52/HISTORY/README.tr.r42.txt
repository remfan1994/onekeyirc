İsteğe bağlı r42 dil ve Arapça sağdan sola görüntü paketi.
vipirc 0.7.37 - r42

Yalnızca değiştirilmemiş özgün İngilizce r1 kaynaklarıyla kullanın. r41 veya önceki paketlerin üzerine kurmayın. Uygulama sürümü değişmez.

vipirc: ek uyarılar, bağlantı hataları, UTF-8 girişi, DCC ve ekran yönergeleri çevrildi.

tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
tar -xzf vipirc-0.7.37-languages-r42.tar.gz
sh vipirc-0.7.37-languages-r42/apply.sh vipirc-0.7.37
cd vipirc-0.7.37
./configure && make
./vipirc --version

VIPIRC_LANG=tr ./vipirc --help
vipirc 0.7.37 [lang-rtl-r42]

IRC komutları, seçenek adları, yapılandırma değerleri, kullanıcı adları, sunucu yanıtları ve ağ verileri aynen korunur. Akıcı konuşanların incelemesi gereklidir; vipirc tam ncurses yapısı doğrulanmadı.

doc/UI-LOCALIZATION-r42.txt
doc/CLI-LOCALIZATION-r33.txt
doc/SECURITY-FULL-tr.txt

r39 YENİLİKLERİ:
- 45 vipirc görüntü iletisi çeviriye bağlandı; bir onekeyirc yanıt sınırı uyarısı da çevrildi.
- Yalnızca eşleşen, değiştirilmemiş İngilizce r1 kaynağına uygulayın; paketleri üst üste kurmayın.

r41 YENILIKLERI: Bağlamsal yardım, ayar açıklamaları, liste etiketleri ve hata iletileri çevrildi. Yalnızca temiz İngilizce r1 kaynağına uygulayın.

r41 YENILIKLERI: komut yardimi ve durum iletileri cevrildi. Yalnizca temiz Ingilizce r1 kaynagina uygulayin.

r42 YENI: bağlam yardımı, durum ve geçmiş başlıkları çevrildi. Yalnız temiz İngilizce r1 kaynağına uygulayın.
