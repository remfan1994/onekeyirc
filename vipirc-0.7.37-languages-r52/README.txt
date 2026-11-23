OPTIONAL GUIDED SETUP LANGUAGE OVERLAY r52 - vipirc-0.7.37

English r1 original and application version 0.7.37 remain unchanged.
Apply only to a freshly extracted matching English source r1. NEVER layer
on r51 or any older locale pack, and never apply to a configured tree.

  tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
  tar -xzf vipirc-0.7.37-languages-r52.tar.gz
  sh vipirc-0.7.37-languages-r52/apply.sh vipirc-0.7.37
  cd vipirc-0.7.37
  ./configure && make
  VIPIRC_LANG=fr ./vipirc --version
  VIPIRC_LANG=es ./vipirc --version

Expected: vipirc 0.7.37 [lang-rtl-r52]
Supported locales: id ms tr es pt fr de ru it ar.

This release source-links 27 guided setup help messages. In onekeyirc,
11 were already in the locale catalog and are reused without changing
translations; in vipirc, the 27 were missing from the catalog.

Only display data is translated, not config choices, sensitive credentials,
validation, network protocols or executable behavior. Do not interpret
text translations as printf format strings.

Run: make check-setup-help-r52 && make check-l10n-security
In onekeyirc also: make check-localized-manuals && make check-arabic-visual
Vipirc full ncurses build not verified in this container; test on a system
with curses development headers. Fluent language review remains necessary.
