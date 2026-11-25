STANDALONE OPTIONAL LANGUAGE PACK 1.0-a FOR vipirc-1.0-a

Application version: 1.0-a (public alpha). English source revision: r1.
Languages: id ms tr es pt fr de ru it ar. 2676 keys per locale (installer translations are separate).
Based on RC4, descended from language revision r107. Localized configure messages and Unix manual pages for all ten languages. Updated the Arabic terminal guides.
Public alpha; not final stable 1.0. Community translation review and platform testing are invited.

REQUIRED INSTALL ORDER IN A FRESH DIRECTORY:
  tar -xzf vipirc-1.0-a.tar.gz
  tar -xzf vipirc-languages-1.0-a.tar.gz
  VIPIRC_LANG=fr sh vipirc-languages-1.0-a/apply.sh vipirc-1.0-a
  cd vipirc-1.0-a
  VIPIRC_LANG=fr ./configure && make
  VIPIRC_LANG=fr ./vipirc --version

Expected marker: [lang-rtl-1.0-a]
Extracting alone does not apply translations. Do not layer packs.
The installer validates all package files and replacement-source hashes.
Use a fresh, private extraction directory; verify the published archive hash
before extraction. If extraction fails, re-extract the original English source.

OPTIONAL CHECKS AFTER APPLYING:
  make check-dcc-command-l10n
  make check-milestone-notices-l10n
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
  python3 tests/test_full_manual_headings.py
  python3 tests/test_manual_ui_labels.py
  python3 tools/test_named_fields.py
  python3 tools/generate_localizations.py --check

Native-speaker signoff, full vipirc ncurses/Arabic terminal tests,
and live OpenBSD/GPG/IRC interoperability are outstanding.

  cc -std=c99 -O2 tests/test_welcome_l10n.c -o /tmp/irc-welcome-l10n
  VIPIRC_LANG=fr TEST_EXPECT_TRANSLATION=1 /tmp/irc-welcome-l10n

  python3 tests/test_setup_control_labels.py

  python3 tests/test_context_control_labels.py
  make check-context-setup-app

  python3 tests/test_settings_hint_lookup.py

  cc -std=c11 -O2 tests/test_prompt_display_l10n.c -o /tmp/irc-prompt-l10n
  VIPIRC_LANG=fr /tmp/irc-prompt-l10n

  cc -std=c99 -O2 tests/test_dcc_diagnostic_l10n.c -o /tmp/irc-dcc-l10n
  VIPIRC_LANG=fr /tmp/irc-dcc-l10n

  python3 tests/test_dcc_validation_catalog.py

RC2 changes: translated /summary, TLS/plaintext mismatch diagnostics and optional setup entry labels.
  make check-summary-l10n
  make check-transport-l10n
