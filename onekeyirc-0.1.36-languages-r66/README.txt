STANDALONE OPTIONAL LANGUAGE PACK r66 FOR onekeyirc-0.1.36

Application version: 0.1.36 (unchanged); clean English source r1 required.
Languages: id ms tr es pt fr de ru it ar.
1679 catalog keys per language; 21 new numeric/WHOIS display messages.
Localized WHOIS identity/idle/account/channel details, channel directory,
topic metadata, modes, MOTD and server notices. Nicknames, channels,
server-supplied messages and IRC protocol identifiers remain literal.

NEW IN THIS PACK: 21 translated WHOIS, channel-list, topic, and numeric
reply display templates across ten languages. Raw server data stays literal.

REQUIRED INSTALL ORDER (in a NEW directory):
  tar -xzf onekeyirc-0.1.36-english-source-r1.tar.gz
  tar -xzf onekeyirc-0.1.36-languages-r66.tar.gz
  sh onekeyirc-0.1.36-languages-r66/apply.sh onekeyirc-0.1.36
  cd onekeyirc-0.1.36
  ./configure && make
  ONEKEYIRC_LANG=fr ./onekeyirc --version

Expected: onekeyirc 0.1.36 [lang-rtl-r66]
Extracting the patch without apply.sh does NOT activate translations.
DO NOT apply this patch atop any previous language overlay.

After applying, verify translated entries:
  python3 tools/generate_localizations.py --check
  cc -std=c11 -O2 -Wall -Wextra -Werror -Isrc -o /tmp/onekeyirc-event-l10n tests/test_conversation_event_l10n.c
  ONEKEYIRC_LANG=fr /tmp/onekeyirc-event-l10n
  ONEKEYIRC_LANG=ar /tmp/onekeyirc-event-l10n

Additional source-linked checks after applying the pack:
  cc -std=c11 -O2 -Wall -Wextra -Werror -Isrc -o /tmp/irc-numeric-test tests/test_numeric_reply_l10n.c
  ONEKEYIRC_LANG=fr /tmp/irc-numeric-test
  ONEKEYIRC_LANG=ar /tmp/irc-numeric-test

Not verified: complete vipirc ncurses UI; live IRC/GPG/bouncer/DCC; native-speaker
signoffs; all Arabic terminal layouts. See release report for detailed coverage.
