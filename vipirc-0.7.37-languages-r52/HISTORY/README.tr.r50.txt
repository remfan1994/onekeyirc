İsteğe bağlı r50 dil paketi: vipirc-0.7.37

Yalnızca değişmemiş İngilizce r1 kaynak koduna uygulayın; r49 ya da diğer paketlerin üzerine kurmayın.
Yeni kimlik bilgisi dosyası yapılandırma ve yan dosyalarından farklı olmalıdır; önerilen kimlik bilgisi dizinini kullanın.
GPG şifrelemesi sürüyor; bekleyin veya iptal etmek için geri dönün.
Yerel GPG anahtarının boşluksuz 40 veya 64 onaltılık basamaklı tam parmak izini kullanın.
Doğrulanmış TLS için yes, şifresiz IRC için no girin.
Akıcı dil incelemesi ve vipirc tam ncurses arayüz testi hâlâ gereklidir.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r50.tar.gz
  sh vipirc-0.7.37-languages-r50/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=tr ./vipirc --version

vipirc 0.7.37 [lang-rtl-r50]
doc/SETUP-VALIDATION-r50.txt
HISTORY/README.tr.r49.txt
