حزمة اللغات الاختيارية r43: vipirc-0.7.37

طبقها على المصدر الإنجليزي r1 الأصلي دون تعديل فقط؛ لا تثبتها فوق r42 أو حزمة أخرى.
الترجمات الجديدة تشمل حالة GPG وقوائم القنوات ومساعدة الإعدادات ونقل DCC وقارئ ردود الأوامر.
لا تزال مراجعة المتحدثين المتمكنين واختبارات واجهة ncurses الكاملة لبرنامج vipirc مطلوبة.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r43.tar.gz
  sh vipirc-0.7.37-languages-r43/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=ar ./vipirc --help

vipirc 0.7.37 [lang-rtl-r43]

doc/UI-LOCALIZATION-r43.txt
HISTORY/README.ar.r42.txt
