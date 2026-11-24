İsteğe bağlı dil yaması r66 - onekeyirc-0.1.36 (tr).
Komut satırına genel bakış (seçenek yazımları değişmez):
Yalnızca arşivi açmak çeviriyi etkinleştirmez. Derlemeden önce temiz İngilizce r1 kaynak üzerinde apply.sh çalıştırın. Önceki dil paketinin üzerine kurmayın.

Günlük görünümünde / Komut'u açar; normal yazmak Oluştur'u açar.
Bu sunucu için kalıcı günlük kullanılamıyor:

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r66.tar.gz
  sh onekeyirc-0.1.36-languages-r66/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=tr ./onekeyirc --version
  ONEKEYIRC_LANG=tr ./onekeyirc --help

Gerekli sürüm işareti: onekeyirc 0.1.36 [lang-rtl-r66]
15 yeni anahtar; dil başına toplam 1658 anahtar. Uygulama sürümü değişmedi. Ana dili konuşanların incelemesi bekleniyor.

Kaydedilmiş sunucu profili bulunamadı.
Ayarlar kaydedildi; komut kullanım bilgisi kaydedilemedi:

Bu güncelleme ağ/TLS hatalarını ve bağlantı durumlarını çevirir; sistem ayrıntıları değiştirilmez.

Yeni: güvenli bağlantı kesme, SASL hataları ve iptal edilen iletiler. Protokol komutları değişmedi.

GPG şifre çözme başarısız oldu; anahtarı istemci dışında açıp yeniden bağlanın.

Kimlik doğrulama, doğrulanmış TLS gerektirir

Yeni: 15 IRC etkinlik bildirimi (JOIN/PART/QUIT/NICK/KICK/MODE/INVITE/TOPIC/CTCP) cevrildi. Kullanici adlari ve sunucu metinleri degistirilmez.
