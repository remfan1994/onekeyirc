onekeyirc 0.1.35 - Dil Paketi r9 (tr)
========================================================

--- r9 ---
Güncel r9 durumu: sabit komut/konu yardım tablolarındaki çevrilebilir 474 satırın tümü çevrildi. Yalnızca komutlardan oluşan üç takma ad eşlemesi değişmeden kalır; 477 tablo satırının tümünün katalog girdisi vardır. Paketi uyguladıktan sonra doc/HELP-REFERENCE-tr.txt dosyasına bakın. Tam MANUAL.txt, SECURITY.md ve bazı üretilen/istemciye özgü yardımlar ile tanı iletileri İngilizcedir. Çeviriler hâlâ ana dili konuşan kişilerce gözden geçirilmelidir. r9 paketini yalnızca yeni çıkarılmış İngilizce r1 kaynağına uygulayın; r8 veya başka paket üzerine asla uygulamayın.


Bu paket arayüz, yardım ve bazı belgelerin çevirilerini ekler. Tek başına bir program değildir.

Yalnızca aşağıda belirtilen English r1 kaynak arşivini kullanın. Aynı program sürümüne sahip eski bir arşiv veya değiştirilmiş bir kaynak ağacı uyumlu değildir.
  onekeyirc-0.1.35-english-source-r1.tar.gz

Her iki arşivi de aynı üst dizine açın:
  tar -xzf onekeyirc-0.1.35-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.35-languages-r9.tar.gz

Paketi configure veya make komutunu çalıştırmadan ÖNCE temiz English r1 kaynağına uygulayın:
  sh onekeyirc-0.1.35-languages-r9/apply.sh onekeyirc-0.1.35

Programı yapılandırın ve derleyin:
  cd onekeyirc-0.1.35
  ./configure --prefix=/usr/local
  make

Türkçe arayüzle başlatın:
  ONEKEYIRC_LANG=tr ./onekeyirc

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

ONEKEYIRC_LANG=ar ONEKEYIRC_ARABIC_VISUAL=1 ./onekeyirc
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-tr.txt
