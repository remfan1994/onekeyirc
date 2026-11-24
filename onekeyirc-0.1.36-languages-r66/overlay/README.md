## Current optional language pack: r65 (onekeyirc-0.1.36)

Apply `onekeyirc-0.1.36-languages-r65/apply.sh` to CLEAN English source r1
BEFORE ./configure and make. Verify `[lang-rtl-r65]` using --version.
1658 catalog keys per language; this update adds 15 IRC conversation-event
display templates (JOIN, PART, QUIT, NICK, KICK, MODE, INVITE, TOPIC, CTCP).
IRC wire data and nicknames are not translated. No native-speaker signoff.

Apply `onekeyirc-0.1.36-languages-r64/apply.sh` to CLEAN English source r1
BEFORE ./configure and make. Verify `[lang-rtl-r64]` using --version.
1643 catalog keys per language; this update adds 55 fixed configuration
validation messages in all ten supported languages.
Security rules, serialization, authentication and IRC wire text remain literal.
Do not layer language packs. Older release notes below are historical.

---

## Current optional language pack: r60 (onekeyirc-0.1.36)

Use `onekeyirc-0.1.36-languages-r60/apply.sh` on untouched onekeyirc-0.1.36 English source r1
before `./configure && make`. Confirm `[lang-rtl-r60]` via `--version`.
Catalog: 1513 keys per language; 51 new keys in this patch.
New: network/TLS diagnostics, resolver error prefixes, and 15
connection-state labels. Do not layer patches or translate IRC commands.
Previous version markers below are historical, not current guidance.

---

## Current optional language patch: r59 (onekeyirc 0.1.36)

Run `onekeyirc-0.1.36-languages-r59/apply.sh` on clean onekeyirc-0.1.36 English r1 before build.
The expected version marker is `[lang-rtl-r59]`.
This patch translates 18 new profile/session error strings per locale
and connects 19 client.c display paths; earlier revision headers and
version markers below are archival notes, not installation guidance.
Do not layer language packs on a previously patched source tree.

---

## Optional language connection pass r54 (2026-11-23)

Additional displayed connection/TLS/SASL/reconnect diagnostics are translated across all 10 locales. Run the package apply.sh against fresh English r1 **before** configuring and compiling. Validate the `[lang-rtl-r55]` --version marker. Never stack language patches.

# Language pack r53: translated FINISH review and credential notices

Display-only localization in ten languages. Use a clean English r1 tree and apply.sh before compiling.
Do not layer language overlays. No changes to accepted config values, file handling or IRC protocol.

# Optional language-pack revision r52: guided setup summaries

Localized bouncer identity guidance, credential filename and path notices, and apply/save/connect results.
Use ONEKEYIRC_LANG=fr or =es to test French and Spanish.
Apply this standalone patch ONLY to fresh matching English r1 source, not over r51.

# Guided setup help translations r51 (optional overlay)

Twenty-seven setup-help display paths are now translated; the onekeyirc
catalog already contained eleven of these keys, and their existing values
were preserved. Correct literal choices, exact source verification,
authentication, GPG, saved configuration, user input, and IRC wire protocol
remain unchanged. Strings are display data and never printf formats.

A French or Spanish tester can use `ONEKEYIRC_LANG=fr` or
`ONEKEYIRC_LANG=es` on a fresh installed build.
Apply only to clean matching English r1 source; never layer over r50.
Tests: `make check-setup-help-r51` and `make check-l10n-security`.

# Optional guided-setup error localization r50 for onekeyirc-0.1.36

34 additional fixed validation, profile-selection and credential-operation
messages are translated into the ten optional locales. User-supplied input,
config choices, authentication, file handling, IRC wire and app version are
unchanged. Translated strings are passed only as data through a fixed "%s"
format, or are selected as data for an existing fixed "%s" format.

Apply only to pristine matching English r1 source, never over r49 or any
previous language pack. Verify with `make check-setup-errors-r50`.
Human-language and native terminal review remain necessary.

# Optional language and Arabic RTL patch r49 for onekeyirc-0.1.36

This standalone overlay extends the earlier source-linked localization with 28
translated setup/credential validation and security notices in each of ten
locales. Guided setup now translates its displayed errors for credential-path
collisions, GPG and password requirements, bouncer identities, verified TLS,
and setup choices. Validation, file creation, secrets, commands and IRC wire
behavior are unchanged. The original application version is unchanged.

Apply ONLY to a fresh matching English r1 source; never over r48 or an
already-patched/configured tree. Source-linked tests: `make
check-setup-validation-r49`. Test fluent wording on actual terminals.


Standalone overlay for an unchanged, clean English r1 source tree.
Do not layer it over r47 or any other language patch.
The application version remains 0.1.36.

This revision adds 50 distinct generic command descriptions across 51
command entries, and eight previously English-only guided-setup help texts
for TLS, bouncers, credentials, GPG and private logging. The minimalist
command picker already uses its separate, translated native-command catalog.
Generic /background help now correctly names onekeyirc --resume. Command
syntax, credentials, settings and IRC protocol remain unchanged.

Run make check-command-localization-r48, make check-l10n-security,
make check-localized-manuals and make check-arabic-visual.
Native-speaker and live-network testing remain outstanding.
See the patch-root README.txt for installation and integrity checks.

## Historical optional language patch notes

## Historical language patch notes

# Optional language and Arabic RTL patch r32

Added ten-language CLI diagnostics and --help overview. No change to application version or IRC behavior. Current installation instructions are in the package-root README.txt.

Optional localization and RTL patch r29 for onekeyirc 0.1.36.
New: three additional translated SECURITY.md sections in ten languages.
English SECURITY.md remains authoritative; fluent-speaker review pending.
See current package-root README.txt for installation.

Optional localization/RTL patch r28 for onekeyirc 0.1.36.
English source and RTL patch are separate archives. RTL behavior is optional.
Use signals=no is an application feature of onekeyirc 0.1.36 (not the patch).
See SIGNALS-CHANGE.txt or doc/USE-SIGNALS-en.txt for keyboard behavior.
Older patch release notes below are historical.

Optional localization patch r27: onekeyirc 0.1.35 English r1 unchanged.
Partial translated SECURITY.md chapters: doc/SECURITY-PARTIAL-XX.txt;
see doc/SECURITY-COVERAGE-r27.tsv. English SECURITY.md is authoritative.

# Optional localization and RTL patch r19 for onekeyirc 0.1.35

Patched --version: onekeyirc 0.1.35 [lang-rtl-r19].
Source-linked translated documentation: Trust boundary.
The original English r1 source and full English MANUAL.txt/SECURITY.md are unchanged.
No runtime functionality changes other than the patch-version label.
Current installation instructions: use the package-root README.txt.
Do not layer patches; translators should follow the exact English source references.

## Archived previous readme and release notes

# Optional localization and RTL patch r18 for onekeyirc 0.1.35

Patched --version: onekeyirc 0.1.35 [lang-rtl-r18].
The original English r1 source is unchanged. For correct commands
see the archive README.txt and the r18 translation status.
Do not layer patches. Full MANUAL.txt and SECURITY.md are still English.
New documentation: DCC-MINIMAL locale guides.

## Earlier release documentation (historical)

# Optional language+RTL patch r17

This is applied over the original English source r1, which is unchanged.
After rebuild, --version reports onekeyirc 0.1.35 [lang-rtl-r17].
New source-linked manual translation: doc/BACKGROUND-COMMAND-XX.txt.
Coverage inventory: doc/TRANSLATION-STATUS-r17.txt.
The full MANUAL.txt and SECURITY.md are NOT yet fully translated.
This release changes no RTL runtime/editor logic except its version label.
Previous notes below are historical, not current install directions.

# Optional language+RTL security patch r15

English app onekeyirc 0.1.35 stays unchanged. Patched builds report onekeyirc 0.1.35 [lang-rtl-r15].
Display controls are sanitized; the wire payload is unchanged.
Verify external SHA-256 sums and apply to clean English r1 source.
See doc/LOCALIZATION-SECURITY-en.txt for security guidance.

# Optional localization + Arabic RTL patch r14

Apply this overlay only to fresh English source r1. `--version` shows
`onekeyirc 0.1.35 [lang-rtl-r14]` after rebuilding. No RTL C code is
added to the separate English source. Keep both source and patch versions
when filing bug reports. This r14 package retains all r12 Arabic fixes.

# onekeyirc 0.1.36

An application of ThatIAm.vip (www.thatiam.vip), designed by remfan1994.
Source is supplied under the included IALA (Insha-Allah License Agreement).
Read [LICENSE.md](LICENSE.md) and [NOTICE](NOTICE) before redistribution.

onekeyirc is the standalone, command-oriented IRC client. It keeps a live
conversation tail above a compact input field, can remain connected while
detached from SSH, and supports saved profiles for several IRC servers. It
uses POSIX terminal interfaces directly and has no curses or terminfo
dependency. The former standalone name was 1keyirc.

Start with the build and first-connection steps below. For a problem, use
[the troubleshooting guide](doc/TROUBLESHOOTING.txt) to identify the state
to inspect and the next action. To change the implementation, start with
[CONTRIBUTING.md](CONTRIBUTING.md). The [security guide](SECURITY.md)
describes the operating assumptions and limits.

## Documentation map

- [INSTALL](INSTALL): dependencies, building, installation, checks, and upgrades.
- [MANUAL.txt](MANUAL.txt): the complete top-level program manual.
- [Getting started](doc/onekeyirc-start.txt): a short first-session walkthrough.
- [Configuration example](examples/onekeyirc.ini): supported INI sections and settings.
- [Key bindings](doc/HOTKEYS.txt): user-selected keys, actions, table and commands.
- [Bouncers](doc/BOUNCERS.txt): ZNC and soju profiles, playback, and privacy.
- [Session controls](doc/SESSION-CONTROLS.txt): attach, recover, detach, and close.
- [Troubleshooting](doc/TROUBLESHOOTING.txt): symptoms, diagnostics, and next actions.
- [Localization](doc/LOCALIZATION.md): supported languages, encoding, review, and safety boundaries.
- [Localized command guides](doc/COMMANDS-id.txt): short command explanations (id, ms, tr, es, pt, fr, de, ru, it, ar).
- [Localized navigation guides](doc/NAVIGATION-id.txt): channel, private chat and view selection in ten languages.
- [Localized authentication and bouncer guides](doc/AUTH-BOUNCERS-id.txt): selected /help ZNC, soju, credential and CA instructions in ten languages.
- [Localized first-use help](doc/FIRST-USE-id.txt): complete startup, messaging and message-limit topic text in ten languages.
- [Localized practical guide](doc/PRACTICAL-GUIDE-id.txt): 10 complete interactive help sections in each of 10 languages (use PRACTICAL-GUIDE-XX.txt).
- [Help translation audit](doc/HELP-LOCALIZATION-COVERAGE.txt): exact help lines still in English.
- [OpenBSD VPS guide](doc/OPENBSD-VPS.txt): saved startup and rc.d service setup.
- [SECURITY.md](SECURITY.md): trust boundaries, safe deployment, and reporting.
- [CONTRIBUTING.md](CONTRIBUTING.md): editing, style, review, and release workflow.
- [Source map](doc/SOURCE-MAP.txt): ownership, call paths, and where to make changes.
- [Test guide](tests/README.md): focused checks, dependencies, and evidence limits.
- [DESIGN.md](DESIGN.md): source structure and implementation invariants.
- [CHANGELOG.md](CHANGELOG.md): release history.

The instructions below describe the current source. Reports in `doc/` whose
names include a release or date remain historical records; read their scope
and environment notes before treating a reported check as applicable to a
different platform or build. In particular, see the
[documentation refinement review](doc/DOCUMENTATION-REFINEMENT-0.7.36-0.1.35.txt),
[source readability review](doc/DOCUMENTATION-REVIEW-0.7.35-0.1.34.txt), the
[recovery review](doc/STABILITY-REVIEW-0.7.34-0.1.33.txt), the
[preceding security review](doc/SECURITY-REVIEW-0.7.33-0.1.32.txt), and
[OpenBSD validation record](doc/OPENBSD-VALIDATION.txt).

## Build and first connection

From the extracted source directory:

    ./configure
    make
    ./onekeyirc --help
    ./onekeyirc

Use `./onekeyirc` for the local build, or `onekeyirc` after installing it as
explained in INSTALL. Later shell examples use the installed name; before
installation, use `./onekeyirc` from this build directory instead.

Commands beginning with `/` belong inside the client. Shell commands begin
with the executable name. Type each example separately. Replace the sample
host, nickname, and channel with your own; `irc.example.org` is a placeholder.

Dismiss the welcome, then type this command. Its initial `/` opens Command:

    /connect -nick mynick irc.example.org 6697

Wait for the server's welcome/registration reply, then enter:

    /join #mychannel

Once the channel is joined, ordinary typing composes a message to the
selected conversation. Check the destination before pressing Enter.
A new destination defaults to verified
TLS on port 6697. A port number alone does not disable TLS. `-tls` (also
`-ssl`) or a `+PORT` selects TLS explicitly; `-plain` explicitly selects
unencrypted IRC. Credential authentication requires verified TLS.

A matching saved server reuses its settings. `/server HOST [PORT]` is a
connection alias. With several identities on one bouncer host, select the
intended saved profile by number instead of assuming the hostname identifies
the right account. Without a supplied nickname, the client uses a valid Unix
login name, or `user` as a fallback.

`/quickstart` offers guided connection setup. `/panel` opens the settings
selector: 0 edits application preferences; 1 through 8 edit server profiles.
Read the final action before accepting it. Quickstart defaults to Connect
once, server panels to Save only, and application preferences to Save.
Saving or applying connection edits does not interrupt a live connection.
If your server requires credentials, configure that profile before
connecting; the simple example above does not set up authentication.

For an offline look at the interface:

    onekeyirc --test-mode

Test mode makes no IRC connections, does not read credential files or
decrypt secrets, and skips configuration and command-count saves. It is a
UI aid, not a test of your real server, password, certificate, or boot
setup.

## Save the connection and understand what is saved

After a successful connection:

    /autoconnect on
    /saveconf

`/autoconnect on` requests a connection at future startup. `/saveconf` saves
application preferences and healthy server profiles, and remembers the
selected server as the next-launch default when that profile saves
successfully. Saving alone never enables autoconnect. Leave autoconnect off
and use `/connect` after launch if you prefer manual connection.

Use the save scope that matches your change:

- `/save`: save the selected server's effective settings, including staged
  connection changes.
- `/save input` or `/save output`: identical aliases that save the primary
  profile, including application preferences and staged primary connection
  settings. They do not save only the named input/output section or the
  currently selected secondary profile.
- `/save all`: save all healthy server profiles separately.
- `/saveconf`: save application preferences and profiles, and remember the
  selected server as described above.

Each file is replaced atomically, but a multi-profile save is not one
transaction. Check every diagnostic: an earlier successful save is not
rolled back if another profile fails.

`/autojoin add #mychannel` changes the next connection's channel list;
`/join #mychannel` joins now and updates the running list. Explicitly staged
`/autojoin` edits take precedence when saving. Keys supplied to `/join` are
not saved. Endpoint, TLS, authentication, and staged startup edits take
effect on the next explicit `/disconnect` then `/connect`. Automatic retries
retain the interrupted connection's settings.

## Configuration and local files

The default primary file is:

    $XDG_CONFIG_HOME/onekeyirc/config.ini
    $HOME/.config/onekeyirc/config.ini       when XDG_CONFIG_HOME is unset or empty

`XDG_CONFIG_HOME`, when nonempty, must be absolute. Use `--config FILE` (`-C FILE`)
for an exact alternate file. It must already exist unless you are creating
it with `--init-config`. For a new configuration only:

    onekeyirc --init-config
    onekeyirc --check-config

Initialization refuses to overwrite an existing file. `--check-config`
validates the main file and saved server profiles without connecting or
reading/decrypting credentials. It does not verify the remote account or
whether a GPG agent will be unlocked after reboot.

If the new default file is absent, an existing `1keyirc/config.ini` under
the same XDG/home base is reused in place, with its adjacent saved profiles.
The new path wins if both exist. Invalid or inaccessible candidates produce
an error; no configuration is silently moved. See
[rename and upgrade notes](doc/ONEKEYIRC-RENAME.txt).

The primary config owns application-wide input, output, session, and presence
preferences. Additional server profiles keep connection, authentication,
bouncer, startup, watch, and ignore settings beside it in the `.servers`
catalog and `.d` profile directory. Paths inside INI files are literal:
there is no shell quoting, `~` expansion, or `$HOME` expansion. Relative paths
are relative to the program's launch directory; absolute paths are best for
services and credentials.

Command-line `--nick`, `--port`, `--channel`, `--tls`, `--plain`, and `--ca-file`
overrides normally apply to the remembered default server. An explicit
`--server` selects the primary profile; changing its hostname clears the old
destination's credentials and startup state before connecting. This is not
a way to select an arbitrary saved bouncer identity. Configuration creation
and validation apply overrides to the primary file.

A `.commands` sidecar contains bounded command-name usage counts only, never
arguments or messages. Full command history, drafts, and ordinary scrollback
are in memory. Optional `.log` transcripts are described below. Copying the
main INI file alone is not a complete backup of a multi-server setup or its
separately stored credentials.

## Terminal interaction

Ordinary typing, Insert, or Enter from the live log opens Compose. A leading
`/` opens Command instead. The first typed character is retained. The default
`line` layout has one input row below the live tail. For a larger editor:

    /set composer_mode fullscreen
    /save input

Use `line` instead of `fullscreen` to return to the compact layout. Incoming
chat remains visible during composition and command entry.

Compose uses arrows to move the caret. Enter sends by default; Escape
cancels the draft and returns to the log. `/set send_mode review` adds a
review step; save it with `/save input`. A composition that needs several IRC
messages requires explicit split-send approval. The server's byte limit is
different from the number of rows drawn by your terminal.

In Command, Up recalls matching accepted commands; Down moves back toward
the current typed line or into suggestions. Enter on a selected suggestion
inserts it; a fresh Enter executes it. Tab/Shift-Tab completes command
names in Command and nicknames in Compose.
The command history is not a sent-chat history: Up in Compose moves the
caret and does not recall previously sent messages. Complete command history
lives in RAM, and sensitive commands are omitted.

From the watch log, Up/Down scroll rows, PageUp/PageDown scroll pages, Home
goes to the oldest retained text, and End returns to the live tail. Left,
Right, Delete, and Backspace do not open input from that view. Escape there
backgrounds the job; use `/quit` to end it deliberately.

Recognized bracketed paste cannot execute or send by itself. Paste into
chat/the watch log stays message text, including a leading slash. Paste
inside Command stays an edited command; pasted line breaks and tabs become
spaces. Do not paste a block of separate commands as one operation.

To paste one command example, open Command first. Typing `/` opens it with
a slash already present: paste the remainder without its leading slash,
or remove that prefilled slash before pasting the complete command. Check
the resulting line, let the input settle, then press a fresh Enter.
Unframed paste cannot reliably be distinguished from typing; the manual
describes the terminal timing limits and send-review controls.

Terminal-generated Ctrl-C and Ctrl-Z signals are OFF by default. While
composing a message or command, Ctrl-C or Ctrl-Z cancels that unsent draft,
just like Escape; neither exits nor suspends the program. Use `/panel` then
application preferences (panel 0) to change **Use signals**, or open Interface
settings in full vipirc. Save writes `[input] use_signals=true|false` to the
primary configuration; Apply changes the active terminal without saving.
With `use_signals=true`, your terminal's configured interrupt/suspend keys
work normally and may terminate or suspend the client. The previous
`force_terminal_signals` key remains accepted on load for compatibility,
but the new explicit boolean disables signals when false, regardless of
inherited tty flags. External SIGTERM/SIGHUP and explicit /quit still work.

The terminal is restored to its exact prior settings when the client exits.
Ctrl-D remains inert unless the user binds it. Panel configuration is
application-wide across server sessions. Pasted controls cannot trigger
cancellation or send confirmation.

## Optional custom key bindings

No custom keys are assigned by default. Use `/bindings actions` and
`/bindings keys` to choose names, then assign only the keys you want:

    /bind F7 summary
    /bind Alt-c command
    /save input

These examples are not installed defaults. `/unbind F7` removes one binding;
`/unbind all` clears the list. Save with `/save input` to keep your choices
after restart. Bindings belong to the primary config and apply across servers.
The config format is an optional `[hotkeys]` section with `KEY=action` rows.

Full vipirc has an editable table at App -> Key bindings and `/bindings`.
The minimalist interface lists bindings and edits them with the commands
above. Only main chat and command entry accept custom actions; passwords,
paste, readers and confirmations keep their own controls. No arbitrary
commands or send macros are accepted. See [HOTKEYS.txt](doc/HOTKEYS.txt) for
the action list, draft protection, terminal aliases and saving behavior.

## Channels, queries, and server profiles

Enter the command in the left column; the description is explanatory text.
Numbers and names here are examples: use the current list for your workspace.

| In-program command | Effect |
| --- | --- |
| `/servers` | List server profiles; `/sl` is an alias. |
| `/server 2` | Select profile 2 without connecting it. |
| `/connect 2` | Connect profile 2. |
| `/channels` | List channel views; `/cl` is an alias. |
| `/channel #mychannel` | Select an existing channel view. |
| `/channel 0` | Show server messages and MOTD. |
| `/queries` | List private conversations; `/ql` is an alias. |
| `/query alice` | Open a query; `/pm` is an alias. |
| `/part` | Leave the current channel. |
| `/close` | Close the current retained conversation. |

Server numbers are 1 through 8; profile 1 is the permanent primary profile.
These are not the job numbers printed by `--sessions`. `/server add HOST
[PORT] [NICK]` creates an additional profile with fresh credentials and
manual connection, without selecting, connecting, or saving it. Removing a
secondary profile with `/server close NUMBER` removes its session/catalog
entry but leaves its configuration file on disk.

`/list` requests the network's channel directory, while `/channels` lists
the selected connection's retained channel views, including joined/parted
state. Use `/help list` for server masks and local filters. `/summary`
shows the current client state without making a remote request; it is
useful when diagnosing a connection.

## ZNC, soju, and credentials

Use a separate saved profile for each upstream bouncer network. In `/panel`,
set the TLS bouncer host/port, bouncer type, username, network, and optional
stable client identifier. The client constructs the required login identity.
Supply the bouncer password itself, not a hand-assembled password string.

- ZNC uses a server-password source. See [ZNC example](examples/bouncer-znc.ini).
- soju uses a SASL password source. See [soju example](examples/bouncer-soju.ini).

Credential sources can be an owned private password file, a GPG-encrypted
file, or an explicit private-config literal. Use exactly one source per
credential. Credential files must be owned regular files with mode 0600 or
0400; symlinks are rejected. Prefer the setup panel for entering credentials
without putting passwords into shell arguments or command history. Active
bouncer authentication changes belong in `/panel`, not `/autoauth` edits.

For a direct server profile, a private-file SASL setup can use:

    /autoauth sasl myaccount file /absolute/private/account.secret
    /save

Optional GPG credentials require GnuPG 2.2.20 or newer and an agent able to
decrypt without an interactive prompt. The panel can create a new encrypted
file for an existing local encryption key. It does not store the private
key's passphrase or unlock it at boot. See the manual before choosing GPG
for unattended startup.

Bouncers restore channel membership, so the client does not replay the saved
autojoin list for them. Explicit autocommands still run on registration.
Automatic idle/background AWAY is disabled for bouncer profiles unless
`automatic_away=true`; manual `/away` and `/back` remain available. Timestamped
history pushed by the bouncer is displayed with bounded retention. This is
not an on-demand archive browser or automatic multi-network discovery.

Use `/znc` or `/soju` for the corresponding service commands. ZNC module
messages such as `/msg *status ...` are also supported. These service commands
are excluded from input history and persistent transcripts; recognized
bouncer service replies go to the unlogged server view. TLS protects the
client-to-bouncer connection; upstream TLS and authentication are configured
on the bouncer separately.
Read [BOUNCERS.txt](doc/BOUNCERS.txt) for supported capabilities and limits.

## Keep a job running and recover after SSH drops

Inside the client, `/background` releases the terminal and keeps the managed
worker running. To start an unattended job from saved configuration:

    onekeyirc --background --reconnect
    onekeyirc --sessions
    onekeyirc --resume

A loaded configuration or explicit `--server` is required for unattended
startup. Save autoconnect first if you want saved profiles connected at
startup. A successful background launch means the local worker is ready;
inspect `/servers` or `/summary` after resuming to verify IRC registration.

The standalone client always uses its minimalist interface; `-N` is not
a supported frontend choice.

If a lost SSH terminal leaves a job marked attached or busy, first list the
jobs. Substitute the current job number, not a PID or server-profile number:

    onekeyirc --sessions
    onekeyirc --detach-session NUMBER
    onekeyirc --resume NUMBER

Detaching releases the terminal controller while preserving the worker,
connections, drafts, and retained history. To deliberately terminate that
entire job instead:

    onekeyirc --close-session NUMBER

Closing ends every IRC connection and transfer in the job; it cannot then
be resumed. `/quit` does the same from inside the client. If close times out,
an accepted request may still finish later; recheck `--sessions` before
assuming it is still live. Do not delete session files or signal an assumed
PID based on the job number.

Session management options are separate operations. Do not mix them with
`--config`, connection options, or `--background`. Run them as the same Unix
account and client namespace that started the worker. `/set single_session
on` followed by `/save input` can limit that namespace to one background job;
it does not limit the IRC connections inside that job.

`--reconnect` retries transient failures with delays increasing from 5 to
300 seconds. It does not itself initiate a connection or save a preference.
Certificate, authentication, configuration, and protocol failures are held
for correction. `/disconnect` cancels retry until an explicit connection.
`--no-connect` suppresses startup connections. Unsent chat is discarded after
a lost connection, and saved autocommands run on each new registration.

Use the managed background workflow before relying on survival of terminal
loss. A VPS reboot or worker exit ends that process's in-memory history and
drafts. A boot service starts a new worker from saved configuration. See
[SESSION-CONTROLS.txt](doc/SESSION-CONTROLS.txt) and the
[OpenBSD guide](doc/OPENBSD-VPS.txt).

## Logs, catch-up, and limits

Persistent logging is off by default. To enable it and remember the choice:

    /set persistent_logging on
    /save output

Each saved profile appends to its adjacent `.log`; unsaved profiles retain
only RAM history. The log must be an owned regular 0600 file in an owned
0700 directory. Each file stops at 16 MiB rather than growing indefinitely;
live chat continues. Turn logging off, archive the log, then enable it again.
Do not loosen the file checks to work around a refusal.

In the minimalist interface, `/catchup` returns to the last locally sent
composition's anchor in the selected channel or query. It tries an eligible
private log first, then retained RAM. Logging must already have been
enabled when the anchor was created and remain healthy. It does not import
old logs at startup or recover anchors from an exited process. Rotation or
replacement invalidates a disk anchor; available RAM may still be used.
Ordinary scrolling also has bounded retention.

DCC SEND is optional active IPv4 file transfer. Every incoming offer needs
explicit acceptance. DCC is a separate direct, unencrypted, unauthenticated
peer connection even when IRC uses TLS. Paths are literal, existing files
are not overwritten, and incoming files require a private receive directory.
Read the manual and SECURITY.md before accepting or offering a file.

## Troubleshooting

Start with [TROUBLESHOOTING.txt](doc/TROUBLESHOOTING.txt) for an ordered
check and next action. Common distinctions are:

- A second display row appears clipped: switch back to that conversation or
  return to its live tail and compare retained text. Drawing, local echo,
  server receipt, and another user's receipt are separate things. Do not
  resend solely because a row is missing. Report the exact version, terminal,
  dimensions, interface, paste method, and steps with nonprivate sample text.
- Up does not reload a sent message: command history belongs to Command;
  Compose arrows move the caret. Chat drafts are not a sent-message recall
  list. See the input section above.
- Resume says busy: use the inspect/detach/resume sequence above. Close only
  when the entire job should end.
- A setting did not survive restart: verify the save scope and diagnostic,
  and whether you launched with the same config path and Unix account.
- TLS or authentication is held: inspect server view 0 and `/summary`, check
  hostname, certificate trust, credential ownership/mode, and GPG readiness,
  then explicitly reconnect after correcting the cause.
- Logging stops: check the diagnostic, private directory/file ownership,
  concurrent use, and the 16 MiB limit. Live chat may still be working.
- A new executable seems unchanged: installed files do not replace code in
  an already running worker. Close that job deliberately and launch the new
  version; old RAM history and drafts end with the old process.

For reports, include `onekeyirc --version`, the OS and terminal, interface,
exact nonsecret steps, and whether the issue occurs in `--test-mode`.
Redact credentials, channel keys, private messages, and identifying paths.
Security-sensitive findings should follow SECURITY.md rather than a public
transcript dump.

## Help, local notice, and attribution

    onekeyirc --help
    onekeyirc --manual
    man onekeyirc

Inside the client, `/help` opens the reader. In the minimalist interface,
`/help ls` lists topics, `/help commands` lists commands, and `/help NAME`
opens a topic or command reference. `/help /ls` explains the local list
command instead of listing topics. `/manual` opens the complete reference.
Help can be read offline.

Once per running worker, the ordinary display includes this local notice:

    Cow killing is the bloodshed of the innocent. www.bloodguiltcurse.net - ban beef.

It is not sent to IRC or inserted into AWAY text. An unattended worker shows
it when a display becomes available; reconnecting, changing channels, and
reattaching do not create new notices for that same worker.

ThatIAm.vip and remfan1994 attribution and the supplied IALA license remain
in NOTICE and LICENSE.md. Dependency licenses and maintenance remain those
of the installed dependency versions.

## Language support

This release includes 452 localizable interface strings in ten languages:
Indonesian, Malay, Turkish, Spanish, Portuguese, French, German,
Russian, Italian, and Arabic. Navigation, settings labels, and selected
setup prompts use translated strings; technical help, many diagnostics and
some other on-screen messages remain in English. English is the fallback.

Choose the interface language when starting the program, for example:

    VIPIRC_LANG=id LANG=en_US.UTF-8 onekeyirc
    VIPIRC_LANG=tr LANG=en_US.UTF-8 onekeyirc

Use a UTF-8 locale actually installed on your host, as shown by `locale -a`.
`VIPIRC_LANG` chooses the display language; `LANG` or `LC_CTYPE` controls
terminal character encoding. An existing detached worker retains the
language from its original launch; restart it to change language.

See [doc/LOCALIZATION.md](doc/LOCALIZATION.md) for language selection,
translation maintenance, testing, Arabic limitations, and untranslated areas.
The IRC command vocabulary, wire protocol, configuration keys, and
security-sensitive behavior remain unchanged.

For source reuse, modification and redistribution, see `doc/USE-MODIFY-REDISTRIBUTE.txt`.

Localization: run `/help language` to see launch-time language choices, or read
`doc/LOCALIZATION.md`. Short offline instructions for ten languages are in
`doc/START-id.txt`, `doc/START-ms.txt`, `doc/START-tr.txt`, and the other
`doc/START-XX.txt` files. Exact commands, settings keys and safety explanations
remain authoritative in the English manual.

Localized deployment/security extracts: doc/OPERATIONS-XX.txt (XX is a supported two-letter language code). These are draft translations; check SECURITY.md for the authoritative English details.

## Arabic text in language pack r11

Optional Arabic shaping, paragraph wrapping and visual cursor/mouse editing
are available in both minimalist and full ncurses interfaces. Use an installed
UTF-8 locale and the system FriBidi library:

    ONEKEYIRC_LANG=ar ONEKEYIRC_ARABIC_VISUAL=1 ./onekeyirc

Leave visual mode off if your terminal already shapes/reorders Arabic. Stored
and transmitted text remains logical UTF-8. The English and Arabic terminal
guides in doc/ARABIC-TERMINALS.txt and doc/ARABIC-TERMINALS-ar.txt explain exact
scope, key behavior and remaining native platform/font acceptance work.

## Arabic refinements in language pack r12

The optional Arabic mode now shares one completed editor layout between
text and cursor, preserves insertion stops at clamped vertical boundaries,
keeps ncurses combining marks with their base at terminal margins, and
reuses paragraph layouts during minimalist reader traversals. History
counting and painting in full ncurses share a per-record map as well.

Enable it using the commands above after applying r12 to fresh English r1
source. Current instructions and limits are in doc/ARABIC-TERMINALS.txt and
doc/ARABIC-TERMINALS-ar.txt. Translation coverage is unchanged; native
OpenBSD/terminal/font acceptance and linguistic review remain pending.

## Optional language and RTL patch r16

This source tree has the optional RTL and translation patch applied. For localized material start with doc/SECURITY-BOUNDARIES-XX.txt. These are selected manual/security excerpts, not full translations. The original English MANUAL.txt and SECURITY.md remain unchanged; Arabic visual rendering and editor logic remain as audited in r15.


Optional security translation r30: protocol/terminal, managed sessions, and bouncer playback sections.


Optional localization r31: complete draft SECURITY.md companions for all ten languages.
Native-speaker security review and native vipirc ncurses testing remain outstanding.
