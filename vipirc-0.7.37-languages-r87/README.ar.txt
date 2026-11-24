حزمة لغة اختيارية r87 - vipirc-0.7.37 (ar)
Enter: تنفيذ | Esc: المحادثة
تم الاحتفاظ بالمسودة؛ تابع التحرير. لم يُرسل شيء.
2437 keys / 20 new; لم تكتمل مراجعة المتحدثين الأصليين بعد.

شغّل apply.sh على المصدر الإنجليزي r1 النظيف قبل التجميع؛ لا تثبت حزمة فوق أخرى.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r87.tar.gz
  sh vipirc-0.7.37-languages-r87/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=ar ./vipirc --version

[lang-rtl-r87]  vipirc 0.7.37
