Необязательный языковой пакет r87 - vipirc-0.7.37 (ru)
Enter: выполнить | Esc: чат
Черновик сохранён; продолжайте правку. Ничего не отправлено.
2437 keys / 20 new; Проверка носителями языка ещё не выполнена.

До сборки запустите apply.sh для чистых английских исходников r1; не накладывайте пакеты друг на друга.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r87.tar.gz
  sh vipirc-0.7.37-languages-r87/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=ru ./vipirc --version

[lang-rtl-r87]  vipirc 0.7.37
