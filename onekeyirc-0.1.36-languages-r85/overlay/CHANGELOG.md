Optional localization r85: 2010 keys per locale (+2). Summary report label and interactive terminal requirement. IRC commands and values remain literal. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r84: 2008 keys per locale (+6). Background startup, detach instructions, session-list server counts and nested filesystem diagnostics. Wire protocol and security checks are unchanged. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r83: 2002 keys per locale (+17). Numeric configuration diagnostics, field limits, GPG UID and permission-mode errors. Numeric values, config identifiers and paths remain literal. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r82: 1985 keys per locale (+26). GPG credential and encrypted-file diagnostics. Underlying security policy, filenames, OS error strings and credentials remain unchanged. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r81: 1959 keys per locale (+9). Connection argument validation, bouncer restrictions, invalid input, and mention clearing. Server names, topics, system errors and command tokens remain literal. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r80: 1950 keys per locale (+41). Hotkey descriptions, binding results, watch and ignore summaries, and server commands. Server names, topics, system errors and command tokens remain literal. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r79: 1909 keys per locale (+27). Server summaries, network directory counts, staged auto-join and profile save notices. Server names, topics, system errors and command tokens remain literal. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r78: 1882 keys per locale (+27). Conversation counts, roster headings, automatic command readers and parser errors. Command contents and channel keys remain hidden. IRC names and command tokens remain literal. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r77: 1855 keys per locale (+31). Credential path labels, settings command summaries and connection failures. Path bytes remain recoverable; settings and summary redact credential paths and contents. Literal configuration tokens remain unchanged. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r76: 1824 keys per locale (+32). Authentication, auto-join and presence command summaries. Credential contents remain redacted; only explicit auth inquiries show configured paths. Named fields preserve literal identifiers and messages. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r75: 1792 keys per locale (+6). Connection and command messages, directory filters and UI labels. Named fields preserve IRC identifiers and other literal values. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r74: 1786 keys per locale (+11). File transfer notices and channel directory display. IRC identifiers, wire commands and numeric fields remain literal. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r73: 1775 keys per locale (+13). Command cancellation, watch status, and interface dialogs where applicable; IRC commands and identifiers remain literal. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r72: 1762 keys per locale (+14). Additional interface and connection notices; literal protocol fields, secrets and commands are not translated. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r71: 1748 keys per locale (+22). Localized standard uncommon IRC numeric replies; literal server fields preserved and nonstandard forms fall back to generic display. Apply to clean English r1 before configure/make. Application version unchanged.

Optional localization r70: 1726 keys per locale (+4). Localized ANSI composer byte counts, editor coordinates, stream history omission and setup labels. Apply to clean English r1 before configure/make; verify [lang-rtl-r70]. Application version unchanged.

## Optional localization r69

- Translate five profile and configuration synchronization diagnostic forms across all ten locales. Preserve IDs and opaque loader/OS details; retain original message on overflow.

Optional language patch r68 for onekeyirc-0.1.36 (application version unchanged)
- Translated 27 source-linked formatted configuration and GPG diagnostic templates in 10 locales.
- The display renderer inserts raw OS error text and sanitized paths as literal named fields.
- English fallback and bounded-buffer refusal preserve original diagnostics on failure.
- No changes to configuration validation, credential processing, GPG execution or IRC protocol.

Language-overlay revision r67 (no application version change)
Translated seven numeric error families plus four channel-list completion templates in ten locales. Only local display code changed; protocol and configuration did not.

OPTIONAL LOCALIZATION UPDATE r66, onekeyirc-0.1.36 (application version unchanged).
21 source-linked IRC numeric reply messages translated in ten language catalogs.
WHOIS, channel list, topic metadata, account/mode, MOTD, server notices.
Untrusted server text is inserted only as literal named fields; English fallback preserved.
No changes to socket, protocol, configuration or credential operations.
Install on clean English r1; verify [lang-rtl-r66].

## Optional localization pack r65 (2026-11-23)
- Localize 15 source-linked IRC event notices across all ten supported languages.
- Use named fields and unchanged English fallback for displayed server data.
- Add compiled template and field-preservation test.
- Preserve application version, IRC protocol, configuration and credentials.

### Optional localization pack r64 (2026-11-23)
- 55 fixed configuration diagnostic keys added to each of ten locales.
- Source-linked tests for 72 fixed messages and 29 real parser/validator failures.
- Display-only bounds guard for short credential diagnostics; no config/credential producer changes.
- Literal configuration keys, option values and IRC commands preserved.

## Language pack r63 - 2026-11-23 (application version unchanged)

- Added 30 source-linked configuration diagnostics per locale (1588 keys total).
- Covered verified TLS, SASL/server credential source conflicts, file modes,
  unsafe config replacement, path validation and saved profile errors.
- Localized validation details in six command display paths per client.
- Kept source config writers, parsing, crypto, network and IRC wire unchanged.
- Added compiled ten-language exact-display and English-fallback tests.
- Native language review and complete vipirc ncurses UI testing pending.

## Language pack r62 - 2026-11-23 (application version unchanged)

- Added 31 source-linked credential and GPG warning keys to each of ten languages (1558 total keys per locale).
- Localized fixed GPG decryption and key errors and selected private-file diagnostics at display boundaries.
- Known credential and GPG-file path labels now translate without changing literal paths or system error details.
- Source-side credential/config validation, GPG child handling, filesystem modes, and IRC protocol remain unchanged.
- Added compiled ten-locale diagnostic rendering and safe fallback tests.
- Full vipirc ncurses testing, live OpenBSD GPG/IRC and native-speaker review remain pending.

## Language pack r61 - 2026-11-23 (app version unchanged)

- 14 new catalog keys per language in id/ms/tr/es/pt/fr/de/ru/it/ar.
- Localized 11 fixed safety disconnect reasons, two local queue
  cancellation notices, and explicit user-disconnect messages.
- Only src/app.c and src/client.c presentation call sites changed;
  no IRC wire command, credential source, TLS or retry logic changed.
- Added compiled ten-locale lookup/source-link tests and fallback.
- Full vipirc ncurses/Arabic geometry, real IRC/GPG, and native
  reviewer verification remain outstanding.

## Optional language pack r60 - 2026-11-23 (app version unchanged)

- 51 added catalog keys per language (1513 total); ten languages.
- Localized 37 network diagnostics, 15 connection-state labels, and
  OS/library diagnostic prefixes at the final display boundary.
- Does not touch net.c: retryability, TLS policy, socket operations,
  resolver IPC, IRC commands and wire bytes stay unchanged.
- Added real compiled translation tests using percent signs, UTF-8
  file paths and unknown raw error messages.

## Optional language pack r59 - 2026-11-23 (application version unchanged)

- Localized 18 new profile/session display messages in ten languages;
  connected one already-translated message to its actual source path.
- Bounded partial-save and durability warning labels with literal raw details.
- Preserved config syntax, profile save transactions, IRC bytes, and
  programmatic disconnect/QUIT reasons. Added compiled locale tests.

## Optional language pack r58 - 2026-11-23 (application version unchanged)

- Localized 13 CLI help lines and 10 persistent log-error templates in all
  ten supported languages, with unchanged shell commands and config keys.
- Preserved raw operating-system error details as data, never formatting.
- Source-linked tests check all 23 keys in compiled C catalog lookups.
- Unmodified English usage and strict private log handling.

## Optional language pack r57 - 2026-11-23 (application version unchanged)

- Canonical CLI option explanations now use 23 translated descriptions;
  the 23 literal option strings and arguments remain independent of locale.
- Usage label, program synopsis and local command-line orientation also
  translate; 26 new source-linked catalog keys per locale.
- onekeyirc help correctly names ONEKEYIRC_LANG as its preferred override.
- Updated CLI tests check the exact locale option row and unchanged flags.
- Remaining long-form --help prose, logging and ncurses details are not
  represented as fully translated in this patch.

## Optional language pack r56 - 2026-11-23 (application version unchanged)

- 35 source-linked status templates translated across ten locales, including
  resume/attach errors, authenticated detach/close outcomes, stale job counts,
  numbered success results and older-worker compatibility warnings.
- Named placeholders are validated and substituted as data. Job numbers,
  paths, permissions, literal CLI flags, TLS/IRC and worker wire identifiers
  remain unchanged. Unconfirmed state changes are never reported as success.
- Background worker IPC and full session list remain otherwise unchanged.
- Pending: unlocalized CLI usage, some persistence/logging and app/UI paths;
  session label inside worker reply is intentionally kept protocol-stable.
- Native-speaker validation and live terminal/Crostini/OpenBSD checks remain.

## Optional language pack r55 - 2026-11-23 (base version unchanged)

- 30 additional source-backed messages in ten locales, including protected job
  allocation errors, single-session safety, SIGCHLD startup checks, registry
  availability, status fields and terminal attachment refusal.
- Added dedicated lookup and actual session-list translations in background.c.
  Job numbers, paths, access modes, peer credentials and worker-control protocol
  identifiers remain literal. Translations are never printf formats.
- Updated isolated IPC test fixtures to supply the localization lookup.
- Validation: 30 source paths (all newly translated), exact catalog
  parity, source-linked checks, session display and IPC tests.
- Still untranslated: some resume/close errors, dynamic success notices, CLI
  help, private logging failures and other secondary interface branches.
- Native speaker review and live OpenBSD/Crostini terminal tests remain due.

## Language patch r54 - 2026-11-23 (application 0.1.36 unchanged)

Added 29 source-linked connection/authentication keys in ten locales; bounded named-field diagnostic rendering and preserved literal protocol/config fields.

## 2026-11-23: optional localization r53

- Localize guided FINISH review tables, actions, bouncer status, GPG creation notices.
- Preserve literal configuration data, filenames, account strings and protocol bytes.

## 2026-11-23: optional source-linked localization r52

- 25 new localized display keys across ten languages.
- Bouncer identity help, GPG file paths, state and connection summaries.
- No changes to source credential validation, configuration, files or IRC protocol.

## 2026-11-23: guided setup display localization r51

- 27 fixed guided setup help display paths, including server/identity,
  authentication, history, composer, signal handling, suggestions, watch/ignore.
- Reused previously translated onekeyirc catalog strings; added 16
  new English lookup keys per locale in onekeyirc.
- Source-linked and safe, no changes to configuration values or validation.
- Optional standalone on clean English r1; never layer overlays.

## 2026-11-23: optional setup display errors r50 (app version unchanged)

- 34 new source-linked error/confirmation/help messages per locale.
- No change to validation rules, credentials, network commands or file safety.
- Standalone overlay only on clean English r1; do not layer on r49.

## 2026-11-23: Optional setup-validation localization r49 (app unchanged)

- Localize 28 displayed setup error messages in all ten languages.
- Keep credential validation, key handling, file operations, choices, and network data unchanged.
- Use fixed %s wrappers for translated error messages; never interpret translated text as a format.
- Standalone patch, requiring clean matching English r1 source, not r48.

## 2026-11-23: Optional command-description localization r48 (no version change)

- Translate 51 formerly English command suggestion descriptions across all 10 locales.
- Route minimalist command picker descriptions through display-only translation.
- Keep command names, aliases, wire text, arguments, key bindings and dispatch unchanged.
- Correct the program name in onekeyirc's background command description.
- Standalone patch; install only on clean English source r1, never over r47.

## 2026-11-23: Optional localization overlay r45 (no app version change)

- Added multilingual runtime status, contextual-help and message-review text.
- Preserve variable server data through checked literal named-field insertion.
- New catalog/link and compiled formatter regressions for 10 languages.
- English r1 remains the installation baseline; apply this overlay only to a
  fresh source tree. This is not an application release or protocol change.
- Fluent-speaker and native terminal review remain pending.

## Optional language patch r32 (English application unchanged)

- Translate fixed CLI startup errors and add a concise localized --help introduction in ten languages.
- Preserve command/option spelling and fall back to English for unlisted locales.
- Add source-linked catalog and CLI regression tests.
- Security translations remain drafts pending fluent review.

# Optional language/RTL patch r29

Translated three more SECURITY.md sections into ten languages: local history/logs/privacy, automatic commands/cancellation, and user-defined key bindings. Security drafts now cover seven sections, with English source hashes and tests. No runtime changes beyond the patch revision label.

## Optional localization + RTL patch r27 (onekeyirc 0.1.35, English r1 unchanged)

Added source-linked draft translations in ten languages of four SECURITY.md
sections: trust boundary, TLS/credentials/endpoint identity, configuration
and private files, and child processes/synchronous work. Added status and
security-translation QA. No changes to IRC, editor, TLS, or RTL behavior.
Original English SECURITY.md remains complete and authoritative.

## Optional localization+RTL patch r19 (2026-11-21)

- Source-linked documentation: O.
- Ten new translations per chapter, unchanged program behavior except patch label.
- No changes to original English MANUAL.txt or SECURITY.md.

## Optional localization+RTL patch r18 (2026-11-21)

- Source-linked chapter translations, localized in ten languages.
- Current installation instructions separated from historical README content.
- No executable behavior changes other than the optional patch label.

## Optional localization+RTL patch r17 (2026-11-21)

- Complete source-linked translation of doc/BACKGROUND-COMMAND-en.txt in ten locales.
- English reference and honest coverage inventory included.
- Fixed option spelling in translated prose with automated literal-token QA.
- No IRC/RTL/editor changes beyond --version patch marker.
- English source and application version unchanged.

# Optional language+RTL patch r15 (2026-11-21)

- Replaced unsafe Unicode direction/annotation controls in sanitized text.
- Rejected those controls in raw Arabic display-layout and row APIs.
- Added a ten-language security quick guide (plus English), not a full SECURITY.md translation.
- Switched patch preflight to SHA-256 for every overridden original file and blocked symlinks.
- Preserved the English source and application version; patched binaries identify r15.

# Language + RTL patch r14 (2026-11-21)

- Retained all functional source modifications and translations from r12.
- Identified patched executables as `onekeyirc 0.1.35 [lang-rtl-r14]` with `--version`.
- Preserved the untouched English r1 baseline and original app version.
- Refreshed patch installer/documentation and checksum manifests.

## Language pack r12 (English source r1, application version unchanged)

- Prepare one editor snapshot for both geometry and painting; complete all
  temporary allocation before exposing it. Preserve a coherent logical
  fallback after a transient failure, including footer position indicators.
- Keep the current logical insertion stop and affinity when vertical motion
  is clamped to its existing row, including one-column ambiguous caret cells.
- Paint ncurses base/mark groups as atomic complex cells at the right and
  bottom window edges, retaining the supported mark capacity. Clip wide
  groups together. Reuse each history record's map for row count and paint.
- Reuse scoped paragraph maps through minimalist reader, help, catch-up,
  scrollback, setup and review traversals. Remember a failed map within that
  paragraph and release it before the source buffer is replaced.
- Reject unavailable or unnecessary wide-text conversion before encoding
  and allocating a temporary copy. Retain the ordinary logical path.
- Add allocation-failure, coherent-frame, paragraph-reuse, margin-cell and
  clamped-movement regressions. Keep r11's translation corpus and r10's
  complete minimalist manuals. Update English/Arabic terminal guides.

## Language pack r11 (English source r1, application version unchanged)

- Replace row-only Arabic presentation with bounded paragraph bidi analysis,
  shaped-width wrapping, per-line joining and original logical/byte source maps.
- Share glyph/caret maps between minimalist and full ncurses readers/editors,
  including mouse targets, manual search highlights, settings and setup fields.
- Add visual Left/Right and mapped vertical/mouse navigation, wrap affinity,
  resize handling and select-all. Keep Home/End document endpoints and logical
  cluster deletion. Preserve password masking and logical secret-field motion.
- Keep editable lam-alef positions separate; preserve base/mark clusters and
  exact original drafts, completion text, saved text and IRC wire bytes.
- Permit explicit visual text independently of menu language. Require a UTF-8
  codeset and usable widths. Retain logical fallback for absent/partial loaders,
  disabled conversion, unsupported input and mapping/allocation failures.
- Paint each live WATCH record from one complete map before atomic queueing.
  Clear stale visual affinity after nickname completion. Omit mouse/highlight
  metadata if a transient allocation failure makes its layout differ from paint.
- Separate DL_LIBS from TLS linking and use documented OpenBSD version lookup.
- Add layout/editor/real-ncurses and loopback PTY checks; refresh Arabic and
  English terminal guides. Preserve r10's 844 entries and complete minimalist
  manuals. Native OpenBSD, physical terminals/fonts and native review remain open.

## Language pack r10 (English source r1, application version unchanged)

- Add 87 catalog entries per language: generated program-specific help,
  command-entry instructions and the remaining short command descriptions.
  All ten catalogs now contain 844 entries.
- Export complete minimalist manuals in ten languages plus English, using
  the actual C formatter and auditing every prose lookup and rendered value.
- Make minimalist --manual follow UI language selection without requiring a
  terminal or connection. Export logical UTF-8 even with visual mode enabled.
- Format trusted help substitutions before translation; pass translations as
  data, never printf format strings. Keep arbitrary user/server text literal.
- Enlarge command heading storage so a bounded translated description cannot
  be truncated when a literal /command prefix is added.
- Pin the English documentation generator's language overrides and preserve
  the explicit English manual API. English formatter output is unchanged.
- Add a complete Arabic terminal guide and install all new manual documents.
- Full ncurses documentation/RTL, other contextual help and diagnostics,
  visual input editing, native OpenBSD and native-speaker review remain open.

## Language pack r9 (English source r1, application version unchanged)

- Complete the fixed command/topic help tables in ten languages: 474 prose
  lines translated, three command-only alias mappings retained literally.
- Add 78 missing catalog keys per language; translate the English conjunction
  in the three mixed-prose alias lines. Catalog total: 757 entries.
- Add a consolidated fixed-table help reference for every language, preserving
  literal syntax/examples and explicitly excluding generated additions.
- Restore earlier COMMANDS guides and give the six-command r8 excerpts their
  own CHANNEL-COMMANDS filenames; both generators now coexist.
- Allow exact source help lines up to 1024 UTF-8 bytes while keeping other
  translated labels capped at 256. Test complete runtime help output.
- Extend optional Arabic shaping/reordering to the live WATCH log; queue full
  rendered records atomically, validate UTF-8 strictly, and match FriBidi's ABI.
- Full ncurses RTL, visual input editing, complete manuals and native review
  remain unfinished. IRC command semantics and stored text are unchanged.

# onekeyirc changes

## 0.1.35 - 2026-11-17 (America/Los_Angeles)

- Add an ordered troubleshooting guide for session recovery, saved settings,
  startup, credentials, input/history and logs, with source-checked commands.
- Put first-session steps ahead of optional key customization; distinguish
  shell commands, typed commands, and paste into chat versus Command.
- Correct save input/output scope: both save the primary effective profile,
  including staged primary connection fields, not separate INI sections.
- Refine maintained manuals/help and contributor change examples; explain
  clean rebuilds after compiler/flag changes and test fixture boundaries.
- Regenerate and verify the manual exports and installed man pages.
  Runtime command behavior and existing security controls are unchanged.

## 0.1.34 - 2026-11-17 (America/Los_Angeles)

- Add CONTRIBUTING.md, a source ownership/call-path map, and a focused
  test guide for community contributors.
- Expand module, lifetime, boundary and cleanup comments; split dense
  state/cleanup statements onto readable lines without changing C logic.
- Add a read-only shared-source checker and tests for both source archives.
- Correct generated-manual ownership, test dependencies, workspace summary
  responsibilities, mode-specific Tab completion and frontend-switch limits.
- Regenerate in-program/help exports and manual pages from their sources;
  install contributor guides with their source-relative paths intact.
- Preserve dated security/recovery evidence. This pass changes documentation,
  presentation and maintainer tooling; it adds no runtime security feature.

## 0.1.33 - 2026-11-16 (America/Los_Angeles)

- Retire obsolete minimalist review redraw/send-plan state when typing returns
  to editing, preventing a zero-timeout CPU spin after blocked terminal output.
- Release interrupted LIST requests on disconnect and command-qualified server
  rejections; preserve partial cached entries and allow a new request or clear.
- Stage a complete executable beside its installed destination before rename;
  preserve the old binary if staging fails and support replacing a running one.
- Clarify partial-save diagnostics without changing save transaction semantics.
- Add focused recovery, credential-resource, installation and private multi-server
  load regressions; synchronize help, manuals and release evidence.

## 0.1.32 - 2026-11-16 (America/Los_Angeles)

- Gate ordinary keys, shortcuts and mode changes after bracketed paste
  on a completed visible frame, a later input batch and a quiet interval.
- Refuse paste review credit for undersized or failed/overflowed frames;
  retain the draft and require review after terminal changes.
- Exclude recognized private authentication copies from transcripts
  regardless of current/old sender nickname, including bouncer replay
  and qualified service targets that reach the fallback display path.
- Publish --init-config with atomic no-clobber creation, preserving a
  competing destination and reporting post-publication failures clearly.
- Add focused regressions and update help/manual/security documentation,
  including the separate TLS-library maintenance requirement.

## 0.1.31 - 2026-11-16 (America/Los_Angeles)

- Add an empty-by-default table of up to 64 user-selected key bindings with
  20 named local actions, shared across the client's server profiles.
- Add /bind, /unbind and /bindings, explicit /save input persistence, and
  an editable Key bindings table in full vipirc's App menu.
- Support F1..F12, Ctrl-letter, Alt-letter/digit and named navigation keys;
  normalize terminal aliases and preserve inherited signal assignments.
- Keep paste, password/setup fields, readers and confirmations outside custom
  dispatch; preserve drafts and refuse unsafe minimalist draft retargeting.
- Validate config loads atomically, reject duplicate/unknown/overflow entries,
  and retain application preferences through profile and pending-config flows.
- Expand help, manuals, examples and source-root documentation, with focused
  config, command, input, draft, paste and table regression checks.

## 0.1.30 - 2026-11-16 (America/Los_Angeles)

- Refresh the source-root README, INSTALL, MANUAL, security model and developer
  guide around current behavior; preserve earlier security reports verbatim.
- Expand embedded help with documentation, logging, sessions, TLS and
  troubleshooting topics; synchronize printable manuals and installed man pages.
- Clarify command recall versus chat editing, saved defaults versus resume,
  bouncer identity/credentials, log-based catch-up and inherited terminal signals.
- Add source comments for ownership, trust boundaries, bounded replay,
  terminal recovery, DCC file identity and credential-helper lifetimes.
- Add optional docs/check-docs maintenance targets, and retain source-relative
  documentation links after installation alongside existing flat doc paths.
- Refresh examples and OpenBSD instructions. No runtime protocol, transport,
  session-control, editor or security-policy behavior changes in this release.

## 0.1.29 - 2026-11-16 (UTC)

- Suppress incoming bouncer control copies by their service destination even
  when replay retains an earlier nickname, keeping their contents out of
  display history and persistent logs.
- Honor the final SASL mechanism declaration within and across CAP LS lines.
- Require waitable child ownership before first background-session startup,
  including a signal-policy change between preparation and detach.
- Reserve missing standard descriptors at process entry without changing
  inherited streams or their flags, protecting background directory handles.
- Load custom CA bundles from one pinned nonblocking regular-file descriptor,
  reject empty or over-16-MiB inputs and size changes, preserve certificates,
  AUX trust and CRLs, and never prompt for a bundle password.
- Clear stale bouncer identity when --server switches to a different host.
- Add reproducible regressions, generated CAP/BATCH state checks, protected-page
  input boundaries, and DCC counter edge tests with explicit platform limits.


## 0.1.28 - 2026-11-16 (America/Los_Angeles)

- Add explicit ZNC and soju profiles using existing verified TLS and password
  loaders; assemble account/network/client identity without storing a combined
  password in command history or local echoes.
- Negotiate bounded CAP and pushed playback support; show original UTC times,
  route replayed own private messages correctly, and suppress history actions.
- Preserve bouncer-restored memberships; keep automatic away opt-in per profile.
- Add guarded /znc and /soju controls, setup transaction tests and real local
  TLS/PTY fixtures, with installed bouncer examples and usage documentation.
- Keep endpoint/login/password edits atomic across concurrent setup drafts;
  discard old startup credential commands when changing connection identity.
- Suppress live and replayed copies of outgoing bouncer service commands.
- Preserve compact display and catch-up rendering with original UTC times.
- Inherit fullscreen composer preference when adding or reloading profiles.
- Skip the already-displayed history prefix in the minimalist live output path.

## 0.1.27 - 2026-11-16 (UTC)

- Yield between TLS control records so one read cannot bypass the event-loop
  budget; keep read-ahead disabled and preserve decoded-data wakeups.
- Respect pending TLS write retries before other TLS I/O, including poll and
  pending-data behavior while a write waits for socket space.
- Retain credential-helper process ownership until both result pipes finish;
  cancel and timeout can then stop a same-group descendant that holds a pipe.
- Refuse helper/resolver startup under an auto-reaping SIGCHLD policy, before
  creating child resources, rather than retaining an unsafe numeric identity.
- Restore current presence intent when an unsent scripted AWAY/BACK is
  cancelled or rejected; account for delayed confirmations of earlier commands.
- Retry a failed automatic-away clear after reattach/local activity at a bounded
  interval, while preserving newer manual/script/background intent.
- Add real loopback TLS and native socketpair/SCM_RIGHTS regressions, controlled
  helper tests, and normal/sanitizer make check-deep targets. The full ncurses,
  native named-socket session lifecycle and native OpenBSD limits are explicit.

## 0.1.26 - 2026-11-16 (UTC)

- Add one local Bloodguilt Curse reminder per worker, shared across servers and frontend changes; no automatic IRC message or AWAY modification.
- Preserve inherited terminal signal settings by default; optional [input] force_terminal_signals=true enables signals without assigning interrupt, quit or suspend keys.
- Bound failed automatic-away retries when server-advertised line limits or queue pressure prevent the message from being queued.
- Exclude recognized private credential commands from minimalist command recall even with repeated or separated leading colons.
- Match message-command recall to the accepted IRC chunk plan, so a credential verb beginning a later chunk remains excluded while canonical usage counts survive.
- Bind durable catch-up anchors to the original log file and complete sent record; refuse stale anchors after local rotation or rewrite.
- Add focused presence, credential-history, DCC failure-path and notice regression coverage, with a separate follow-up evidence report.

## 0.1.25 - 2026-11-15 (America/Los_Angeles)

- Prevent remote chat held behind message review from forcing a zero-timeout poll loop.
- Wait only for writable readiness while a DCC receive acknowledgement is blocked.
- Maintain sorted bounded rosters with binary lookup and single-entry insertion/repositioning; re-sort when CASEMAPPING changes.
- Cache catch-up row totals with a bounded sparse record index, preserving the pinned log end and sanitization.
- Add targeted availability, DCC backpressure, hostile-state, roster and catch-up regressions.
- Refresh older terminal and CLI test expectations after baseline reproduction, retaining draft, destination and no-accidental-send assertions.
- Record current review evidence separately from historical security/session validation.

## 0.1.24 - 2026-11-12 (America/Los_Angeles)

- Add authenticated `--detach-session NUMBER` to non-destructively release an attached/stale terminal controller while preserving the worker, IRC connections, drafts and retained history.
- Wait for worker confirmation that terminal release completed before reporting detach success; a later `--resume NUMBER` can attach a new SSH terminal.
- Keep `--close-session NUMBER` destructive and distinct: it terminates the whole job and cannot be resumed.
- Extend the local control protocol with a token-authenticated detach request and reject wrong tokens, passed descriptors and malformed control packets.
- Add real AF_UNIX/PTY regression coverage for attached -> external detach -> continued IRC/PING -> retained detached traffic -> resume, plus focused ASan/UBSan control tests.


## 0.1.23 - 2026-11-12 (America/Los_Angeles)

- Add optional PART reasons and configurable `part_message`; `/close` uses the same default reason when it parts a channel.
- Add optional QUIT reasons and configurable `quit_message`; minimalist quit sends the selected reason best-effort on every live server before shutdown.
- Add `background_away` (default on): successful `/background` and managed terminal/SSH loss queue `AWAY :Detached from terminal` only after terminal detach has succeeded.
- Add local-idle auto-away (default 15 minutes) with configurable `autoaway_minutes` and `autoaway_message`; zero disables the timer.
- Clear only client-generated away state on resume or genuine local input. Manual `/away` status is never overwritten or automatically cleared.
- Add `[presence]` configuration and `/set` controls for departure and away defaults, shared across server profiles.
- Extend PTY, terminal-loss, command, configuration and sanitizer regressions for the new presence state without weakening the existing blocked-terminal detach boundary.

## 0.1.22 - 2026-11-10 (America/Los_Angeles)

- Make Escape from the minimalist `Send this message?` review return to the exact composer/command draft and caret instead of cancelling it.
- Add `/catchup` for channels and queries: start at the most recent locally sent composition, using bounded retained memory when available or an already-enabled private persistent log after rollover.
- Keep catch-up per server/conversation, snapshot the log while reading, and scan the capped disk transcript without importing a long absence into permanent RAM.
- Anchor split IRC sends at the first chunk of the user composition so catch-up never begins halfway through a long message.
- Harden the catch-up reader with the existing private-path policy: exact-0700 parent, owned 0600 single-link regular file, symlink refusal, nonblocking open and 16 MiB ceiling.
- Add PTY coverage for memory fallback, channel/query separation, detached `--resume` catch-up, stable snapshots, review Escape, security-path cases and focused ASan/UBSan runs.

## 0.1.21 - 2026-11-10 (America/Los_Angeles)

- Classify OpenSSL 3.5 `SSL_ERROR_SYSCALL` socket resets as reconnectable when the error queue carries the same transient OS errno as an `ERR_LIB_SYS` entry.
- Keep certificate verification failures, TLS protocol failures, invalid descriptors, permission failures and contradictory error reports held rather than auto-retried.
- Expand transport regression coverage with TLS read reset and TLS bad-descriptor cases; the classification suite now covers 31 scenarios.

## 0.1.20 - 2026-11-10 (America/Los_Angeles)

- Keep review-mode draft text and the `Send this message?` prompt on a temporary alternate screen so edit/re-review cycles no longer pollute terminal scrollback or retained chat history.
- Suppress the redundant queued/paced success notice after an explicitly reviewed send in the minimalist frontend; direct sends still obey `queue_notices`.
- Hold incoming IRC records while review owns the temporary screen, then render them when the live log returns.
- Add PTY coverage proving review/edit/re-review leaves exactly one final self-message, plus vipirc -i and sanitizer validation.

## 0.1.19 - 2026-11-10 (America/Los_Angeles)

- Audit live-log navigation keys: Up scrolls one retained row, Page Up one page, and Home jumps to the oldest retained page; Down/Page Down/End stay or return live.
- Prevent Left/Right/Delete/Backspace from unexpectedly opening a blank composer in the live log.
- Recognize the standard Insert sequence and use Insert/Enter as explicit empty-composer keys.
- Keep editor, command-history, help-reader, setup and scrollback key meanings context-specific and add PTY regression coverage for the live-log policy.

## 0.1.18 - 2026-11-10 (America/Los_Angeles)

- Keep main-chat scrollback anchored when matching IRC traffic arrives while older history is being read; incoming rows no longer consume the next Up/Page Up movement.
- Revalidate Page Up/Page Down through the standalone PTY path, including Page Down returning to the live tail without opening the composer.

## 0.1.17 - 2026-11-10 (America/Los_Angeles)

- Fix the minimalist main chat view so Page Up/Page Down browse retained chat history instead of opening the composer.
- Add an internal scrollback view: repeated Page Up moves older, Page Down moves newer, and End/Escape returns to the live tail.
- Keep ordinary compose-on-key behavior unchanged and add a PTY regression proving Page Up/Page Down never fall through to composition.

## 0.1.16 - 2026-11-08 (America/Los_Angeles)

- Wrap minimalist live chat by terminal cells when the input field is open,
  retaining the newest visible rows without discarding message text.
- Align the minimalist editor renderer with its caret layout after a newline
  immediately follows an exactly full row.
- Add focused display/send regressions; retain existing session controls and
  security hardening. See doc/DISPLAY-CLIPPING.txt for scope and evidence.

## 0.1.15 - 2026-11-09

- Add --close-session NUMBER with owned-peer/token verification and a separate
  shutdown-completion reply; attached jobs can be inspected and closed.
- Detach managed workers after SSH/terminal/controller loss, retain session
  state, and bound foreground hangup waiting and lost-terminal cleanup.
- Keep the standalone frontend fixed to idle while preserving the shared
  configuration schema and session-control behavior.
- Add CLI, protocol, terminal-loss and native-host lifecycle regressions;
  record host-blocked AF_UNIX/full-curses gates separately from fixture passes.

## 0.1.14 - 2026-11-08

- Preserve the sensitivity of complete credential batches across every split
  message, including custom services, and exclude SASET password forms from
  in-memory command history.
- Honor explicit minimalist background requests despite blocked display output;
  bound detach/resume notice writes and discard obsolete stdio presentation.
- Add focused security, sanitizer, hostile-idle and detach-boundary make gates.
- Sanitize cached LIST topics before filtering and rendering, including bidi
  controls; preserve normal Unicode topics.
- Harden optional transcript logging: private non-symlink parent, nonblocking
  FIFO-safe open, exact mode checks, nonblocking lock and 16 MiB per-file cap.
- Exclude incoming authentication-service replies as well as known outgoing
  credential traffic from disk transcripts; align direct settings application.
- Refuse recognized private authentication commands on plaintext before review,
  queueing and transmission; inspect split chunks and keep ordinary chat usable.
- Verify DCC send-source identity between selection and open. vipirc also
  refuses a replaced graphical file-browser directory.
- Disable process core dumps before loading configuration, refuse mismatched
  real/effective IDs, and probe compiler/linker hardening flags.
- Expand hostile-input tests, TLS rejection cases and focused security gates.
  See doc/SECURITY-REVIEW.txt for evidence and environmental test gaps.

## 0.1.13 - 2026-11-08

- Fix blank no-activity resume: a detached job now redraws retained conversation
  history when `onekeyirc --resume` reattaches even if nothing arrived while it
  was in the background.
- Keep canonical bounded scrollback in the shared application state instead of
  discarding it after the minimalist renderer caches the lines.
- Add optional `[output] persistent_logging=true|false` (default false). Enabled
  logging appends server/conversation records to owned regular 0600 `.log`
  sidecars for saved profiles, refuses unsafe modes/symlinks, leaves unsaved
  profiles memory-only, and filters obvious authentication-service output.
- Add `[output] program_messages=true|false` (default true) for hiding
  low-priority local feedback while retaining errors, byte-limit warnings and
  send confirmations. `queue_notices` remains independently configurable.
- Add `[session] single_session=true|false` (default false). When enabled, a
  second background job is refused so bare `onekeyirc --resume` is unambiguous;
  one session can still connect to multiple IRC servers.
- Extend the authenticated resume protocol with an explicit frontend request.
  onekeyirc accepts only its minimalist frontend while sharing the protocol and
  session code with vipirc. Older workers that reject the extension are retried
  with the established attach packet so an upgrade does not strand a live job.
- Add regressions for no-activity resume replay, private logging, program-message
  filtering, single-session collision refusal and requested-frontend handoff.

## 0.1.12 - 2026-11-07

- Keep the selected conversation tail visible while Compose or Command is open.
  Incoming chat repaints the retained live tail without moving the draft caret.
- Add application-wide `composer_mode=line|fullscreen`, available through
  `/set`, `/settings`, `/panel` > 0 Application preferences, and `[input]`.
  Line mode is the default and uses one bottom input row; fullscreen keeps a
  multi-line editor while reserving a smaller live-tail pane.
- Bound line-mode command suggestions to three visible entries so predictions
  cannot displace the live feed. Fullscreen suggestions are bounded separately.
- Make the IRC byte budget pre-emptive: reaching one-message capacity requires
  an explicit Enter acknowledgement before more text can be added. Drafts that
  require multiple IRC messages still require the existing send-time consent.
- Extend config, setup, command and terminal regressions for both presentation
  modes, live-tail repainting, prediction bounds and limit acknowledgement.

## 0.1.11 - 2026-11-03

- Change Escape from the main/log view to background the client using the same
  detach path as `/background`, instead of terminating the IRC process.
- Keep Escape inside composition, review, response readers, context help, and
  setup as a local cancel/back action before a later Escape from the log can
  detach the session.
- Add a terminal regression that verifies tty restoration, continued PING
  handling while detached, `onekeyirc --resume`, and explicit `/quit` cleanup.

## 0.1.10 - 2026-11-03

- Expand `/list` with minimum/maximum users, local channel-name and topic
  filters, channel/users/topic sorting, direction and display limits while
  keeping the IRC wire request portable: only MASK is sent to the server.
- Add a bounded structured network-channel cache shared with vipirc.
- Add `/list show [--sort ...] [--asc|--desc] [--limit N]` so the minimalist
  client can inspect, re-sort and re-limit cached results without a second
  LIST request. `/list clear` removes the cache.

## 0.1.9 - 2026-10-30

- Rename the standalone client from 1keyirc to onekeyirc throughout its
  executable, build, installed files, help, manuals, examples and rc.d daemon.
  The OpenBSD service name remains onekeyirc.
- Prefer the new onekeyirc default configuration directory. If its config
  is absent, reuse an existing 1keyirc default in place without moving
  credentials, server profiles or command-count files. Explicit --config
  paths remain authoritative; unsafe candidates are reported, not bypassed.
- Use the full onekeyirc name in background notices and the new job registry;
  retain the private session wire protocol for compatibility with older jobs.
- Keep shared runtime behavior aligned with vipirc 0.7.10 and vipirc -i.
  Both vipirc interfaces retain their usual vipirc configuration and jobs.
- Add upgrade instructions that stop an older service before replacing its
  control script. Installation does not remove the older executable or config.

## 0.1.8 - 2026-10-29

- Guided GPG setup offers absolute private destinations beside the selected
  config, validates permissions/ownership/collisions before secret entry,
  prepares accepted default directories, and identifies planned/created files.
- Credential errors identify paths and precise causes; no secret contents are
  printed. No-overwrite, symlink, inode and cancellation protections remain.
- Explicit /autoauth status shows complete configured file locations locally.
  /autojoin distinguishes selected server, registration/authentication,
  running versus pending settings, and actual channel membership.
- Full vipirc uses its existing scrollable reader for these inquiries. Shared
  command and setup behavior stays aligned with vipirc -i and 1keyirc.
- Add install-openbsd-service to install the actual rc.d text wrappers and
  executable from their separate source paths, with guards before writes.
- Add shared early-path, full-path, collision, retry, real-GPG/TLS/SASL/autojoin,
  and inquiry regression checks. See doc/VALIDATION.txt for executed results.

## 0.1.7 - 2026-10-29

- Keep shared source and runtime option behavior synchronized with vipirc
  0.7.8, including full-interface unattended startup in the companion.
- Add /mentions clear with the same workspace-wide behavior as full vipirc,
  and correct command-help syntax for file authentication and server options.
- Clarify the OpenBSD service installation: /etc/rc.d/onekeyirc takes the
  shell control script, while /usr/local/bin/1keyirc is the executable.
  Add diagnosis and repair for meta-script/unexpected-operand errors.
- Record all-three interface parity as a maintenance requirement and extend
  the shared background-startup and reconnection regression coverage.

## 0.1.6 - 2026-10-29

- Fix OpenBSD compilation by explicitly including the Internet socket
  headers in every networking translation unit and DCC test that uses them.
- Request OpenBSD extensions before including system headers, preserving
  terminal resize signals, peer credentials and session file locks with
  the strict POSIX/XSI build settings.
- Add --background for direct unattended startup in the minimalist interface
  (1keyirc, or vipirc -i). Prepare the private resume endpoint before starting
  network or credential helpers; retain --sessions and --resume for access.
- Add opt-in --reconnect with bounded exponential backoff for temporary
  transport failures. Preserve explicit disconnect, TLS/authentication error
  holds, staged settings and offline isolation. Never replay queued chat.
- Add OpenBSD VPS boot instructions and a non-root rc.d service example,
  with local startup and reconnection regression coverage.
- Clear saved automatic commands when a command-line server override changes
  the host, keeping commands and any embedded secrets with their own server.
- Preserve unread activity while detached, including a terminal hangup and
  network input arriving during the same poll cycle.

## 0.1.5 - 2026-10-29

- Add /summary as a separate local reader grouped by server, covering
  channel 0, joined/left channels, queries, activity, queues, pending startup
  commands and file transfers. Refresh with r; retain the previous snapshot
  if replacement fails. Preserve targets, drafts and unread counts, and
  return to the snapshot when an opened command is cancelled.
- Add /list [MASK] for network channel discovery with replies in channel 0;
  keep /ls and /cl as local inventories. Server filtering controls masks.
- Add five command-suggestion choices: Commonly used (default), Recent
  history, Most used with this configuration, Alphabetical, and Contextual.
  Contextual captures the actual originating screen and retains other
  matching commands. Expose the preference in /panel and /set.
- Down selects suggestions; Enter inserts before separate submission after
  the input is drawn. Preserve typed arguments and refuse invisible
  suggestions on small terminals. Delete removes recent entries or resets
  personal counts while retaining catalog commands.
- Store bounded canonical-name counts only in each config's private
  .commands file, with automatic reads/writes only for saved configurations.
  Keep unsaved counts in memory until the first successful Save. Keep complete
  history in memory and skip count-file I/O in test mode.
- Make ? explain both the selected item and screen, with an About screen
  link and a return path preserving input/selection. All editable question
  marks remain literal; setup Tab can focus its heading for context help.

## 0.1.4 - 2026-10-29

- Add active IPv4 DCC SEND commands for explicit send, list, accept, reject
  and cancellation without a curses dependency. Relative paths use the
  launch directory; file/privacy, queue, size and concurrency limits remain
  bounded and offline mode prevents transfer I/O.
- Render readable membership, MOTD, WHOIS, invite and failure messages;
  retain useful server detail without internal identity/capability noise.
  Preserve authentication and channel-key privacy.
- Accept common exact command aliases and separate one-based query numbers
  in /query and /pm as well as nicknames. Closing a query renumbers the list.
- Make Escape go back one question in /panel and /quickstart, closing setup
  at its selector or first question. Retain normal terminal job controls.
- Let /panel create encrypted credentials using a full fingerprint of an
  existing local GPG key, hidden password confirmation, bounded asynchronous
  encryption and new private ciphertext files. No plaintext files, shell,
  key download or overwrite of existing destinations is introduced.
- Add global queue_notices and /set queue_notices on|off. Hiding routine
  queued/paced success acknowledgments leaves errors and consent visible.
- Add per-server /autocommands with a strict 16-command whitelist, a default
  15-second post-registration/SASL delay, two-second completion spacing,
  private config storage, verified TLS, staged edits and explicit cancellation.
  Prevent bursts after stalls, secret local echoes and oversized message splits.
- Extend deterministic core/config/client tests for grammar/privacy, timing,
  authentication gates, partial/blocked TLS writes, cancel/wipe, queue/budget
  failure, profile isolation, staged reconnect, query numbers and global notices.
  Strict optimized and ASan/UBSan core/config/client runs passed.

## 0.1.3 - 2026-10-28

- Remove recurring key hints from ordinary screens; retain the complete
  keyboard documentation, command usage, real errors, and send warnings.
- Open /panel at a selector defaulting to 0 Application preferences.
  That path edits History lines, Send mode, and Output format with Save or
  Apply choices and works before configuring a server. Server panels
  1 through 8 omit those fields and keep their existing final choices.
- Use server numbers starting at 1 throughout commands, lists, panels,
  and origin labels. Channel 0 still shows server/MOTD. Saved profile IDs
  and configuration layout are unchanged.

- Return from a successful /users roster to the previously selected chat
  log with Enter, including an empty roster. Escape and q also return;
  the arrow and page keys still browse long rosters. Help retains its
  normal Enter scrolling behavior.
- Retain direct /connect HOST, /join, and /saveconf, with the Unix login
  nickname default and no prerequisite profile or server-number setup.
- Keep the standalone source package independent of curses, with its own
  configure, build, tests, install, documentation, and configuration.

## 0.1.2 - 2026-10-27

- Connect directly with `/connect HOST` or `/server HOST`; a numbered
  profile does not have to be added or selected first.
- Use the valid Unix login name as the default nickname, or `user` as a
  fallback. Explicit nicknames use `/nick` or the connection `-nick` option.
- Keep verified TLS for new connections. Accept explicit `-plain`, `-tls`,
  `-ssl`, and `+PORT` choices without interpreting an ordinary port as a
  request to turn encryption off. Reject extra positional arguments.
- Reuse an existing matching connection profile; give a new destination
  fresh authentication and startup settings.
- Add `/query NICK`, with `/pm` retained as its compatible alias.
- Add `/saveconf` for application preferences, healthy server profiles, and
  the selected default connection. On a later launch `/connect` alone can
  reconnect, including when the remembered server is a secondary profile.
- Preserve explicit autoconnect choices. Save individual files privately
  and atomically; report partial failures without claiming a full rollback.
- Remember secondary defaults by stable profile ID in the existing catalog;
  report invalid defaults before an implicit connection can use another host.
- Lead help and documentation with the direct command workflow, keeping
  `/quickstart` and `/panel` as optional configuration guides.

## 0.1.1 - 2026-10-27

- Distribute an independent source package with its own configure, build,
  test, install, and uninstall targets. Build only the standalone client,
  without checking for or linking a terminal display library.
- Add `/quickstart` for the four connection essentials and `/panel` for
  guided editing of any saved profile. Both review changes before saving,
  applying, or connecting; cancellation leaves unapplied answers unused.
- Keep password answers masked and out of command history. Changing a
  configured destination resets the old destination's authentication,
  joins, auto-connect, and CA settings in the setup draft.
- Default selected-chat output to `[HH:MM:SS] <nickname> text`. An explicit
  `output_format full` preference retains full date and origin context.
- Keep help and long command responses in a separate bounded reader,
  with scrolling, preserved chat context, and Escape to return.
- Correct repeated Escape, delayed terminal-key sequences, and Escape
  from an editor without a selected chat destination. Held cancellation
  keys do not immediately become a separate exit request.
- Standardize list aliases: `/cl` and `/channels`, `/ql` and `/queries`,
  `/sl` and `/servers`.
- Keep help, man page, complete manual, config defaults, and startup
  instructions specific to 1keyirc.
- Expand local setup, command, terminal, signal, paste, background-session,
  and bounded-output regression checks.

## 0.1.0

- Initial standalone client: direct terminal interface, compact in-memory
  history, separate composition, command completion and history, multiple
  saved server profiles, TLS and authentication, channel/query/mention
  views, sending review, private configuration, and background/resume.


Optional security translation r30: protocol/terminal, managed sessions, and bouncer playback sections.


Optional localization r31: complete draft SECURITY.md companions for all ten languages.
Native-speaker security review and native vipirc ncurses testing remain outstanding.

r48 follow-up: eight setup help explanations are now source-linked in all ten languages. The minimalist command picker uses its separate pretranslated native catalog.
