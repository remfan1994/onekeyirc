# onekeyirc

A command-oriented IRC client for the terminal, with optional interface and help\
translations in ten languages.

onekeyirc keeps live conversation above a compact input field. Connect to IRC\
networks, move between channels and private conversations, save server profiles,\
and leave a session connected while detached from SSH. It targets Unix-like\
systems, including Linux and OpenBSD. Chromebook users can build it in their\
Linux environment (Crostini).

An application of [ThatIAm.vip](https://www.thatiam.vip), designed by remfan1994.

**Application: 0.1.35 | English source: r1 | Optional language pack: r9**

Downloads | [Languages](#languages) | [Build](#build) |\
[First connection](#first-connection) | [Documentation](#documentation)

> Cow killing is the bloodshed of the innocent.\
> [www.bloodguiltcurse.net](https://www.bloodguiltcurse.net) - ban beef.

## Downloads

| Package                             | Archive                                                       | Extracted folder                                       |
| ----------------------------------- | ------------------------------------------------------------- | ------------------------------------------------------ |
| onekeyirc 0.1.35, English source r1 | [Source .tar.gz](onekeyirc-0.1.35-english-source-r1.tar.gz)   | [Browse source](onekeyirc-0.1.35/)                     |
| Optional language pack r9           | [Language pack .tar.gz](onekeyirc-0.1.35-languages-r9.tar.gz) | [Browse language pack](onekeyirc-0.1.35-languages-r9/) |

The archives and extracted folders are two ways to obtain the same respective\
packages. Use the source alone for English. For the translated build, use the\
source together with the language pack.

These are source packages. Build instructions are below, and each extracted\
folder includes its own detailed README.

The application version, English source revision, and language-pack revision\
are tracked separately. This combination is **onekeyirc 0.1.35, English source**\
**r1, language pack r9**. The r9 pack includes translation updates and code\
changes for the optional Arabic display mode.

## What you can do

- Follow live chat while composing messages in a compact or fullscreen editor.
- Keep saved profiles for multiple servers and navigate channels and private\
  conversations from the keyboard.
- Use guided connection setup, command completion, and in-program help.
- Connect with verified TLS, including configured ZNC and soju bouncer profiles.
- Detach from a running session and return to it later.
- Choose custom key bindings and an optional review step before sending.
- Read translated interface labels, command explanations, and topic guides\
  after applying the language pack.

onekeyirc uses POSIX terminal interfaces directly. It has no curses or terminfo\
dependency. The [source README](onekeyirc-0.1.35/README.md) explains the interface\
and session behavior in more detail.

## Languages

English is included in the source. The optional r9 pack adds the following\
languages. Each installation link opens that language's own README; each help\
link opens its translated command and topic reference.

| Language   | Code | Installation                                          | Help reference                                                          |
| ---------- | ---- | ----------------------------------------------------- | ----------------------------------------------------------------------- |
| Indonesian | `id` | [README](onekeyirc-0.1.35-languages-r9/README.id.txt) | [Help](onekeyirc-0.1.35-languages-r9/overlay/doc/HELP-REFERENCE-id.txt) |
| Malay      | `ms` | [README](onekeyirc-0.1.35-languages-r9/README.ms.txt) | [Help](onekeyirc-0.1.35-languages-r9/overlay/doc/HELP-REFERENCE-ms.txt) |
| Turkish    | `tr` | [README](onekeyirc-0.1.35-languages-r9/README.tr.txt) | [Help](onekeyirc-0.1.35-languages-r9/overlay/doc/HELP-REFERENCE-tr.txt) |
| Spanish    | `es` | [README](onekeyirc-0.1.35-languages-r9/README.es.txt) | [Help](onekeyirc-0.1.35-languages-r9/overlay/doc/HELP-REFERENCE-es.txt) |
| Portuguese | `pt` | [README](onekeyirc-0.1.35-languages-r9/README.pt.txt) | [Help](onekeyirc-0.1.35-languages-r9/overlay/doc/HELP-REFERENCE-pt.txt) |
| French     | `fr` | [README](onekeyirc-0.1.35-languages-r9/README.fr.txt) | [Help](onekeyirc-0.1.35-languages-r9/overlay/doc/HELP-REFERENCE-fr.txt) |
| German     | `de` | [README](onekeyirc-0.1.35-languages-r9/README.de.txt) | [Help](onekeyirc-0.1.35-languages-r9/overlay/doc/HELP-REFERENCE-de.txt) |
| Russian    | `ru` | [README](onekeyirc-0.1.35-languages-r9/README.ru.txt) | [Help](onekeyirc-0.1.35-languages-r9/overlay/doc/HELP-REFERENCE-ru.txt) |
| Italian    | `it` | [README](onekeyirc-0.1.35-languages-r9/README.it.txt) | [Help](onekeyirc-0.1.35-languages-r9/overlay/doc/HELP-REFERENCE-it.txt) |
| Arabic     | `ar` | [README](onekeyirc-0.1.35-languages-r9/README.ar.txt) | [Help](onekeyirc-0.1.35-languages-r9/overlay/doc/HELP-REFERENCE-ar.txt) |

The r9 pack translates all prose in the fixed command and topic help tables.\
The full English manuals remain the complete reference: some contextual help,\
generated additions, and diagnostics still need translation. All ten\
translations are drafts awaiting native-speaker review. See the\
[pack overview](onekeyirc-0.1.35-languages-r9/README.txt) and\
[coverage notes](onekeyirc-0.1.35-languages-r9/overlay/doc/HELP-LOCALIZATION-COVERAGE.txt).

### Choose a language after building

Launch the translated build with a language code from the table. For example,\
from the source directory:

```sh
ONEKEYIRC_LANG=id ./onekeyirc
```

Replace `id` with the code you want. Use `ONEKEYIRC_LANG=en` for English.\
Language selection applies when a new process starts; an existing detached\
worker keeps the language it started with.

Use a UTF-8 locale, terminal, and font suitable for your language. The language\
override selects translated text; your system locale controls character\
handling. Command names such as `/join` and `/help`, configuration keys, and\
literal command arguments keep their original spelling.

## Build

Requirements:

- A C11 compiler, `make`, and a POSIX shell.
- OpenSSL-compatible TLS development headers and libraries.
- A maintained certificate-authority trust store for public TLS connections.
- An ANSI-compatible terminal; use UTF-8 for translated text.

See [INSTALL](onekeyirc-0.1.35/INSTALL) for platform requirements, alternate\
library paths, installation options, and upgrades.

Choose one of the following builds. Start in a clean working directory\
containing the downloaded archives. If you already have the fresh extracted\
folders, skip the `tar` commands.

### English build

```sh
tar -xzf onekeyirc-0.1.35-english-source-r1.tar.gz
cd onekeyirc-0.1.35
./configure
make
./onekeyirc
```

### Build with language pack r9

Apply r9 to a **fresh English source r1 tree**, before configuring or compiling.\
The supplied `apply.sh` script checks the expected source and rejects a tree\
that already has a language pack. When upgrading from an older pack, begin\
again with a fresh copy of English source r1.

```sh
tar -xzf onekeyirc-0.1.35-english-source-r1.tar.gz
tar -xzf onekeyirc-0.1.35-languages-r9.tar.gz
sh onekeyirc-0.1.35-languages-r9/apply.sh onekeyirc-0.1.35
cd onekeyirc-0.1.35
./configure
make
ONEKEYIRC_LANG=id ./onekeyirc
```

This builds the translated text into the executable. The language pack is\
applied to source before compilation. Choose the display language when you\
launch the resulting program.

The build leaves `onekeyirc` in the source directory. You can run it there with\
`./onekeyirc`, or follow [INSTALL](onekeyirc-0.1.35/INSTALL) for a per-user or\
system installation.

### Try the interface offline

From the build directory:

```sh
./onekeyirc --test-mode
```

Test mode uses a built-in simulator and makes no IRC connections. To preview\
the translated build in Spanish, for example:

```sh
ONEKEYIRC_LANG=es ./onekeyirc --test-mode
```

## First connection

Start `./onekeyirc` and dismiss the welcome screen. Type `/quickstart` inside\
the client for guided setup. Use `/panel` when configuring a bouncer profile.

For a direct connection, enter these commands one at a time inside the client,\
using your own hostname, nickname, and channel. `irc.example.org` is a\
placeholder. Wait for registration to complete before joining the channel.

```text
/connect -nick mynick irc.example.org 6697
/join #mychannel
```

New destinations default to verified TLS on port 6697. After joining, ordinary\
typing composes a message to the selected conversation. Check the destination\
before sending.

Use `/help` for command help and `/quit` to exit. Escape from the live watch\
view backgrounds the job. See the\
[first-session guide](onekeyirc-0.1.35/doc/onekeyirc-start.txt) and\
[session controls](onekeyirc-0.1.35/doc/SESSION-CONTROLS.txt) for the full workflow.

## Arabic terminal display

Arabic translation is included; terminal display support is experimental.\
The r9 pack provides optional letter shaping and right-to-left presentation\
for output, including the live conversation view.

On a UTF-8 terminal that does not already handle Arabic shaping and ordering,\
the optional mode can use the system FriBidi shared library:

```sh
ONEKEYIRC_LANG=ar ONEKEYIRC_ARABIC_VISUAL=1 ./onekeyirc
```

Leave `ONEKEYIRC_ARABIC_VISUAL` unset or set it to `0` when the terminal already\
handles Arabic. Input editing still uses logical order; visual right-to-left\
cursor movement and selection are unfinished. Actual appearance also depends\
on the terminal and font. Read the\
[Arabic terminal guide](onekeyirc-0.1.35-languages-r9/overlay/doc/ARABIC-TERMINALS.txt)\
for setup, fallback behavior, and wrapping and copying limitations.

## Documentation

| What you need                                      | Read                                                                            |
| -------------------------------------------------- | ------------------------------------------------------------------------------- |
| Detailed introduction and everyday use             | [Source README](onekeyirc-0.1.35/README.md)                                     |
| Dependencies, building, installation, and upgrades | [INSTALL](onekeyirc-0.1.35/INSTALL)                                             |
| Complete English program reference                 | [MANUAL.txt](onekeyirc-0.1.35/MANUAL.txt)                                       |
| Configuration examples                             | [Example configuration](onekeyirc-0.1.35/examples/onekeyirc.ini)                |
| Custom key bindings                                | [Key-binding guide](onekeyirc-0.1.35/doc/HOTKEYS.txt)                           |
| ZNC and soju connections                           | [Bouncer guide](onekeyirc-0.1.35/doc/BOUNCERS.txt)                              |
| OpenBSD service setup                              | [OpenBSD VPS guide](onekeyirc-0.1.35/doc/OPENBSD-VPS.txt)                       |
| Problems and recovery                              | [Troubleshooting](onekeyirc-0.1.35/doc/TROUBLESHOOTING.txt)                     |
| Operating assumptions and security reporting       | [SECURITY.md](onekeyirc-0.1.35/SECURITY.md)                                     |
| Current r9 language behavior and translation work  | [Localization guide](onekeyirc-0.1.35-languages-r9/overlay/doc/LOCALIZATION.md) |

Localized topic guides can also be browsed in the\
[language pack's documentation directory](onekeyirc-0.1.35-languages-r9/overlay/doc/).\
After applying the pack, those files are available under `doc/` in your source\
tree.

## Help improve onekeyirc

Bug reports, documentation corrections, and native-speaker translation reviews\
are welcome. For a useful report, include:

- Application version, English source revision, and language-pack revision.
- Operating system and terminal name.
- Selected language and whether optional Arabic visual mode is enabled.
- Steps to reproduce the problem, what happened, and what you expected.

For translation corrections, identify the language, file or command, original\
wording, and suggested replacement. The\
[localization guide](onekeyirc-0.1.35-languages-r9/overlay/doc/LOCALIZATION.md)\
describes the catalogs and checks. Developers can start with\
[CONTRIBUTING.md](onekeyirc-0.1.35/CONTRIBUTING.md) and the\
[source map](onekeyirc-0.1.35/doc/SOURCE-MAP.txt).

## License and project

Source is supplied under the included **IALA (Insha-Allah License Agreement)**.\
Read [LICENSE.md](onekeyirc-0.1.35/LICENSE.md) and\
[NOTICE](onekeyirc-0.1.35/NOTICE) for the supplied terms and dependency notices.

Published by [ThatIAm.vip](https://www.thatiam.vip).
