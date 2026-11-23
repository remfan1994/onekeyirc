القيمة الافتراضية لإعداد Use signals هي no. يلغي Ctrl-C وCtrl-Z الرسالة أو الأمر غير المرسل، ولا ينهيان البرنامج ولا يعلّقانه. اختر yes من اللوحة 0 أو إعدادات Interface في vipirc للسماح بإشارات الطرفية. يطبّق Apply التغيير فوراً، ويحفظ Save أيضاً [input] use_signals=true|false. تُستعاد إعدادات الطرفية الأصلية عند الخروج أو التعليق.

vipirc 0.7.37: optional language + Arabic RTL patch r28
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch archive: vipirc-0.7.37-languages-r28.tar.gz

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r28.tar.gz
    sh vipirc-0.7.37-languages-r28/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37 && ./configure && make
    ./vipirc --version

Expected: vipirc 0.7.37 [lang-rtl-r28]
Language: VIPIRC_LANG=ar
See doc/USE-SIGNALS-ar.txt and README.txt.
Do not apply over r27 or older application versions.
