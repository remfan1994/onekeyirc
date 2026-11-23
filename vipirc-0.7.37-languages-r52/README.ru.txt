Дополнительный языковой пакет r52: vipirc-0.7.37

Применяйте только к исходному английскому коду r1 без изменений; не устанавливайте поверх r51 или других пакетов.
Укажите имя или IP сервера IRC без URL и порта. Для ZNC или soju используйте /panel.
Выберите none, file (закрытый текстовый файл), gpg (шифрованный файл) или plain (скрытое значение в настройках).
direct = отправка через Enter; review = проверка и повторный Enter. Для всех серверов.
Имя или маска nick!user@host.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r52.tar.gz
  sh vipirc-0.7.37-languages-r52/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=ru ./vipirc --version

vipirc 0.7.37 [lang-rtl-r52]
doc/SETUP-HELP-r52.txt
HISTORY/README.ru.r51.txt
