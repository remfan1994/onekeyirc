Необязательный языковой пакет 1.0-a - onekeyirc-1.0-a (ru)
Публичная альфа-версия; приглашаем сообщество проверять переводы и тестировать платформы.
Отчёт /summary
Требуется интерактивный терминал, совместимый с ANSI/VT
2195 ключей перевода на язык; Проверка носителями языка ещё не выполнена.

До сборки запустите apply.sh для чистых английских исходников r1; не накладывайте пакеты друг на друга.

  tar -xzf onekeyirc-1.0-a.tar.gz
  tar -xzf onekeyirc-languages-1.0-a.tar.gz
  ONEKEYIRC_LANG=ru sh onekeyirc-languages-1.0-a/apply.sh onekeyirc-1.0-a
  cd onekeyirc-1.0-a
  ONEKEYIRC_LANG=ru ./configure && make
  ONEKEYIRC_LANG=ru ./onekeyirc --version

[lang-rtl-1.0-a]  onekeyirc 1.0-a
