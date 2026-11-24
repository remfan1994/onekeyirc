İsteğe bağlı dil paketi r87 - vipirc-0.7.37 (tr)
Enter: çalıştır | Esc: sohbet
Taslak korundu; düzenlemeye devam edin. Hiçbir şey gönderilmedi.
2437 keys / 20 new; Ana dili konuşanlar tarafından inceleme henüz yapılmadı.

Derlemeden önce temiz İngilizce r1 kaynağına apply.sh uygulayın; paketleri üst üste yüklemeyin.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r87.tar.gz
  sh vipirc-0.7.37-languages-r87/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=tr ./vipirc --version

[lang-rtl-r87]  vipirc 0.7.37
