/* Display-only translation for known security diagnostics.
 * The credential and configuration helpers produce stable English errors and
 * enforce security policy. Translate only after those helpers return.
 * Pathnames, OS messages, command names and IRC wire bytes are never parsed
 * as translation formats or interpreted as settings.
 */
#ifndef IRC_SECURITY_DIAGNOSTIC_L10N_H
#define IRC_SECURITY_DIAGNOSTIC_L10N_H
#include "l10n.h"
#include "display_l10n.h"
#include "config_number_l10n.h"
#include <stdio.h>
#include <string.h>

typedef struct IrcSecurityPattern {
    const char *english;
    const char *separator;
    const char *label;
} IrcSecurityPattern;

static inline const char *irc_security_diagnostic_inner(const char *raw, char *out, size_t cap, int allow_nested)
{
    if (!raw) return "";
    const char *exact = irc_tr(raw);
    if (exact && strcmp(exact, raw) != 0) return exact;
    const char *numeric = irc_config_number_diagnostic(raw, out, cap);
    if (numeric != raw) return numeric;
    /* config_open_gpg_file wraps parent_dir failures with a full stop and
     * a displayed path. Match only the documented parent_dir producers,
     * localize that reason once, then append the untouched path. */
    if (allow_nested) {
        const char *separator = strstr(raw, ". GPG file: ");
        if (separator && separator > raw && (size_t)(separator - raw) < 512) {
            char reason[512], reason_display[1536];
            size_t n = (size_t)(separator - raw);
            memcpy(reason, raw, n); reason[n] = 0;
            if (!strcmp(reason, "Invalid or overlong file path") || !strcmp(reason, "Invalid file name") ||
                !strcmp(reason, "Parent traversal (..) is not allowed in file paths") ||
                !strcmp(reason, "Configuration directory must belong to you and have mode 0700") ||
                (!strncmp(reason, "Cannot open file directory: ", sizeof("Cannot open file directory: ") - 1) &&
                  reason[sizeof("Cannot open file directory: ") - 1]) ||
                (!strncmp(reason, "Cannot open file directory (symlinks are refused): ", sizeof("Cannot open file directory (symlinks are refused): ") - 1) &&
                  reason[sizeof("Cannot open file directory (symlinks are refused): ") - 1])) {
                const char *shown = irc_security_diagnostic_inner(reason, reason_display, sizeof reason_display, 0);
                const IrcDisplayField fields[] = {{"detail", shown}, {"path", separator + sizeof(". GPG file: ") - 1}};
                if (out && cap && irc_display_render(out, cap, "{detail}. GPG file: {path}", fields, 2) && strcmp(out, raw))
                    return out;
                return raw;
            }
        }
    }
    /* The fixed English text must start the diagnostic and end immediately
     * before the marker. Everything after the marker remains literal data. */
    static const IrcSecurityPattern patterns[] = {
        {"Enter a filename, not a directory (at most 255 bytes).", " Path: ", "Path:"},
        {"Parent traversal (..) is not allowed in credential paths.", " Path: ", "Path:"},
        {"Credential directory does not exist; choose the offered default or an existing private directory.", " Path: ", "Path:"},
        {"This credential path component is not a directory.", " Path: ", "Path:"},
        {"New credential directory must be owned by you with mode 0700.", " Path: ", "Path:"},
        {"Credential parent is not a directory.", " Path: ", "Path:"},
        {"All 64 default credential filenames exist; choose a new custom filename.", " Path: ", "Path:"},
        {"Only the offered default credential directory can be created automatically.", " Path: ", "Path:"},
        {"GPG encryption failed; check the local key and GPG installation. This credential file was not published.", " Path: ", "Path:"},
        {"GPG did not confirm complete protected encryption; this credential file was not published.", " Path: ", "Path:"},
        {"The newly created credential was changed or replaced; it was left untouched.", " Path: ", "Path:"},
        {"Cannot write and synchronize this encrypted credential; this file was not published.", " Path: ", "Path:"},
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

    /* Profile IDs are restricted to ASCII letters, digits, dots, dashes
     * and underscores by config.c. The first colon-space separates the ID
     * from the opaque loader detail. Only two exact fixed reasons are keys. */
    const char *profile_prefix = "Server profile ";
    size_t pn = strlen(profile_prefix);
    if (allow_nested && strncmp(raw, profile_prefix, pn) == 0) {
        const char *start = raw + pn;
        const char *sep = strstr(start, ": ");
        char profile[128];
        if (sep && sep > start && (size_t)(sep - start) < sizeof profile && sep[2]) {
            size_t len = (size_t)(sep - start);
            memcpy(profile, start, len);
            profile[len] = 0;
            if (strspn(profile, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") == len &&
                strspn(profile, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") > 0 &&
                !strstr(profile, "..")) {
                const char *reason = sep + 2;
                const char *key = "Server profile {profile}: {detail}";
                size_t count = 2;
                char reason_display[1024];
                const char *shown_reason = irc_security_diagnostic_inner(reason, reason_display, sizeof reason_display, 0);
                IrcDisplayField fields[2] = {{"profile", profile}, {"detail", shown_reason}};
                if (!strcmp(reason, "file is missing")) {
                    key = "Server profile {profile}: file is missing"; count = 1;
                } else if (!strcmp(reason, "server is required")) {
                    key = "Server profile {profile}: server is required"; count = 1;
                }
                if (out && cap && irc_display_render(out, cap, key, fields, count) && strcmp(out, raw))
                    return out;
            }
        }
        return raw;
    }

    /* The file browser appends a fixed instruction after the OS error.
     * Match the entire wrapper; never reinterpret the error as a format. */
    {
        const char prefix[] = "Cannot read the launch directory: ";
        const char suffix[] = ". Use Type path.";
        size_t length = strlen(raw), a = sizeof prefix - 1, b = sizeof suffix - 1;
        if (length > a + b && !strncmp(raw, prefix, a) &&
            !strcmp(raw + length - b, suffix)) {
            char detail[512];
            size_t n = length - a - b;
            if (n < sizeof detail) {
                memcpy(detail, raw + a, n); detail[n] = 0;
                const IrcDisplayField fields[] = {{"detail", detail}};
                if (out && cap && irc_display_render(out, cap,
                    "Cannot read the launch directory: {detail}. Use Type path.", fields, 1) && strcmp(out, raw))
                    return out;
            }
            return raw;
        }
    }

    /* These are producer-verified, display-only error prefixes. The dynamic
     * suffix is an OS error or already-sanitized path. Keep it verbatim.
     * For path-bearing messages split only at the producer's exact separator.
     * Do not translate protocol text or use untrusted text as a format string. */
    static const struct {
        const char *prefix;
        const char *marker;
        const char *english;
    } dynamic[] = {
        {"Cannot open directory: ", NULL, "Cannot open directory: {detail}"},
        {"Cannot open directory (symlinks refused): ", NULL, "Cannot open directory (symlinks refused): {detail}"},
        {"Cannot read directory: ", NULL, "Cannot read directory: {detail}"},
        {"Directory listing stopped: ", NULL, "Directory listing stopped: {detail}"},
        {"Configuration was created but directory synchronization failed: ", NULL, "Configuration was created but directory synchronization failed: {detail}"},
        {"Configuration was replaced but directory synchronization failed: ", NULL, "Configuration was replaced but directory synchronization failed: {detail}"},
        {"Cannot open file directory: ", NULL, "Cannot open file directory: {detail}"},
        {"Cannot open file directory (symlinks are refused): ", NULL, "Cannot open file directory (symlinks are refused): {detail}"},
        {"Cannot open private log file: ", NULL, "Cannot open private log file: {detail}"},
        {"Cannot inspect configuration path: ", NULL, "Cannot inspect configuration path: {detail}"},
        {"Cannot open private file (symlinks are refused): ", NULL, "Cannot open private file (symlinks are refused): {detail}"},
        {"Cannot read private file: ", NULL, "Cannot read private file: {detail}"},
        {"Cannot inspect existing configuration: ", NULL, "Cannot inspect existing configuration: {detail}"},
        {"Cannot create private configuration: ", NULL, "Cannot create private configuration: {detail}"},
        {"Cannot write configuration: ", NULL, "Cannot write configuration: {detail}"},
        {"Cannot flush configuration: ", NULL, "Cannot flush configuration: {detail}"},
        {"Cannot close new configuration: ", NULL, "Cannot close new configuration: {detail}"},
        {"Cannot create configuration without replacing an existing file: ", NULL, "Cannot create configuration without replacing an existing file: {detail}"},
        {"Configuration was created, but temporary-name cleanup failed: ", NULL, "Configuration was created, but temporary-name cleanup failed: {detail}"},
        {"Cannot replace configuration: ", NULL, "Cannot replace configuration: {detail}"},
        {"Cannot open server catalog lock: ", NULL, "Cannot open server catalog lock: {detail}"},
        {"Cannot lock server catalog: ", NULL, "Cannot lock server catalog: {detail}"},
        {"Cannot inspect new profile: ", NULL, "Cannot inspect new profile: {detail}"},
        {"Cannot open GPG credential file (symlinks are refused): ", " GPG file: ", "Cannot open GPG credential file (symlinks are refused): {detail} GPG file: {path}"},
        {"Cannot inspect the GPG credential file: ", " GPG file: ", "Cannot inspect the GPG credential file: {detail} GPG file: {path}"},
        {"Cannot determine the absolute credential location: ", NULL, "Cannot determine the absolute credential location: {detail}"},
        {"Cannot open credential directory: ", " Path: ", "Cannot open credential directory: {detail} Path: {path}"},
        {"Cannot create private credential directory: ", " Path: ", "Cannot create private credential directory: {detail} Path: {path}"},
        {"Cannot inspect credential directory: ", " Path: ", "Cannot inspect credential directory: {detail} Path: {path}"},
        {"Cannot check the credential destination: ", " Path: ", "Cannot check the credential destination: {detail} Path: {path}"},
        {"Cannot check default credential names: ", " Path: ", "Cannot check default credential names: {detail} Path: {path}"},
        {"Cannot create a private encrypted credential file: ", " Path: ", "Cannot create a private encrypted credential file: {detail} Path: {path}"},
        {"Cannot publish this credential: ", " No existing file was replaced. Path: ", "Cannot publish this credential: {detail} No existing file was replaced. Path: {path}"},
    };
    for (size_t i = 0; i < sizeof dynamic / sizeof dynamic[0]; ++i) {
        size_t n = strlen(dynamic[i].prefix);
        if (strncmp(raw, dynamic[i].prefix, n) != 0) continue;
        const char *detail_start = raw + n;
        if (!*detail_start) continue;
        IrcDisplayField fields[2] = {{"detail", detail_start}, {"path", ""}};
        size_t count = 1;
        char detail[512];
        if (dynamic[i].marker) {
            const char *separator = strstr(detail_start, dynamic[i].marker);
            if (!separator || separator == detail_start) continue;
            size_t length = (size_t)(separator - detail_start);
            if (length >= sizeof detail) continue;
            memcpy(detail, detail_start, length);
            detail[length] = 0;
            const char *path = separator + strlen(dynamic[i].marker);
            if (!*path) continue;
            fields[0].value = detail;
            fields[1].value = path;
            count = 2;
        }
        /* If a locale entry or the destination is unsuitable, keep the
         * original complete error, never a truncated translation. */
        if (out && cap &&
            irc_display_render(out, cap, dynamic[i].english, fields, count)) {
            if (strcmp(out, raw) != 0) return out;
        }
        return raw;
    }
    return raw;
}
/* One level of nested diagnostics is sufficient for the real producers. */
static inline const char *irc_security_diagnostic(const char *raw, char *out, size_t cap)
{
    return irc_security_diagnostic_inner(raw, out, cap, 1);
}
#endif
