# Security model for onekeyirc 0.1.35

This is the current security contract for the client. The accompanying
MANUAL.txt covers operation; doc/SESSION-CONTROLS.txt covers SSH recovery;
doc/BOUNCERS.txt covers ZNC and soju. This document describes controls and
their limits, not a guarantee that the program has no vulnerabilities.

## Trust boundary

IRC servers, peers, nicknames, topics, protocol fields, filenames and pasted
text are untrusted input. The operating system, terminal, C/TLS libraries,
certificate trust store and the user's account remain trusted components.
The full vipirc interface also trusts curses. GPG adds its executable,
keyring and agent. Server profiles share one process and are not isolated
security compartments. Another process with the same account privileges can
generally inspect private files and memory.

Run as an ordinary account. Before reading configuration, operational
invocations reject mismatched real/effective user or group IDs, disable core
files and set umask 0077. Linux also disables process dumpability. Missing
standard descriptors are reserved with /dev/null before opening application
resources; inherited open streams and their flags remain unchanged. Required
startup-protection failure stops the invocation. These controls do not provide
pledge/unveil, seccomp, privilege separation or protection from host compromise.

configure probes compiler/linker hardening supported by the selected host,
including stack protection, fortified libc checks, format diagnostics, PIE,
RELRO, immediate binding and a non-executable stack. Inspect the configured
flags on the deployment host; not every platform supports every flag.

## TLS, credentials and endpoint identity

TLS requires version 1.2 or later and verifies the certificate chain and
selected hostname. Configured SASL,
server PASS and enabled nonempty automatic-command lists require verified
TLS. There is no silent plaintext fallback. Encryption of a password on disk
does not enable transport encryption. IRC TLS protects the connection to the
selected server or bouncer; it is not end-to-end message encryption.

Maintain the linked TLS library as well as this source. The review host
used OpenSSL 3.5.7; OpenSSL documents CVE-2026-35189, certificate-related
memory growth during ordinary TLS processing, fixed in 3.5.9 in that
branch. This source release does not patch that external library. Check
the deployment vendor's version/backports and current security updates:
https://openssl-library.org/news/secadv/20260929.txt

An empty ca_file selects the system trust store. A custom CA bundle is a
trusted local PEM file: the loader opens it once, checks the opened regular
file, reads its bounded contents and parses that copy without prompting for
a password. It rejects empty files, files larger than 16 MiB, nonregular
sources and detected size changes. Certificate/AUX trust and CRL entries are
preserved. CA paths may resolve through symlinks because these are explicitly
selected public trust material, unlike private configuration/credential
paths. Selecting a different CA changes which issuers the client trusts.
The loader is synchronous; regular-file I/O can still stall on slow storage.

Each credential selects exactly one source: a private plaintext file, a
private GPG-encrypted file, or an explicit inline value. SASL and server PASS
sources are separate. A different --server hostname clears inherited
authentication, CA selection, bouncer identity, channels and startup commands.
A same-host override, port change or nickname change does not provide that
reset. Review the selected profile before connecting to a new service.

Direct SASL PLAIN requires its configured account. soju derives the SASL
identity from bouncer fields. Negotiation uses the final advertised SASL
mechanism declaration, waits for acknowledgement/challenge, and only then
reads the credential. A required authentication failure does not become an
unauthenticated registration. ZNC uses its server PASS source instead.

Recognized private authentication commands entered manually also require
verified TLS. Validation applies to logical lines and all planned chunks and
is repeated at send time. Recognized private forms are omitted from command
recall and persistent transcript logging. Their live terminal representation
can still contain the entered text. Recognition covers common service verbs,
not arbitrary secrets in prose or every third-party service protocol.

## Configuration and private files

Configuration is literal INI: no shell, variable or tilde expansion. Unknown
sections/keys and invalid values fail validation. Files are bounded at 64 KiB.
Configuration and secret path traversal rejects symlink components and '..'.
Reading an existing config requires an owned regular file that is not
writable by another user. Inline credentials and any nonempty automatic-
command list, even disabled, require exact mode 0600 or 0400. Reading config
or credential files does not require a private parent directory. Saving
requires an owned final parent with no group/other permissions; use mode
0700 for the ordinary private-directory layout. Generated configs use mode
0600 and private atomic replacement. Save rewrites supported values and
replaces comments. --init-config uses atomic no-clobber publication: an
existing object, including one created during initialization, is never
replaced. A reported cleanup/directory-sync error after publication can
leave the completed private config in place. Filesystems without hard
links fail safely. Transcript logging has stricter exact-mode directory
checks, described below.

Plaintext and GPG credential sources must be owned regular files with exact
mode 0600 or 0400. Decrypted/plaintext passwords are one nonempty line:
SASL permits up to 1024 bytes; server PASS up to 503 bytes. The complete ZNC
identity-plus-password must also fit that bounded PASS frame. Credential
selection has no fallback to another source when the selected source fails.

GPG decryption starts after verified TLS. It uses direct fixed arguments,
bounded nonblocking pipes, a separate process group and a five-second deadline.
The application does not invoke a shell, pass a secret in argv, create a
plaintext temporary file, fetch keys, start helper daemons or prompt for a
key passphrase. Unlock a protected key outside the client. Ciphertext input
and created ciphertext are bounded at 64 KiB; helper status is bounded at
16 KiB. Success requires completed integrity-protected decryption and a
successful helper exit. Diagnostics do not print decrypted text/helper output.

The setup panel can encrypt a new password with an existing local key's full
40/64-hex fingerprint. Hidden password/confirmation buffers stay bounded.
Only the final explicit action starts encryption. It creates no public key.
The offered default destination is beside the selected profile; merely viewing
it creates nothing. Explicit acceptance may create missing owned mode-0700
parents. Custom parents must already exist; existing directories are not
chmodded. Publication is mode 0600 and no-clobber, with directory/file identity
and destination conflicts checked again before publication.

Apply only or Connect once can create the explicitly requested ciphertext
without saving configuration. A failed config save can leave completed
ciphertext for retry. Cancellation removes only unchanged files created by
that attempt; it retains saved references and conservatively retains a file
when the existing config cannot be safely checked. Publication followed by a
sync error may therefore leave a reported file. Empty created directories
can remain. Retry rechecks complete ciphertext, private mode and inode.

Temporary credential buffers are wiped where owned by the application. Inline
values remain in config/session memory while required for Save or reconnect.
This does not prove erasure from library/helper memory, swap, backups, snapshots
or privileged memory inspection. Do not include real credentials in a report.

## Child processes and synchronous work

DNS resolution runs in a supervised waitable direct child with a deadline;
its cleanup acts on that directly owned child. The resolver is not placed in
a helper process group. GPG has a separate process group and keeps its
waitable direct-child identity until output pipes finish, so cancellation
can stop same-group inherited pipe writers. GPG is a trusted program, not a
sandbox: descendants deliberately leaving that group are outside its cleanup
contract.

Resolver/helper starts and first background-session creation reject inherited
SIGCHLD=SIG_IGN or SA_NOCLDWAIT. First detach rechecks after preparation;
managed re-detach does not fork. Embedding the modules under an unrelated
handler that reaps their children is unsupported. Regular-file and metadata
operations remain synchronous even where O_NONBLOCK prevents FIFO-open waits.

## Protocol, terminal input and bounded state

There is no shell escape, raw IRC injection command, plugin/script engine or
automatic CTCP response. Commands use an allowlisted parser. Unknown aliases
fail instead of guessing a network action. Incoming protocol fields, UTF-8,
nicknames, targets and parameter counts have explicit limits. Display paths
remove terminal controls and unsafe formatting; LIST topics are sanitized
before entering the directory cache.

The outgoing budget includes command syntax, target, relay identity and
action wrapper. Smaller valid server LINELEN values reduce the budget.
Splits preserve UTF-8 scalars. Multiple-message sends need separate consent;
bracketed paste cannot confirm its own send. The per-server chat queue has
64 entries and at least 1.2-second pacing, with a separate 64-entry control
queue. These limits do not guarantee delivery or override server flood rules.

The program supports eight server connections, 32 views per server, 4096
stored users per channel and 8192 LIST entries. A truncated roster/directory
is incomplete. Disconnect or an explicit LIST rejection releases a pending
request while preserving partial cached entries; it does not pretend the
snapshot is complete. A later request or explicit clear remains possible.
Unsolicited private messages create query views only below
the eight-query automatic limit; extra conversations fall back to server/
mentions views. Explicit queries remain possible within the overall view cap.

Retained text has an 8 MiB budget per server plus a 50..5000-line per-view
setting. The minimalist display stream separately caps retained records at
2048 and text at 512 KiB. These are not total RSS limits: drafts, rosters,
queues, libraries and allocator overhead use additional memory. A bounded
program can still spend CPU on hostile traffic. Ignore masks reduce displayed
user chat, not receipt/parsing work; they are not an access-control boundary.

The terminal's signal-enable state and interrupt/quit/suspend assignments are
inherited by default. [input] force_terminal_signals=true explicitly enables
signals without assigning Ctrl-C, Ctrl-Z or any other keys. Whether a key
signals the process therefore depends on terminal settings. Literal Ctrl-D
is not an application exit shortcut unless explicitly bound in the main
chat/command prompt. Normal/handled exits restore terminal
modes; SIGKILL cannot run cleanup.

Recognized bracketed paste remains literal. Before the opening marker is
recognized, the client cannot distinguish a paste from ordinary terminal
input. After a recognized paste ends, ordinary keys and custom actions
wait for a later input batch and a quiet interval. An accepting editor
or form must be fully displayed; an undersized or failed frame is not
review credit. Repeated keys extend the interval. Resize/reattach clears
display credit. Isolated Escape has a 500 ms grace period; incomplete sequences allow
1000 ms between continuation bytes while networking continues. A delayed
opening marker can act as Escape (backgrounding from the minimalist log).
Once capture begins, split closing markers have no inter-byte timeout.
Unframed paste has no equivalent safety boundary.

## Local history, saved logs and privacy

Command recall is bounded to 64 entries/64 KiB and stays in memory. Chat
composition is not a sent-chat recall interface. Known credential/control
forms are excluded. Optional frequency data in <config_path>.commands stores
at most 96 canonical command names and counts, without arguments, targets,
messages or passwords. Automatic reads/writes require a saved configuration;
invalid/unsafe count files do not prevent startup. Test mode skips this I/O.

Persistent transcripts are off by default. Each enabled saved profile appends
to <config_path>.log, requiring an owned exact-mode-0700 parent and an owned
single-link regular exact-mode-0600 file. Descriptor-relative traversal and
O_NOFOLLOW reject symlinks; O_NONBLOCK prevents a planted FIFO from blocking
before type checks. Logging creates no parent directories.

The append cap is 16 MiB per active log. A nonblocking advisory lock serializes
the size check/write among cooperating clients. Unsafe paths, a busy lock,
failed writes or a reached cap disable logging for that profile with a
diagnostic; live chat continues. Nothing is automatically truncated/deleted.
Archive or move the log, then explicitly toggle logging off and on. External
archives are the user's responsibility. Disk I/O can still stall on slow or
remote storage; the logger is not asynchronous.

Help/aggregate mentions copies, unsent drafts, SASL/PASS buffers, recognized
private commands and known service replies are excluded from transcripts.
Bouncer control destinations receive additional filtering described below.
Unknown service names and secrets typed as ordinary chat can still be logged.
Logs survive exit. Terminal recording, server logging and recipients' copies
are independent of this setting.

Catch-up uses this live worker's last eligible locally sent message. It first
tries the private log with a validated file/record anchor and pinned end, then
falls back to retained memory. Rotation or changed identity invalidates the
disk anchor; it does not silently select an unrelated record. Replayed own
messages do not replace the anchor. A fresh process does not import old logs
or reconstruct a terminated worker's catch-up anchor.

## Automatic commands and cancellation

Each profile has at most 16 literal commands, each at most 511 UTF-8 bytes.
Only message, single-channel join, nick, away/back and WHOIS operations are
accepted. Listings/errors hide arguments because text may contain secrets.
The connection owns a fixed startup-command snapshot. Edits are staged for
the next explicit connection; automatic retry retains the interrupted
connection's settings. Identity changes clear inherited private command text.

Execution starts after registration and required SASL. The initial delay is
0..300 seconds; subsequent intervals are 2..60 seconds after a completed
write. Only one automatic frame is pending, so a stall cannot cause a catch-up
burst. Automatic messages must fit one frame and are not split/locally echoed.
An error stops subsequent commands with a redacted indexed diagnostic.

/cancel removes unsent queued text and future automatic work. A partially
written frame or an in-flight TLS retry must retain its bytes and may finish,
even when the retry has not yet reported accepted bytes. Sent bytes cannot
be recalled. Disconnect clears the transport/queues. Automatic-away retries
are timed and yield to newer accepted manual/script/background intent.

## Managed sessions and SSH loss

Background creates an account-private local job endpoint and lifetime lease.
The registry is separate for each program and Unix user, with 64 numbered
slots. It is not an Internet listener or a reboot-persistence mechanism.
Discovery/attach/control validate private paths, file identities and the
selected worker's incarnation token; job numbers are never treated as PIDs.
Same-user peer credentials are checked where supported by the platform.

One terminal can attach at a time. Resume validates transferred terminal
descriptors and bounded TERM/locale metadata. Discovery is bounded, skips
unsafe objects and does not recursively remove unexpected registry content.
A live unresponsive worker keeps its lease and is reported as unverified.
Bare --resume selects only a sole verified available detached worker with no
unverified candidate. single_session restricts background-job creation, not
the number of IRC connections inside an existing worker.

--detach-session NUMBER releases the selected terminal/controller while
preserving the worker, connections, drafts, transfers and retained history.
--close-session NUMBER ends the whole worker and discards unsaved RAM state.
Detach waits for confirmed terminal release; close waits for endpoint/lease
teardown. Acceptance alone is not completion. Controls have deadlines and
never kill a guessed numeric PID. Do not unlink endpoints as a recovery step.

Loss of an already managed terminal/controller detaches the worker. An
ordinary foreground process that never became managed has no resumable job.
Installing a new executable does not upgrade a running worker's protocol;
older workers may need their existing controls to stop cleanly.

The minimalist output queue is bounded and explicit background requests can
be serviced while it is blocked. Cleanup/notice writes are best effort and
may be omitted on a stopped terminal. Full ncurses drawing still uses
synchronous terminal output; a permanently blocked open terminal can delay
its control poll. A timeout is reported honestly, not as a forced-kill success.
Neither client configures SSH server idle-disconnect policy.

## ZNC, soju and playback

Explicit bouncer profiles require verified TLS and an account/network
identity, with an optional client name. ZNC uses a server-password source;
soju requires SASL PLAIN and a SASL-password source. The setup transaction
applies endpoint, identity and credentials together and clears stale drafts
when those bindings change. Upstream IRC authentication belongs to the
bouncer's own configuration.

Bouncer-restored JOINs determine membership; saved channel lists are not
replayed. Explicit startup commands remain deliberate exceptions. Automatic
away is off by default for a bouncer profile so one device does not impose
its idle state on another. Manual /away and /back remain explicit actions.
Local worker controls do not administer the remote bouncer service.

/znc and /soju control text fits one paced message and is omitted from recall,
local echoes and persistent logs. Incoming copies addressed to the bouncer
service are suppressed even with an old nickname or unknown batch reference.
Service replies remain visible in the server view but are not persisted.
Recognized private authentication copies are excluded from transcripts
regardless of sender nickname, replay framing or qualified service
destination. Their visible retained text is not a secret store.
Text can be visible while composed, and the bouncer administrator can receive
and log it. This is not secrecy from the selected bouncer.

Known history batches are display-only: they cannot change current membership
or presence, generate new mention alerts, answer CTCP or activate DCC offers.
Nested/interleaved history boundaries are bounded to 16 batches and 64-byte
references; malformed/inconsistent/over-limit framing disconnects. Unknown
batch references are handled conservatively as display-only. Original valid
timestamps display in UTC but never drive local timers or pacing.

The client supports pushed buffers, not an on-demand bouncer archive browser
or account-wide network discovery. It does not request echo-message,
draft/chathistory or znc.in/playback. A timestamp alone is not proof of history;
unbatched legacy playback keeps ordinary live handling. Arbitrary replay is
not deduplicated across fresh processes. See doc/BOUNCERS.txt for setup/limits.

## DCC file transfers

Classic active IPv4 DCC SEND is plaintext and unauthenticated, independent of
IRC TLS. A nickname does not authenticate the connecting peer. The displayed
endpoint may be a private/loopback address. Sending advertises an address;
acceptance connects to the shown peer and reveals the process host's address.
An offer alone creates no file/socket. Accept/send need an explicit action.
Over SSH, files and sockets belong to the remote process host.

Sources must be regular files, with no symlink path components. They are
streamed, not snapshotted or protected from later modification; use a stable
source. Receive creates a new mode-0600 file in an owned private directory,
refusing traversal, symlinks and existing destinations. Partial cleanup
removes only the same device/inode created by the transfer. Files are never
automatically opened or executed. Byte-count acknowledgements are not hashes.

There are eight pending offers, four active transfers and twelve retained
records, with a maximum file size of 4294967295 bytes. Offers expire after
five minutes; active transfers fail after two minutes without progress.
Repeated unchanged acknowledgements do not extend that deadline. DCC CHAT,
resume, passive/reverse transfer, unknown-size files and IPv6 are unsupported.
There is no public-address discovery or automatic NAT/firewall configuration.
IRC disconnect leaves accepted direct transfers alive; closing their server
or quitting ends them. /cancel does not replace transfer-specific cancellation.

## Offline checks and evidence

--help, --version and --manual do not load configuration or credentials.
--check-config validates config/catalog syntax, values and file protections;
it does not test password access/decryption, DNS, TLS or a real login.
--test-mode loads valid profiles into an in-process simulator without IRC/DCC
networking, credential/CA reads, GPG or config saves. Inline values may be
parsed but are never sent. Explicit Background can still create local session
IPC. Success in simulation does not establish real-server compatibility.

The current recovery review covers vipirc 0.7.34 / onekeyirc 0.1.33 in
doc/STABILITY-REVIEW-0.7.34-0.1.33.txt and doc/STABILITY-PASS-RESULTS.json.
It records reproduced failures, corrections, resource-failure and bounded-load
checks, and platform limits. The preceding security review remains in
doc/SECURITY-REVIEW-0.7.33-0.1.32.txt and doc/SECURITY-PASS-RESULTS.json.
Earlier named/date-stamped reports retain their original source versions,
test workloads and platform limits; they are not rerun claims.

Local protocol fixtures do not prove operation against a real ZNC/soju daemon
or IRCNow account. Full ncurses runtime, native named AF_UNIX lifecycle,
OpenBSD/LibreSSL and deployment-host behavior need their stated target checks.
A short stress test does not prove months of unattended reliability.
Report the version, platform, reproducible steps and redacted diagnostics;
omit passwords, private keys and private chat transcripts.

## User-defined key bindings

The primary config owns at most 64 bindings; the default table is empty.
Keys normalize to a fixed identity and actions come from a local allowlist.
There is no arbitrary command, shell expansion, raw IRC or direct send action.
Unknown/duplicate/oversized bindings reject a config load atomically. Only
explicit Save persists edits, through the existing private-file writer.

Custom dispatch is confined to main chat and command entry. Paste and modal
fields/readers/confirmations do not execute custom actions. Function/Alt
decoding cannot reinterpret captured paste as an action, and a paste-ending
input burst does not immediately execute a shortcut. Unframed paste remains
indistinguishable from typing, and terminal interception of keys precedes
the program. Bindings never reassign terminal signal characters or enable
signals. See doc/HOTKEYS.txt for exact scope and draft protection.
