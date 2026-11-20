# onekeyirc architecture and maintainer guide

Version 0.1.35, 2026-11-17. An application of ThatIAm.vip,
designed by remfan1994.

This guide records current module responsibilities, ownership rules and
development checks. Start with README.md for use, INSTALL for building,
MANUAL.txt for interactive commands and SECURITY.md for the threat model
and limits. CONTRIBUTING.md gives the patch workflow; doc/SOURCE-MAP.txt
traces common changes and tests/README.md maps checks. CHANGELOG.md
records release changes.

onekeyirc is the standalone minimalist frontend of the shared IRC core.
It is intended for low-overhead terminal use, including a resumable worker
on a VPS. The installed runtime uses C11/POSIX interfaces, system TLS and
optionally GnuPG; it does not require curses or Python.

The current scope includes multiple server profiles, direct IRC or configured
ZNC/soju bouncers, explicit DCC SEND, optional private logs and local catch-up.
There is no scripting/plugin engine, shell command execution, automatic URL
opening or automatic acceptance of transfers. The interface stays small while
the shared core retains the same validation and ownership boundaries.

## Architecture and responsibilities

One worker can manage up to eight independent IRC server sessions. A terminal
frontend presents those sessions; it does not own the network protocol. Keep
parsing, measurement and preview operations free of network and filesystem
effects. Make explicit actions responsible for connecting, sending, saving,
accepting a transfer and ending a background job.

Module map:

- `src/main.c`: command-line operations, startup descriptor reservation,
  configuration selection and the lifetime of the shared session driver.
- `src/client.c`, `src/client.h`: fixed server slots, profile identities,
  lazy App allocation, primary-file preferences and scoped profile saves.
- `src/app.c`, `src/app.h`: one IRC connection's registration/authentication,
  queues, buffers, rosters, presence, history, logging and reconnect policy.
- `src/net.c`, `src/net.h`: resolver child, nonblocking TCP/TLS progress,
  trust loading, identity verification and classified transport failures.
- `src/protocol.c`, `src/protocol.h`: bounded IRC parsing, tag decoding,
  case mapping, UTF-8 sanitizing, payload measurement and message splitting.
- `src/config.c`, `src/config.h`, `src/autocommands.c`: configuration syntax,
  validation, private file operations and the restricted automatic-command
  grammar. They do not invoke a shell.
- `src/credentials.c`, `src/credentials.h`: bounded asynchronous GnuPG jobs,
  integrity/status checks, child ownership and encrypted credential creation.
- `src/background.c`, `src/background.h`: worker/controller lifecycle,
  numbered job discovery and authenticated local terminal handoff.
- `src/dcc.c`, `src/dcc.h`, `src/dcc_commands.c`: explicit active IPv4 DCC
  SEND offers, file/transfer ownership, progress and command validation.
- `src/editor.c`, `src/editor.h`, `src/nicks.c`: logical Unicode editing,
  shared cell geometry and bounded nickname completion.
- `src/commands.c`, `src/commands.h`: command metadata, aliases, suggestions,
  private in-memory command recall and argument-free usage statistics.
- `src/idle.c`, `src/idle_commands.c`, `src/idle_setup.c`: the minimalist
  terminal frontend, its command/help surface and staged setup workflow.
- `src/summary.c`: read-only workspace overview of retained servers, views
  and transfers. It does not open logs or change selection. Minimalist
  catch-up and saved-log paging are implemented in `src/idle.c`.
- `src/sim.c`: deterministic offline conversation simulation used by
  `--test-mode` and focused tests; it is not a real-network compatibility test.

The onekeyirc build selects `ONEKEY_ONLY` and compiles the minimalist
frontend. Shared interface/manual headers support the common core and test
interfaces; this archive does not provide the vipirc curses frontend.

Shared modules are maintained in both source archives. The `VIPIRC_` header
guards and several historical test/environment identifiers intentionally
remain shared; they do not select the executable's displayed name. Changes
to common behavior and comments should be mirrored and checked in both
builds, including conditional `ONEKEY_ONLY` paths.

## Ownership and stable identities

`Client` owns a fixed array of `ClientSlot` objects. Slots never compact;
primary slot zero can disconnect but cannot be closed. An unopened slot owns
its seed configuration, and an opened slot owns its `App`. Frontends borrow
these objects. Closing/reusing a slot creates a new runtime token.

Three different identifiers must remain distinct:

- The displayed server number selects a slot for a current user action.
- The runtime token identifies that slot's current incarnation. Deferred
  send plans, DCC associations and presentation records use this token so a
  replacement server cannot inherit a previous server's pending action.
- The saved profile ID identifies a configuration across later startups.

A background job number is a fourth, unrelated identifier. It identifies a
verified local worker through the background protocol, not a server slot or
an operating-system PID.

The outer session driver owns the shared `Background` and `DccManager`
objects. Frontend replacement must preserve them and the Client. Standalone
frontend entry points also support privately owned instances for tests and
callers; their `own_background`/`own_dcc` paths must free only those instances.
Final shutdown cancels transfers, tears down App state, acknowledges worker
closure and releases the remaining background resources.

Each App's buffers are the canonical retained conversation history. The
minimalist frontend's stream is a bounded presentation cache, and terminal
output is disposable presentation. A screen clear or interface replacement
must not be implemented by deleting App history.

## Event loop and transport contracts

Each frontend polls the descriptors requested by its active components and
uses the earliest relevant timeout. Timer work must continue when poll has no
ready descriptor. In particular, call the App/network and DCC progress paths
on timeout-only turns, and continue collecting retired credential children.
Use monotonic clocks for transport, helper, queue and transfer deadlines.

The transport owns DNS, TCP and TLS progression; the App owns IRC
registration, authentication, output queues and reconnect decisions. Network
setup is not entirely asynchronous: `net_open` constructs the TLS context and
loads trust material before starting resolver/connect progress. Regular-file
reads and filesystem lookup can still block on the underlying filesystem.

The resolver is a waitable child owned by its Net object. Descriptor cleanup
in that child must cover the actual inherited descriptor table, including
descriptors above a subsequently lowered soft limit. Do not add a generic
SIGCHLD handler that reaps children owned by Net, credentials or background
startup. Automatic child reaping is incompatible with these ownership rules.

TLS requires peer-chain and hostname/IP verification, with TLS 1.2 as the
minimum. There is no automatic plaintext fallback. A custom CA path is opened
once, checked through that descriptor and read into a bounded buffer. A
symlink to a regular trust file is supported; descriptor pinning prevents a
path reopen race, but does not make concurrently modified file contents
immutable. Trust files and the TLS library's default trust environment remain
trusted local inputs. An invalid custom store fails connection setup.

`net_events` defines the current readiness interest. If `net_write` reports
would-block, keep the exact in-flight bytes and length until positive progress
or failure; do not splice a new queue entry into that write. A TLS write
waiting for socket output takes precedence over reads, including decoded-byte
checks. TLS control records must yield to the event loop instead of producing
an internal read retry loop with no application data.

Application control and chat queues are separate and bounded. Partial writes
retain their queue entry; accepted chat is paced. A disconnect discards queued
messages rather than silently resending them after reconnect. `net_retryable`
classifies known temporary transport failures; it does not reconnect. The App
adds registration/authentication policy and backoff, and a user disconnect
cancels pending reconnects. Certificate, identity and configuration failures
must not be converted into endless automatic retries.

An in-progress LIST belongs to the current connection. Disconnect and the
supported command-qualified rejection numerics must release its active latch,
retain partial cached results, and mark the snapshot incomplete. Unrelated or
historical replies must not cancel a live request. Completed caches survive
disconnect unchanged; retry remains an explicit user action.

## Registration, bouncers and history privacy

Keep capability advertisement, requested capabilities, acknowledged
capabilities, SASL progress and registration success as separate states. A
server's final SASL declaration controls whether PLAIN is offered, including
multiple declarations on one CAP LS line and later continuation lines. An
earlier positive offer must not survive a later declaration without PLAIN.
Credential bytes are released only at the intended authentication stage on a
verified TLS connection when the selected authentication requires it.

Bouncer profiles explicitly distinguish ZNC and soju credentials from
network-service authentication. Bouncer identity, network selection and
service commands have their own validators and queue paths. Do not teach a
generic nickname field to reinterpret a password or service command. See
`doc/BOUNCERS.txt` and the example profiles for supported user workflows.

IRCv3 BATCH references are bounded state, not trusted annotations. Parent
references must exist, references must be unique while active, and a parent
cannot close while a child remains active. History classification propagates
through nested batches. Inconsistent framing disconnects; an unknown or
invalid message batch reference is treated as display-only history rather
than permission to apply live events.

Playback may display PRIVMSG/NOTICE text. It must not repeat historical
membership changes, authentication transitions, presence changes, PING
responses or DCC acceptance paths. A valid server-time tag changes display
time only. Message-scoped time/history fields are cleared before and after
dispatch so later local messages cannot inherit peer metadata.

Outgoing service/authentication copies are classified by their destination
and content, including bouncer replay under a previous nickname. The current
nickname alone is insufficient to decide whether a line is safe for disk
logging. Incoming service text and sensitive send plans also pass through
their dedicated suppression paths. Input recall and usage statistics are
separate: recall omits sensitive commands; persisted usage contains canonical
command names and counts, never argument text.

## Configuration and credential lifetimes

Distinguish the running connection configuration, staged startup fields, a
temporary candidate and the saved file. `app_config_effective` produces the
desired next-connection view by overlaying staged fields on live settings.
It does not claim that the current server is already using those values.
Its result may contain secrets and must be wiped when the temporary copy is
no longer needed.

Validation and necessary replacement allocations happen before live mutation.
When saving is requested, complete the relevant save before applying the
in-memory preference change. Startup-only changes stay staged until a
connection attempt. The pending mask is derived from the declared startup
field table; adding a new connection identity field requires reviewing that
table, config validation, serialization, setup and tests together.

New server configurations inherit presentation preferences, not endpoint
credentials. Retargeting an existing profile must not silently carry its
old automatic commands to a new identity. Each save operation has a precise
scope: a profile write and a default-profile catalog reference are separate
commits, and callers report partial success instead of pretending the pair
was atomic. Command usage sidecars are also separate from profile saves.

Config and secret file operations validate the opened objects, file type and
ownership; secret files require the accepted private modes. New private
configuration files use bounded output, an exclusive temporary file, flush
and rename within a pinned directory. Do not substitute path-only prechecks
or shell expansion. The filesystem and the account's own processes remain
part of the local trust boundary; private modes do not isolate hostile code
already running as the same user.

GnuPG is an optional trusted local helper, invoked with fixed argument vectors
and bounded nonblocking pipes. Plaintext is accepted only after successful
exit and complete integrity/status checks. A success result does not free its
job: callers still release the job and wipe their own plaintext copy.
Cancellation closes pipes, wipes buffers and retires children for later
nonblocking collection. While either helper pipe remains open, preserve the
unreaped direct child so its process-group identity stays owned even when a
descendant retained a writer. This cleanup contract is not a sandbox for an
arbitrary malicious helper.

Encrypted credential creation is a separate transaction. It requires a full
fingerprint, encrypts before publishing, refuses an existing destination and
publishes a new private file. A failed final directory sync may still mean
the file was linked; use the published-state accessor when reporting results.
Rollback removes only an unchanged file created by that transaction.

## Background worker and terminal ownership

At process startup, reserve missing standard descriptors before opening
configurations, sockets or private files. Later terminal handoff deliberately
replaces descriptors 0, 1 and 2; unrelated resources must never occupy those
numbers accidentally.

An ordinary foreground session creates no background socket or extra worker.
For the first explicit background operation, prepare and validate ownership
before restoring the frontend's terminal state. Refuse the first fork while
a resolver or credential helper remains owned, or credential cleanup is
pending. The worker inherits established connections, buffers and transfers;
the successful original parent exits without freeing that copied state.

Once managed, the worker keeps its identity and detaches again without
forking. A separate foreground controller owns the shell job and passes
terminal descriptors and validated terminal metadata through local IPC.
Discovery, attach, detach and close use the private job registry and worker
incarnation token; do not replace them with PID files or numeric kill calls.
Bound candidate handshakes, descriptor counts and poll work, and process only
the poll snapshot associated with that event-loop turn.

Detach drops terminal ownership while preserving IRC connections and worker
state. Close requests cooperative teardown of the whole verified worker.
The latter can affect several server sessions and active transfers. A
successful close is reported after teardown acknowledgement; an unavailable
or incompatible worker is an error, not permission to signal a guessed PID.

On managed terminal/controller loss, discard obsolete presentation, detach
and keep the worker resumable. An ordinary foreground process has no managed
worker to resume. Session controls and SSH transport keepalives address
different problems; this program does not configure the SSH server.

onekeyirc resumes its minimalist frontend. In the ordinary log view,
Escape requests background/detach; it is not an unconditional process exit.
Use the documented quit/close operations when the worker should end.

The minimalist terminal layer preserves inherited ISIG and VINTR/VSUSP
semantics unless `force_terminal_signals` is explicitly enabled. The option
defaults to false. Ctrl-C and Ctrl-Z are not hard-coded substitute application
commands; test terminal signal behavior with the inherited terminal settings
as well as the opt-in override. Normal exits and handled transitions restore
the saved terminal state.

## Text, input history and rendering

IRC parser fields borrow the mutable input line. Consume or copy them before
that storage is reused. Successful syntax parsing does not establish a
sender's identity, authorize an action or make text safe for terminal output.
Sanitize peer/configuration/error text at presentation boundaries and keep
decoded IRC tag controls away from terminal escape interpretation.

The Editor stores logical Unicode text and caret position; wrapping is a
projection onto available cells. Byte budgets for IRC messages and display
cell widths are different quantities. Measure the complete destination and
relay overhead before splitting; preserve UTF-8 boundaries and every accepted
payload byte. A multiline or oversized draft builds an explicit send plan,
which pins server identity and destination through review. Recheck the plan
at acceptance rather than resolving against whichever screen is now active.

The minimalist input screen computes editor/status geometry before wrapping
the visible chat tail. A fresh frame includes both regions, so growing the
editor cannot consume the last chat row. Canonical history remains intact
when terminal rows change. Streaming output advances presentation cursors only
after queueing succeeds; slow-terminal handling must not create duplicate
chat or an unbounded output queue.

A review redraw request and its send plan belong only to review mode. Every
transition back to editing must retire them even if the output queue still
contains a partial editor frame; otherwise stale redraw work can force
zero-timeout polling forever. Keep the draft and composer/command identity.

Command recall is not chat history. In minimalist command entry, Up/Down
browse safe accepted commands for the original draft prefix and runtime
server token. Keep that original draft stable across repeated arrow events.
In ordinary message composition, arrows navigate editor text. Do not make
holding Up in a multiline message silently replace that message with prior
commands. Full-interface command recall has a frontend-owned, per-server
history object with the same privacy and bounded-retention intent.

Catch-up anchors belong to a server/view and the identity of a real outbound
chat record. Eligible private disk history is tried first, with retained
in-memory history as fallback. Old/missing/replaced/truncated anchor sources
must be handled explicitly instead of inventing missing conversation text.
Persistent logs are optional, default off, and bounded; they are neither
encrypted archives nor an unlimited recovery mechanism. A logging error
stops that logging path and is reported without sacrificing the IRC session.

## Resource bounds

These are application bounds, not claims about every IRC network's limits.
When changing one, review its allocation arithmetic, parser/serializer,
related UI truncation, queue behavior and boundary tests together.

- Client: 8 server slots; each App has 32 buffers, including server, mentions
  and help, and each channel roster can retain up to 4096 users.
- Retained App text: 8 MiB shared by that App's buffers; configured history
  is 50-5000 lines per buffer. Help has room for at least 2048 lines.
  onekeyirc starts with 100 history lines per buffer.
- Minimalist presentation stream: at most 2048 records and 512 KiB; its
  effective record limit can be lower with the selected history setting.
- Editor: 8192 Unicode scalars and 16384 encoded UTF-8 bytes.
- Command recall: at most 64 entries and 64 KiB of strings. Persisted command
  usage has 96 name/count entries and no arguments.
- Outbound IRC: 64 control and 64 chat queue entries; ordinary outgoing body
  at most 510 bytes, with per-message relay overhead reducing payload space.
- Batch framing: 16 active references of at most 64 bytes each.
- TLS: at most 16 resolved addresses, a 30-second setup deadline and a
  5-second address attempt; custom trust input is at most 16 MiB.
- Credentials: at most 1024 plaintext secret bytes and 65536 encrypted bytes;
  helper jobs have a 5-second deadline. Final protocol framing may impose a
  smaller usable secret size for a particular authentication path.
- DCC: 8 pending offers, 4 active transfers, 16 KiB blocks and a maximum
  advertised file size of UINT32_MAX. Pending offers expire after 5 minutes;
  an active transfer without progress expires after 2 minutes.
- Persistent log: at most 16 MiB per log file; hitting the limit stops that
  logging path until the user archives the file and re-enables logging.
- Background discovery: at most 64 numbered jobs with bounded registry and
  query waits. Incarnation tokens contain 16 random bytes.

## DCC transfer and filesystem invariants

DCC is classic active IPv4 SEND only: no DCC CHAT, resume or passive transfer.
Parsing an offer has no network/filesystem effects and commits output only
after full validation. A valid offer still needs explicit acceptance. The
separate DCC connection is neither encrypted nor authenticated by IRC TLS;
an advertised address is peer-supplied data, and an outgoing listener accepts
the first connecting peer. User-facing help must preserve those limits.

A receiver owns its open file descriptor and the exact newly created inode,
with a pinned destination directory. Cancellation removes a partial file only
if the current leaf still names that unchanged inode. It must not unlink a
replacement placed at the same pathname. Receiving refuses overwrite and
unsafe path traversal; an outgoing source is an explicitly selected regular
file. The manager survives frontend changes and cancels associated transfers
when their originating server token is closed.

Decode each complete four-byte network-order acknowledgement as uint32_t.
Acknowledgements may repeat but cannot decrease or exceed bytes actually
sent. The file-size ceiling prevents counter wrap; do not apply signed
comparisons or implicit modulo accounting. Completion requires the whole
advertised file to have been transferred and acknowledged. Sending waits for
the outstanding block's acknowledgement before reading the next block.
Timeout processing must run even with an empty readiness set.

## Development and documentation workflow

Read `INSTALL` for the platform's compiler/TLS/curses dependencies and
configure overrides. A normal local development sequence is:

    ./configure
    make
    make docs
    make check-docs
    make check-unit

`Makefile.in` is the maintained build source; `./configure` creates Makefile.
Python 3 is needed for documentation generation and Python tests, not for
the installed client. `scripts/sync_docs.py` implements `make docs` and
`make check-docs`; the check target detects stale exported text without
silently rewriting it.

For this build, the hand-maintained in-program minimalist manual is in
`src/idle_commands.c`. `make docs` captures the built client's `--manual`
output into top-level `MANUAL.txt` and `doc/onekeyirc-manual.txt`. Change the
source help first, then regenerate the exports. `scripts/sync_docs.py`
also generates `doc/onekeyirc.1` from its maintained CLI metadata and the
complete exported help; do not edit that generated man page directly.
See CONTRIBUTING.md for the source-of-truth table and exact commands.

Keep README quick starts, INSTALL, SECURITY, examples, in-program help and
the man page consistent with the source. Update safety-relevant comments
when their invariant changes; prefer a short explanation of ownership or
ordering over a comment that merely repeats the next statement. Comments
must describe current behavior and its limits, not hoped-for guarantees.
Historical audits and validation reports in `doc/` describe the versions and
environments they name. Add current evidence instead of silently editing an
old result into a new release claim.

Choose checks for the code boundary changed:

- Parser/editor/configuration changes: `make check-unit`, including the
  bounded parser, application, client and configuration tests.
- CAP/SASL/playback/bouncer changes: `make check-bouncers` and
  `make check-security-boundaries`; add `make check-bouncers-terminal` for
  the PTY/mock-peer presentation path.
- Resolver, trust-file and TLS readiness changes: `make check-net-retry`,
  the boundary suites, and the corresponding `tests/test_net_*.py` harnesses
  invoked with the build flags in Makefile.in.
- Session ownership, startup descriptors and terminal loss:
  `make check-session-controls`, `make check-session-lifecycle`,
  `make check-terminal-signals` and `make check-session-notice`.
- Input tail wrapping and retained stream behavior:
  `make check-idle-display`, `make check-idle-availability` and
  `make check-stream`.
- Credentials: `make check-deep` and the boundary suite; `make check-gpg`
  and `make check-setup-gpg` additionally exercise a real local GnuPG binary.
- Recovery and resource failures: `make check-stability` and its sanitizer
  variant; `make check-install` exercises private executable publication.
  `make check-idle-load` uses eight private TLS peers and a real minimalist
  PTY for bounded load, reconnect, draft and quiet-CPU checks.
- Broad release validation: `make check` and `make check-security`, with
  the relevant `check-*-sanitize` target for memory/undefined-behavior
  changes. Sanitizers complement functional assertions; they do not prove
  protocol safety or availability on their own.

The onekeyirc targets use the standalone executable and minimalist terminal
fixtures. When a shared change also affects vipirc, run the corresponding
vipirc tests rather than inferring its full-interface result from this build.

Fixtures may include implementation files or replace transport/terminal
boundaries to exercise state transitions deterministically. Such tests do
not replace a real named AF_UNIX/SCM_RIGHTS worker attachment, actual curses
rendering, or a real ZNC/soju deployment. Run native background lifecycle tests
on a host that permits those sockets; run terminal tests with working PTYs.
OpenBSD/LibreSSL compatibility requires that actual build/runtime environment.

For a release, record the source version, compiler/library versions, exact
commands and results, including skips, unavailable dependencies and fixture
substitutions. Local mock servers should not contact a public IRC network.
Successful counter-edge tests do not mean that a multi-gigabyte transfer was
performed, and an offline simulator does not establish live interoperability.
When a check cannot run, keep that limitation visible rather than labeling
the complete platform or protocol validated.

## Custom bindings

`config.h` defines bounded terminal-independent key identities and local
action IDs; `config.c` owns parsing, canonical names, the action allowlist,
validation and private persistence. A `Config` begins with zero bindings.
The `[hotkeys]` parser rejects normalized duplicates, while explicit edits
replace one binding transactionally. There are no arbitrary command bodies.

`client.c` propagates primary-owned bindings to new, loaded, lazy and live
profiles. `app_config_effective` starts from the current live preferences,
then overlays only pending connection fields. Binding edits must preserve
those pending fields, and unrelated settings forms must merge only edits.

`idle_commands.c` supplies /bind, /unbind and /bindings. Frontends map decoded
keys to the shared IDs only after paste and modal ownership are established.
Custom dispatch is limited to chat and command entry; no action submits text
or confirms a dialog. The minimalist reader retains editor/caret state, and
destination changes refuse a nonempty draft. Full vipirc stages only the
binding table and merges it into a fresh primary config at Apply/Save, so it
never stores a second long-lived copy of credentials or unrelated settings.

See doc/HOTKEYS.txt and the focused hotkey tests before expanding the action
allowlist or terminal decoder. In particular, test split escape sequences,
paste termination, alias collisions, retained drafts and primary persistence.

## Security follow-up invariants

A bracketed-paste closing marker changes input state but does not supply
consent for trailing bytes. Ordinary key/mouse dispatch and custom
actions pass the same frontend review gate. Completed output, sufficient
geometry, a later input batch and a quiet interval are separate checks.
Undersized/fallback frames and terminal replacement cannot grant credit.

Incoming PRIVMSG/NOTICE privacy is classified before display dispatch.
Recognized private authentication destinations/content are excluded from
persistent logs even when a replay sender is an old nickname or a target
requires fallback display. Per-message suppression resets after dispatch.
Ordinary channel conversation keeps the existing logging behavior.

config_create is distinct from config_save: it links a fully written
private temporary inode into a missing destination in the same pinned
directory. EEXIST is refusal, with no replacing fallback. Once published,
cleanup/sync failure reports that a complete config may remain; it never
removes a competing destination. Explicit Save keeps replacement semantics.
