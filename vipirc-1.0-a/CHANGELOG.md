## 1.0-a - public alpha

- Promote 1.0rc4 to the public alpha; retain its implementation and language catalogs.
- Align all active release metadata, installation instructions, and version checks.
- Invite community translation review and platform testing before a stable 1.0.

## 1.0rc4 - release candidate

- Keep final 1.0 reserved for release verification.
- Synchronize the English minimalist manual and release-version test.

## 1.0rc3 - release candidate

- Keep final 1.0 reserved for release verification.
- Repair the idle configuration serialization test to expect the canonical use_signals key.

## 1.0rc2 - release candidate

- Reserve 1.0 for the final verified release.
- Align application version and documentation with the matching optional candidate pack.
- Repair obsolete force_terminal_signals member references in regression fixtures; runtime configuration continues to accept the old key as an alias.

# Changelog

## 1.0rc1 - staging

- Promote 0.7.37 English r1 to 1.0rc1; optional language pack 1.0rc2 is version-matched.
- Preserve established client behavior; prepare for final localization review.

## 0.7.36 - 2026-11-17 (America/Los_Angeles)

- Add an ordered troubleshooting guide for session recovery, saved settings,
  startup, credentials, input/history and logs, with source-checked commands.
- Put first-session steps ahead of optional key customization; distinguish
  shell commands, typed commands, and paste into chat versus Command.
- Correct save input/output scope: both save the primary effective profile,
  including staged primary connection fields, not separate INI sections.
- Refine maintained manuals/help and contributor change examples; explain
  clean rebuilds after compiler/flag changes and test fixture boundaries.
- Distinguish full/minimalist server command numbers from panel and worker
  numbers, and clarify each frontend's split-send confirmation.
- Regenerate and verify the manual exports and installed man pages.
  Runtime command behavior and existing security controls are unchanged.

## 0.7.35 - 2026-11-17 (America/Los_Angeles)

- Add CONTRIBUTING.md, a source ownership/call-path map, and a focused
  test guide for community contributors.
- Expand module, lifetime, boundary and cleanup comments; split dense
  state/cleanup statements onto readable lines without changing C logic.
- Add a read-only shared-source checker and tests for both source archives.
- Correct generated-manual ownership, test dependencies, workspace summary
  responsibilities, mode-specific Tab completion and frontend-switch limits.
- Regenerate in-program/help exports and manual pages from their sources;
  install contributor guides with their source-relative paths intact.
- Repair recovery-test Makefile prerequisites to use the existing build
  directory target, allowing the documented checks to start.
- Preserve dated security/recovery evidence. This pass changes documentation,
  presentation and maintainer tooling; it adds no runtime security feature.

## 0.7.34 - 2026-11-16 (America/Los_Angeles)

- Retire obsolete minimalist review redraw/send-plan state when typing returns
  to editing, preventing a zero-timeout CPU spin after blocked terminal output.
- Release interrupted LIST requests on disconnect and command-qualified server
  rejections; preserve partial cached entries and allow a new request or clear.
- Stage a complete executable beside its installed destination before rename;
  preserve the old binary if staging fails and support replacing a running one.
- Clarify partial-save diagnostics without changing save transaction semantics.
- Add focused recovery, credential-resource, installation and private multi-server
  load regressions; synchronize help, manuals and release evidence.

## 0.7.33 - 2026-11-16 (America/Los_Angeles)

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

## 0.7.32 - 2026-11-16 (America/Los_Angeles)

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

## 0.7.31 - 2026-11-16 (America/Los_Angeles)

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

## 0.7.30 - 2026-11-16 (UTC)

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

## 0.7.29 - 2026-11-16 (America/Los_Angeles)

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

## 0.7.28 - 2026-11-16 (UTC)

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

## 0.7.27 - 2026-11-16 (UTC)

- Add one local Bloodguilt Curse reminder per worker, shared across servers and frontend changes; no automatic IRC message or AWAY modification.
- Preserve inherited terminal signal settings by default; optional [input] force_terminal_signals=true enables signals without assigning interrupt, quit or suspend keys.
- Bound failed automatic-away retries when server-advertised line limits or queue pressure prevent the message from being queued.
- Exclude recognized private credential commands from minimalist command recall even with repeated or separated leading colons.
- Match message-command recall to the accepted IRC chunk plan, so a credential verb beginning a later chunk remains excluded while canonical usage counts survive.
- Bind durable catch-up anchors to the original log file and complete sent record; refuse stale anchors after local rotation or rewrite.
- Add focused presence, credential-history, DCC failure-path and notice regression coverage, with a separate follow-up evidence report.

## 0.7.26 - 2026-11-15 (America/Los_Angeles)

- Prevent remote chat held behind message review from forcing a zero-timeout poll loop.
- Wait only for writable readiness while a DCC receive acknowledgement is blocked.
- Maintain sorted bounded rosters with binary lookup and single-entry insertion/repositioning; re-sort when CASEMAPPING changes.
- Cache catch-up row totals with a bounded sparse record index, preserving the pinned log end and sanitization.
- Add targeted availability, DCC backpressure, hostile-state, roster and catch-up regressions.
- Refresh older terminal and CLI test expectations after baseline reproduction, retaining draft, destination and no-accidental-send assertions.
- Record current review evidence separately from historical security/session validation.

## 0.7.25 - 2026-11-12 (America/Los_Angeles)

- Add authenticated `--detach-session NUMBER` to non-destructively release an attached/stale terminal controller while preserving the worker, IRC connections, drafts and retained history.
- Wait for worker confirmation that terminal release completed before reporting detach success; a later `--resume NUMBER` can attach a new SSH terminal.
- Keep `--close-session NUMBER` destructive and distinct: it terminates the whole job and cannot be resumed.
- Extend the local control protocol with a token-authenticated detach request and reject wrong tokens, passed descriptors and malformed control packets.
- Add real AF_UNIX/PTY regression coverage for attached -> external detach -> continued IRC/PING -> retained detached traffic -> resume, plus focused ASan/UBSan control tests.


## 0.7.24 - 2026-11-12 (America/Los_Angeles)

- Add optional PART reasons and configurable `part_message`; channel close uses the same default PART reason.
- Add optional QUIT reasons and configurable `quit_message`; quit commands/confirmation use best-effort QUIT across live server sessions.
- Add `background_away` (default on): successful backgrounding and managed terminal/SSH loss queue AWAY only after the terminal has been detached.
- Add local-idle auto-away (default 15 minutes), configurable `autoaway_minutes`/`autoaway_message`, and automatic return only for client-generated away state.
- Preserve manual `/away` across background/resume and ordinary user activity.
- Add `[presence]` configuration plus minimalist `/set` controls and ncurses Settings fields for the new preferences.
- Extend PTY, terminal-loss, command, configuration and sanitizer regressions while retaining the existing blocked-terminal detach security boundary.

## 0.7.23 - 2026-11-10 (America/Los_Angeles)

- In `vipirc -i`, make Escape from `Send this message?` return to the exact Compose/Command draft and caret instead of cancelling it.
- Add minimalist `/catchup` for channels and queries: start at the most recent locally sent composition, using bounded retained memory when available or an already-enabled private persistent log after rollover.
- Keep catch-up per server/conversation, snapshot the log while reading, and avoid importing a long absence into permanent chat RAM.
- Anchor split sends at the first IRC chunk, and harden the read path with the existing 0700/0600/no-symlink/16-MiB private-log policy.
- Add PTY coverage for memory fallback, channel/query separation, detached `--resume` catch-up, stable snapshots, unsafe log paths and focused ASan/UBSan runs.
- Leave the regular ncurses conversation renderer unchanged; its ordinary Page Up/Page Down history remains bounded in memory.

## 0.7.22 - 2026-11-10 (America/Los_Angeles)

- Classify OpenSSL 3.5 `SSL_ERROR_SYSCALL` socket resets as reconnectable when the error queue carries the same transient OS errno as an `ERR_LIB_SYS` entry.
- Keep certificate verification failures, TLS protocol failures, invalid descriptors, permission failures and contradictory error reports held rather than auto-retried.
- Expand transport regression coverage with TLS read reset and TLS bad-descriptor cases; the classification suite now covers 31 scenarios.

## 0.7.21 - 2026-11-10 (America/Los_Angeles)

- Keep vipirc -i review-mode draft text and the `Send this message?` prompt on a temporary alternate screen so edit/re-review cycles no longer pollute terminal scrollback or retained chat history.
- Suppress the redundant queued/paced success notice after an explicitly reviewed send in the minimalist frontend; direct sends still obey `queue_notices`.
- Hold incoming IRC records while review owns the temporary screen, then render them when the live log returns.
- Leave the regular ncurses frontend unchanged; focused PTY coverage exercises the actual vipirc -i main/idle path.

## 0.7.20 - 2026-11-10 (America/Los_Angeles)

- Audit minimalist live-log navigation keys: Up scrolls one retained row, Page Up one page, and Home jumps to the oldest retained page; Down/Page Down/End stay or return live.
- Prevent Left/Right/Delete/Backspace from unexpectedly opening a blank composer in vipirc -i.
- Recognize the standard Insert sequence and use Insert/Enter as explicit empty-composer keys in the minimalist frontend.
- Keep the full ncurses input model unchanged: its persistent input field retains normal editor navigation and Page Up/Page Down history scrolling.

## 0.7.19 - 2026-11-10 (America/Los_Angeles)

- Keep minimalist scrollback anchored when matching IRC traffic arrives while older history is being read; incoming rows no longer consume the next Up/Page Up movement.
- Revalidate Page Up/Page Down through the actual vipirc -i main/idle path and keep the full ncurses Page Up/Page Down handler unchanged.
- Synchronize the embedded full manual release banner with the package version.

## 0.7.18 - 2026-11-10 (America/Los_Angeles)

- Fix the minimalist main chat view so Page Up/Page Down browse retained chat history instead of opening the composer.
- Add an internal scrollback view: repeated Page Up moves older, Page Down moves newer, and End/Escape returns to the live tail.
- Keep ordinary compose-on-key behavior unchanged and add a PTY regression proving Page Up/Page Down never fall through to composition.

## 0.7.17 - 2026-11-08 (America/Los_Angeles)

- Wrap minimalist live chat by terminal cells when the input field is open,
  retaining the newest visible rows without discarding message text.
- Align the minimalist editor renderer with its caret layout after a newline
  immediately follows an exactly full row.
- Add focused display/send regressions; retain existing session controls and
  security hardening. See doc/DISPLAY-CLIPPING.txt for scope and evidence.

## 0.7.16 - 2026-11-09

- Add --close-session NUMBER with owned-peer/token verification and a separate
  shutdown-completion reply; attached jobs can be inspected and closed.
- Detach managed workers after SSH/terminal/controller loss, retain session
  state, and bound foreground hangup waiting and lost-terminal cleanup.
- Save the default interface as [session] default_interface=idle|ncurses,
  with Settings/panel and /set controls. -i and -N/--ncurses override one launch;
  -n remains the nickname option. Bare --resume retains the worker frontend.
- Add CLI, protocol, terminal-loss and native-host lifecycle regressions;
  record host-blocked AF_UNIX/full-curses gates separately from fixture passes.

## 0.7.15 - 2026-11-08

- Preserve the sensitivity of complete credential batches across every split
  message, including custom services, and exclude SASET password forms from
  in-memory command history.
- Honor explicit minimalist background requests despite blocked display output;
  bound detach/resume notice writes and discard obsolete stdio presentation.
- Add focused security, sanitizer, hostile-idle and detach-boundary make gates.
- Avoid a zero-width warning-dialog loop during a very narrow terminal resize.
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

## 0.7.14 - 2026-11-08

- Fix minimalist resume repainting: `vipirc -i --resume` now redraws retained
  conversation history even when no IRC activity arrived while detached.
- Make bounded scrollback session-owned instead of minimalist-frontend-owned so
  a background vipirc job can be reattached with either frontend. `vipirc
  --resume` requests ncurses; `vipirc -i --resume` requests the minimalist
  interface. IRC connections, channels, queries, unread state, DCC state and
  retained history remain in the same live process.
- Extend the authenticated local resume handshake with an explicit requested
  frontend while keeping the established v1 session-query packet layout stable.
  If an older live worker rejects the extension, retry the established attach
  packet so upgrades do not strand it; that legacy attachment keeps its existing
  frontend.
- Add optional `[output] persistent_logging=true|false` (default false). Enabled
  logging appends server/conversation records to owned regular 0600 `.log`
  sidecars for saved profiles, refuses unsafe modes/symlinks, leaves unsaved
  profiles memory-only, and filters obvious authentication-service output.
- Add `[output] program_messages=true|false` (default true) to hide low-priority
  local feedback such as draft cancellation and input-cleared notices without
  hiding errors, byte-limit warnings or send confirmations. Existing
  `queue_notices` remains a separate control for queued/paced acknowledgements.
- Add `[session] single_session=true|false` (default false). When enabled, only
  one background job may exist in the vipirc namespace, making bare `--resume`
  deterministic while preserving multiple IRC-server connections in that job.
- Preserve a shared DCC manager and canonical App state across frontend changes.
  Add regressions for no-activity resume replay, private logging, program-message
  filtering, single-session collision refusal and requested-frontend handoff.

## 0.7.13 - 2026-11-07

- Update the shared minimalist interface used by `vipirc -i` and onekeyirc so
  Compose and Command retain a live view of the selected conversation tail.
- Add application-wide `composer_mode=line|fullscreen` through `[input]`,
  `/set`, `/settings`, and `/panel` > 0. Line is the default: one input row at
  the bottom with command predictions capped so the live feed remains visible.
  Fullscreen keeps a multi-line editor while reserving a smaller live-tail pane.
- Repaint incoming IRC activity during editing without moving the draft caret,
  changing command selection, or blocking network service.
- Make the computed IRC byte budget pre-emptive: reaching one-message capacity
  requires explicit Enter acknowledgement before more text can be added. The
  existing send-time confirmation remains mandatory for split transmission.
- Extend shared config, setup, command and terminal regressions and keep the
  minimalist production/test sources synchronized with onekeyirc 0.1.12.

## 0.7.12 - 2026-11-03

- Change only the minimalist `vipirc -i` Escape behavior: a standalone Escape
  from the main/log view now takes the same safe detach/background path as
  `/background` instead of terminating the IRC client.
- Preserve nested-screen behavior: Escape in composition, review, command
  responses, readers, context help, and setup still cancels or returns one
  level rather than detaching immediately. Regular ncurses vipirc keeps its
  existing Escape navigation and quit-confirmation behavior.
- Add a terminal regression that verifies Escape restores the tty, leaves the
  worker answering IRC PINGs, permits `vipirc --resume`, and removes the job
  only after explicit `/quit`.

## 0.7.11 - 2026-11-03

- Expand `/list` with portable local filtering by minimum/maximum users,
  channel-name substring and topic substring, plus channel/users/topic sorting,
  ascending/descending order and display limits. Only the optional IRC mask is
  sent to the server, so the added filters do not depend on extended-LIST
  server syntax.
- Add a bounded structured channel-directory cache (8192 entries maximum).
  Failed refresh queueing preserves the previous directory; incoming LIST
  numerics update the cache while the request is active.
- Add a full-screen ncurses network channel browser with Channel, Users and
  Topic columns, keyboard/vim navigation, mouse header sorting, reversible
  column sorting, paging, join, refresh and Command access.
- Keep minimalist parity: `vipirc -i` supports the same request filters and
  `/list show [--sort ...] [--asc|--desc] [--limit N]` for cached table output
  without another network request. `/list clear` removes the cache.

## 0.7.10 - 2026-10-30

- Rename the companion standalone client from 1keyirc to onekeyirc 0.1.9,
  with synchronized shared runtime code, help and compatibility handling.
- Keep both vipirc interfaces on the existing vipirc configuration and job
  registry, including compatibility with their running background sessions.
- Document the standalone rename, preserved legacy configuration paths and
  OpenBSD service upgrade order. See doc/ONEKEYIRC-RENAME.txt.

## 0.7.9 - 2026-10-29

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

## 0.7.8 - 2026-10-29

- Extend --background to the full interface. Start without a terminal or
  TERM, and initialize the full display when a terminal resumes the job.
  Keep vipirc -i and standalone 1keyirc behavior aligned.
- Apply runtime connection overrides to the remembered default server when
  --server is omitted, consistently across all three interfaces.
- Preserve unread state when terminal detach and incoming network data
  share a poll cycle. Report handled termination signals consistently.
- Add the full-interface OpenBSD vipirc service alongside vipirc_idle.
  Clarify that rc.d takes the shell service script, and document repair of
  the meta-script/unexpected-operand installation error.
- Make /mentions clear available in the minimalist interface as well as
  full vipirc, clearing captured mentions across live servers. Keep command
  help syntax consistent with accepted authentication and connection options.
- Require separate full, minimalist and standalone validation and keep
  shared production files synchronized between the source packages.

## 0.7.7 - 2026-10-29

- Fix OpenBSD compilation by explicitly including the Internet socket
  headers in every networking translation unit and DCC test that uses them.
- Request OpenBSD extensions before including system headers, preserving
  terminal resize signals, peer credentials and session file locks with
  the strict POSIX/XSI build settings.
- Keep manual-search character-width comparisons valid for OpenBSD's signed
  wint_t as well as the unsigned type used by the tested Linux build.
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

## 0.7.6 - 2026-10-29

- Add /summary as a separate local snapshot reader across all servers,
  channel 0, joined/left channels, queries, unread/mentions, outgoing work,
  startup commands and transfers. Refresh by r or the full interface's
  keyboard/mouse Refresh action; preserve the previous snapshot on failure.
  Reading preserves conversation state and drafts; command cancellation
  returns to the snapshot.
- Add /list [MASK] for a registered server's network channel list, with
  replies in channel 0. Keep /ls and /cl as local view lists.
- Add Commonly used (default), Recent history, Most used with this
  configuration, Alphabetical, and Contextual suggestion modes. Contextual
  ranking captures the screen that opened Command, including client screens
  over a selected channel. Other matching commands remain discoverable.
- Down selects suggestions; Enter inserts before a separate displayed-input
  submission. Keep existing arguments, and prevent queued Enter from turning
  insertion into execution. Delete removes Recent entries or resets personal
  usage counts without deleting catalog commands. Refuse invisible selection
  when the terminal cannot display suggestion rows.
- Persist bounded canonical-name usage counts only in each config's private
  .commands file. Read/write count files automatically only for saved
  configurations; keep unsaved counts in memory until the first successful
  Save. Keep full command history ephemeral and skip count-file I/O in test mode.
- Make ? explain the selected item and its containing screen with a parent
  help link and a return path preserving origin state. Keep ? literal in
  editors. Native setup can focus its question heading with Tab for help.
- Keep the Settings suggestion-mode selector usable at 9 rows by 20 columns.

## 0.7.5 - 2026-10-29

- Preserve terminal foreground/background defaults throughout the full-screen
  interface; use bold printable focus outlines without initializing a palette
  or leaving screen-wide reverse video active. Explain that graphical
  `/help TEXT` searches the complete manual.
- Add a bounded directory browser to DCC Source file, with the action row
  below its heading. Choosing a file fills the form; Offer remains explicit.
  Share `/dcc NICK PATH`, list, accept, reject, cancel, address and port options
  with the minimalist interface. Relative paths use the launch directory.
- Present readable IRC membership, MOTD, WHOIS, invite and error messages;
  retain useful server detail and consume internal identity/capability replies.
  Keep authentication replies and channel keys out of display history.
- Accept common exact command aliases with the same validation and privacy
  rules. Number `/ql` separately from channels and accept names or numbers
  in `/query` and `/pm`; renumber after a query closes.
- Make Escape go back through guided setup. Share `/panel` and `/quickstart`
  across both interfaces; retain the graphical Settings forms. Add explicit
  GPG credential creation using an existing local full key fingerprint,
  hidden password confirmation and non-overwriting private ciphertext files.
- Add global `[output] queue_notices` and `/set queue_notices on|off` to hide
  routine queued/paced acknowledgments while preserving errors and consent.
- Add per-server `/autocommands`: up to 16 validated commands, default delay
  15 seconds after registration/required SASL and two seconds between completed
  writes. Require verified TLS for enabled lists and private config files for
  every nonempty list. Preserve live snapshots, cancel unsent sequences,
  reject oversized automatic messages, suppress their local echoes and avoid
  catch-up bursts or focus changes from automatic joins.
- Extend deterministic tests for readable/malformed server text, bounded
  command grammar, literal/private config data, delays, authentication gates,
  blocked/partial TLS writes, cancellation and wiping, queue/budget failures,
  independent profiles, staged reconnects, query numbers and notification scope.
  Strict optimized core/config/client suites and ASan/UBSan runs passed.

## 0.7.4 - 2026-10-28

- Remove recurring key hints from native screens while retaining the full
  keyboard reference, command usage, real errors, and send warnings.
- Split native /panel into default choice 0 Application preferences and
  server panels 1 through 8. Global history, send mode, and output format
  can be saved/applied without a server or connection. Server panels retain
  their existing save/apply/connect choices and omit the global fields.
- Use one-based server numbers consistently in the native interface while
  retaining zero-based curses server numbers, channel 0 as server/MOTD,
  and the existing saved identities and configuration layout.

- In the minimalist interface, return from a successful /users roster to
  the previously selected log with Enter; preserve help Enter scrolling
  and roster navigation with arrow/page keys.

- Use Delete as the existing Close action in server/channel/query lists,
  preserving confirmations and protected entries; editor Delete still edits.
  Escape on vipirc asks Really quit? [Y/N], with fresh y/Y required. The
  Back (b) control returns to chat and terminal signal handling is preserved.

- Open Command with / from the vipirc screen and return there when cancelled.
  Use all available suggestion rows, show their range/total, and support
  Page Up/Page Down and mouse-wheel browsing without changing command history.
- Share direct /connect, server/profile management, startup commands,
  /query, /saveconf, and preference parsing with the command-oriented interface.
  Add graphical /ls, /panel, /quickstart, and topic/command help destinations.
  Preserve local command results in the server view and defer session removal
  until the active command handler returns.
- Reopen a closed query from a retained mention without sending a message.
  Retain its nickname separately from display text in lazy, bounded metadata
  with ring eviction, resizing, and teardown cleanup. Add workspace-wide
  Clear mentions and /mentions clear; keep source conversations and drafts.
- Explain /cancel as stopping unsent chat waiting on the selected server;
  it cannot retract transmitted text.

- Make selected item text and brackets bright white, without dim hint
  attributes; monochrome terminals retain bold text and visible brackets.
- Make Escape from every query chat open Queries with that query selected.
  Returning from the list preserves the query, draft, and caret. F1 remains
  the direct route to the vipirc application screen.
- Make Open on a server show its messages and MOTD (channel 0) directly.
  Mouse row clicks follow the same route. Remove the redundant Channels
  action; F3 or /channels opens the selected server's channel list.
- Place server/channel/query actions directly beneath their heading and
  above the list. Horizontal arrows select an action; vertical arrows
  select an item, with both highlighted together.
- Repeat the welcome warning below the vipirc application-screen title,
  before Server and Nick. Replace the repeated website/designer line with
  a blank separator.
- Add About immediately before the license entry. It contains version,
  website, designer, license, and on-demand local process and conversation
  information. Memory is labeled as peak resident use, CPU as accumulated
  user/system time, and unavailable data remains explicit. No background
  resource sampler is added.
- Make the manual return to the screen that opened it, with Return to
  vipirc for the application-screen route. Remove the live connection
  diagnostics above its documentation. Add a visible Search control and
  / entry into bounded literal search, with n/N and Previous/Next for
  wrapped, ASCII case-insensitive matches. Search stays local and outside
  command history; pasted text cannot activate it or submit a search.
- Retain the direct-connection and saved-default fixes in 0.7.3 while adding
  the roster-return improvement to the minimalist interface.

## 0.7.3 - 2026-10-27

- Make native `/connect HOST` the ordinary first-use route. `/server HOST`
  connects in the same way; creating and selecting a numbered profile first
  is no longer required. Matching sessions reuse their own profile, while
  new destinations receive fresh credentials and startup settings.
- Default missing native nicknames to a valid Unix login name, with `user`
  as the fallback, including command-line connections without `--nick`.
- Accept explicit TLS/plain transport options, `+PORT` for TLS, and
  `-nick`/`--nick` for a chosen nickname. Bare port numbers do not disable
  TLS. Extra positional arguments are rejected without treating them as a
  nickname or forwarding them to a server.
- Add `/query NICK` as the familiar private-conversation command; `/pm`
  remains compatible.
- Add `/saveconf` to save application preferences and healthy server
  profiles and remember the selected default connection. A subsequent
  launch can use `/connect` alone, including for a saved secondary server.
  Autoconnect toggles remain unchanged. Individual saves are private and
  atomic; partial failures are reported and successful writes remain saved.
- Store a remembered secondary default by stable profile ID in the existing
  server catalog. Invalid or unreadable defaults are reported; bare
  `/connect` waits for explicit selection instead of choosing another host.
  Saving a primary-only configuration creates no unnecessary catalog files.
- Put direct connection commands first in native help, manuals, and startup
  guides. `/quickstart` and `/panel` remain optional guided alternatives.
- Keep the two independently buildable source packages, product-specific
  documentation, and the curses-free standalone build.

## 0.7.2 - 2026-10-27

- Added native `/quickstart` for Server/TLS/Port/Nickname, plus `/panel` for
  guided profile configuration. Answers remain private until an explicit
  final save/apply/connect choice; passwords are masked and omitted from
  command history, and Escape discards unapplied edits.
- Help and manuals use the program's own name, paths, defaults, and invocation. The native vipirc manual has a separate installed text file.
- Replaced Alt+number category navigation with F1 through F6. A global,
  default-on Shortcut labels setting controls the visible F-key prefixes;
  hidden labels do not disable shortcuts or change category identities.
- Standardized list shortcuts in both interfaces: `/cl` or `/channels`,
  `/ql` or `/queries`, and `/sl` or `/servers`. Help, completion, and dispatch
  use the same names; each short/full pair shares one operation.
- Completing a no-argument screen command now accepts the completion's
  trailing space. Extra arguments and composed newlines are not discarded.
- Minimalist selected-chat output now defaults to time, nickname, and text.
  `/set output_format full` restores date/server/target metadata;
  `/set output_format compact` returns to concise output. `/save output`
  preserves the primary profile's `[output] format` choice.
- Help and multiline command output use a bounded separate reader with
  ASCII headings and dividers, instead of entering conversation history.
  Chat collection and network polling continue while reading. Escape
  returns to chat, and cancelling a command opened from the reader restores it.
- Corrected repeated/expired Escape decoding in the minimalist frontend,
  including Command and Compose without a selected channel or query.
  Terminal signal controls and fragmented SSH key sequences retain their roles.
- Added application-wide Settings > Interface > Hints. The saved
  `[interface] hints=false` option hides optional key and field guidance;
  errors, validation results, controls, and safety confirmations stay visible.
  Curses titles, context, content, actions, and optional tips use consistent
  placement and emphasis.
- Includes the 0.7.1 list controls, parenthesized counts, license placement,
  Mentions-first ordering, and queued-mouse target protections.

## 0.7.1 - 2026-10-27

- Left/Right and h/l in the server, channel, and query lists now select the
  footer action for the selected item. Up/Down selects an item while keeping
  that action, and Enter executes it. Both selections stay highlighted without
  a separate focus step. Horizontal arrows no longer open adjacent categories
  or reveal empty categories.
- List selection changes invalidate queued mouse coordinates until repaint,
  including changes within a batch of mouse events. A click cannot execute
  a compact footer action that has not yet been displayed.
- Header counts use parentheses, keeping them separate from the fixed
  Alt+number shortcuts: for example, `2 Servers (2)` and `3 Channels (12)`.
- Moved the vipirc panel's license entry below Background, separated by a
  blank line, and spelled it out as `Insha-Allah license agreement`.
- The combined Mentions/unread screen now presents Mentions first, followed
  by Unread conversations, with separate headings and server groups.
- Updated the built-in manual, man page, and interface documentation for
  the revised controls and presentation.

## 0.7.0 - 2026-10-27

- Added the command-oriented `vipirc -i` interface using the application's
  existing configuration and saved server profiles.
- The minimalist log uses bounded memory. Slash opens Command; ordinary
  input opens a separate composer while networking continues. Direct send
  is the default, with an optional two-Enter review workflow. A message that
  would split always needs confirmation. Terminal interrupt and suspension
  retain their normal roles, Ctrl-D is inert, and Escape from the log exits.
- Added one command catalog for minimal help, completion and usage, with
  `/help ls`, `/help commands`, per-command examples, saved-server
  autoconnect/autojoin/autoauth controls and a complete standalone man page.
  Command history, transcript retention and queued command output are bounded.
- The ncurses header uses Alt+1 through Alt+6 and mouse selection; arrows
  operate the current screen or editor. The fixed order is vipirc, Servers,
  Channels, Queries, Mentions/unread, File transfers. Empty entries remain
  hidden. Setting a server, joining a channel and opening a query reveal the
  corresponding next levels without changing category numbers.
- Alt+1 exposes Server and Nick links into that server's configuration.
  Settings returns to the vipirc screen on Escape. IALA opens the complete
  included agreement. Authentication choices have a distinct bounded popup.
  The hardware caret remains visible while editing, including TERM=screen.
- Alt+5 groups unread conversations and full timestamped mention lines under
  separate headings and server groups. Stable source identifiers prevent
  stale clicks from opening a renamed, closed or replacement conversation.
- Explicit Connect applies validated staged connection settings while
  disconnected, without requiring a program restart. Active connections are
  not retargeted by Apply or Save. Diagnostics name invalid fields and keep
  connection encryption separate from password storage. NUL input remains
  rejected, with a TLS-mismatch hint when the response resembles a TLS record.
- Background sessions have stable local job numbers. `--sessions` lists
  jobs, bare `--resume` resumes a sole verified available detached job, and
  `--resume NUMBER` selects one explicitly. Owner-private registries, bounded
  handshakes, nonblocking file validation and incarnation tokens protect job
  selection. Legacy absolute socket paths remain accepted.
- Fixed a terminal-hangup race in the foreground resume process: a failed
  control write can no longer discard a queued detach or exit acknowledgment.
  Reply draining is bounded, preserves partial framing, and retains failure
  status for missing, incomplete or malformed replies.
- Added real-terminal regressions for the revised hierarchy and input modes,
  stale send-plan connection checks, command-output backpressure, numbered
  background jobs and failure-path cleanup. Release validation records actual
  test commands, platform, measured resource use and testing limitations.

## 0.6.0 - 2026-10-27

- The header now omits empty categories while keeping a fixed numbered order:
  vipirc, Servers, Channels, Queries, File transfers, Mentions, and Unread
  views. Retained history keeps its category available. A fresh workspace
  without a configured server shows only vipirc. Visible categories use
  separators above a full-width white divider.
- Escape from chat, or Up from the first visual input row, focuses the
  header. Left/Right or h/l opens available categories immediately and wraps;
  Down/Enter enters the shown screen. Alt+1 through Alt+7 opens an available
  numbered category. Escape restores chat and its draft/caret. Chat letters
  remain literal text; Tab/Shift-Tab now completes and cycles nicknames.
- Nickname completion uses the current server's case rules and local roster
  or query peers. It preserves following text, checks byte/scalar bounds
  before replacement, and participates in the normal early message warning.
  Completion performs no network request and cannot submit a message.
- Settings now has an app-wide home listing Interface, every server profile,
  and New server. Any profile's connection, autoconnect, autojoin, account
  authentication, history, watch terms, and ignores can be edited without
  switching chat. Each saved profile retains its independent startup choice.
- Mouse and User list preferences are app-wide. Interface Save writes the
  primary configuration before updating current and future sessions. Profile
  saves remain isolated; connection edits still take effect at next startup.
  Tab/Shift-Tab stages valid field edits and moves among Settings fields.
  Short Settings and transfer choices now place focus brackets beside text.
- Clear and Send appear beneath the fixed input only when mouse reporting
  is available and enabled. Enter and /clearinput remain keyboard routes.
  Send-queue notices use the footer without taking conversation space.
- Clicking a displayed nickname opens Query, Whois, Ignore/Unignore, and
  Back choices. Clicking a #channel joins it or switches to its joined view.
  Rendered targets retain bounded copies, so roster changes and history
  eviction cannot redirect a pending click. Retained channel punctuation
  is matched exactly, including names sharing a shorter channel prefix.
- /background and the vipirc panel's Background choice release the terminal
  while IRC sessions, drafts, and accepted transfers continue in memory.
  vipirc --resume SOCKET attaches another local or SSH terminal, adapting
  its geometry, TERM, and locale. Ordinary foreground use adds no helper or
  resume endpoint; detach creates a private, same-account local endpoint.
- Background handshakes, descriptor ownership, terminal validation, and
  signal forwarding are bounded. Resumed terminal loss detaches the worker;
  an explicit quit or interrupt ends the session. Pending resolver or
  credential helpers defer first detach. Offline background mode uses local
  IPC only and preserves the simulator's IRC/DCC and credential restrictions.
- Fixed stale navigation origins after accepting a cross-server unread
  conversation, preserving the selected view when returning from a panel.
  Compact terminals retain usable manual content below the new divider.
- Added completion, background lifecycle, stale nickname/history targets,
  exact channel-click, universal Settings, and multi-profile startup
  regressions. Updated the built-in manual, man page, configuration example,
  and interface/security documentation. Release validation records the
  executed platforms, commands, durations, and limits separately.

## 0.5.0 - 2026-10-27

- Added a fixed global header with vipirc, Servers, Channels, Queries, File
  transfers, Mentions, and Unread views. Tab advances, Shift-Tab reverses,
  and Enter opens a destination across main and secondary screens. Positive
  counts append to categories; the current conversation has its own row.
- Chat starts ready to type. Ordinary keys dismiss temporary header selection
  and edit the draft; main hjkl are letters. Escape returns to chat editing.
  White outlines indicate selected controls and the complete fixed input
  border indicates editing focus. Clear/Send sit below the field;
  /clearinput is the keyboard action for clearing the draft.
- The vipirc panel shows live connection details and Settings, Manual, and
  Command choices. The complete disconnected-capable manual remains directly
  available through /help, /manual, /view help, and --manual.
- Server/channel/query rows support direct left/right traversal. Space
  switches row/footer focus without changing the selected row; Down past
  the last row also reaches actions. Empty Queries provides `New query...`.
  List actions use the rendered conversation snapshot before repaint.
- Fixed a resize notification arriving immediately before the event loop's
  wait. Curses input is checked nonblockingly after every bounded wait, so
  an idle screen resizes without needing another key; input batches remain
  bounded and idle operation still sleeps.
- Added a separate Command prompt preserving the chat draft and caret.
  Right at the end of a command-name prefix completes and cycles implemented
  names; candidate clicks preserve arguments. Up/Down browses per-server
  prefix history. Accepted commands return to chat or open their panel;
  failures remain editable. Join/New prefills Command. Paste cannot execute
  automatically and needs a fresh Enter/Run.
- Command history is bounded to 64 entries and 64 KiB of strings per server,
  is never persisted, deduplicates consecutive entries, and omits keyed joins
  and known credential forms. Message commands retain byte warnings and
  default-No split review; declined actions do not enter command history.
- Added explicit inline plaintext and encrypted GPG credential sources for
  SASL and server PASS, alongside private plaintext files. One source per
  credential is required. Inline values preserve literal bytes after '='
  and require mode 0600/0400; Settings masks them. GPG runs asynchronously
  after verified TLS with fixed arguments, bounded output, integrity checks,
  a five-second deadline, and no terminal prompt or source fallback.
- Removed the obsolete Focus visibility setting. Existing reverse/brackets
  values are accepted for migration, ignored, and omitted on Save. Selected
  Settings and transfer widgets share the uniform focus renderer.
- Updated CLI help, embedded manual, man page, installation guide, design and
  security documentation, and example configuration for these changes.

## 0.4.1 - 2026-10-26

- Main-screen mouse clicks use the controls that were actually drawn. An
  incoming nickname change or unread-count update can no longer move a
  queued click to another status control before the next redraw.
- Added a deterministic regression for network changes between drawing and
  mouse dispatch, with checks that stale coordinates are rejected after
  screen, view, or terminal-size changes.
- Corrected two remaining README references to the removed Actions menu.
  Away belongs to the selected server, and ignored senders are edited in
  Settings. No additional runtime dependency or main-screen control was added.

## 0.4.0 - 2026-10-26

- Replaced the dedicated main toolbar and Actions menu with focus on existing
  status values. Two header rows show server, conversation, Queries, Transfers,
  Mentions, and unread views; history starts immediately below. Settings and
  Help use the lower status row, and Clear belongs to the input divider.
- Server identity and Channel 0: Server open the server list. Channel and query
  names open their separate lists. A global unread picker labels every source
  server/channel/query and opens its exact conversation; /unread and Navigation
  w provide direct routes. Mentions copies do not duplicate unread totals.
- Arrows and hjkl now always navigate outside text editors, where letters
  remain text. Removed the navigation preset selector and indicator. Legacy
  keymap=original|vim config values are accepted but ignored and omitted from
  generated files; /keymap explains the fixed navigation behavior.
- Main-screen Escape and Settings open the editable configuration panel
  directly. Auto-connect, Auto-join channels, Auto-authentication, roster,
  mouse, watch terms, and ignores remain staged behind Apply/Save/Cancel.
  Focus visibility keeps brackets for readability and falls back automatically
  when the terminal cannot advertise reverse video.
- Contextual server operations now include Channels and Away. Transfers has
  its own status location and t shortcut. Help uses ?; h always means left.
  The roster uses u, /users, or Settings. Clear and x cover the whole draft;
  the redundant main Select all action and a shortcut were removed.
- Updated command help, the built-in manual, man page, installation guide,
  design contract, and example configuration to match the revised controls.

## 0.3.1 - 2026-10-26

- Fixed stale queued mouse clicks crossing transfer-review, field-editor,
  choice, Settings basic/advanced, or conversation transitions. A newly
  displayed consequential control needs a fresh action on that screen.
- Caret attributes cover the complete display width of a wide glyph in the
  chat, Settings, and transfer editors. Selection and caret movement no
  longer leave a half-repainted CJK character or shift following text.
- The event loop polls only active descriptors. Unused server slots no
  longer cause poll to fail when the process has a small descriptor limit;
  disconnected sessions continue receiving their timer service.
- Configuration validation rejects leading/trailing ASCII spaces in text
  values and checks bounded termination before scanning strings. Settings
  Apply and saving fail clearly instead of silently changing a watched phrase
  or credential-file path after reload; interior spaces remain supported.
- Added deterministic allocation-failure, DCC lifecycle/parser stress,
  transport and storage fault/concurrency, seeded PTY, and sustained TLS
  multi-server longevity tests. Limits, cleanup, state preservation, and
  responsiveness are asserted under failure as well as normal operation.
- Resource measurements verify the child process identity across PID
  namespaces before reading CPU, memory, or descriptor counts.
- No new application feature or runtime dependency was added in this patch.

## 0.3.0 - 2026-10-25

- One terminal now supports up to eight isolated server sessions. The Servers
  picker provides Open/New/Connect/Disconnect/Close/Back, with independent
  drafts, profiles, credentials, views, filters, and background protocol work.
- New server forms provide basic and advanced connection fields before the
  first connection. Add stays disconnected; Connect starts immediately;
  neither saves implicitly. Explicit Save creates a separate private profile
  and adjacent startup catalog. Confirmed secondary Close removes its startup
  reference, retains the profile file, and cancels its transfers.
- Server/channel/query lists show unread activity and mention counts before
  their labels. Reading a conversation clears its own counts; server totals
  exclude copied Mentions lines and outgoing local echoes.
- Explicit active IPv4 DCC SEND provides offer review, private no-overwrite
  receive files, send forms, cancellation, byte progress, and timeouts.
  Transfers are unencrypted peer connections with visible address disclosure.
  Global limits are eight pending offers, four active transfers, and twelve
  retained records. No CHAT, resume, passive transfers, or auto acceptance.
- The shared event loop services every IRC session and active transfer.
  Disconnect retains accepted transfers; Close/quit cancels their scope.
  Queued unsent offers are removed on cancellation or expiry.
- Offline mode previews multiple servers and transfer review without DNS,
  credential reads, transfer file access, or sockets. Save stays disabled;
  closing a simulated server leaves its saved catalog/profile unchanged.
- Expanded documentation and local PTY regressions cover multi-server routing,
  isolation, unread counts, profile persistence, DCC data flow, cancellation,
  and SSH host/file/address boundaries.
- /away MESSAGE sets away status; /away with no text and /back clear it.
  The indicator follows server acknowledgments and resets on disconnect.
  Actions exposes away controls and a shortcut to the ignore settings.
- /join accepts one channel and an optional transient key. Only the channel
  name is retained for auto-join; keys are never persisted.
- Bounded local ignore masks filter user PRIVMSG/NOTICE before mentions or
  query creation. Commands and Settings edit the list; [ignore] mask entries
  persist in normal mode. Server state and existing history remain intact.
- Startup begins in Navigation with the original keymap. Tab on the main
  screen switches typing/control focus and preserves drafts. Enter activates
  the highlighted control. Headers show both mode and keymap first.
- Main-screen Escape opens staged Settings from either mode. Escape from a
  secondary screen returns to main Navigation; ? opens Actions.
- The welcome preserves Unix interrupt/suspend controls and configured
  terminal signal characters. Ordinary printable keys, Enter, Tab, and
  navigation keys dismiss it; other control bytes do not.
- Ordinary Ctrl-D is ignored, including with an empty draft. /quit and
  Actions > Quit provide explicit exits; custom terminal signal mappings
  remain effective.
- /help, /manual, /view help, Actions > Help, and original-keymap h open one
  secondary manual reader. Back/Escape/Enter/q restores the prior main view
  and draft in Navigation. Tab stays in the reader; view cycling skips Help.
- Optional Vim-style navigation adds i, h/l, j/k, g/G, and preset-specific
  chooser controls. Both presets start in Navigation and share the same
  Input editor. /keymap reports or changes the original|vim choice, also
  stored as [interface] keymap; /save persists it during normal operation.
- The focused main toolbar contains Servers, Channels, Queries, Mentions,
  Users, and Actions. All four arrows move focus; Enter activates its control.
- Tab stays inside Actions and choosers and displays the return-to-chat hint.
  Warning dialogs ignore Tab; pasted tabs become spaces and cannot switch focus.
- The Actions menu provides typing, views, user lists, draft clearing and
  selection, mouse/keymap/focus preferences, help, connection controls, and quit.
- Focused controls, chooser rows, and menu entries support reverse video
  (default) or brackets through focus_style and the Actions toggle.
- Settings provides staged connection, interface, and mention fields with
  Edit/Apply/Save/Cancel. Live preferences apply in memory; connection edits
  require Save and restart. /save preserves pending startup choices.
- Settings server changes clear inherited authentication references and
  auto-join channels. Field paste remains data; credential contents stay hidden.
- Channel actions are Open/Join/Part/Close/Back; query actions are
  Open/New/Close/Back.
  Up/Down selects a row, Left/Right selects an action, and Enter executes it.
  Part/Close resets the action to Open; narrow bars retain visible focus.
- Input reserves a fixed height from the current view's single-message byte
  budget and terminal width, including a caret cell. Longer drafts scroll
  within that area; typing and deleting no longer change its height.
- An immediate editing warning appears when a payload line reaches its byte
  budget or the draft needs multiple messages. Enter/Esc returns to editing
  without sending; ordinary keys cannot dismiss it, with key-repeat guards.
- Completed bracketed paste is checked without discarding its draft. /msg,
  /me, and // checks use their outgoing payload; other commands are exempt.
- The existing default-no multi-message confirmation still runs on send.
  Earlier releases checked length only when submitting. Draft storage
  remains bounded at 8192 Unicode scalars and 16384 UTF-8 bytes.
- Expanded input and protocol documentation explains fixed row allocation,
  separate draft/message limits, and UTF-8 bytes versus displayed glyphs.
- Added SSH locale diagnostics and explained how remote locale and terminal
  capabilities relate to UTF-8 display and IRC message byte counts.
- Respects the active LC_CTYPE. Non-UTF-8 or invalid locales use explicit
  ASCII terminal mode with a welcome note and persistent marker; non-ASCII
  input is refused and incoming Unicode is displayed with replacements
  while the stored network text remains unchanged.
- Navigation a or Actions > Select all input selects the whole draft and
  enters Input. Typing/paste replaces it, Backspace/Delete clears it, and
  Tab returns to Navigation; Escape opens Settings, cancelling selection
  without discarding text. Home/End move to the
  complete draft's endpoints.
- Navigation x, Actions > Clear input, and the clickable [Clear] control
  clear the whole draft directly.
- Optional mouse caret placement, channel/query switching, and wheel
  scrolling are enabled by default where supported. Toolbar controls and
  chooser actions are clickable. /mouse [on|off] updates the interface mouse
  preference; /save preserves it in normal use.
  Mouse actions cannot send chat or acknowledge safety prompts.

## 0.2.0 - 2026-10-25

- Canonical -h/--help and -v/--version immediate command-line output, with
  -V retained as a version compatibility alias.
- --test-mode runs the interface with bounded in-process simulated IRC
  activity, using the selected configuration and no real network connection.
- Offline testing covers views, input editing, mention filters, simulated
  membership, message splitting, and applicable queue/pacing behavior.
- Simulated watch events rotate through all configured strings; changes
  made with /watch are reflected at the next watch event.
- Test mode does not resolve server names, read authentication secrets,
  validate TLS/SASL, or automatically write configuration. /save is disabled.
- Test mode supplies missing nickname/server values without prompts and
  uses #test-one and #test-two when no channels are configured.
- --no-connect can leave the simulator stopped; /connect starts simulation
  only. Normal startup still begins with the welcome screen.
- Expanded manual and installation guidance distinguishes syntax validation,
  interactive simulation, and real network verification.

## 0.1.0 - 2026-10-24

Initial original implementation for Unix-like terminals.

- Welcome screen with the requested vegetarian warning.
- Server, channel, nickname, port, TLS, and config command-line options.
- Missing-value terminal prompts with the Unix username as the nick default.
- Server/MOTD, channel, private conversation, mentions, and help views.
- Toggleable channel user list and nickname/custom literal matches.
- Channel chooser with permanent server entry 0, switching, leaving, joining,
  and closing views with draft-discard confirmation.
- Separate private-conversation chooser and /pm to open a view without sending.
- One server per process; independent terminal instances for several servers.
- Flat UTF-8 input editor with visible wrapping and cross-row Backspace.
- Literal bracketed paste and explicit multi-message send confirmation.
- Conservative protocol byte budgeting and paced, bounded chat sending.
- Editable/generated private configuration, auto-connect, and auto-join.
- Verified TLS, optional SASL PLAIN, and separate server-password files.
- Bounded parsing/history/state and sanitized terminal display.
- Embedded manual, Unix man page, example configuration, and build guide.
- Unit and local integration tests, with sanitizer build support.

This is the first implementation release. Platform-specific test coverage
and measured resource use are recorded separately in the release validation
report; this changelog does not claim native testing on every target OS.
