/* Display-only translation for known security diagnostics.
 * The credential and configuration helpers produce stable English errors and
 * enforce security policy. Translate only after those helpers return.
 * Pathnames, OS messages, command names and IRC wire bytes are never parsed
 * as translation formats or interpreted as settings.
 */
#ifndef IRC_SECURITY_DIAGNOSTIC_L10N_H
#define IRC_SECURITY_DIAGNOSTIC_L10N_H
#include "l10n.h"
#include <stdio.h>
#include <string.h>

typedef struct IrcSecurityPattern {
    const char *english;
    const char *separator;
    const char *label;
} IrcSecurityPattern;

static inline const char *irc_security_diagnostic(const char *raw, char *out, size_t cap)
{
    if (!raw) return "";
    const char *exact = irc_tr(raw);
    if (exact && strcmp(exact, raw) != 0) return exact;
    /* The fixed English text must start the diagnostic and end immediately
     * before the marker. Everything after the marker remains literal data. */
    static const IrcSecurityPattern patterns[] = {
        {"Symlink credential directories are refused.", " Path: ", "Path:"},
        {"Credential path already exists; choose a new filename. Existing files and symlinks are never replaced.", " Path: ", "Path:"},
        {"GPG encryption timed out; this credential file was not published.", " Path: ", "Path:"},
        {"The selected local GPG key is missing, expired, revoked, or unusable for encryption.", " Path: ", "Path:"},
        {"The credential directory changed; cancel and reopen the panel.", " Path: ", "Path:"},
        {"The encrypted credential exists, but its directory could not be synchronized; retry Finish.", " Path: ", "Path:"},
        {"The GPG credential file does not exist.", " GPG file: ", "GPG file:"},
        {"Symlink GPG credential files are refused.", " GPG file: ", "GPG file:"},
        {"The GPG credential must be a regular file.", " GPG file: ", "GPG file:"},
        {"The GPG credential must contain 1 to 65536 encrypted bytes.", " GPG file: ", "GPG file:"},
    };
    for (size_t i = 0; i < sizeof patterns / sizeof patterns[0]; ++i) {
        const IrcSecurityPattern *p = &patterns[i];
        size_t n = strlen(p->english), m = strlen(p->separator);
        /* Avoid reading beyond a short diagnostic, including string literals
         * used in standalone tests and truncated error buffers. */
        if (strlen(raw) < n + m || strncmp(raw, p->english, n) != 0 ||
            strncmp(raw + n, p->separator, m) != 0)
            continue;
        const char *translated = irc_tr(p->english);
        if (!translated || strcmp(translated, p->english) == 0) return raw;
        const char *label = irc_tr(p->label);
        const char *path = raw + n + m;
        /* Do not truncate a UTF-8 filename or a diagnostic mid-sequence.
         * On overflow, preserve the original producer message. */
        if (!out || !cap) return raw;
        size_t total = strlen(translated) + 1u + strlen(label) + 1u + strlen(path);
        if (total >= cap) return raw;
        (void)snprintf(out, cap, "%s %s %s", translated, label, path);
        return out;
    }
    return raw;
}
#endif
