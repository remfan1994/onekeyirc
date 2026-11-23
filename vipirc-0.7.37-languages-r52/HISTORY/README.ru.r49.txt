Дополнительный языковой пакет r49: vipirc-0.7.37

Применяйте только к исходному английскому коду r1 без изменений; не устанавливайте поверх r48 или других пакетов.
Проверить пути учетных данных или подготовить аутентификацию
Открыть список каналов этого сервера
Открыть настройки соединения выбранного сервера
yes = проверенное шифрованное соединение; no = IRC без шифрования.
Все еще требуются проверка носителями языка и полная проверка ncurses-интерфейса vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r49.tar.gz
  sh vipirc-0.7.37-languages-r49/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=ru ./vipirc --version

vipirc 0.7.37 [lang-rtl-r49]
doc/COMMAND-LOCALIZATION-r49.txt
HISTORY/README.ru.r48.txt
