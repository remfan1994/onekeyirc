STANDALONE OPTIONAL LANGUAGE PACK r87 FOR vipirc-0.7.37

Application version: 0.7.37 (unchanged). English source revision: r1.
Languages: id ms tr es pt fr de ru it ar. 2437 keys per locale (+20).
New: ncurses navigation hints, manual-search geometry, draft-preservation notices and startup error.
No native-speaker validation is claimed.

REQUIRED INSTALL ORDER IN A FRESH DIRECTORY:
  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r87.tar.gz
  sh vipirc-0.7.37-languages-r87/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=fr ./vipirc --version

Expected marker: [lang-rtl-r87]
Extracting alone does not apply translations. Do not layer packs.

OPTIONAL CHECKS AFTER APPLYING:
  make check-ui-hint-l10n
  make check-context-reader-l10n
  make check-background-display-l10n
  make check-config-number-l10n
  make check-indirect-diagnostic-l10n
  make check-connection-bouncer-l10n
  make check-hotkey-watch-reader-l10n
  make check-server-directory-reader-l10n
  make check-conversation-reader-l10n
  make check-settings-path-l10n
  make check-auth-status-l10n
  make check-connection-ui-l10n
  make check-transfer-notice-l10n
  make check-dialog-status-l10n
  make check-additional-interface-l10n
  make check-uncommon-numeric-l10n
  make check-dynamic-diagnostic-l10n
  cc -std=c99 -O2 tests/test_terminal_ui_l10n.c -o /tmp/irc-ui-l10n
  VIPIRC_LANG=ar /tmp/irc-ui-l10n
  python3 tools/generate_localizations.py --check

Native-speaker signoff, full vipirc ncurses/Arabic terminal tests,
and live OpenBSD/GPG/IRC interoperability are outstanding.
