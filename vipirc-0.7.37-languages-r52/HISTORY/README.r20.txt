OPTIONAL vipirc 0.7.36 TRANSLATIONS + ARABIC RTL PATCH r20

1. tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
2. tar -xzf vipirc-0.7.36-languages-r20.tar.gz
3. sh vipirc-0.7.36-languages-r20/apply.sh vipirc-0.7.36
4. cd vipirc-0.7.36 && ./configure && make
5. ./vipirc --version    # vipirc 0.7.36 [lang-rtl-r20]

Apply ONLY to freshly extracted matching English source r1.
Do not layer r20 over r18 or any older patch. Check SHA-256 checksums.
Ten languages: id ms tr es pt fr de ru it ar. English still available.
No Arabic RTL, network, DCC, security or editor behavior changed from r18.
English MANUAL.txt and SECURITY.md remain the authoritative source.
New translated chapters are DRAFTS pending native-speaker review.
See doc/TRANSLATION-STATUS-r20.txt for coverage and limitations.
Historical release READMEs are in HISTORY/; do not run old commands.

Translation progress: vipirc 27/52 complete source-linked full-manual chapters; all documents are drafts pending native review.
