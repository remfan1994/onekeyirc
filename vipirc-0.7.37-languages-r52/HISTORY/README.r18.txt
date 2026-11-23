OPTIONAL vipirc 0.7.36 TRANSLATIONS + ARABIC RTL PATCH r18

1. tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
2. tar -xzf vipirc-0.7.36-languages-r18.tar.gz
3. sh vipirc-0.7.36-languages-r18/apply.sh vipirc-0.7.36
4. cd vipirc-0.7.36 && ./configure && make
5. ./vipirc --version    # vipirc 0.7.36 [lang-rtl-r18]

Apply ONLY on a newly extracted matching English source r1 tree.
Do NOT layer r18 over r17 or any previous patch, and do not apply the
patch to an already configured/modified tree. Verify release SHA-256.
Languages: id ms tr es pt fr de ru it ar. English remains available.
The source patch includes optional Arabic RTL rendering and editing.
No RTL or protocol behavior has changed since r17 except the version tag.
Original full English MANUAL.txt and SECURITY.md remain authoritative.
See doc/TRANSLATION-STATUS-r18.txt for the exact scope and gaps.
Older readmes are archived in HISTORY/ for provenance only; their
installation commands refer to obsolete patch versions.

New: doc/DCC-FULL-XX.txt and doc/INPUT-MOUSE-XX.txt in all 10 languages.
