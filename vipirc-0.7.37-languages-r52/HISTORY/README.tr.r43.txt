İsteğe bağlı r43 dil paketi: vipirc-0.7.37

Yalnızca değişmemiş İngilizce r1 kaynak koduna uygulayın; r42 ya da diğer paketlerin üzerine kurmayın.
Yeni çeviriler: GPG durumu, kanal listesi, Ayarlar ve DCC aktarım yardımı, komut yanıtı okuyucusu.
Akıcı dil incelemesi ve vipirc tam ncurses arayüz testi hâlâ gereklidir.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r43.tar.gz
  sh vipirc-0.7.37-languages-r43/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=tr ./vipirc --help

vipirc 0.7.37 [lang-rtl-r43]

doc/UI-LOCALIZATION-r43.txt
HISTORY/README.tr.r42.txt
