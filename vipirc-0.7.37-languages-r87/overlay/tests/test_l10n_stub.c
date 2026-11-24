/* English fallback for test fixtures that do not link src/idle_setup.c. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
const char *irc_tr(const char *text) { return text; }
