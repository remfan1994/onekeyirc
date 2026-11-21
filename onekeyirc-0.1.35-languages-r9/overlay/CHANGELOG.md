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
