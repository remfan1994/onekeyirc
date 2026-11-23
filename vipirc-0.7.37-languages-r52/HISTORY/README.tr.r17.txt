r17: vipirc icin arka plan işleri, SSH kurtarma, oturum kapatma ve çalışan süreç yükseltme bölümleri bütünüyle çevrildi. doc/SESSION-MANAGEMENT-tr.txt dosyasına bakın. Tam ncurses kılavuzu ve SECURITY.md henüz bütünüyle çevrilmedi; RTL kodu değişmedi.
--version: vipirc 0.7.36 [lang-rtl-r17]
Apply: sh vipirc-0.7.36-languages-r17/apply.sh vipirc-0.7.36

=== r16 and earlier notes (historical) ===
r16: Tam arayüz için çevrilmiş başlangıç bölümleri (yalnızca vipirc) ve genişletilmiş güvenlik modeli açıklamaları eklendi. MANUAL.txt ve SECURITY.md hâlâ tam olarak çevrilmiş değildir. RTL işleyişi r15 ile aynıdır.
doc/NCURSES-START-tr.txt
doc/SECURITY-BOUNDARIES-tr.txt
--version: vipirc 0.7.36 [lang-rtl-r16]
Aplicar/Apply: sh vipirc-0.7.36-languages-r16/apply.sh vipirc-0.7.36

=== Prior r15 and earlier notes (historical, not current installation instructions) ===
OPTIONAL LANGUAGE + RTL PATCH r15 (tr)

r15, özgün İngilizce kaynak kodu on dil ve Arapça RTL için isteğe bağlı yamadan ayrı tutar. Güvenlik denetimi Unicode yön kontrollerini, SHA-256 doğrulamasını ve sembolik bağlantıların reddini kapsar. Yamayı derlemeden önce yalnızca temiz r1 kaynağına uygulayın. Ayrıntılar: doc/LOCALIZATION-SECURITY-tr.txt. --version çıktısı [lang-rtl-r15] içerir.

-- Earlier release history --
vipirc 0.7.36 - Dil Paketi r14 (tr)
========================================================

--- r14 ---
r14, asil Ingilizce kaynak kodunu cevirileri VE Arapca RTL yamasini iceren istege bagli paketten ayri tutar. Derlemeden once uygulandiginda --version [lang-rtl-r14] isaretini gosterir. Temel uygulama surumu degismez. Paketi yalnizca yeni acilmis Ingilizce r1 kaynagina uygulayin.


--- r12 ---
r12, isteğe bağlı Arapça görüntülemeyi iyileştirir. Editör her yeniden çizimde metin ve imleç için tek bir ortak yerleşim kullanır; böylece bellek ayrılamadığında mantıksal görüntülemeye birlikte dönerler. ncurses görsel modunda birleştirici işaretler terminalin sağ alt hücresinde de temel harfle birlikte kalır. İlk veya son satırın ötesine dikey hareket, mevcut ekleme konumunu korur. Paragraf yerleşimlerinin yeniden kullanılması kaydırma sırasında yapılan işi azaltır. Ön denetimler gereksiz veya kullanılamayan dönüşümü atlayarak mantıksal görüntülemeyi korur. Taslaklar, mesajlar ve günlükler mantıksal sırada UTF-8 olarak kalır. Çeviri kapsamı değişmez; tam ncurses kılavuzu ve SECURITY.md İngilizcedir. Gerçek OpenBSD/Crostini terminal ve yazı tipi testleri ile yetkin Arapça konuşurların incelemesi hâlâ gereklidir. r12'yi yalnızca yeni açılmış English r1 kaynağına uygulayın, r11 üzerine uygulamayın.


--- r11 ---
r11, onekeyirc, vipirc -i ve vipirc --ncurses için isteğe bağlı, paragraf düzeyinde Arapça harf biçimlendirmesi ve satır kaydırma ile görsel metin düzenleme, imleç hareketi ve fareyle konumlandırma ekler. Saklanan metin, gönderilen iletiler ve günlükler mantıksal sıradaki UTF-8 biçimini korur. Sistem FriBidi kitaplığı ve UTF-8 yerel ayarı gerekir; arayüz dilinden bağımsız olarak ONEKEYIRC_ARABIC_VISUAL=1 veya VIPIRC_ARABIC_VISUAL=1 kullanın. Terminaliniz çift yönlü metni zaten düzenliyorsa bu seçeneği kapalı bırakın. Home/End belgenin başına/sonuna gider; Backspace/Delete mantıksal karakter kümelerini düzenler. Gerçek OpenBSD/Crostini terminallerinde yazı tipi denetimleri ve Arapçanın ana dili konuşurları tarafından gözden geçirilmesi henüz yapılmadı. Her dildeki 844 katalog girdisi ve eksiksiz minimalist kılavuzlar korunur. Tam ncurses kılavuzu ve SECURITY.md hâlâ İngilizcedir. Uygulama sürümleri değişmez. r11 yalnızca yeni açılmış English r1 kaynaklarına uygulanmalıdır; r10 üzerine uygulanmamalıdır.


--- r10 ---
r10, istemciye özgü üretilen yardımı çevirir ve doc/MINIMAL-MANUAL-tr.txt içinde tam çevrilmiş minimalist kılavuzu sağlar. onekeyirc --manual ve vipirc -i --manual görüntüleme dilini kullanır; terminal gerektirmeden mantıksal sırada UTF-8 yazar. Tam vipirc ncurses kılavuzu, SECURITY.md, kalan arayüz/tanı iletileri ve sağdan sola düzenleyici henüz tamamlanmadı. r10 paketini yalnızca yeni çıkarılmış İngilizce r1 kaynağına uygulayın, asla r9 üzerine uygulamayın.


--- r9 ---
Güncel r9 durumu: sabit komut/konu yardım tablolarındaki çevrilebilir 474 satırın tümü çevrildi. Yalnızca komutlardan oluşan üç takma ad eşlemesi değişmeden kalır; 477 tablo satırının tümünün katalog girdisi vardır. Paketi uyguladıktan sonra doc/HELP-REFERENCE-tr.txt dosyasına bakın. Tam MANUAL.txt, SECURITY.md ve bazı üretilen/istemciye özgü yardımlar ile tanı iletileri İngilizcedir. Çeviriler hâlâ ana dili konuşan kişilerce gözden geçirilmelidir. r9 paketini yalnızca yeni çıkarılmış İngilizce r1 kaynağına uygulayın; r8 veya başka paket üzerine asla uygulamayın.


Bu paket arayüz, yardım ve bazı belgelerin çevirilerini ekler. Tek başına bir program değildir.

Yalnızca aşağıda belirtilen English r1 kaynak arşivini kullanın. Aynı program sürümüne sahip eski bir arşiv veya değiştirilmiş bir kaynak ağacı uyumlu değildir.
  vipirc-0.7.36-english-source-r1.tar.gz

Her iki arşivi de aynı üst dizine açın:
  tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.36-languages-r15.tar.gz

Paketi configure veya make komutunu çalıştırmadan ÖNCE temiz English r1 kaynağına uygulayın:
  sh vipirc-0.7.36-languages-r15/apply.sh vipirc-0.7.36

Programı yapılandırın ve derleyin:
  cd vipirc-0.7.36
  ./configure --prefix=/usr/local
  make

Türkçe arayüzle başlatın:
  VIPIRC_LANG=tr ./vipirc

IRC komutları, komut satırı seçenekleri ve yapılandırma anahtarları çevrilmez. Gösterildiği gibi yazın.

Çeviriler henüz taslaktır; bazı yardım metinleri ve tam kılavuzlar İngilizce kalmıştır. Kesin güvenlik yönergeleri için İngilizce MANUAL.txt ve SECURITY.md dosyalarına bakın. UTF-8 terminal kullanın.

r4 yeniligi: /help background, /help catchup ve /help logging aciklamalari tamamen cevrildi. RECOVERY-tr.txt oturum kurtarmayi ve gunluk sinirlarini aciklar.

PACK-SHA256SUMS dosyası sağlama toplamlarını içerir. apply.sh kaynak uyumluluğunu denetler. Değiştirilmiş kaynak dosyalarının üzerine elle kopyalama yapmayın.


PACK-SHA256SUMS / README.txt / MANUAL.txt / SECURITY.md

r5 yenilikleri: /help autocommands, /help dcc, /help bindings, /help bind, /help unbind, /help transfers ve /help hotkeys tamamlandi. doc/WORKFLOWS-tr.txt dosyasina bakin.

r6 yeniliği: /help configuration, /help summary, /help server, /help users ve /help watch tamamlandı. Kurulumdan sonra doc/CONFIGURATION-tr.txt ve doc/STATUS-tr.txt dosyalarına bakın.

r7 yenilikleri: /help set, /help list, yerel kanal gezintisi ve komut takma adları için 32 yeni yardım açıklaması. Kurulumdan sonra doc/SETTINGS-tr.txt ve doc/CHANNELS-tr.txt dosyalarını okuyun. Kapsam: 384/477; 93 satır hâlâ İngilizce.

--- r8 ---
Görsel Arapça deneysel olup yalnızca minimalist arayüz içindir. Sistem FriBidi kitaplığını kurup aşağıdaki değişkeni etkinleştirin. Terminal zaten Arapça biçimlendiriyorsa kullanmayın. Düzenleyici, kayıtlı metin ve tam ncurses ekranı değişmez. FriBidi yoksa mantıksal Unicode metni korunur.

VIPIRC_LANG=ar VIPIRC_ARABIC_VISUAL=1 ./vipirc -i
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-tr.txt
