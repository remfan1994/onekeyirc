حزمة اللغات الاختيارية r52: vipirc-0.7.37

طبقها على المصدر الإنجليزي r1 الأصلي دون تعديل فقط؛ لا تثبتها فوق r51 أو حزمة أخرى.
أدخل اسم خادم IRC أو عنوان IP بلا URL أو منفذ. لإعداد ZNC أو soju استخدم /panel.
اختر none أو file (ملف نصي خاص) أو gpg (ملف مشفر) أو plain (قيمة مخفية في الإعدادات).
direct = الإرسال بضغطة Enter؛ review = المراجعة والتأكيد بضغطة Enter ثانية. لكل الخوادم.
اسم مستعار أو نمط nick!user@host.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r52.tar.gz
  sh vipirc-0.7.37-languages-r52/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=ar ./vipirc --version

vipirc 0.7.37 [lang-rtl-r52]
doc/SETUP-HELP-r52.txt
HISTORY/README.ar.r51.txt
