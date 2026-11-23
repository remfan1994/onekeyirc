Optional language + Arabic RTL patch r42 for vipirc 0.7.37

Requires a fresh, unchanged vipirc 0.7.37 English source r1 tree.
Do not apply this patch over any previous language overlay. English application versions
remain unchanged.

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r42.tar.gz
    sh vipirc-0.7.37-languages-r42/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37
    ./configure && make
    ./vipirc --version

Expected patched version: vipirc 0.7.37 [lang-rtl-r42]
Set VIPIRC_LANG=id|ms|tr|es|pt|fr|de|ru|it|ar to select a locale.

RETAINED FROM r32: 24 fixed start-up errors, four additional messages and an eight-line
--help introduction now have source-matched translations for all ten locales.
The full --help option table remains English so flags and syntax are copyable.
This pass does not translate every warning or all ncurses diagnostic messages.
See doc/CLI-LOCALIZATION-r33.txt and doc/CLI-LOCALIZATION-r33.tsv.

Full SECURITY.md translation drafts and prior manuals remain included,
awaiting fluent security review. Original English SECURITY.md is authoritative.
Use signals=no remains the application-wide default; this patch does not
change networking, terminal signal handling, RTL or Arabic wire text.
Long-form documentation was completed in prior r31; no new application
version number is required for a translation-only update.

RETAINED FROM r33:
- Three dynamic startup diagnostics are localized without interpreting format
  directives. Literal options, error strings, server numbers and paths are preserved.
- Selected full-screen key-binding labels/status messages, Settings
  close/discard outcomes and prompt history misses are localized.
- English fallback and command/keyboard identifiers remain unchanged.
- Native-speaker review and full ncurses builds are still pending.

NEW IN r34:
- Source-linked display notices: 13 onekeyirc / 20 fixed plus 5 named-field vipirc.
- No change to IRC messages, application version or English source r1.
- See doc/UI-LOCALIZATION-r34.txt for linked keys and remaining caveats.
- Native-language review is still needed; full vipirc ncurses build unverified.

NEW IN r35:
- vipirc: Settings and message-queue safety, connect errors, dynamic status.
- onekeyirc: eight additional draft/confirm/terminal notices.
- Sources, versions, IRC protocol and config behavior are unchanged.
- See doc/UI-LOCALIZATION-r36.txt, .tsv and .json for details.
- Fresh English source r1 is REQUIRED; r36 is not an incremental upgrade.

NEW IN r36:
- vipirc: 58 source-linked display paths, including 2 safe numeric templates.
- onekeyirc: 9 catch-up, draft and command-frequency notices.
- Ten-language updated catalogs and QA files; no program version change.
- Install only against fresh matching English source r1.

NEW IN r38:
- vipirc: 42 linked context-help, status, UI and dynamic command-error messages.
- All ten locale catalogs updated; base application source/version unchanged.
- Use matching fresh English source r1, not r36 or any patched source.

NEW IN r38: 47 source-linked messages across ten languages.
Apply only to fresh English r1 source, never an earlier optional patch.

NEW IN r39:
- 45 vipirc display labels, status notices and safe field messages; one onekeyirc response limit notice.
- Apply only to clean matching English r1 source; do not layer overlays.

NEW IN r41: additional display labels, settings hints, command diagnostics and context-help translations.

NEW IN r41: translated command help and status messages; use clean English r1 only.

NEW IN r42: localized context help, status and history headings. English r1 source only.
