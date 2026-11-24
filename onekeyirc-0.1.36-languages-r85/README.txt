STANDALONE OPTIONAL LANGUAGE PACK r85 FOR onekeyirc-0.1.36

Application version: 0.1.36 (unchanged). English source revision: r1.
Languages: id ms tr es pt fr de ru it ar. 2010 keys per locale (+2).
New: Summary report label and interactive terminal requirement.
No native-speaker validation is claimed.

REQUIRED INSTALL ORDER IN A FRESH DIRECTORY:
  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r85.tar.gz
  sh onekeyirc-0.1.36-languages-r85/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=fr ./onekeyirc --version

Expected marker: [lang-rtl-r85]
Extracting alone does not apply translations. Do not layer packs.

OPTIONAL CHECKS AFTER APPLYING:
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
  ONEKEYIRC_LANG=ar /tmp/irc-ui-l10n
  python3 tools/generate_localizations.py --check

Native-speaker signoff, full vipirc ncurses/Arabic terminal tests,
and live OpenBSD/GPG/IRC interoperability are outstanding.
