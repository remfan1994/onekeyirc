حزمة لغة اختيارية r85 - onekeyirc-0.1.36 (ar)
تقرير /summary
يلزم وجود طرفية تفاعلية متوافقة مع ANSI/VT
2010 keys / 2 new; لم تكتمل مراجعة المتحدثين الأصليين بعد.

شغّل apply.sh على المصدر الإنجليزي r1 النظيف قبل التجميع؛ لا تثبت حزمة فوق أخرى.

  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r85.tar.gz
  sh onekeyirc-0.1.36-languages-r85/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=ar ./onekeyirc --version

[lang-rtl-r85]  onekeyirc 0.1.36
