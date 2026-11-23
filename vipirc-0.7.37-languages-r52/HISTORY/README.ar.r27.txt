أضيفت مسودات ترجمة أربعة أقسام من SECURITY.md. ويظل المستند الأمني الإنجليزي المرجع المعتمد. راجع الأوامر والحدود بالرجوع إلى الأصل، ولا تزال هناك حاجة إلى مراجعة مختصين يتقنون اللغة.

vipirc 0.7.36: optional language + RTL patch r27
English source: vipirc-0.7.36-english-source-r1.tar.gz
Patch: vipirc-0.7.36-languages-r27.tar.gz

    tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.36-languages-r27.tar.gz
    sh vipirc-0.7.36-languages-r27/apply.sh vipirc-0.7.36
    cd vipirc-0.7.36 && ./configure && make
    ./vipirc --version    # vipirc 0.7.36 [lang-rtl-r27]

Language: VIPIRC_LANG=ar
Draft security chapters: doc/SECURITY-PARTIAL-ar.txt
Apply only to a fresh English r1 source tree, not over an earlier patch.
English SECURITY.md remains authoritative. Earlier instructions: HISTORY/.
