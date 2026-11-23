حزمة اللغات الاختيارية r50: vipirc-0.7.37

طبقها على المصدر الإنجليزي r1 الأصلي دون تعديل فقط؛ لا تثبتها فوق r49 أو حزمة أخرى.
يجب أن يختلف ملف بيانات الاعتماد الجديد عن ملفات الإعدادات والملفات الملحقة بها؛ اختر المجلد الخاص المقترح.
لا يزال تشفير GPG جاريا؛ انتظر أو ارجع لإلغائه.
استخدم بصمة مفتاح GPG المحلي الكاملة: 40 أو 64 رقما ست عشريا دون فراغات.
أدخل yes لاتصال TLS موثق أو no لاتصال IRC غير مشفر.
لا تزال مراجعة المتحدثين المتمكنين واختبارات واجهة ncurses الكاملة لبرنامج vipirc مطلوبة.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r50.tar.gz
  sh vipirc-0.7.37-languages-r50/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=ar ./vipirc --version

vipirc 0.7.37 [lang-rtl-r50]
doc/SETUP-VALIDATION-r50.txt
HISTORY/README.ar.r49.txt
