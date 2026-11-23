Optional language + Arabic RTL patch r32 for vipirc 0.7.37

Requires a fresh, unchanged vipirc 0.7.37 English source r1 tree.
Do not apply r32 over r31 or any older patch. English application versions
remain unchanged.

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r32.tar.gz
    sh vipirc-0.7.37-languages-r32/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37
    ./configure && make
    ./vipirc --version

Expected patched version: vipirc 0.7.37 [lang-rtl-r32]
Set VIPIRC_LANG=id|ms|tr|es|pt|fr|de|ru|it|ar to select a locale.

NEW r32: 24 fixed start-up errors, four additional messages and an eight-line
--help introduction now have source-matched translations for all ten locales.
The full --help option table remains English so flags and syntax are copyable.
This pass does not translate every warning or all ncurses diagnostic messages.
See doc/CLI-LOCALIZATION-r32.txt and doc/CLI-LOCALIZATION-r32.tsv.

Full SECURITY.md translation drafts and prior manuals remain included,
awaiting fluent security review. Original English SECURITY.md is authoritative.
Use signals=no remains the application-wide default; this patch does not
change networking, terminal signal handling, RTL or Arabic wire text.
Long-form documentation was completed in prior r31; no new application
version number is required for a translation-only update.
