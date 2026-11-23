#ifndef VIPIRC_EDITOR_CURSES_H
#define VIPIRC_EDITOR_CURSES_H
/* The full frontend paints the same visual map used by editor navigation.
 * This helper is deliberately presentation-only: callbacks never write to the
 * logical draft. Secret fields keep their existing masked/logical renderer. */
#include "editor.h"
#include "arabic_visual.h"
#include <curses.h>
#include <limits.h>

/* Write a base and its marks as one ncurses cell. Writing the base first can
 * leave the cursor on the preceding cell after bottom-right wrap fails;
 * adding marks afterward would then decorate a different character. The
 * terminal's cchar_t storage bounds the number of marks it can represent. */
static inline void irc_curses_cluster_at(int y, int x,
        const wchar_t *text, size_t count, attr_t attrs, short pair)
{
    wchar_t characters[CCHARW_MAX + 1];
    cchar_t cell;
    size_t used = count < CCHARW_MAX ? count : CCHARW_MAX;
    if (!used) return;
    for (size_t i = 0; i < used; ++i) characters[i] = text[i];
    characters[used] = 0;
    if (setcchar(&cell, characters, attrs, pair, NULL) == ERR) return;
    (void)attr_set(attrs, pair, NULL);
    /* ncurses can return ERR after writing the bottom-right cell because the
     * cursor cannot advance. The complete cell has already been placed. */
    (void)mvadd_wch(y, x, &cell);
}

/* Paint complete clusters from the paragraph map without shaping its wrapped
 * rows again. A clipped base always clips its marks as well. */
static inline void irc_curses_arabic_paint(const IrcArabicLayout *layout,
        int y, int x, int width, int skip, int rows)
{
    attr_t attrs = A_NORMAL;
    short pair = 0;
    (void)attr_get(&attrs, &pair, NULL);
    for (size_t i = 0; i < layout->glyph_count;) {
        const IrcArabicGlyph *glyph = &layout->glyphs[i];
        size_t end = i + 1;
        while (end < layout->glyph_count && layout->glyphs[end].row == glyph->row &&
                layout->glyphs[end].col == glyph->col &&
                layout->glyphs[end].first == glyph->first &&
                layout->glyphs[end].past == glyph->past) ++end;
        int py = y + glyph->row - skip, px = x + glyph->col;
        if (glyph->row >= skip && glyph->row - skip < rows &&
            py >= 0 && py < LINES && glyph->col >= 0 && glyph->col < width &&
            px >= 0 && px < COLS && glyph->cells > 0 &&
            glyph->cells <= width - glyph->col && glyph->cells <= COLS - px) {
            wchar_t text[CCHARW_MAX];
            size_t count = end - i;
            if (count > CCHARW_MAX) count = CCHARW_MAX;
            for (size_t j = 0; j < count; ++j)
                text[j] = (wchar_t)layout->glyphs[i + j].codepoint;
            irc_curses_cluster_at(py, px, text, count, attrs, pair);
        }
        i = end;
    }
}

typedef struct IrcCursesEditorPosition {
    int total, row, col, first, span;
} IrcCursesEditorPosition;

typedef struct IrcCursesEditorPaint {
    int top, left, width, height, first;
    int caret_row, caret_col, caret_span;
    attr_t attrs;
} IrcCursesEditorPaint;

static inline void irc_curses_editor_cluster(void *context, int row, int col,
        const wchar_t *text, size_t count, int cells,
        size_t logical_first, size_t logical_past)
{
    IrcCursesEditorPaint *paint = context;
    int y = paint->top + row - paint->first, x = paint->left + col;
    (void)logical_first;
    (void)logical_past;
    if (row < paint->first || row - paint->first >= paint->height ||
        y < 0 || y >= LINES || col < 0 || col >= paint->width ||
        x < 0 || x >= COLS || cells < 0 || cells > paint->width - col ||
        cells > COLS - x || count > INT_MAX) return;
    irc_curses_cluster_at(y, x, text, count, paint->attrs,
                          (short)PAIR_NUMBER(paint->attrs));
    if (row == paint->caret_row && cells > 0 &&
        paint->caret_col >= col && paint->caret_col < col + cells) {
        /* A hardware block caret or reverse attribute must not split the
         * second cell of a wide glyph. The logical index remains unchanged. */
        paint->caret_col = col;
        paint->caret_span = cells;
    }
}

/* Return zero before painting when visual conversion is unavailable, so the
 * caller can retain its original logical renderer. A temporary visual scroll
 * calculation is published only after a mapped frame succeeds. */
static inline int irc_curses_editor_visual(const Editor *editor, int width,
        int top, int left, int height, int *scroll, attr_t attrs,
        IrcCursesEditorPosition *position)
{
    int total, row, col, first;
    if (!editor || !scroll || !position || width < 1 || height < 1) return 0;
    EdVisualSnapshot *snapshot = ed_visual_snapshot(editor, width);
    if (!snapshot) return 0;
    ed_visual_snapshot_layout(snapshot, &total, &row, &col);
    first = *scroll;
    int maximum = total > height ? total - height : 0;
    if (first > maximum) first = maximum;
    if (first < 0) first = 0;
    if (row < first) first = row;
    if (row >= first + height) first = row - height + 1;
    IrcCursesEditorPaint paint = {
        top, left, width, height, first, row, col, 1, attrs
    };
    ed_visual_snapshot_render(snapshot, first, height, irc_curses_editor_cluster, &paint);
    ed_visual_snapshot_free(snapshot);
    *scroll = first;
    position->total = total;
    position->row = row;
    position->col = paint.caret_col;
    position->first = first;
    position->span = paint.caret_span;
    (void)attrset(A_NORMAL);
    return 1;
}
#endif
