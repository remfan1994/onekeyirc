Test the source with: `make check-l10n-security`. The target works in the curses-free client and vipirc with its normal build toolchain.

# Optional localization security tests r15

From a patched source tree, compile with:

  cc -std=c11 -D_XOPEN_SOURCE=700 -Isrc tests/test_localization_security.c src/protocol.c -ldl -o build/test_localization_security
  ./build/test_localization_security
  python3 tests/test_localization_catalog_security.py

# Test guide

Run commands from the source root. Start with the boundary changed, then
broaden coverage when a dependency or remaining risk requires it. This guide
describes available checks, not results from a particular machine. Historical
reports under doc/ retain their original versions and limitations.

## Localization checks

    python3 tools/generate_localizations.py --check
    python3 tests/test_locale_catalog.py
    python3 tests/test_first_use_translation.py
    make check-l10n

`make check-l10n` additionally compiles and runs the C locale-selection test.
With a built executable, it also runs a private offline pseudo-terminal
smoke test using `--test-mode --no-connect` and Indonesian labels. It does
not contact an IRC server or use real credentials. The full vipirc frontend
needs native ncursesw development headers for the complete build.

## Smallest useful checks

After `./configure`, a direct test target builds only that fixture and its
dependencies; it does not execute the test. For parser/editor work:

    make build/test_protocol build/test_editor
    ./build/test_protocol
    ./build/test_editor

Other focused binaries include build/test_app, build/test_config,
build/test_idle_commands, build/test_idle_catchup and build/test_summary.
Read the matching test and Makefile.in recipe before interpreting a pass.
The help/preference walkthroughs in CONTRIBUTING.md show how to select a
small set from a concrete change. When changing toolchains or flags, clean
the configured tree before rebuilding its test binaries as described there.
For documentation, use `make check-docs`; it checks generated outputs against
their sources and, in vipirc, exercises the embedded manual. See
CONTRIBUTING.md before regenerating docs.

For paired source maintenance, these need Python but no configured build:

    python3 scripts/check_shared.py /path/to/other-source-tree
    python3 tests/test_shared.py

The first compares actual shared source files; the second tests the checker
against private fixtures, including changed/missing files and invalid inputs.
Neither runs the IRC application.

Installation has a similarly independent fixture entry point:

    python3 tests/test_install.py

It uses the real Makefile.in recipes with a private executable and temporary
DESTDIR; no configured client or curses headers are needed. Besides binary
replacement/failure cases, it checks that installed tests/README.md neither
overwrites nor removes the project README.md, preserves other legacy flat
copies, and cleans up installed documentation. It does not launch the IRC
client. With a configured Makefile, `make check-install` runs the same script.

## Select the boundary

- `make check-stability`: interrupted LIST recovery, terminal output/draft
  recovery, and credential-helper resource lifetime.
- `make check-efficiency`: roster maintenance, indexed saved-log catch-up
  and stream state. `make check-stream` isolates the stream fixture.
- `make check-security-pass`: config creation, replay/authentication privacy,
  DCC parser properties and hotkey checks. It is one focused group.
- `make check-security-boundaries`: descriptor ownership, standard descriptor
  reservation, guarded memory edges, DCC counters, session CLI and CA files.
- `make check-bouncers`: profile, command, setup, CAP/SASL and replay-state
  fixtures. `make check-bouncers-terminal` adds actual terminal interaction.
- `make check-net-retry`: real transport retry harness; `make check-deep`
  includes TLS work-budget and resolver-ownership probes.
- `make check-idle-display`, `make check-idle-availability` and
  `make check-hotkeys-terminal`: terminal rendering and input behavior.
- `make check-session-controls`: control parsing/CLI and hangup fixtures.
  `make check-session-lifecycle` adds native worker/terminal handoffs.
- `make check-gpg` and `make check-setup-gpg`: actual local GnuPG credential
  and guided-setup integrations.
- `make check-install`: real install recipe in a temporary DESTDIR with
  executable/document fixtures, including replacement failures, a running
  inode, distinct README paths and uninstall cleanup.
- `make check-idle-load`: a 60-second private TLS/PTY load/recovery exercise.
  In vipirc, --full here selects that binary's minimalist interface; this
  is not a full-curses test or a long production soak.

`make check-unit` is an aggregate, not a C-only or entirely offline suite.
Its added prerequisites include check-deep, check-bouncers,
check-security-boundaries and check-stability; Python and transport-boundary
requirements therefore apply. `make check` and `make check-security` are
broader collections. Read all prerequisite declarations: Makefile.in extends
some targets later in the file. Full-only UI tests exist only in vipirc.

## Host requirements and evidence

Build requirements are in INSTALL: a C11 compiler, POSIX facilities and TLS
development libraries; vipirc also needs wide-character curses. Python
harnesses use local fixtures, not a public IRC account. Network integrations
need permitted loopback sockets. TLS fixtures can require Python's ssl module
and the openssl executable. Terminal cases need usable PTYs, termios/job
control and appropriate terminal/locale support. Full session lifecycle
tests require pathname AF_UNIX bind/listen/connect and descriptor passing.
Socketpair/SCM_RIGHTS tests alone do not establish pathname-listener coverage.

Real GPG tests require gpg, gpgconf and permission to run their temporary
private agents and keyrings. Setup integration also needs its PTY/TLS peers.
Never substitute a personal keyring or live session for a missing fixture.

Some C fixtures include production .c files or replace transport/helper
functions to reach deterministic failure states. test_app.c uses an in-memory
Net; that pass does not exercise TLS. A fake helper tests lifecycle logic,
not real GPG interoperability. Keep fixture and native-integration results
separate, including which frontend and binary actually ran.

For common first changes, these are useful distinctions (filenames below
are relative to tests/):

- test_commands.c checks catalog lookup, suggestions and history functions;
  test_idle_commands.c drives shared command dispatch with temporary profiles.
  Neither proves that a physical key reaches the intended command in a PTY.
- test_summary.c checks the bounded read-only summary and its output through
  a capture callback. Reader scrolling, resizing and draft return belong to
  frontend checks.
- test_config.c and test_idle_config.c use temporary files to exercise real
  parsing and persistence. test_idle_setup.c drives the setup model directly;
  the corresponding Python terminal harnesses check displayed interactions.
- test_idle_catchup.c includes the production reader and compares it against
  an independent temporary-file traversal. It does not drive a PTY or a live
  network stream. Use check-stream for stream-state fixtures and a PTY target
  for display claims.

Add an assertion at the failing boundary, keeping any test doubles explicit.
A test that observes a queue or callback should report that result; a separate
transport/terminal test is needed to claim network delivery or visible output.

Sanitizer groups such as `make check-stability-sanitize` and
`make check-security-sanitize` rebuild with AddressSanitizer and
UndefinedBehaviorSanitizer. They need compiler/runtime support and do not
replace normal builds or native terminal tests. If host restrictions prevent
a check, record that limit and the exact affected command. Do not quietly
remove instrumentation, disable leak checks or replace curses and call it
equivalent coverage.

Record source version/hash, OS, compiler/library versions, commands, flags,
environment overrides and actual outcomes. Preserve failure/skip diagnostics:
the TLS-budget harness can return 77 for missing fixture APIs, and check-gpg
can print a skipped result when tools are absent. A successful aggregate exit
therefore is not proof every optional integration executed. For load reports,
retain duration, options and unavailable measurements; a short run
does not establish an unbounded lifetime, and a clean sanitizer run is not
a security guarantee.

Localization continuation tests (2026-11-19): `tests/test_help_translation_catalog.py`
checks that translated help preserves IRC command names, environment variables,
placeholders and example addresses. `tests/test_l10n_help_terminal.py` exercises
real `/help` rendering in ten languages using an offline pseudo-terminal. It
requires the program executable, never connects to IRC and is not a
native-speaker or bidirectional-terminal acceptance test. The test has a
bounded deadline for a stalled PTY. `tools/generate_locale_guides.py --check`
ensures localized offline quick-start guides match the source catalogs.

Localization continuation: run `python3 tools/audit_help_coverage.py --check`,
`python3 tests/test_help_prose_translation.py`,
`python3 tests/test_diagnostic_translation.py`, and the offline PTY command
checks in `tests/test_l10n_command_terminal.py --binary ./onekeyirc`.
The terminal checks need a UTF-8 locale and are not an Arabic RTL proof.

### Localized practical-help review

`python3 tools/generate_locale_practical_guides.py --check` verifies ten
source-derived practical guides. `python3 tests/test_practical_help_translation.py`
ensures ten complete help sections and protects command/option/file spelling,
including the effect of `--close-session`. The offline pseudo-terminal test
`tests/test_l10n_practical_terminal.py` opens documentation, security and
quick-start help in each language. These tests do not certify natural-language
wording, native OpenBSD builds or terminal bidirectional rendering.

## Arabic r12 failure and edge regressions

    make check-arabic-visual

This includes test_editor_visual_failure.c in normal and compile-disabled
forms. That fixture includes editor.c to inject each allocation failure; do
not link a second editor.c object. A successful snapshot must provide both
geometry and painting without further allocation, preserve logical source,
and release its map and scratch storage after each failure.

The shared editor fixture also covers clamped Up/Down in a one-column field.
test_arabic_visual_core.c checks one map per immutable paragraph traversal,
stable logical fallback after a failed first map, EOF, source/width/mode
changes, and complete editor-frame fallback with secret-field masking.

Full vipirc also provides make check-arabic-ncurses. Its real-cell checks
cover combining marks at the right and bottom edges, clipped wide groups,
snapshot failure/caret/footer agreement, and one record map shared by
history row counting and painting. Existing mixed text, highlights, mouse
targets, setup/settings fields and secret-field checks remain included.

These are automated text/cell and private loopback checks. Actual OpenBSD,
Crostini terminal/font appearance and native-speaker acceptance are separate.
