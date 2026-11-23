حزمة اللغات الاختيارية r44: vipirc-0.7.37

طبقها على المصدر الإنجليزي r1 الأصلي دون تعديل فقط؛ لا تثبتها فوق r43 أو حزمة أخرى.
تعرض هذه القوائم العروض المفتوحة على الخادم المحدد. يختار Up/Down؛ ويختار Left/Right الإجراء Open أو New أو Part أو Close؛ وينفذ Enter. القناة 0 هي الخادم. يعرض /cl و/channels القنوات المحلية؛ يفتح /list [MASK] [OPTIONS] دليل الشبكة؛ يعرض /ql المحادثات الخاصة.
لا تزال مراجعة المتحدثين المتمكنين واختبارات واجهة ncurses الكاملة لبرنامج vipirc مطلوبة.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r44.tar.gz
  sh vipirc-0.7.37-languages-r44/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=ar ./vipirc --help

vipirc 0.7.37 [lang-rtl-r44]

doc/UI-LOCALIZATION-r44.txt
HISTORY/README.ar.r43.txt
