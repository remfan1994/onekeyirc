/* Display-only command-entry helpers. Never change the logical command, its
 * arguments, or the protocol limit. Paint and hit testing share geometry. */
#ifndef IRC_PROMPT_DISPLAY_L10N_H
#define IRC_PROMPT_DISPLAY_L10N_H
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "l10n.h"
#include "display_l10n.h"
#include "ui_hint_l10n.h"

static inline int irc_prompt_cells(const char *p, int maximum)
{
    int cells = 0;
    mbstate_t state = {0};
    while (*p && cells < maximum) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, p, MB_CUR_MAX, &state);
        int w;
        if (n == (size_t)-1 || n == (size_t)-2 || !n) {
            n = 1; w = 1; memset(&state, 0, sizeof state);
        } else { w = wcwidth(wc); if (w < 0) w = 1; }
        if (w > maximum - cells) break;
        cells += w; p += n;
    }
    return cells;
}

static inline int irc_prompt_target(char *out, size_t cap, const char *target)
{
    const IrcDisplayField fields[] = {{"target", target}};
    return irc_display_render(out, cap, "On: {target}", fields, 1);
}

static inline int irc_prompt_bytes(char *out, size_t cap, size_t count)
{
    char value[32];
    (void)snprintf(value, sizeof value, "%zu", count);
    const IrcDisplayField fields[] = {{"count", value}};
    return irc_display_render(out, cap, "{count} bytes", fields, 1);
}

static inline int irc_prompt_budget(char *out, size_t cap, size_t limit)
{
    char value[32];
    (void)snprintf(value, sizeof value, "%zu", limit);
    const IrcDisplayField fields[] = {{"limit", value}};
    return irc_display_render(out, cap, "text budget <={limit}/message", fields, 1);
}

static inline int irc_prompt_range(char *out, size_t cap, size_t first,
                                   size_t shown, size_t total)
{
    char a[32], b[32], c[32];
    (void)snprintf(c, sizeof c, "%zu", total);
    if (shown) {
        (void)snprintf(a, sizeof a, "%zu", first + 1);
        (void)snprintf(b, sizeof b, "%zu", first + shown);
        const IrcDisplayField fields[] = {{"first", a}, {"last", b}, {"total", c}};
        return irc_display_render(out, cap, "Commands {first}-{last} of {total}", fields, 3);
    }
    if (total) {
        const IrcDisplayField fields[] = {{"total", c}};
        return irc_display_render(out, cap, "Commands {total}; resize", fields, 1);
    }
    const char *text = irc_tr("Commands (0 matches)");
    if (!cap) return 0;
    if (strlen(text) >= cap) { out[0] = 0; return 0; }
    memcpy(out, text, strlen(text) + 1);
    return 1;
}

/* Optional suffixes must fit in both the buffer and the available cells. */
static inline int irc_prompt_append(char *out, size_t cap, int columns, const char *suffix)
{
    size_t used = strlen(out), n = strlen(suffix);
    if (used >= cap || cap - used <= 3 || n >= cap - used - 3) return 0;
    int cells = irc_prompt_cells(out, INT_MAX);
    if (columns - cells < 3 || irc_prompt_cells(suffix, INT_MAX) > columns - cells - 3) return 0;
    memcpy(out + used, " | ", 3);
    memcpy(out + used + 3, suffix, n + 1);
    return 1;
}

typedef struct IrcPromptButtons { int run_width, cancel_left, cancel_width; } IrcPromptButtons;

static inline IrcPromptButtons irc_prompt_buttons(int columns)
{
    IrcPromptButtons b = {0, 0, 0};
    if (columns < 5) return b;
    int run = irc_prompt_cells(irc_tr("Run"), INT_MAX) + 2;
    int cancel = irc_prompt_cells(irc_tr("Cancel"), INT_MAX) + 2;
    /* Keep both controls visible even when translated labels are long. */
    int available = columns - 1;
    b.run_width = run;
    b.cancel_width = cancel;
    if (run + cancel > available) {
        if (b.run_width > available / 2) b.run_width = available / 2;
        if (b.cancel_width > available - b.run_width) b.cancel_width = available - b.run_width;
        if (b.run_width < run && b.run_width + b.cancel_width < available)
            b.run_width = available - b.cancel_width;
    }
    b.run_width = irc_ui_footer_width("Run", b.run_width, irc_prompt_cells);
    b.cancel_width = irc_ui_footer_width("Cancel", b.cancel_width, irc_prompt_cells);
    b.cancel_left = b.run_width + 1;
    return b;
}

typedef enum IrcPromptHit { IRC_PROMPT_NO_HIT, IRC_PROMPT_RUN_HIT, IRC_PROMPT_CANCEL_HIT } IrcPromptHit;
static inline IrcPromptHit irc_prompt_button_hit(IrcPromptButtons b, int x)
{
    if (x >= 0 && x < b.run_width) return IRC_PROMPT_RUN_HIT;
    if (x >= b.cancel_left && x - b.cancel_left < b.cancel_width) return IRC_PROMPT_CANCEL_HIT;
    return IRC_PROMPT_NO_HIT;
}
#endif
