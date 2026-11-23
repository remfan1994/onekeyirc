Optional language and Arabic RTL patch r43 for vipirc-0.7.37

English source r1 and application version are unchanged.
Standalone patch. Apply ONLY to a clean, matching English r1 tree.
Do NOT apply over r42, r41, any older overlay, or a configured tree.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r43.tar.gz
  sh vipirc-0.7.37-languages-r43/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  ./vipirc --version

Expected version marker: vipirc 0.7.37 [lang-rtl-r43]
Select a translation with VIPIRC_LANG=id|ms|tr|es|pt|fr|de|ru|it|ar.
Unset the environment variable (or use en) for original English display.

New in r43: 19 display-only source-linked messages per locale.
- vipirc: safe GPG credential-type status, full away confirmation,
  channel list and ignore persistence diagnostics, Settings headings,
  and two Settings plus two DCC context-help paragraphs.
- onekeyirc: complete command-response reader navigation, no-selection
  notice and a watch-log selected-item heading.
- Values such as nicknames, credential types, filenames and server text
  are displayed literally; they never become printf format strings.
- Protected IRC commands, configuration values and network text unchanged.

Run: make check-l10n-security; make check-localized-manuals
New r43 tests: make check-ui-localization-r43
Previous README versions are archived under HISTORY/.
See doc/UI-LOCALIZATION-r43.txt and its TSV/JSON key inventory.

Limits: complete vipirc ncurses build unverified without ncurses headers;
no native-speaker, OpenBSD/Crostini terminal or live IRC/DCC tests.
