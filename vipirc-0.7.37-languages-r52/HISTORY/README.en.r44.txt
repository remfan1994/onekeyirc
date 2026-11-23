Optional language and Arabic RTL patch r44 for vipirc-0.7.37

English source r1 and application version are unchanged.
Standalone patch. Apply ONLY to a clean, matching English r1 tree.
Do NOT apply over r43, r42, any older overlay, or a configured tree.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r44.tar.gz
  sh vipirc-0.7.37-languages-r44/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  ./vipirc --version

Expected version marker: vipirc 0.7.37 [lang-rtl-r44]
Select a translation with VIPIRC_LANG=id|ms|tr|es|pt|fr|de|ru|it|ar.
Unset the environment variable (or use en) for original English display.

New in r44: 23 new source-linked strings per locale.
- vipirc: translated main-menu, command suggestion and key binding help;
  local channel versus network LIST distinctions; context help for server,
  joined/unjoined channels, private conversations and unread activity;
  ignore-mask confirmation and literal-field topic, counts, status text.
- onekeyirc: Quickstart setup/help paragraphs, selected help-screen title,
  and the message review scroll position using safe named fields.
- Variable data such as nicknames, server replies, channel topics and counts
  is inserted literally; never used as a printf format string.
- Commands, configuration values, IRC protocol, settings and app versions
  are unchanged. English remains the fallback.

Run: make check-l10n-security; make check-localized-manuals
New r44 tests: make check-ui-localization-r44
Previous README versions are archived under HISTORY/.
See doc/UI-LOCALIZATION-r44.txt and its TSV/JSON key inventory.

Limits: Complete vipirc ncurses build unverified without development headers;
no native-speaker, OpenBSD/Crostini terminal or live IRC/DCC tests.
