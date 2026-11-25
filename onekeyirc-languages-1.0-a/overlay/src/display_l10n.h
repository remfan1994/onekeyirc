/* Localized, display-only named-field rendering for short UI statuses.
 * The translation is never passed to printf. Field values remain literal,
 * including % signs, braces, user names, server text and file paths.
 * A locale with missing/repeated/unknown fields falls back to English;
 * output that cannot fit is rejected rather than splitting UTF-8 bytes.
 * Keep this helper separate from IRC protocol and config serialization. */
#ifndef IRC_DISPLAY_L10N_H
#define IRC_DISPLAY_L10N_H
#include <stddef.h>
#include <string.h>

typedef struct IrcDisplayField {
    const char *name;
    const char *value;
} IrcDisplayField;

static int irc_display_fields_valid(const char *s, const IrcDisplayField *fields, size_t count)
{
    unsigned seen[8] = {0};
    if (!s || !fields || count == 0 || count > 8) return 0;
    for (size_t i = 0; i < count; ++i)
        if (!fields[i].name || !fields[i].value || !*fields[i].name) return 0;
    for (const char *p = s; *p;) {
        if (*p == '}') return 0;
        if (*p != '{') { ++p; continue; }
        const char *end = strchr(++p, '}');
        if (!end || end == p) return 0;
        size_t i;
        for (i = 0; i < count; ++i)
            if (strlen(fields[i].name) == (size_t)(end - p) &&
                memcmp(fields[i].name, p, (size_t)(end - p)) == 0) break;
        if (i == count || ++seen[i] > 1) return 0;
        p = end + 1;
    }
    for (size_t i = 0; i < count; ++i) if (seen[i] != 1) return 0;
    return 1;
}

static inline int irc_display_render_lookup(char *out, size_t cap, const char *english,
                              const IrcDisplayField *fields, size_t count,
                              const char *(*translate)(const char *))
{
    if (!out || !cap) return 0;
    out[0] = '\0';
    const char *s = translate ? translate(english) : english;
    if (!irc_display_fields_valid(s, fields, count)) s = english;
    if (!irc_display_fields_valid(s, fields, count)) return 0;
    size_t used = 0;
    for (const char *p = s; *p;) {
        const char *word;
        size_t length;
        if (*p == '{') {
            const char *end = strchr(p + 1, '}');
            size_t i;
            for (i = 0; i < count; ++i)
                if (strlen(fields[i].name) == (size_t)(end - p - 1) &&
                    memcmp(fields[i].name, p + 1, (size_t)(end - p - 1)) == 0) break;
            if (i == count) { out[0] = 0; return 0; }
            word = fields[i].value;
            length = strlen(word);
            p = end + 1;
        } else {
            word = p++;
            length = 1;
        }
        if (length >= cap - used) { out[0] = 0; return 0; }
        memcpy(out + used, word, length);
        used += length;
    }
    out[used] = 0;
    return 1;
}
static inline int irc_display_render(char *out, size_t cap, const char *english,
                              const IrcDisplayField *fields, size_t count)
{
    return irc_display_render_lookup(out, cap, english, fields, count, irc_tr);
}
#endif /* IRC_DISPLAY_L10N_H */
