Дополнительный языковой пакет r44: vipirc-0.7.37

Применяйте только к исходному английскому коду r1 без изменений; не устанавливайте поверх r43 или других пакетов.
Списки показывают открытые представления выбранного сервера. Up/Down выбирает; Left/Right выбирает Open, New, Part или Close; Enter выполняет. Канал 0 представляет сервер. /cl и /channels показывают локальные каналы; /list [MASK] [OPTIONS] открывает сетевой каталог; /ql показывает личные беседы.
Все еще требуются проверка носителями языка и полная проверка ncurses-интерфейса vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r44.tar.gz
  sh vipirc-0.7.37-languages-r44/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=ru ./vipirc --help

vipirc 0.7.37 [lang-rtl-r44]

doc/UI-LOCALIZATION-r44.txt
HISTORY/README.ru.r43.txt
