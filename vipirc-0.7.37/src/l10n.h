/* English-only base distribution; optional locale overlay replaces this. */
#ifndef IRC_L10N_H
#define IRC_L10N_H
const char *irc_tr(const char *english);
#ifdef IRC_L10N_IMPLEMENTATION
const char *irc_tr(const char *english) { return english; }
#endif
#endif
