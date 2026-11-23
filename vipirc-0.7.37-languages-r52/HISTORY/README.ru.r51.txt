Дополнительный языковой пакет r51: vipirc-0.7.37

Применяйте только к исходному английскому коду r1 без изменений; не устанавливайте поверх r50 или других пакетов.
Выберите один из показанных номеров панелей.
Введите имя хоста или IP-адрес IRC-сервера.
Введите direct (один Enter) или review (два Enter).
Создание файла GPG отключено в --test-mode; помощник, keyring и файлы не использовались.
Все еще требуются проверка носителями языка и полная проверка ncurses-интерфейса vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r51.tar.gz
  sh vipirc-0.7.37-languages-r51/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=ru ./vipirc --version

vipirc 0.7.37 [lang-rtl-r51]
doc/SETUP-ERRORS-r51.txt
HISTORY/README.ru.r50.txt
