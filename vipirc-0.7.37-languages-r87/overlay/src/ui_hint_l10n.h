/* Localize optional curses hints before measuring terminal cells, not bytes.
 * A narrow manual-search field drops its label but keeps both delimiters.
 * Use the same origin for rendering and cursor/mouse navigation. */
#ifndef IRC_UI_HINT_L10N_H
#define IRC_UI_HINT_L10N_H
#include <limits.h>
#include <stddef.h>
#include "l10n.h"
typedef int (*IrcUiTextCells)(const char *, int);
static inline const char *irc_ui_hint(const char *full, const char *compact,
                                     int width, IrcUiTextCells cells)
{
    if (width <= 0) return NULL;
    const char *text = full ? irc_tr(full) : NULL;
    if (text && cells(text, INT_MAX) <= width) return text;
    text = compact ? irc_tr(compact) : NULL;
    return text && cells(text, INT_MAX) <= width ? text : NULL;
}
static inline int irc_ui_search_start(int columns, IrcUiTextCells cells)
{
    int n = cells(irc_tr("Find"), INT_MAX);
    /* label + colon + space + '[' + at least 8 editing cells + ']' */
    return columns >= 12 && n <= columns - 12 ? n + 3 : 1;
}
#endif
