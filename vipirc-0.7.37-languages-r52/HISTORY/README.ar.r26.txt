تعليمات حزمة r26

تظل هذه الحزمة الاختيارية منفصلة عن الشيفرة الإنجليزية الأصلية. يعرض قارئ ncurses وخيار --manual مسودة الترجمة المحددة بواسطة VIPIRC_LANG إذا كان ملفها متاحاً وآمناً، وإلا فيُستخدم الدليل الإنجليزي المضمّن. لم يتغير RTL. ما زالت المراجعة اللغوية مطلوبة.

vipirc-0.7.36-english-source-r1.tar.gz
vipirc-0.7.36-languages-r26.tar.gz

    tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.36-languages-r26.tar.gz
    sh vipirc-0.7.36-languages-r26/apply.sh vipirc-0.7.36
    cd vipirc-0.7.36 && ./configure && make
    VIPIRC_LANG=ar ./vipirc --manual

--version: vipirc 0.7.36 [lang-rtl-r26]
VIPIRC_MANUAL_DIR=/absolute/path (optional manual directory override)

طبّقها على نسخة جديدة من المصدر الإنجليزي r1، لا فوق تصحيح سابق.
MANUAL.txt and SECURITY.md in English remain authoritative.
Historical instructions are archived under HISTORY/ and must not be used to install r26.
