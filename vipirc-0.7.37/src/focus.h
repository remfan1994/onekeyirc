#ifndef VIPIRC_FOCUS_H
#define VIPIRC_FOCUS_H

#include <curses.h>

/* The terminal owns its foreground and background. This interface needs
 * emphasis, not a color palette: starting curses color mode can replace the
 * terminal's defaults even when only a selected control uses the new pair.
 * Keep ordinary cells on pair zero and focus on bold printable outlines. */
static inline void vipirc_focus_colors(void)
{
    /* This is also called when a suspended/detached client acquires a new
     * terminal. Do not carry reverse video or a nondefault pair into its base. */
    (void)bkgd((chtype)' ');
    (void)attrset(A_NORMAL);
}

static inline attr_t vipirc_focus_attrs(void)
{
    return A_BOLD;
}

#endif
