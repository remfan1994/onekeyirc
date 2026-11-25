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

/* A compact chooser has a two-cell arrow on each side of one button.
 * Use this same geometry for paint and hit testing. A translated label's
 * UTF-8 byte count is unrelated to the number of terminal cells it occupies.
 * Reserve room for brackets and arrows before measuring a long label. */
typedef struct IrcUiCompactChooser {
    int left, width;
} IrcUiCompactChooser;

static inline IrcUiCompactChooser irc_ui_compact_chooser(const char *label,
                                                         int columns,
                                                         IrcUiTextCells cells)
{
    IrcUiCompactChooser result = {0, 0};
    if (columns < 6) return result;
    result.width = cells(label, columns - 6) + 2;
    result.left = (columns - result.width - 4) / 2;
    return result;
}

typedef enum IrcUiChooserHit {
    IRC_UI_CHOOSER_NONE, IRC_UI_CHOOSER_PREV,
    IRC_UI_CHOOSER_ACTIVATE, IRC_UI_CHOOSER_NEXT
} IrcUiChooserHit;

static inline IrcUiChooserHit irc_ui_compact_chooser_hit(IrcUiCompactChooser layout,
                                                         int x)
{
    if (layout.width < 2 || x < layout.left ||
        x >= layout.left + layout.width + 4) return IRC_UI_CHOOSER_NONE;
    if (x < layout.left + 2) return IRC_UI_CHOOSER_PREV;
    if (x < layout.left + layout.width + 2) return IRC_UI_CHOOSER_ACTIVATE;
    return IRC_UI_CHOOSER_NEXT;
}
/* Footer buttons share a translated, bounded cell width for paint and hit. */
static inline int irc_ui_footer_width(const char *key, int columns, IrcUiTextCells cells)
{
    if (columns < 2) return 0;
    return cells(irc_tr(key), columns - 2) + 2;
}
static inline int irc_ui_footer_hit(const char *key, int columns, int x, IrcUiTextCells cells)
{
    return x >= 0 && x < irc_ui_footer_width(key, columns, cells);
}
#endif
