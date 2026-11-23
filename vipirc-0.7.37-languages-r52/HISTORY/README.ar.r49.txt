حزمة اللغات الاختيارية r49: vipirc-0.7.37

طبقها على المصدر الإنجليزي r1 الأصلي دون تعديل فقط؛ لا تثبتها فوق r48 أو حزمة أخرى.
افحص مواقع ملفات الاعتماد أو جهز المصادقة
افتح قائمة قنوات هذا الخادم
افتح إعدادات اتصال الخادم المحدد
yes = اتصال مشفر موثق؛ no = IRC غير مشفر.
لا تزال مراجعة المتحدثين المتمكنين واختبارات واجهة ncurses الكاملة لبرنامج vipirc مطلوبة.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r49.tar.gz
  sh vipirc-0.7.37-languages-r49/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=ar ./vipirc --version

vipirc 0.7.37 [lang-rtl-r49]
doc/COMMAND-LOCALIZATION-r49.txt
HISTORY/README.ar.r48.txt
