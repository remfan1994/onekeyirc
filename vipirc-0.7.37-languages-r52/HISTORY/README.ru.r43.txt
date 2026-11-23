Дополнительный языковой пакет r43: vipirc-0.7.37

Применяйте только к исходному английскому коду r1 без изменений; не устанавливайте поверх r42 или других пакетов.
Переведены статусы GPG, список каналов, справка по настройкам и передаче DCC, просмотр ответов команд.
Все еще требуются проверка носителями языка и полная проверка ncurses-интерфейса vipirc.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r43.tar.gz
  sh vipirc-0.7.37-languages-r43/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=ru ./vipirc --help

vipirc 0.7.37 [lang-rtl-r43]

doc/UI-LOCALIZATION-r43.txt
HISTORY/README.ru.r42.txt
