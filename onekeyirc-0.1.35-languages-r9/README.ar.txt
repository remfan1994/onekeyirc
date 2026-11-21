onekeyirc 0.1.35 - حزمة اللغات r9 (ar)
========================================================

--- r9 ---
حالة r9 الحالية: ترجمت جميع الأسطر النصية البالغ عددها 474 في جداول مساعدة الأوامر والمواضيع الثابتة. تبقى ثلاث مطابقات أسماء بديلة مؤلفة من أوامر فقط حرفية؛ ولكل أسطر الجداول البالغ عددها 477 إدخالات في فهرس الترجمة. بعد التطبيق، راجع doc/HELP-REFERENCE-ar.txt. ما زالت MANUAL.txt وSECURITY.md الكاملتان وبعض المساعدة المولدة أو الخاصة بكل عميل ورسائل التشخيص بالإنجليزية. تحتاج الترجمات إلى مراجعة متحدثين أصليين. طبق r9 على مصدر English r1 جديد فقط، ولا تطبقه فوق r8 أو أي حزمة أخرى.


ترجمات للواجهة والمساعدة وبعض الوثائق. ليست برنامجا مستقلا.

استخدم فقط أرشيف المصدر English r1 المذكور أدناه. لا تستخدم مصدرا قديما أو معدلا.
  onekeyirc-0.1.35-english-source-r1.tar.gz

فك ضغط الأرشيفين في المجلد نفسه:
  tar -xzf onekeyirc-0.1.35-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.35-languages-r9.tar.gz

طبّق الحزمة قبل تشغيل configure أو make:
  sh onekeyirc-0.1.35-languages-r9/apply.sh onekeyirc-0.1.35

اضبط البرنامج وابنه:
  cd onekeyirc-0.1.35
  ./configure --prefix=/usr/local
  make

شغّل البرنامج بالعربية:
  ONEKEYIRC_LANG=ar ./onekeyirc

لا تترجم أوامر IRC وخيارات التشغيل ومفاتيح الإعداد. اكتبها كما هي.

الترجمات أولية. بعض المساعدة والأدلة الكاملة لا تزال بالإنجليزية. لتفاصيل الأمان اقرأ MANUAL.txt وSECURITY.md. يلزم طرفية UTF-8؛ عرض العربية من اليمين إلى اليسار غير مؤكد.

يحتوي PACK-SHA256SUMS على البصمات. يتحقق apply.sh من توافق المصدر. لا تنسخ الملفات فوق مصدر معدل.

الجديد في r4: اكتملت ترجمة /help background و/help catchup و/help logging. يشرح RECOVERY-ar.txt استئناف الجلسات وحدود حفظ السجلات.

PACK-SHA256SUMS / README.txt / MANUAL.txt / SECURITY.md

جديد في r5: اكتملت ترجمة /help autocommands و/help dcc و/help bindings و/help bind و/help unbind و/help transfers و/help hotkeys. راجع doc/WORKFLOWS-ar.txt.

الجديد في r6: اكتملت ترجمة /help configuration و/help summary و/help server و/help users و/help watch. بعد التثبيت اقرأ doc/CONFIGURATION-ar.txt وdoc/STATUS-ar.txt.

الجديد في r7: 32 شرحاً إضافياً للمساعدة بشأن /help set و/help list والتنقل المحلي والأسماء البديلة للأوامر. بعد التثبيت اقرأ doc/SETTINGS-ar.txt وdoc/CHANNELS-ar.txt. التغطية: 384/477؛ بقي 93 شرحاً بالإنجليزية.

--- r8 ---
عرض العربية المرئي تجريبي ومخصص للواجهة المبسطة فقط. ثبّت مكتبة FriBidi في النظام وفعّل المتغير أدناه. لا تفعله إن كانت الطرفية تشكل العربية بنفسها. لا تتغير المحررات أو النصوص المحفوظة أو واجهة ncurses الكاملة. عند غياب FriBidi يبقى ترتيب Unicode الأصلي.

ONEKEYIRC_LANG=ar ONEKEYIRC_ARABIC_VISUAL=1 ./onekeyirc
doc/ARABIC-TERMINALS.txt / doc/COMMANDS-ar.txt
