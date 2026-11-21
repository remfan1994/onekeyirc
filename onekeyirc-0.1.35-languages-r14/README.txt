onekeyirc 0.1.35 - OPTIONAL LANGUAGE + ARABIC RTL PATCH r14
=================================================

Read README.XX.txt in your language:
  id Indonesian  ms Malay       tr Turkish  es Spanish  pt Portuguese
  fr French      de German      ru Russian  it Italian  ar Arabic

CURRENT MANUAL COVERAGE
  844 catalog entries per language, unchanged from r11.
  The complete existing minimalist --manual output remains translated in
  all ten languages: 582 distinct prose keys for onekeyirc, 581 for vipirc.
  This package's client uses 582 prose keys; the two clients together
  use 594 distinct prose keys. Catalog totals also include other UI labels.

  Coverage includes all 59 command sections, 19 topic sections, generated
  client-specific configuration/interface/history/session help, command
  descriptions and command-entry explanations. All 128 command examples,
  three command-only alias mappings, 19 topic identifiers and usage syntax
  stay literal. The catalogs and complete manual exports are unchanged in r12.

  Full vipirc ncurses MANUAL.txt, SECURITY.md, some contextual help and
  remaining UI/diagnostic prose are not fully translated. The full ncurses
  visual renderer is integrated; translating its English manual narrative
  is separate work. Wording remains draft pending proficient native review.

SOURCE AND INSTALLATION
  Exact required source: onekeyirc-0.1.35-english-source-r1.tar.gz
  Apply only to a FRESH English r1 tree before configure or make.
  Never apply over r10, r11, r12 or any other pack. An archive with the same
  application version but another source revision is incompatible.

  tar -xzf onekeyirc-0.1.35-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.35-languages-r14.tar.gz
  sh onekeyirc-0.1.35-languages-r14/apply.sh onekeyirc-0.1.35
  cd onekeyirc-0.1.35
  ./configure --prefix=/usr/local
  make

  The application version remains 0.1.35. The original English r1 source
  archive is unchanged. Patched binaries report [lang-rtl-r14] via --version.
  Identify this build as English source r1 plus language+RTL patch
  r14. The pack contains source/documentation, not a precompiled executable.
  Every overwritten existing C/header source and configure is checked against
  the pinned English r1 input before any overlay file is copied.

LOCALIZED MANUAL EXPORTS
  ONEKEYIRC_LANG=ar ./onekeyirc --manual > manual-ar.txt
  ONEKEYIRC_LANG=en ./onekeyirc --manual > manual-en.txt

  Change ar to any language code above. The selected display language is
  honored by onekeyirc --manual and vipirc -i --manual. Plain-text output
  needs no terminal, configuration or IRC connection, and stays logical
  UTF-8 even with optional Arabic visual mode enabled. A language override
  does not set the terminal's UTF-8 locale. The English formatter API remains
  deterministic; the original English documentation stays reproducible.

OFFLINE DOCUMENTATION
  doc/MINIMAL-MANUAL-en.txt and doc/MINIMAL-MANUAL-XX.txt contain the complete
  minimalist manual in English and the ten translated languages.
  doc/MINIMAL-MANUAL-LOCALIZATION.txt explains scope and regeneration.
  doc/ARABIC-TERMINALS-ar.txt contains the Arabic terminal/display guide.
  doc/ARABIC-TERMINALS.txt contains the full English display reference.
  doc/HELP-REFERENCE-XX.txt remains the narrower fixed command/topic reference.
  COMMANDS-XX.txt, CHANNEL-COMMANDS-XX.txt, localized READMEs and earlier topic
  guides remain included. Historical README sections keep their old labels.

OPTIONAL ARABIC DISPLAY AND EDITING
  ONEKEYIRC_LANG=ar ONEKEYIRC_ARABIC_VISUAL=1 ./onekeyirc
  The option selects paragraph-aware Arabic shaping and bidirectional
  presentation in minimalist readers, retained history, live WATCH output,
  notices and editors, and in the full vipirc ncurses text/editor paths.
  The display preference works independently of the selected menu language.

  Requires an available system FriBidi shared library and a UTF-8 locale.
  Use it for terminals that do not already shape/reorder Arabic. Leave the
  option unset or 0 when the terminal already performs bidirectional layout.
  No FriBidi library or development headers are bundled or required at link
  time; missing optional rendering support preserves ordinary logical UTF-8.

  Levels are resolved over each paragraph before physical wrapping. Arabic
  joining resets at each wrapped row, combining marks stay with their base,
  and source maps connect visible cells to the original logical draft.
  Left/Right follow visual caret stops and paragraph flow across wraps;
  Up/Down and mouse placement use the same geometry. Home/End retain the
  editor's existing document-start/document-end behavior. Backspace/Delete
  edit logical clusters, and select-all still selects the complete draft.
  Editor lam-alef stays as separate editable letters with contextual forms.
  Masked secret fields keep their existing logical navigation and masking.

  r12 prepares one shared editor layout and paint buffer for each redraw.
  Text and cursor use that same prepared snapshot; if preparation fails,
  the whole editor redraw follows the ordinary logical path. Up/Down that
  is clamped at the first or last row keeps the current insertion stop,
  including when multiple logical stops share one visible cell.

  Paragraph layouts are reused during minimalist reader operations to reduce
  repeated work while scrolling. Early checks bypass unnecessary conversion
  or unavailable layout APIs before allocating a temporary UTF-8 copy.
  In visual mode, ncurses paints each base and its supported marks together,
  so the bottom-right window cell preserves its marks and neighboring text.
  Each displayed ncurses history record also shares one map between row
  counting and painting, keeping its wrapped height consistent if temporary
  allocation fails. The map is reused only for the current draw.

  Each layout is bounded to 8192 Unicode scalars and 16384 UTF-8 bytes,
  matching the editor limits. Oversized read-only paragraphs, missing APIs
  or failed temporary allocation retain logical display without truncating
  stored text. Wrapping prefers spaces and complete clusters; this is not a
  full Unicode line-break or grapheme-segmentation implementation. Screen
  selection/copy from a shaped terminal can capture presentation order.
  Manual exports, stored drafts, logs and IRC wire text remain logical UTF-8.

  Native OpenBSD execution, physical Crostini/terminal and font appearance,
  and proficient native-speaker review remain pending. Automated layout,
  real ncurses wide-cell and PTY checks do not replace those validations.
  See the terminal guides for limits and the recorded validation scope.

CHANGE BOUNDARY
  This cumulative pack retains the r10 complete minimalist manual work.
  r11 added shared paragraph layout/source maps, visual editor geometry,
  nickname-completion caret invalidation, and full ncurses drawing adapters.
  Its loader/build handling and settings buffer correction are retained.
  r12 refines redraw consistency, corner-cell combining marks, clamped
  vertical cursor movement, paragraph reuse and early logical fallback.
  It adds allocation-failure coverage for the editor snapshot and extends
  the focused rendering/navigation regression checks. These refinements do
  not expand the translation scope or claim native terminal acceptance.
  These are display/editing/help changes. Commands, protocol bytes, saved
  configuration, stored messages/logs and original logical draft text are
  not shaped or reordered. Designated help prose allows up to 1024 UTF-8
  bytes; short descriptions and ordinary labels remain bounded to 256 bytes.

PACK-SHA256SUMS and PACK-MANIFEST.json inventory the package, including modes.
After applying, SHA256SUMS and SOURCE-MANIFEST.json inventory the complete
localized source tree. Manifest/checksum metadata modes are listed explicitly.
