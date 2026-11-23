r25 ile vipirc tam el kitabının 52 bölümünün tümü taslak olarak çevrildi. Son beş bölüm eklendi; diller için tek dosyalık çevrimdışı el kitapları var. Program içi ncurses yardım hâlâ İngilizcedir. Ana dili incelemesi gerekir.

Özgün İngilizce kaynak: vipirc-0.7.36-english-source-r1.tar.gz
İsteğe bağlı dil paketi ve RTL yaması: vipirc-0.7.36-languages-r25.tar.gz

İki arşivi aynı üst dizine açın.
Yapılandırmadan ve derlemeden önce yamayı yalnızca bir kez uygulayın:
    sh vipirc-0.7.36-languages-r25/apply.sh vipirc-0.7.36

Derleme: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r25]
Dil seçimi: VIPIRC_LANG=tr

Önceki yamanın üzerine kurmayın; yeni açılmış İngilizce r1 kaynağı kullanın.
Özgün İngilizce kaynak değişmemiştir. İngilizce MANUAL.txt ve SECURITY.md yetkili başvuru belgeleridir; çeviri taslakları ana dili konuşanların incelemesini gerektirir.
Eski talimatlar HISTORY/ içinde saklanır; oradaki komutları çalıştırmayın.
