OPTIONAL vipirc 0.7.36 TRANSLATIONS + ARABIC RTL SOURCE PATCH r21

1. tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
2. tar -xzf vipirc-0.7.36-languages-r21.tar.gz
3. sh vipirc-0.7.36-languages-r21/apply.sh vipirc-0.7.36
4. cd vipirc-0.7.36 && ./configure && make
5. ./vipirc --version    # vipirc 0.7.36 [lang-rtl-r21]

Apply ONLY to freshly extracted matching English source r1.
Do not layer r21 over any older language/RTL patch. Check SHA-256 hashes.
Locales: id ms tr es pt fr de ru it ar; English is the fallback.
The optional RTL, networking, editing and security behavior is identical
in this revision to r20. The original English application version and
source r1 remain unchanged; only the optional patch label advances.
vipirc has 39 of 52 full-manual chapters in ten source-linked translation drafts.
See doc/TRANSLATION-STATUS-r21.txt for remaining work and limitations.
English MANUAL.txt and SECURITY.md remain authoritative.
See HISTORY/ for outdated release instructions. DO NOT execute old commands.
