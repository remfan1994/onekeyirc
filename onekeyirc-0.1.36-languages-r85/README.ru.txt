Необязательный языковой пакет r85 - onekeyirc-0.1.36 (ru)
Отчёт /summary
Требуется интерактивный терминал, совместимый с ANSI/VT
2010 keys / 2 new; Проверка носителями языка ещё не выполнена.

До сборки запустите apply.sh для чистых английских исходников r1; не накладывайте пакеты друг на друга.

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r85.tar.gz
  sh onekeyirc-0.1.36-languages-r85/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=ru ./onekeyirc --version

[lang-rtl-r85]  onekeyirc 0.1.36
