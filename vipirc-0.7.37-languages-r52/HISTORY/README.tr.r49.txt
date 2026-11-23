İsteğe bağlı r49 dil paketi: vipirc-0.7.37

Yalnızca değişmemiş İngilizce r1 kaynak koduna uygulayın; r48 ya da diğer paketlerin üzerine kurmayın.
Kimlik bilgisi konumlarını incele veya kimlik doğrulamayı hazırla
Bu sunucunun kanal listesini aç
Seçili sunucunun bağlantı ayarlarını aç
yes = doğrulanmış şifreli bağlantı; no = şifresiz IRC.
Akıcı dil incelemesi ve vipirc tam ncurses arayüz testi hâlâ gereklidir.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r49.tar.gz
  sh vipirc-0.7.37-languages-r49/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=tr ./vipirc --version

vipirc 0.7.37 [lang-rtl-r49]
doc/COMMAND-LOCALIZATION-r49.txt
HISTORY/README.tr.r48.txt
