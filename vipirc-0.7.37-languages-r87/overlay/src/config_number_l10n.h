/* Display-only parser for fixed configuration diagnostics. Numeric and
 * configuration identifiers are not translated or interpreted as formats.
 * Unknown/truncated messages retain their original text. */
#ifndef IRC_CONFIG_NUMBER_L10N_H
#define IRC_CONFIG_NUMBER_L10N_H
#include "l10n.h"
#include "display_l10n.h"
#include <string.h>

static inline int irc_config_field_valid(const char *s)
{
    static const char *const names[] = {
        "server", "nick", "username", "realname", "channel", "auth", "auth_user",
        "password", "server_password", "password_file", "password_gpg_file",
        "server_password_file", "server_password_gpg_file", "ca_file", "part_message",
        "quit_message", "background_away_message", "autoaway_message",
        "bouncer.username", "bouncer.network", "bouncer.client"
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i)
        if (!strcmp(s, names[i])) return 1;
    const char *index = NULL;
    if (!strncmp(s, "mentions.word[", 14)) index = s + 14;
    else if (!strncmp(s, "ignore.mask[", 12)) index = s + 12;
    if (!index) return 0;
    size_t n = strspn(index, "0123456789");
    return n && index[0] != '0' && index[n] == ']' && index[n+1] == 0;
}

/* Match literal segments and capture only the known producer's field types.
 * A path is the final field; its original bytes, including spaces and braces,
 * survive rendering. No untrusted format strings or numeric conversion. */
static inline int irc_config_number_match(const char *raw, const char *key,
                                          char *out, size_t cap, const char *(*translate)(const char *))
{
    const char *r = raw, *k = key;
    char names[4][16], values[4][64];
    IrcDisplayField fields[4];
    size_t count = 0;
    while (*k) {
        if (*k != '{') {
            if (*r != *k) return 0;
            ++r; ++k;
            continue;
        }
        const char *end = strchr(++k, '}');
        if (!end || count == 4 || (size_t)(end-k) >= sizeof names[0]) return 0;
        size_t len = (size_t)(end-k);
        memcpy(names[count], k, len); names[count][len] = 0;
        k = end + 1;
        fields[count].name = names[count];
        if (!strcmp(names[count], "path")) {
            if (*k || !*r) return 0;
            fields[count++].value = r;
            r += strlen(r);
            break;
        }
        if (!strcmp(names[count], "field")) {
            len = strspn(r, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._[]");
        } else if (!strcmp(names[count], "mode")) {
            len = strspn(r, "01234567");
            if (len != 4) return 0;
        } else {
            size_t sign = !strcmp(names[count], "received") && *r == '-' ? 1 : 0;
            len = strspn(r+sign, "0123456789");
            if (!len || len > 20) return 0;
            len += sign;
        }
        if (!len || len >= sizeof values[0]) return 0;
        memcpy(values[count], r, len); values[count][len] = 0;
        if (!strcmp(names[count], "field") && !irc_config_field_valid(values[count])) return 0;
        fields[count].value = values[count];
        ++count; r += len;
    }
    if (*r || !count) return 0;
    return irc_display_render_lookup(out, cap, key, fields, count, translate);
}

static inline const char *irc_config_number_diagnostic_lookup(const char *raw, char *out, size_t cap,
                                                       const char *(*translate)(const char *))
{
    static const char *const keys[] = {
        "Invalid or unknown configuration entry at line {line}",
        "Invalid server catalog entry at line {line}",
        "Default server at catalog line {line} is not a listed profile",
        "port must be between 1 and 65535 (received {received})",
        "history lines must be between 50 and 5000 (received {received})",
        "At most {limit} custom key bindings are allowed",
        "autocommands.command accepts at most {limit} entries",
        "mentions.word accepts at most {limit} entries",
        "ignore.mask accepts at most {limit} entries",
        "Invalid automatic command at position {position}; use /msg, /join, /nick, /away, /back or /whois",
        "{field} exceeds its text limit",
        "{field} cannot contain control characters",
        "{field} cannot begin or end with spaces",
        "{field} exceeds its byte limit",
        "{field} cannot contain control bytes",
        "The GPG credential is owned by UID {owner}; your UID is {uid}. GPG file: {path}",
        "The GPG credential has mode {mode}; use private mode 0600 or 0400. GPG file: {path}"
    };
    if (!raw || !out || !cap) return raw;
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; ++i) {
        if (irc_config_number_match(raw, keys[i], out, cap, translate))
            return strcmp(raw, out) ? out : raw;
    }
    return raw;
}
static inline const char *irc_config_number_diagnostic(const char *raw, char *out, size_t cap)
{
    return irc_config_number_diagnostic_lookup(raw, out, cap, irc_tr);
}
#endif
