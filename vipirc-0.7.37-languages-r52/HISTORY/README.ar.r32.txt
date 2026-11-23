تترجم رقعة r32 أخطاء بدء التشغيل وملخص --help. تظل أوامر IRC والخيارات بالإنجليزية. توجد مسودات لكل أقسام SECURITY.md، ويبقى النص الإنجليزي المرجع المعتمد.

vipirc 0.7.37 - optional language and Arabic RTL patch r32
English source: vipirc-0.7.37-english-source-r1.tar.gz
Patch: vipirc-0.7.37-languages-r32.tar.gz
    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r32.tar.gz
    sh vipirc-0.7.37-languages-r32/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37
    ./configure && make
    ./vipirc --version
Expected: vipirc 0.7.37 [lang-rtl-r32]
Language: VIPIRC_LANG=ar
Use --help for a localized introduction and English option list.
See doc/CLI-LOCALIZATION-r32.txt and doc/SECURITY-FULL-ar.txt.
Do not layer patches or alter the English source version.
