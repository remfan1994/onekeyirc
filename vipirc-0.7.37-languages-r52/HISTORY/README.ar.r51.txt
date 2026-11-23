حزمة اللغات الاختيارية r51: vipirc-0.7.37

طبقها على المصدر الإنجليزي r1 الأصلي دون تعديل فقط؛ لا تثبتها فوق r50 أو حزمة أخرى.
اختر أحد أرقام اللوحات المعروضة.
أدخل اسم مضيف خادم IRC أو عنوان IP.
أدخل direct (ضغط Enter مرة) أو review (مرتين).
إنشاء ملفات GPG معطل في --test-mode؛ لم تتم الاستعانة بأي مساعد أو keyring أو ملف.
لا تزال مراجعة المتحدثين المتمكنين واختبارات واجهة ncurses الكاملة لبرنامج vipirc مطلوبة.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r51.tar.gz
  sh vipirc-0.7.37-languages-r51/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=ar ./vipirc --version

vipirc 0.7.37 [lang-rtl-r51]
doc/SETUP-ERRORS-r51.txt
HISTORY/README.ar.r50.txt
