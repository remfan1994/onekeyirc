/* Display-only localization for standard, less common IRC numerics.
 * Accept only the conventional parameter count: network-specific variants
 * and truncated replies retain the existing generic server display. The
 * recipient is routing metadata; all remaining fields are literal data.
 * No state transitions, commands or server-supplied text are translated. */
#ifndef IRC_UNCOMMON_NUMERIC_L10N_H
#define IRC_UNCOMMON_NUMERIC_L10N_H
#include "l10n.h"
#include "display_l10n.h"

typedef struct IrcUncommonNumeric {
    int code;
    size_t count;
    const char *english;
    const char *names[3];
} IrcUncommonNumeric;

static const IrcUncommonNumeric irc_uncommon_numerics[] = {
    {263, 2, "Server is busy; try {command} again: {message}", {"command", "message"}},
    {315, 2, "End of WHO for {target}: {message}", {"target", "message"}},
    {341, 2, "Invited {nick} to {channel}.", {"nick", "channel"}},
    {369, 2, "End of WHOWAS for {nick}: {message}", {"nick", "message"}},
    {406, 2, "No nickname history for {nick}: {message}", {"nick", "message"}},
    {411, 1, "No recipient supplied: {message}", {"message"}},
    {412, 1, "No message text supplied: {message}", {"message"}},
    {431, 1, "No nickname supplied: {message}", {"message"}},
    {436, 2, "Nickname collision for {nick}: {message}", {"nick", "message"}},
    {441, 3, "{nick} is not on {channel}: {message}", {"nick", "channel", "message"}},
    {442, 2, "You are not on {channel}: {message}", {"channel", "message"}},
    {443, 3, "{nick} is already on {channel}: {message}", {"nick", "channel", "message"}},
    {451, 1, "Not registered with the server: {message}", {"message"}},
    {462, 1, "Already registered with the server: {message}", {"message"}},
    {463, 1, "Host not allowed to connect: {message}", {"message"}},
    {464, 1, "Server password rejected: {message}", {"message"}},
    {465, 1, "Server access denied: {message}", {"message"}},
    {467, 2, "Channel key is already set for {channel}: {message}", {"channel", "message"}},
    {472, 2, "Unknown channel mode {mode}: {message}", {"mode", "message"}},
    {481, 1, "Permission denied: {message}", {"message"}},
    {501, 1, "Unknown user mode flag: {message}", {"message"}},
    {502, 1, "Cannot change another user's modes: {message}", {"message"}},
};

static int irc_uncommon_numeric_render(char *out, size_t cap, int code,
                                      char *const *params, size_t n)
{
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!params || !n || !params[0]) return 0;
    for (size_t i = 0; i < sizeof irc_uncommon_numerics / sizeof irc_uncommon_numerics[0]; ++i) {
        const IrcUncommonNumeric *entry = &irc_uncommon_numerics[i];
        if (entry->code != code) continue;
        if (n != entry->count + 1) return 0;
        IrcDisplayField fields[3];
        for (size_t j = 0; j < entry->count; ++j) {
            if (!params[j + 1]) return 0;
            fields[j].name = entry->names[j];
            fields[j].value = params[j + 1];
        }
        return irc_display_render(out, cap, entry->english, fields, entry->count);
    }
    return 0;
}
#endif
