# Contributing to onekeyirc

Start with a small change whose purpose and resulting behavior you can explain.
The client is a C11/POSIX program with a shared IRC core and explicit limits on
memory, queued output, files and child processes. A contribution should make
those responsibilities easier to follow.

Read [LICENSE.md](LICENSE.md) and [NOTICE](NOTICE) before reusing or
redistributing source. They contain the project's terms and dependency
notices. This guide describes the engineering workflow.

## Get oriented

- [README.md](README.md): what users do and how they start the client.
- [doc/SOURCE-MAP.txt](doc/SOURCE-MAP.txt): files, entry points and practical
  routes through the code.
- [DESIGN.md](DESIGN.md): object ownership, event-loop contracts and invariants.
- [tests/README.md](tests/README.md): choose checks for the boundary you changed.
- [SECURITY.md](SECURITY.md): trust boundaries and deployment limits.

Read the relevant public header before its implementation. Headers describe
units, borrowed objects and result conventions; implementation comments explain
ordering that is easy to break. Follow a concrete action from the frontend to
the core rather than reading every source file in directory order.

## Build without installing

Install the development dependencies in [INSTALL](INSTALL), then work from the
extracted source directory:

    CC=cc CFLAGS='-O0 -g -Werror' ./configure
    make
    ./onekeyirc --version
    ./onekeyirc --help

Pass build variables through the environment before `./configure`. Edit
`Makefile.in`; `Makefile` and `config.log` belong to the local configuration.
Reconfigure after template changes or when changing platform/toolchain.
When compiler flags or libraries change, run `make clean` after configuring,
then `make`: the build does not treat changed command-line flags as file
dependencies. This also prevents an old test executable from standing in for
the new toolchain. An ordinary source edit only needs its normal rebuild.

The standalone build needs TLS development files but no curses. Full vipirc
also needs wide-character curses development files, including when using its
`-i` frontend. Building onekeyirc exercises shared code but does not establish
that the full curses interface builds or works.

For a local interactive look, use `./onekeyirc --test-mode --no-connect`.
Simulation does not test DNS, certificates, a real account or bouncer. Explicit
background actions can still create local session IPC. For automated checks,
use the private fixtures described in the test guide.

## Keep changes easy to review

Use four spaces in C and Python; Makefile recipes require tabs. Follow nearby
brace and naming style. Prefer one operation per line when ownership, cleanup
or state transitions are involved. Compact tables and simple return guards can
remain compact. Wrap prose and ordinary code where it improves reading;
protocol strings and declarative command tables have different needs.

Explain why a step must precede another, which object owns a resource, or when
a borrowed pointer becomes invalid. Avoid comments that just repeat an
assignment. Update an existing comment when its contract changes. Keep source
section headings searchable, and keep generated files marked as generated.

Use separate changes for presentation cleanup and behavior changes when
possible. A formatting change should preserve tokens and literal strings;
new branches, renamed state, altered bounds or reordered calls need their own
behavioral explanation. Preserve feature-test macros before system headers,
preprocessor continuations, checked arithmetic and error-path cleanup.

Do not assume one universal result convention. For example, configuration
operations and DCC operations have different success/failure conventions.
Check the header and callers before composing helpers. Distinguish bytes,
Unicode scalar positions, terminal cells, milliseconds, displayed numbers,
runtime tokens and saved profile IDs.

## Maintain both source archives

onekeyirc and vipirc share runtime C files and headers. `ONEKEY_ONLY` selects
standalone paths; the historical `VIPIRC_` guards do not identify the running
program. Full-interface modules exist only in the vipirc archive.

Mirror a shared source/header change into the other archive, including useful
comments. With both source trees available, run from either tree:

    python3 scripts/check_shared.py /path/to/other-source-tree

The helper compares the files named by the standalone `SRC` and `HEADERS`
lists. It reports changed or missing files and exits unsuccessfully; it never
copies or rewrites them. It compares shared source bytes, not build results,
runtime behavior, product-specific documentation or tests. Review shared test
and documentation changes separately.
Use distinct extracted vipirc and onekeyirc directories. Copy only the shared
files you changed, then build and regenerate documentation in each tree using
that product's binary. The standalone `MANUAL.txt` is generated; vipirc's
maintained full manual must not replace it. If the companion tree is absent,
report the comparison as unavailable rather than comparing a tree to itself.

## Update the authoritative documentation

Documentation has several sources of truth:

| Subject | Edit here | Regenerated output |
| --- | --- | --- |
| Minimalist commands and topic help | `src/idle_commands.c` | onekeyirc `MANUAL.txt`, `doc/onekeyirc-manual.txt`; vipirc `doc/vipirc-idle-manual.txt` |
| Full vipirc manual | vipirc `MANUAL.txt` | `src/manual.c`, `doc/manual.txt` |
| Launch options and usage | `src/main.c` | Built program's `--help`; checked by the documentation generator |
| Installed manual pages | Canonical manual/help plus `scripts/sync_docs.py` metadata | `doc/onekeyirc.1` or `doc/vipirc.1` |
| Setup and contributor guidance | Root guides, `doc/SOURCE-MAP.txt`, `tests/README.md` | Installed copies |

After editing the maintained sources:

    make docs
    make check-docs

Full vipirc's `make docs` first regenerates the embedded manual, rebuilds the
program, then exports help and the man page. The standalone target builds
before exporting its compiled help. `check-docs` may build prerequisites, but
does not repair stale generated documentation. Never edit `src/manual.c` or
the generated man pages as their primary source.

The full manual must fit `APP_HELP_LINES`, including blank lines. Its capacity
test checks that the first and last lines survive at minimum history. Place
long contributor explanations in these source-tree guides and link to them
from the in-program reference. Ordinary end-user builds use shipped exports
and do not need Python.

For help embedded in C, adjacent quoted pieces form one string literal. Keep
each concatenated literal within 4095 characters, C11's required portable
translation capacity; splitting it across source lines does not reset the
length. Keep a topic concise and put long troubleshooting explanations in
the guides. Verify help edits with the documented `-Werror` build, then
regenerate and check the manuals.

For a command or setting change, update syntax, effects, defaults, examples,
save behavior and failure behavior together. Check both frontends where the
feature is shared. Keep examples free of real credentials and private logs.

## First contribution: clarify a command's help

For example, follow `/summary` from its explanation to the implementation:

    rg -n 'summary' src/idle_commands.c src/commands.c src/summary.c

Read `summary_print()` in `src/summary.h` and its callers. The shared command
catalog in `src/idle_commands.c` supplies the long topic text and examples;
the catalog in `src/commands.c` supplies its short suggestion description.
Change the relevant wording and keep both descriptions consistent. If the
claim concerns output content, inspect `src/summary.c`; if it concerns reader
keys or returning to a draft, inspect the frontend instead.

Mirror the edited shared files, then run the focused existing fixtures in
each configured tree:

    make build/test_commands build/test_idle_commands build/test_summary
    ./build/test_commands
    ./build/test_idle_commands
    ./build/test_summary

These cover command metadata, shared dispatch and summary state/output.
They do not drive a real terminal. For wording alone, inspect the exported
text; for a changed key or reader interaction, select a PTY check from the
test guide. A useful behavioral regression asserts an observable result,
such as retaining the selected conversation, rather than copying a handler's
branches into the test.

onekeyirc's `MANUAL.txt` comes from the compiled topic help. Run `make docs`
followed by `make check-docs`, then inspect `./onekeyirc --manual`. When
mirroring into vipirc, edit its maintained full `MANUAL.txt` separately if
the same explanation appears there, and regenerate that product's exports.
Review the generated diff along with the source change. The generator checks
consistency and required content; it cannot establish that a new prose claim
matches the behavior.

## Trace a preference before changing it

The same concept may have different public and internal names. For example,
`/set output_format compact|full` maps to `Config.output_full`, persisted as
`format=compact|full` in the `[output]` section. Follow the `/set` branch in
`src/idle_commands.c`, defaults/parser/validator/writer in `src/config.c`,
and the `OUTPUT_FORMAT` question and merge in `src/idle_setup.c`. The primary
profile owns this preference; changing the selected server is not enough
to identify which file must be saved.

Trace saving separately: `save()` sends both `/save input` and `/save output`
through `save_one()` for slot zero. It serializes the complete effective
primary profile, including staged connection values from
`app_config_effective()`; these commands do not write isolated input/output
sections. Check save failures and staged values when changing this path.

Start with tests/test_config.c, tests/test_idle_config.c,
tests/test_idle_commands.c and tests/test_idle_setup.c. Their corresponding
`build/test_*` targets build individual fixtures; execute each binary too.
Cover the default, accepted/rejected values, save/reload and Apply/Save
effects relevant to the change. Update the `/set` help and
examples/onekeyirc.ini when their contract changes, then repeat the mirroring
and documentation workflow above. Regenerate the standalone manual and update
vipirc's maintained full reference where the shared explanation also appears.

## Validate and describe the result

Start with the smallest relevant checks in [tests/README.md](tests/README.md),
then broaden coverage when the change crosses another boundary. Broad targets
can require Python, PTYs and private sockets even when named `check-unit`.
Report an unavailable prerequisite explicitly. A substituted UI or transport
fixture is not native terminal or server validation.

A useful patch description contains:

- The concrete problem or documentation gap and who encounters it.
- What changes and why the affected ownership/order remains correct.
- Exact checks run, compiler/platform details, and any unavailable checks.
- Updated help/examples, remaining limitations and any compatibility impact.

For a bug report, include the program version, OS, frontend, relevant terminal
size/locale, minimal reproduction, expected result and actual diagnostic.
Remove credentials and private conversations. Do not substitute an old audit
result for a test of the current change.

When preparing a release, update both version macros in shared `app.h`, current
guide titles and generated exports. Preserve dated audit reports as historical
evidence. Package source and maintained exports without object files, temporary
test executables, private configurations or credentials; record checksums for
the actual delivered files.

For source reuse, modification and redistribution, see `doc/USE-MODIFY-REDISTRIBUTE.txt`.
