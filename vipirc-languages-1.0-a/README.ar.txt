حزمة لغة اختيارية 1.0-a - vipirc-1.0-a (ar)
إصدار ألفا عام؛ نرحب بمراجعة الترجمة واختبارات المجتمع.
Enter: تنفيذ | Esc: المحادثة
تم الاحتفاظ بالمسودة؛ تابع التحرير. لم يُرسل شيء.
2676 مفتاح ترجمة لكل لغة؛ لم تكتمل مراجعة المتحدثين الأصليين بعد.

شغّل apply.sh على المصدر الإنجليزي r1 النظيف قبل التجميع؛ لا تثبت حزمة فوق أخرى.

  tar -xzf vipirc-1.0-a.tar.gz
  tar -xzf vipirc-languages-1.0-a.tar.gz
  VIPIRC_LANG=ar sh vipirc-languages-1.0-a/apply.sh vipirc-1.0-a
  cd vipirc-1.0-a
  VIPIRC_LANG=ar ./configure && make
  VIPIRC_LANG=ar ./vipirc --version

[lang-rtl-1.0-a]  vipirc 1.0-a
