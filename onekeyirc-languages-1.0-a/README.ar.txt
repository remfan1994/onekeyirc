حزمة لغة اختيارية 1.0-a - onekeyirc-1.0-a (ar)
إصدار ألفا عام؛ نرحب بمراجعة الترجمة واختبارات المجتمع.
تقرير /summary
يلزم وجود طرفية تفاعلية متوافقة مع ANSI/VT
2195 مفتاح ترجمة لكل لغة؛ لم تكتمل مراجعة المتحدثين الأصليين بعد.

شغّل apply.sh على المصدر الإنجليزي r1 النظيف قبل التجميع؛ لا تثبت حزمة فوق أخرى.

  tar -xzf onekeyirc-1.0-a.tar.gz
  tar -xzf onekeyirc-languages-1.0-a.tar.gz
  ONEKEYIRC_LANG=ar sh onekeyirc-languages-1.0-a/apply.sh onekeyirc-1.0-a
  cd onekeyirc-1.0-a
  ONEKEYIRC_LANG=ar ./configure && make
  ONEKEYIRC_LANG=ar ./onekeyirc --version

[lang-rtl-1.0-a]  onekeyirc 1.0-a
