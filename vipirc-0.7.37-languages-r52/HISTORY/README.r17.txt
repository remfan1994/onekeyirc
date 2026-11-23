OPTIONAL MULTILINGUAL + ARABIC RTL SOURCE PATCH r17: vipirc 0.7.36

Extract vipirc-0.7.36-english-source-r1.tar.gz, then this patch archive.
sh vipirc-0.7.36-languages-r17/apply.sh vipirc-0.7.36
Build with ./configure && make. Patched --version: vipirc 0.7.36 [lang-rtl-r17].
The English r1 archive stays separate. Do not layer patches.
New translated section: doc/SESSION-MANAGEMENT-XX.txt (ten languages).
Read doc/TRANSLATION-STATUS-r17.txt for documented gaps.
Original MANUAL.txt and SECURITY.md remain authoritative.
RTL and executable protocol behavior are unchanged from r16.

=== Previous release notes (historical, not current instructions) ===
OPTIONAL MULTILINGUAL + ARABIC RTL SOURCE PATCH r17: vipirc 0.7.36

Original English source r1 remains separate and unchanged. This optional
overlay contains translated catalogs and guides AND executable RTL code.
Do not compile or run this overlay alone; apply it to a fresh English tree.

1. Verify the trusted release SHA-256 checksums.
2. tar -xzf vipirc-0.7.36-english-source-r1.tar.gz
3. tar -xzf vipirc-0.7.36-languages-r17.tar.gz
4. sh vipirc-0.7.36-languages-r17/apply.sh vipirc-0.7.36
5. cd vipirc-0.7.36; ./configure; make
6. ./vipirc --version  # should show vipirc 0.7.36 [lang-rtl-r17]

Never layer r17 over r15. The installer verifies source hashes and
refuses a modified, previously configured or already-patched tree.

Languages: id ms tr es pt fr de ru it ar (plus English).
The 844-entry catalogs and Arabic shaping/editor logic are unchanged
from r15; this release extends translated documentation and version ID.
Translated security model excerpts: doc/SECURITY-BOUNDARIES-XX.txt.
Translated full ncurses starter excerpts: doc/NCURSES-START-XX.txt.
Localized mini manuals: doc/MINIMAL-MANUAL-XX.txt.
The full MANUAL.txt and SECURITY.md have NOT been fully translated.
Keep literal commands, paths, CLI switches and config keys unchanged.
These translations are drafts; fluent/native review is required.
RTL may require a UTF-8 locale and system FriBidi runtime.
Use VIPIRC_ARABIC_VISUAL=1 on a terminal without built-in Arabic bidi.
Do not enable visual conversion if the terminal shapes Arabic itself.
For original full security guarantees and limitations read SECURITY.md.
