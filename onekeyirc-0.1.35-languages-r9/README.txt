onekeyirc 0.1.35 - OPTIONAL LANGUAGE PACK r9
================================================

Read README.XX.txt in your language:
  id Indonesian  ms Malay       tr Turkish  es Spanish  pt Portuguese
  fr French      de German      ru Russian  it Italian  ar Arabic

CURRENT HELP COVERAGE
  757 catalog entries in each of ten languages.
  474 translated prose lines in the fixed command/topic help tables.
  Three command-only alias mappings stay literal.
  All 477 table lines have catalog entries; no prose gaps in those tables.
  This is 75 additional translated lines and 78 new keys per locale over r8.

  The count covers 59 command sections and 19 topic sections. It does not
  mean the full documentation is translated. Full MANUAL.txt, SECURITY.md,
  shell --manual exports, contextual/help text outside these fixed tables,
  client-specific generated additions and some diagnostics remain English.
  All translations remain drafts pending native-speaker review.

SOURCE AND INSTALLATION
  Exact required source: onekeyirc-0.1.35-english-source-r1.tar.gz
  Apply to a FRESH English r1 tree before configure or make.
  Do not apply over r8 or another pack; do not use the historical archive
  with the same application number but a different source revision.

  tar -xzf onekeyirc-0.1.35-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.35-languages-r9.tar.gz
  sh onekeyirc-0.1.35-languages-r9/apply.sh onekeyirc-0.1.35
  cd onekeyirc-0.1.35
  ./configure --prefix=/usr/local
  make

  The application version stays 0.1.35. Identify this localized build
  as English source r1 plus language pack r9. Network command syntax,
  configuration keys/values and example commands stay literal.

OFFLINE DOCUMENTATION
  doc/HELP-REFERENCE-XX.txt contains every fixed command/topic help section
  with original usage syntax and examples. Replace XX with the language code.
  doc/COMMANDS-XX.txt restores the earlier command guide.
  doc/CHANNEL-COMMANDS-XX.txt holds the six-command excerpt introduced in r8.
  Earlier generators wrote to the same COMMANDS filename; r9 separates them.
  Localized READMEs and all earlier topic guides remain included.

ARABIC DISPLAY
  Optional visual shaping/reordering now includes live WATCH output and
  notices, as well as the minimalist readers/history screens.
  ONEKEYIRC_LANG=ar ONEKEYIRC_ARABIC_VISUAL=1 ./onekeyirc
  Requires the system FriBidi shared library and a UTF-8 terminal without
  its own Arabic bidi/shaping. Leave the visual variable unset or 0 when
  the terminal already renders Arabic. Missing FriBidi keeps logical UTF-8.
  Full vipirc ncurses RTL, visual input/cursor editing, native OpenBSD and
  physical terminal/font validation remain unfinished.
  See doc/ARABIC-TERMINALS.txt for scope, tests, copying and wrapping limits.

CHANGE BOUNDARY
  This pack includes generated translation data, documentation/build/test
  updates, and shared C presentation changes in idle.c and arabic_visual.h.
  These C changes fix the live Arabic display path, tighten UTF-8 decoding,
  and match FriBidi's function type. They do not transform stored messages,
  IRC packets, saved logs, configuration or logical input.
  Exact help prose may occupy up to 1024 UTF-8 bytes; other labels stay capped
  at 256. The runtime emitter check covers all 4770 expected help lines.

PACK-SHA256SUMS inventories this package. After applying, SHA256SUMS and
SOURCE-MANIFEST.json inventory the complete localized source tree.
