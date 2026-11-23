Optional language + Arabic RTL patch r30 for vipirc 0.7.37

REQUIRES: fresh English r1 source from vipirc-0.7.37-english-source-r1.tar.gz
Do NOT apply this patch to the older vipirc 0.7.36 source or over earlier patches.

    tar -xzf vipirc-0.7.37-english-source-r1.tar.gz
    tar -xzf vipirc-0.7.37-languages-r30.tar.gz
    sh vipirc-0.7.37-languages-r30/apply.sh vipirc-0.7.37
    cd vipirc-0.7.37
    ./configure
    make
    ./vipirc --version

Expected version: vipirc 0.7.37 [lang-rtl-r30]
Without the patch, English build version is simply vipirc 0.7.37.

Main application setting: Use signals (no by default).
  - minimalist: /panel -> panel 0 -> Use signals -> yes/no
  - full vipirc: Settings -> Interface -> Use signals -> on/off
  - Apply affects the active terminal; Save also writes config.
  - no: Ctrl-C and Ctrl-Z cancel unfinished Compose/Command text.
  - yes: terminal signal bindings can interrupt or suspend.
  - stored under [input] use_signals=true|false, owned by primary config.
  - force_terminal_signals accepted for backward compatibility when loading.
See doc/USE-SIGNALS-XX.txt for translated explanations.

The patch provides Indonesian, Malay, Turkish, Spanish, Portuguese, French,
German, Russian, Italian and Arabic localization, draft manuals, plus optional
Arabic bidirectional text/editing. The underlying English client works without
this patch. Set ONEKEYIRC_LANG or VIPIRC_LANG for your language; for terminal
shaping on terminals without their own bidi handling, select ARABIC_VISUAL=1.
Keep a UTF-8 locale for Unicode.

The bundled file list and replacement-source SHA-256 checksums are verified by
apply.sh. It refuses modified source, symlinks and reapplication. English
SECURITY.md remains the authoritative reference pending native-speaker review.
Previous package READMEs remain in HISTORY; do not use their commands.

Security translation progress in r30: ten complete draft sections per language; see doc/SECURITY-PARTIAL-XX.txt and doc/SECURITY-COVERAGE-r30.tsv. Not a security certification.
