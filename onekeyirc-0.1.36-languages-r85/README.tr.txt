İsteğe bağlı dil paketi r85 - onekeyirc-0.1.36 (tr)
/summary raporu
ANSI/VT uyumlu etkileşimli bir terminal gerekiyor
2010 keys / 2 new; Ana dili konuşanlar tarafından inceleme henüz yapılmadı.

Derlemeden önce temiz İngilizce r1 kaynağına apply.sh uygulayın; paketleri üst üste yüklemeyin.

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r85.tar.gz
  sh onekeyirc-0.1.36-languages-r85/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=tr ./onekeyirc --version

[lang-rtl-r85]  onekeyirc 0.1.36
