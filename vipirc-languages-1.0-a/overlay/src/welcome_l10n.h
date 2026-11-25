/* Startup text is display data, not protocol. Keep the URL and key names
 * literal; use English on a non-UTF-8 terminal to avoid corrupted warnings. */
#ifndef IRC_WELCOME_L10N_H
#define IRC_WELCOME_L10N_H
#include "l10n.h"
static inline const char *irc_welcome_warning(int utf8)
{
    const char *key = "Not vegetarian? www.bloodguiltcurse.net will change your mind";
    return utf8 ? irc_tr(key) : key;
}
static inline const char *irc_welcome_prompt(int utf8)
{
    const char *key = "Press a key to start.";
    return utf8 ? irc_tr(key) : key;
}
#endif
