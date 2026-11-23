OPTIONAL vipirc 0.7.36 TRANSLATIONS + ARABIC RTL SOURCE PATCH r23

1. tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
2. tar -xzf vipirc-0.7.36-languages-r23.tar.gz
3. sh vipirc-0.7.36-languages-r23/apply.sh vipirc-0.7.36
4. cd vipirc-0.7.36 && ./configure && make
5. ./vipirc --version    # vipirc 0.7.36 [lang-rtl-r23]

Apply ONLY to freshly extracted matching English source r1.
Do not layer r23 over any older language/RTL patch. Check SHA-256 hashes.
Locales: id ms tr es pt fr de ru it ar; English is the fallback.
The optional RTL, networking, editing and security behavior is identical
in this revision to r22. The original English application version and
source r1 remain unchanged; only the optional patch label advances.
vipirc has 45 of 52 full-manual chapters in ten source-linked translation drafts.
See doc/TRANSLATION-STATUS-r23.txt for remaining work and limitations.
English MANUAL.txt and SECURITY.md remain authoritative.
See HISTORY/ for outdated release instructions. DO NOT execute old commands.

r23 adds the complete SETTINGS and FILES AND ENVIRONMENT chapters in all ten languages (900 English source words per language). No RTL/network/editor code changes beyond the displayed patch revision.
45 of 52 vipirc MANUAL.txt sections now have complete source-linked draft translations.
