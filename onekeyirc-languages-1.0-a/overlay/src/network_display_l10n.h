/* Translate only locally-authored network errors at their display boundary.
 * Never reinterpret error details, server replies, filenames or TLS policy. */
#ifndef IRC_NETWORK_DISPLAY_L10N_H
#define IRC_NETWORK_DISPLAY_L10N_H
#include "l10n.h"
#include <stdio.h>
#include <string.h>
static void irc_localize_network_error(char *out, size_t cap, const char *raw)
{
    static const char *const prefixes[] = {
        "Cannot isolate resolver descriptors:",
        "Name lookup failed:",
        "Cannot inspect resolver child ownership:",
        "Cannot create resolver pipe:",
        "Cannot protect resolver descriptors:",
        "Cannot start resolver:",
        "Connection failed:",
        "TLS certificate rejected:",
        "Socket read failed:",
        "Socket write failed:",
    };
    if (!out || !cap) return;
    if (!raw) raw = "No connection";
    for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; ++i) {
        const size_t length = strlen(prefixes[i]);
        if (strncmp(raw, prefixes[i], length) == 0 && raw[length] == ' ') {
            /* Raw OS/TLS resolver detail is DATA in a fixed format, not a format string. */
            (void)snprintf(out, cap, "%s %s", irc_tr(prefixes[i]), raw + length + 1);
            return;
        }
    }
    (void)snprintf(out, cap, "%s", irc_tr(raw));
}
#endif
