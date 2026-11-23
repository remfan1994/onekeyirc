القيمة الافتراضية لإعداد Use signals هي no. يلغي Ctrl-C وCtrl-Z الرسالة أو الأمر غير المرسل، ولا ينهيان البرنامج ولا يعلّقانه. اختر yes من اللوحة 0 أو إعدادات Interface في vipirc للسماح بإشارات الطرفية. يطبّق Apply التغيير فوراً، ويحفظ Save أيضاً [input] use_signals=true|false. تُستعاد إعدادات الطرفية الأصلية عند الخروج أو التعليق.

vipirc 0.7.37: optional language + Arabic RTL patch r30
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch archive: vipirc-0.7.37-languages-r30.tar.gz

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r30.tar.gz
    sh vipirc-0.7.37-languages-r30/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37 && ./configure && make
    ./vipirc --version

Expected: vipirc 0.7.37 [lang-rtl-r30]
Language: VIPIRC_LANG=ar
See doc/USE-SIGNALS-ar.txt and README.txt.
Do not apply over r27 or older application versions.

توجد الآن مسودات مترجمة لسبعة من أقسام الأمان الاثني عشر أو الثلاثة عشر. ويبقى النص الإنجليزي هو المرجع المعتمد، ولا تزال المراجعة على يد مختصين ناطقين أصليين مطلوبة.
See doc/SECURITY-PARTIAL-ar.txt and doc/SECURITY-COVERAGE-r30.tsv.
