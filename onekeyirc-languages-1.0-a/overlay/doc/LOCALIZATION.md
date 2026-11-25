# Current localization status: onekeyirc 1.0-a, optional language pack 1.0-a (release candidate)

- 2193 English lookup keys and ten complete UTF-8 catalogs.
- Apply only to a fresh onekeyirc 1.0-a English source r1 tree; see README.txt in the standalone pack.
- Both application and pack now use version 1.0-a. IRC protocol tokens, commands, configuration keys and named-field values remain literal.
- Command-line help, contextual readers and substantial diagnostics are localized. Catalog completeness is not proof that every user-facing source string has been translated.
- Localized minimalist manuals are available in doc/MINIMAL-MANUAL-XX.txt. Native-speaker review, full vipirc ncurses/Arabic terminal validation and live OpenBSD testing remain outstanding.
- Maintainer checks: `python3 tools/test_named_fields.py` and `python3 tools/generate_localizations.py --check`.


The following sections are historical revision notes, not current installation instructions or coverage claims.

---

# Language support: onekeyirc 0.1.35

## Current language pack r12

r12 refines Arabic text rendering and editing in all three frontends. An
editor redraw prepares one temporary layout for both text and cursor, with
allocation-free painting after successful preparation. A failed preparation
uses the complete ordinary logical redraw. Up/Down at a clamped boundary
preserves the current logical insertion stop and its visual affinity.

The ncurses painter keeps a base and supported combining marks in one cell
at the right and bottom margins. History row counting and painting share one
per-record map. Minimalist readers reuse a scoped paragraph map while paging,
counting and painting wrapped rows; a failed paragraph map is not retried
partway through that traversal. Early checks avoid unnecessary wide-input
conversion when the optional converter cannot be used.

The translation corpus and complete minimalist manuals are unchanged from
r11: 844 entries in each of ten translated catalogs. Full ncurses MANUAL.txt,
SECURITY.md and some contextual help/diagnostics still need translation.
Native OpenBSD 7.8 execution, physical Crostini/terminal/font checks and
native-speaker review remain pending. Existing logical storage and IRC wire
text are preserved. The application's controls are not mirrored.

Apply r12 to a fresh English r1 tree before configure/make. The original
English archives and application versions are unchanged. Current behavior
and reproduction commands are in the English and Arabic terminal guides;
earlier revision sections below retain their historical scope.


## Previous language pack r11

Arabic text now has optional paragraph-aware shaping/wrapping and visual
cursor/mouse editing in onekeyirc, vipirc -i and full vipirc --ncurses. The
system FriBidi converter is optional, works independently of menu language,
requires a UTF-8 locale, and falls back to the ordinary logical path when
unavailable. Text, commands, configuration, saved logs and IRC wire bytes stay
logical UTF-8. The interface's panel/button arrangement is not mirrored.

Left/Right follow visual caret stops. Up/Down and mouse use the same map where
the field uses vertical editing; existing command-history bindings remain.
Home/End keep document endpoints. Backspace/Delete keep logical cluster
semantics. Existing select-all works; arbitrary range selection is not added.
Secret fields retain masking and logical movement. See the complete English
and Arabic guides: doc/ARABIC-TERMINALS.txt and doc/ARABIC-TERMINALS-ar.txt.

All ten catalogs retain 844 entries and r10's complete minimalist manual
exports. Full ncurses MANUAL.txt, SECURITY.md and some other help/diagnostics
still need translation. Native OpenBSD 7.8 execution, physical Crostini and
other terminal/font checks, and native-speaker proofreading remain pending.
Strict Linux builds, real ncurses-cell and PTY checks cover the implementation;
they do not establish the appearance of a physical terminal font.

The original English r1 archives and application versions are unchanged.
Apply r11 to a fresh English r1 tree. Earlier sections below record the
coverage and limitations of their named revisions, not the current RTL scope.


## Previous language pack r10

Each of the ten catalogs now contains 844 entries, 87 more than r9. Complete
minimalist manuals are available as doc/MINIMAL-MANUAL-XX.txt for en, id, ms,
tr, es, pt, fr, de, ru, it and ar. The actual command formatter is used for
every export, including program-specific configuration, launch defaults,
command descriptions and command-entry instructions. Command syntax,
examples and topic identifiers remain literal.

The complete onekeyirc minimalist manual has 582 distinct prose lookup keys;
all have translations in all ten languages. The pair of clients uses 594
distinct prose keys in total. These counts include the previously completed
fixed command/topic tables; they are not additional to those table counts.
The exports also preserve 128 literal command examples, three pure alias
mappings and 19 topic identifiers. Native-speaker proofreading is outstanding.

The command-line manual now follows the same language selection as the UI:

    ONEKEYIRC_LANG=ar ./onekeyirc --manual > manual-ar.txt

This needs no terminal or IRC connection. Exports stay in logical UTF-8 even
with Arabic visual mode enabled. The English formatter remains available to
maintainer tools, and the English documentation generator pins both language
overrides to en. The application's version and English r1 archive are unchanged.

The full vipirc ncurses MANUAL.txt, SECURITY.md, some contextual assistance,
diagnostics and other application text still require translation. Completing
the minimalist manuals does not complete every document or UI message.

Arabic terminal behavior retains the r9 display-only converter. The terminal
guide is now available in Arabic as doc/ARABIC-TERMINALS-ar.txt. Full ncurses
RTL, visual input/cursor editing, paragraph-aware line wrapping, physical
terminal/font checks and native OpenBSD validation remain unfinished.

Run make localized-manuals to refresh exports and make check-localized-manuals
to audit them after configure. See doc/MINIMAL-MANUAL-LOCALIZATION.txt.
The following r9 section records the earlier status before this pass.


## Previous language pack r9

The source-derived fixed Command.detail and Topic.text tables now have all
477 lines catalogued in all ten languages: 474 translated prose lines and
three command-only alias mappings that correctly stay literal. The three
other alias lines contain the word "and", now translated without changing
any command name. There are 757 catalog entries per language.

This closes the fixed-table help queue, not the entire documentation project.
The full English MANUAL.txt, SECURITY.md, shell --manual exports, some
frontend-specific generated help, contextual help and diagnostics still need
translation. Native-speaker review remains outstanding in all ten languages.

Read doc/HELP-REFERENCE-XX.txt for all 59 command and 19 topic table sections,
with exact command syntax and examples. The reference explicitly excludes
frontend-specific generated additions. Replace XX with a language code.
The original doc/COMMANDS-XX.txt guides are restored; the newer six-command
excerpt has its own doc/CHANNEL-COMMANDS-XX.txt filename. Earlier generators
had overwritten one another's COMMANDS files. Both exports now validate.

Source-derived help prose may use up to 1024 UTF-8 bytes. Other translated
labels keep their 256-byte limit. This prevents required behavioral details
being cut from Arabic, Russian and expanding translations, while remaining
below the runtime help emitter's 2048-byte buffer. The runtime regression
checks each complete translated line after the real command reader emits it.

The experimental optional Arabic display path also covers live WATCH output
in r9; malformed UTF-8 handling is tightened. It remains a display-only
feature for onekeyirc and vipirc -i. Full ncurses RTL and visual caret editing
are still incomplete. See doc/ARABIC-TERMINALS.txt for exact scope and tests.


This is an installable source release with a localized user interface, not a
complete translation of every application message and manual. The same
844 English source keys and ten UTF-8 language catalogs ship in the optional
language packs for both programs. Shared presentation and setup code use the
same catalogs. Translations are drafts pending native-speaker review.

## Choose a language

    VIPIRC_LANG=id onekeyirc
    VIPIRC_LANG=ms onekeyirc
    VIPIRC_LANG=tr onekeyirc
    VIPIRC_LANG=es onekeyirc

Interface-language codes (use just the two-letter code or a standard locale
such as id_ID.UTF-8):

    id Indonesian       ms Malay          tr Turkish
    es Spanish          pt Portuguese     fr French
    de German           ru Russian        it Italian
    ar Arabic

Selection precedence: onekeyirc uses ONEKEYIRC_LANG, then VIPIRC_LANG;
vipirc uses VIPIRC_LANG, then ONEKEYIRC_LANG. Both then check LC_ALL,
LC_MESSAGES and LANG. The first nonempty setting wins. Unrecognized values or missing
translations return the original English term. Environment overrides affect
new processes; detached background workers keep the language at their launch.
The same environment selection is supported in both frontend modes.

IMPORTANT: The display-language override does not activate a Unicode locale.
Set LANG or LC_CTYPE to a UTF-8 locale installed on the host, and use a
UTF-8-capable terminal and font. Check `locale -a` and `locale charmap`.
Arabic stays in logical UTF-8 for storage, parsing and transmission.
r11 adds shared visual text/caret maps for both frontends using an installed
system FriBidi library. Read doc/ARABIC-TERMINALS.txt for current behavior.
Native target-terminal/font and OpenBSD acceptance remain pending.

## Coverage and safety

Included UI: important navigation labels, screen names, common buttons,
settings group labels/field captions, guided setup prompts, and selected
minimalist mode/status labels. Some complex sentences, program diagnostics,
security warnings, long-form assistance, documentation, authentication
control values, and display messages remain in English. This is deliberate:
translated user-interface strings are never used to interpret commands,
IRC wire messages, GPG paths, config directives, or security decisions.

The `/join`, `/part`, `/msg` and other command names stay in English across
all languages. Literal choice tokens such as `yes`, `no`, `direct`, `znc`,
`sasl`, and `tls` are unchanged. Never translate saved field names or
configuration values. If in doubt, retain the English display text rather
than changing functional control flow.

## How to add and review translations

1. Edit `i18n/XX.json`; English master source keys are in `i18n/en.json`.
   JSON files are UTF-8. Every language catalog needs exactly the same keys.
2. Run `python3 tools/generate_localizations.py` to rebuild `src/l10n.h`.
3. Run `python3 tools/generate_localizations.py --check` and
   `python3 tests/test_locale_catalog.py`.
4. After `./configure`, run `make check-l10n` and `make check-docs`.
5. Verify the running terminal at 80 and 40 columns in a real UTF-8 locale.
   Test mouse hit areas, line wrapping, panel selection and return paths.
   Review Arabic under actual target terminals before claiming full support.
6. Compare both catalogs and shared C sources before distributing the pair.

## Distribution size and English-only builds

English source r1 and language pack r12 are separate archives. Apply the
version-matched pack to a fresh English source tree before compiling.
Localized text is compiled into the executable; no runtime catalog is needed.
The pack must never be applied over an earlier installed pack.

For an English-only executable, build from this SAME source archive with:

    make clean             # only if reusing a previously built source tree
    CPPFLAGS=-DIRC_DISABLE_LOCALIZATION ./configure --prefix=/usr/local
    make

This compile-time option omits the localization tables and makes all UI
lookups return English. Existing config keys and IRC protocol syntax remain
unchanged. `make check-l10n` expects all translations and should be run on
an ordinary (localized) build; run `make check-docs` for either kind.

The one-definition localization table lives in `src/idle_setup.c` in both
clients. The generated `src/l10n.h` has external declarations for callers
and only emits its data in the one owning translation unit. Avoid defining
`IRC_L10N_IMPLEMENTATION` elsewhere in application sources, or strings may
be duplicated and the linker may report conflicting definitions.

The generated translation table is static: the applications require no
runtime gettext package, locale database, or network service for lookup.
Use normal `make install` / `make uninstall`; catalogs accompany the source
for maintainers, and are compiled into the executable for end users.

## Test boundaries

`make check-l10n` covers all ten catalogs, locale variants, English fallback,
control-token preservation, source/header consistency, and a no-network
pseudo-terminal smoke test when the standalone client executable exists.
A C unit test does not prove terminal bidirectional layout or native-speaker
translation quality. Full vipirc requires wide-character curses development
headers and a terminal runtime. Native OpenBSD validation is recommended
before deployment to that target.

## Second localization pass

The 267-entry master catalog now includes command-suggestion ordering names,
channel list empty states, conversational notices, paste and draft preservation,
terminal size guidance, and more full-interface selection hints. Source code
translates these only when they are displayed, never before interpreting
commands, connection fields or configuration values. These are still draft
translations, not native-speaker-reviewed software.

The full-screen interface now allows up to 320 bytes for translated setting
field labels; guided setup and minimalist context help use larger bounded
buffers. The renderer clips by terminal cells. Maintainers should test
45-column and 100-column UTF-8 terminals and confirm no truncated UTF-8
sequence or shifted keyboard/mouse focus before accepting new wording.

The no-network PTY smoke test visits the minimalist settings panel in all ten
languages. It verifies UTF-8 output and localized panel labels, not proper
Arabic shaping or semantic accuracy of translations.

Run `python3 tools/review_localizations.py --lang id` to export a CSV
translator review sheet; use --unchanged-only to highlight source-equal
translations for review. Equal text is sometimes intentional (ZNC, TLS,
brand names, acronyms), so treat those rows as candidates, not errors.

## Third localization pass

There are 267 translation keys per language. The new interactive `/help` overview,
its topic/command-index headings, common command summaries, and `/help language`
are translated in all ten languages. Command spelling, shell arguments,
configuration keys, help topic identifiers and protocol syntax remain in English.
Detailed topic reference and security explanations are not yet fully translated.

Short local-language offline introductions are in `doc/START-XX.txt`, with XX
set to id, ms, tr, es, pt, fr, de, ru, it or ar. They are generated from the
same reviewed source strings as the interactive help with:

    python3 tools/generate_locale_guides.py
    python3 tools/generate_locale_guides.py --check

The full English manuals are maintained separately and remain authoritative
for exact security and protocol semantics. Translator submissions must preserve
commands, sample addresses, parameter names and literal environment variables.
Test with `make check-l10n`, including live offline `/help` checks, before
publishing a translated catalog; Arabic still needs native RTL terminal review.

The earlier documentation/comment pass is retained: `doc/USE-MODIFY-REDISTRIBUTE.txt`
and the annotated source code remain part of this distribution.

## Fourth localization pass: detailed help

There are now 290 translation keys in each of ten locales. The 23 newly
localized detailed help lines cover authentication, bouncers, job/session
recovery, TLS boundaries, persistent logging, reconnect and troubleshooting.
The runtime help reader translates only whole source lines; commands,
placeholder identifiers and config values remain literal. Untranslated lines
continue to appear in English without changing command interpretation.

The generated offline companion files doc/TOPICS-XX.txt reproduce the 23
localized runtime help lines. Create or verify them using:

    python3 tools/generate_locale_topic_guides.py
    python3 tools/generate_locale_topic_guides.py --check
    python3 tests/test_detailed_help_translation.py

The English MANUAL.txt and SECURITY.md remain authoritative. These translations
are drafts pending native-language and platform review. Arabic terminal RTL
layout and shaping remain outside the client's current rendering capability.

## Fifth localization pass: command help, diagnostics, and measurement

The current source has 326 catalog keys in ten languages. New translations
cover 20 additional command help lines and 16 diagnostic display strings,
including unknown commands, missing views, setup delays and input failures.
These phrases are translated only at display time. Command parsing, IRC wire
messages, configuration keys and saved data remain unchanged.

Find localized supplemental command instructions in doc/COMMANDS-XX.txt and
run tools/generate_locale_command_guides.py --check to verify them. The tool
reads the same catalog values that the running help renderer uses. Topic
supplements remain in doc/TOPICS-XX.txt; neither replaces the complete
English MANUAL.txt or SECURITY.md.

The reproducible, source-based audit is doc/HELP-LOCALIZATION-COVERAGE.txt.
Run python3 tools/audit_help_coverage.py to regenerate it, or --check to
verify the distribution. The inherited source contains 477 command/topic
help lines; 43 have complete translation keys. The audit deliberately does
not count command examples, user-visible UI labels or syntax as translated
prose. It does not establish quality or proper terminal presentation.

A UTF-8 width fix now sizes interactive help underlines by display cells
rather than bytes. The targeted terminal regression checks localized /help
back and an unknown-command diagnostic in all ten languages. Native OpenBSD,
full ncurses and Arabic bidirectional/shaping behavior are still unverified.

## Sixth localization pass: navigation and private conversations

The current source has 405 catalog entries per language. The previous pass added 39
complete navigation and command-help explanations in ten languages: /channel,
/channels, /cl, /close, /disconnect, /help, /me, /mentions, /msg, /next,
/nick, /pm, /prev, /ql, /queries, /settings, /sl, /unread, /view and /views.
The exact source-level help audit records 122 translated lines of 477; 355
remain English. These figures measure catalog presence, not linguistic quality.

All command names and upper-case placeholders are left unchanged. New
translations do not participate in parsing, persistent configuration or IRC
wire messages. An offline terminal regression displays /help channels across
all ten locales, and the token test rejects changed slash commands or
upper-case placeholders. Native-speaker review is still needed.

Localized offline navigation references are in doc/NAVIGATION-XX.txt, where
XX is one of id, ms, tr, es, pt, fr, de, ru, it, ar. They are generated from
the running help source and catalogs with:

    python3 tools/generate_locale_navigation_guides.py
    python3 tools/generate_locale_navigation_guides.py --check
    python3 tests/test_navigation_help_translation.py
    python3 tests/test_l10n_navigation_terminal.py --binary ./onekeyirc

The terminal test command uses onekeyirc; for a fully built vipirc use the
--full-client option and --binary ./vipirc instead. Neither checks Arabic
bidirectional shaping or terminal features on an actual OpenBSD installation.
The English main manual and source comments remain authoritative.

## Connection and authentication localization continuation

At the preceding pass, catalogs had 405 entries in ten languages. Exact
command/topic whole-line help coverage is 122/477 (25.6%). The new 40
translated help lines cover /connect, /autoauth, /autojoin, /saveconf,
/help reconnect and /help tls. The ten doc/CONNECTION-XX.txt guides are
reproducible excerpts of the live translations; they are not substitutes
for the complete English manual and security guide. All argument names,
flags, channel syntax and port numbers remain literal.

For maintainers:

    python3 tools/generate_locale_connection_guides.py --check
    python3 tests/test_connection_help_translation.py
    python3 tools/audit_help_coverage.py --check

The added offline PTY test covers /help connect and /help autoauth in
all ten languages without contacting IRC servers or reading credentials.
A prior translation of the --check-config warning was repaired in all ten
languages to retain the warning about CA bundles and credential sources.
Neither text checks nor terminal byte checks establish Arabic shaping or
translation fidelity; native speakers must proofread before labeling the
catalogs complete.

## Authentication and bouncer help expansion (0.1.43)

The catalog now has 430 strings per language. The source-audited interactive
help coverage is 148 of 477 lines, across ten draft translations. The additional
25 unique strings cover /soju and /znc, credential file selection and mode,
TLS CA file limits, and selected bouncer behavior. See the ten
`doc/AUTH-BOUNCERS-XX.txt` excerpt guides. The English SECURITY.md and MANUAL.txt
remain authoritative for exact security boundaries. Native-speaker review,
OpenBSD testing and terminal right-to-left rendering are outstanding.

Regenerate and test these guides using:

    python3 tools/generate_locale_bouncer_auth_guides.py
    python3 tests/test_auth_bouncer_help_translation.py
    python3 tests/test_l10n_auth_bouncer_terminal.py --binary ./onekeyirc

The last terminal command applies to the standalone executable. For vipirc -i,
pass --full-client with a built vipirc binary. The new English-only build option
remains available without a separate source archive.

## Deployment and security-help quality pass (0.7.45 / 0.1.44)

Catalogs now have 452 terms per locale. Exact source-line audit counts
170 translated help lines out of 477 (35.6 percent). These figures measure
source-key coverage, not the accuracy of the translations. New drafts cover
selected deployment, terminal, security, history and custom-binding guidance.
The /clear logging explanation was repaired in all ten languages; it explicitly
notes that disabling logging stops new entries without deleting older logs.

`doc/OPERATIONS-XX.txt` contains the new selected source-aligned extracts.
Run `python3 tools/generate_locale_operations_guides.py` to rebuild them,
then `python3 tests/test_operations_help_translation.py` and the offline
`tests/test_l10n_operations_terminal.py --binary ./onekeyirc` test.

The localized extracts are not complete manuals. The English SECURITY.md,
MANUAL.txt and official command syntax remain authoritative pending
native-speaker review. Arabic bidi/shaping still requires target verification.

Language overrides are evaluated per running program: onekeyirc gives
ONEKEYIRC_LANG precedence over VIPIRC_LANG, while vipirc gives VIPIRC_LANG
precedence over ONEKEYIRC_LANG. LC_ALL, LC_MESSAGES and LANG remain fallbacks.
An existing managed worker keeps its environment until restarted.

## Complete practical-help sections (version 0.1.35)

The 519-key draft catalogs now cover 239/477 command/topic help lines by
source presence. Ten help sections (89 complete explanations per language)
are exported as doc/PRACTICAL-GUIDE-XX.txt. They cover startup, messaging,
limits, /quickstart, /save, documentation, security, sessions, troubleshooting,
and bouncers. They are not a translation of the entire MANUAL.txt.

The previously shortened --close-session explanation was corrected in all
ten languages: closing a job ends IRC connections and transfers, loses unsaved
drafts and RAM history, and preserves existing saved files. Source-aligned
tests now check IRC and other immutable technical tokens in all ten guides.

Rebuild and verify these excerpts with:

    python3 tools/generate_locale_practical_guides.py
    python3 tools/generate_locale_practical_guides.py --check
    python3 tests/test_practical_help_translation.py

Full English manuals, detailed security documentation, source comments and
protocol commands remain authoritative. Native-speaker proofreading, the
full ncurses frontend, native OpenBSD, and Arabic RTL shaping are still
separate verification requirements.

## Current separate-pack distribution (r6)

The English source r1 contains code and source comments, but only English
translation lookups. Language pack r6 supplies the generated table, ten
UTF-8 catalogs with 632 keys per locale, localized README files and guides.
It covers 352/477 interactive help lines (73.8%). The other 125 lines,
substantial parts of the manuals and some diagnostics remain English.

New source-aligned guides are doc/CONFIGURATION-XX.txt and doc/STATUS-XX.txt.
They are generated from the runtime help strings, checked for command
spelling and configuration-value preservation, and do not replace the
authoritative English MANUAL.txt or SECURITY.md.

Use a new English r1 tree for each new language-pack revision. Select the
locale with the program-specific variable. The inherited terminal locale
must support UTF-8 and Arabic shaping/bidirectional layout is not assured.

## Optional r7 language pack

Apply the version-matched r7 archive to a fresh English source r1 tree.
The ten catalogs now contain 664 entries each. A source-derived audit
counts 384 of 477 detailed-help lines with translations (80.5%).
The new locale guides doc/SETTINGS-XX.txt and doc/CHANNELS-XX.txt
are derived directly from runtime strings. Original command spellings,
option tokens and alias mappings must not be translated.
This is not a translation of the complete MANUAL.txt or SECURITY.md.
Native-speaker proofing and target terminal checks are outstanding.

## Optional r8 Arabic visual mode (experimental)

Arabic logical text is the default. Most terminal emulators do not reorder
and shape Arabic themselves, but some do. For terminals that DO NOT perform
bidi layout, install the system FriBidi library and enable visual display:

    ONEKEYIRC_LANG=ar ONEKEYIRC_ARABIC_VISUAL=1 ./onekeyirc

This visual mode converts each already-wrapped physical terminal line to
visual presentation glyphs immediately before output. It leaves the stored
logical Unicode text, IRC messages, logs, command syntax and input editor
unchanged. The conversion retains Latin /help and --option tokens. It uses
FriBidi via the system dynamic loader, not a mandatory link-time dependency.
If the shared library or dynamic loader is unavailable, the program falls
back to the original logical UTF-8 output. The build's configure script
checks the dynamic loader; normal English and non-Arabic use is unaffected.

This option is only for onekeyirc or vipirc -i. It does not fix right-to-left
cursor navigation or the full vipirc ncurses frontend. Do not enable it on
terminals that already perform Arabic shaping/reordering (double shaping can
occur). Terminal font support for Arabic presentation forms is required.
Line wrapping, cursor position, copying rendered text and mixed scripts still
need real target-system testing. Native OpenBSD and Arabic-speaker review are
outstanding. Tests on one Linux pseudo-terminal are not a universal guarantee.

For a safe fallback, unset ONEKEYIRC_ARABIC_VISUAL or VIPIRC_ARABIC_VISUAL, or
set it to 0. IRC_DISABLE_LOCALIZATION still makes all UI text English.
