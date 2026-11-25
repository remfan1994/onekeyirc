Необязательный языковой пакет 1.0-a - vipirc-1.0-a (ru)
Публичная альфа-версия; приглашаем сообщество проверять переводы и тестировать платформы.
Enter: выполнить | Esc: чат
Черновик сохранён; продолжайте правку. Ничего не отправлено.
2676 ключей перевода на язык; Проверка носителями языка ещё не выполнена.

До сборки запустите apply.sh для чистых английских исходников r1; не накладывайте пакеты друг на друга.

  tar -xzf vipirc-1.0-a.tar.gz
  tar -xzf vipirc-languages-1.0-a.tar.gz
  VIPIRC_LANG=ru sh vipirc-languages-1.0-a/apply.sh vipirc-1.0-a
  cd vipirc-1.0-a
  VIPIRC_LANG=ru ./configure && make
  VIPIRC_LANG=ru ./vipirc --version

[lang-rtl-1.0-a]  vipirc 1.0-a
