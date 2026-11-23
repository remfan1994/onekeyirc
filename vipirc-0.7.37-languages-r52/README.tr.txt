İsteğe bağlı r52 dil paketi: vipirc-0.7.37

Yalnızca değişmemiş İngilizce r1 kaynak koduna uygulayın; r51 ya da diğer paketlerin üzerine kurmayın.
IRC sunucusunun ana bilgisayar adını veya IP adresini URL ve port olmadan girin. ZNC veya soju için /panel kullanın.
none, file (özel metin dosyası), gpg (şifreli dosya) veya plain (yapılandırmada gizli değer) seçin.
direct = tek Enter ile gönder; review = günlüğü incele ve ikinci Enter ile onayla. Tüm sunucularda geçerlidir.
Takma ad veya nick!user@host kalıbı.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r52.tar.gz
  sh vipirc-0.7.37-languages-r52/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=tr ./vipirc --version

vipirc 0.7.37 [lang-rtl-r52]
doc/SETUP-HELP-r52.txt
HISTORY/README.tr.r51.txt
