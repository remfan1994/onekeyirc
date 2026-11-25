İsteğe bağlı dil paketi 1.0-a - vipirc-1.0-a (tr)
Herkese açık alfa sürümü; topluluğun çeviri incelemeleri ve platform testleri memnuniyetle karşılanır.
Enter: çalıştır | Esc: sohbet
Taslak korundu; düzenlemeye devam edin. Hiçbir şey gönderilmedi.
Dil başına 2676 çeviri anahtarı; Ana dili konuşanlar tarafından inceleme henüz yapılmadı.

Derlemeden önce temiz İngilizce r1 kaynağına apply.sh uygulayın; paketleri üst üste yüklemeyin.

  tar -xzf vipirc-1.0-a.tar.gz
  tar -xzf vipirc-languages-1.0-a.tar.gz
  VIPIRC_LANG=tr sh vipirc-languages-1.0-a/apply.sh vipirc-1.0-a
  cd vipirc-1.0-a
  VIPIRC_LANG=tr ./configure && make
  VIPIRC_LANG=tr ./vipirc --version

[lang-rtl-1.0-a]  vipirc 1.0-a
