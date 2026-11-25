/* Display-only helpers for background jobs. Never change the ASCII wire label,
 * registry identity, resume arguments, or terminal-handshake bytes. */
#ifndef IRC_BACKGROUND_DISPLAY_L10N_H
#define IRC_BACKGROUND_DISPLAY_L10N_H
#include "l10n.h"
#include "client.h"
#include "display_l10n.h"
#include <stdio.h>
#include <string.h>

static inline void irc_bg_startup_error(FILE *out, const char *program, const char *detail)
{
    char display[2048];
    const IrcDisplayField fields[] = {{"detail", detail}};
    const char *shown = irc_display_render(display, sizeof display,
        "background startup failed: {detail}", fields, 1) ? display : NULL;
    if (shown) fprintf(out, "%s: %s\n", program, shown);
    else fprintf(out, "%s: background startup failed: %s\n", program, detail);
}

/* The two complete commands are literal data. No translated flags. */
static inline int irc_bg_detach_notice(char *out, size_t cap, const char *program, unsigned job)
{
    char number[24], resume[96], sessions[96], first[768], last[1024];
    int n = snprintf(number, sizeof number, "%u", job);
    int r = snprintf(resume, sizeof resume, "%s --resume", program);
    int s = snprintf(sessions, sizeof sessions, "%s --sessions", program);
    if (n < 0 || (size_t)n >= sizeof number || r < 0 || (size_t)r >= sizeof resume ||
        s < 0 || (size_t)s >= sizeof sessions) return 0;
    const IrcDisplayField job_field[] = {{"job", number}};
    const IrcDisplayField commands[] = {{"resume", resume}, {"sessions", sessions}};
    if (!irc_display_render(first, sizeof first, "job {job} is running in the background.", job_field, 1) ||
        !irc_display_render(last, sizeof last, "Use {resume} for a sole detached job; {sessions} lists jobs.", commands, 2))
        return 0;
    if (!out || !cap) return 0;
    int len = snprintf(out, cap, "%s %s\n%s %s %s\n%s\n", program, first, irc_tr("Resume:"), resume, number, last);
    if (len < 0 || (size_t)len >= cap) { out[0] = 0; return 0; }
    return len;
}

/* Translate only the suffix of the stable worker label. Validate its complete
 * legacy shape and a bounded server count; preserve nickname/host bytes.
 * Workers still send exactly the same ASCII packet, including to old peers. */
static inline const char *irc_bg_job_label(const char *raw, char *out, size_t cap)
{
    if (!raw || !out || !cap || !strstr(raw, " @ ")) return raw;
    const char *open = strrchr(raw, '(');
    if (!open || open == raw || open[-1] != ' ') return raw;
    const char *digits = open + 1;
    size_t n = strspn(digits, "0123456789");
    if (n == 0 || n > 3 || (n > 1 && digits[0] == '0')) return raw;
    unsigned number = 0;
    for (size_t i = 0; i < n; ++i) number = number * 10u + (unsigned)(digits[i] - '0');
    if (number > CLIENT_MAX_SERVERS) return raw;
    const char *suffix = digits + n;
    int singular = n == 1 && digits[0] == '1';
    if (strcmp(suffix, singular ? " server)" : " servers)")) return raw;
    char count[4], rendered[256];
    memcpy(count, digits, n); count[n] = 0;
    const IrcDisplayField fields[] = {{"count", count}};
    if (!irc_display_render(rendered, sizeof rendered,
        singular ? "({count} server)" : "({count} servers)", fields, 1)) return raw;
    size_t prefix = (size_t)(open - raw), length = strlen(rendered);
    if (prefix >= cap || length >= cap - prefix) return raw;
    if (!strcmp(open, rendered)) return raw;
    memcpy(out, raw, prefix); memcpy(out + prefix, rendered, length + 1);
    return out;
}
#endif
