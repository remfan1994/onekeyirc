İsteğe bağlı r48 dil paketi: vipirc-0.7.37

Yalnızca değişmemiş İngilizce r1 kaynak koduna uygulayın; r47 ya da diğer paketlerin üzerine kurmayın.
Özet, sunucuya göre sunucuları, kanal 0'ı, katılınmış ve ayrılınmış kanal görünümlerini, özel sohbetleri, okunmamışları, bahsetmeleri, bekleyen işleri ve dosya aktarımlarını gruplar. Bahsetmeler okunmamış toplamına eklenmez.
 Gönderilmekte olan bir mesaj tamamlanacak.
Akıcı dil incelemesi ve vipirc tam ncurses arayüz testi hâlâ gereklidir.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r48.tar.gz
  sh vipirc-0.7.37-languages-r48/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=tr ./vipirc --help

vipirc 0.7.37 [lang-rtl-r48]

doc/UI-LOCALIZATION-r48.txt
HISTORY/README.tr.r47.txt

r48: Kabul et / Reddet / Aktarımı iptal et.
DCC aktarımları doğrudan ve şifresiz bağlantı kullanır; seçilen ağ adresini karşı tarafa gösterir. Bir teklifin alınması tek başına dosya veya bağlantı açmaz.
