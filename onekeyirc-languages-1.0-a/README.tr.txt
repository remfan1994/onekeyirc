İsteğe bağlı dil paketi 1.0-a - onekeyirc-1.0-a (tr)
Herkese açık alfa sürümü; topluluğun çeviri incelemeleri ve platform testleri memnuniyetle karşılanır.
/summary raporu
ANSI/VT uyumlu etkileşimli bir terminal gerekiyor
Dil başına 2195 çeviri anahtarı; Ana dili konuşanlar tarafından inceleme henüz yapılmadı.

Derlemeden önce temiz İngilizce r1 kaynağına apply.sh uygulayın; paketleri üst üste yüklemeyin.

  tar -xzf onekeyirc-1.0-a.tar.gz
  tar -xzf onekeyirc-languages-1.0-a.tar.gz
  ONEKEYIRC_LANG=tr sh onekeyirc-languages-1.0-a/apply.sh onekeyirc-1.0-a
  cd onekeyirc-1.0-a
  ONEKEYIRC_LANG=tr ./configure && make
  ONEKEYIRC_LANG=tr ./onekeyirc --version

[lang-rtl-1.0-a]  onekeyirc 1.0-a
