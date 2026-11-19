#ifndef VIPIRC_EDITOR_H
#define VIPIRC_EDITOR_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#define ED_MAX_CHARS 8192
#define ED_MAX_BYTES 16384

/* The cursor is an index in one flat logical buffer, never a screen column. */
typedef struct Editor {
    wchar_t data[ED_MAX_CHARS + 1];
    size_t len;
    size_t pos;
    int goal_col;
    size_t bytes;
    int selection_all;
} Editor;

void ed_init(Editor *e);
/* Select the entire logical draft, including text outside the viewport.
 * Valid insertion replaces it; invalid insertion leaves it untouched. */
void ed_select_all(Editor *e);
/* Cancel selection without changing the text or caret. */
void ed_unselect(Editor *e);
int ed_insert(Editor *e, wchar_t wc);
int ed_backspace(Editor *e);
int ed_delete(Editor *e);
void ed_left(Editor *e);
void ed_right(Editor *e);
void ed_home(Editor *e);
void ed_end(Editor *e);
void ed_move_vertical(Editor *e, int delta, int width);
/* Place the caret at the nearest legal cluster boundary to a visual cell.
 * Rows/columns are zero based; ties prefer the earlier boundary. Selection
 * is cancelled and the vertical goal is reset. Text is never changed. */
void ed_set_cell(Editor *e, int row, int col, int width);

/* Base character plus following zero-width characters; not full UAX #29. */
size_t ed_next(const Editor *e, size_t index);
size_t ed_prev(const Editor *e, size_t index);

/* Width uses wcwidth(), with a one-cell fallback for an unrenderable scalar.
 * A glyph wider than the available row also uses the one-cell fallback.
 * Renderers must draw a replacement in those two cases. */
int ed_glyph_width(wchar_t wc, int width);
int ed_cluster_width(const Editor *e, size_t index, int width);

/* Width <= 0 is treated as 1. The caret always occupies a real screen cell,
 * including a new row after an exactly full line. Coordinates are zero based.
 * Indices inside a combining group resolve to that group's start position. */
void ed_position(const Editor *e, size_t index, int width, int *row, int *col);
void ed_layout(const Editor *e, int width, int *rows,
               int *cursor_row, int *cursor_col);

/* Strict UTF-8, independent of the active locale. from_utf8 is transactional;
 * on failure the original editor is unchanged. to_utf8 returns SIZE_MAX if
 * cap cannot hold the complete string including its terminating NUL. */
size_t ed_to_utf8(const Editor *e, char *out, size_t cap);
int ed_from_utf8(Editor *e, const char *text);

#endif
