يضيف r20 أربعة عشر فصلاً كاملاً من دليل vipirc بالعربية. تظل الأوامر والمسارات حرفية، ولا تزال المراجعة اللغوية المتخصصة مطلوبة.

English source: vipirc-0.7.36-english-source-r1.tar.gz
Optional patch: vipirc-0.7.36-languages-r20.tar.gz
Extract both archives into the same parent directory.
Apply ONCE before configure/build:
    sh vipirc-0.7.36-languages-r20/apply.sh vipirc-0.7.36
Build: cd vipirc-0.7.36 && ./configure && make
--version: vipirc 0.7.36 [lang-rtl-r20]
Locale: VIPIRC_LANG=ar
Original English MANUAL.txt and SECURITY.md remain authoritative.
Historical READMEs are in HISTORY/; do not use their old commands.
