تتوفر مسودات مترجمة لـ 39 من 52 قسما من دليل vipirc. بقيت الأوامر حرفية، ولا تزال المراجعة اللغوية المتخصصة مطلوبة.

English source: vipirc-0.7.36-english-source-r1.tar.gz
Optional patch: vipirc-0.7.36-languages-r21.tar.gz
Extract both archives into the same parent directory.
Apply ONCE before configure/build:
    sh vipirc-0.7.36-languages-r21/apply.sh vipirc-0.7.36
Build: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r21]
Locale: VIPIRC_LANG=ar
Original English MANUAL.txt and SECURITY.md remain authoritative.
Historical READMEs are in HISTORY/; do not use their old commands.
