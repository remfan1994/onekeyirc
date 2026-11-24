/* Standalone fixtures without idle_setup.c use literal English.
 * Some manual/help fixtures provide their own strong irc_tr lookup. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
const char *irc_tr(const char *text) { return text; }
