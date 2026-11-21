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
    /* A bidi junction can give one logical position two visible caret stops.
     * These fields retain the chosen stop only until the next text mutation
     * or width/position change. They never own memory or alter logical text. */
    size_t visual_caret, visual_pos;
    int visual_width, visual_affinity;
} Editor;

/* Logical editing allocates no heap storage. Optional visual layout uses a
 * bounded temporary map; the fixed-size Editor never owns that map. Scalar
 * indexes must not be treated as byte offsets. */
void ed_init(Editor *e);
/* Select the entire logical draft, including text outside the viewport.
 * Valid insertion replaces it; invalid insertion leaves it untouched. */
void ed_select_all(Editor *e);
/* Cancel selection without changing the text or caret. */
void ed_unselect(Editor *e);
/* Editing returns 1 when text changed, 0 when refused or nothing was removed.
 * A refused insertion preserves the existing selection and draft. */
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

/* Opt-in Arabic presentation. These wrappers keep data/pos/len logical and
 * fall back to the ordinary geometry if conversion is disabled/unavailable.
 * Secret/masked fields must continue to use the ordinary ed_* functions.
 * Left/Right follow visible caret stops; Home/End retain this editor's
 * document-start/document-end semantics. Backspace/Delete remain logical
 * cluster operations, matching the character order used for typing/sending. */
int ed_visual_active(const Editor *e, int width);
void ed_visual_position(const Editor *e, size_t index, int width, int *row, int *col);
void ed_visual_layout(const Editor *e, int width, int *rows,
                      int *cursor_row, int *cursor_col);
void ed_visual_left(Editor *e, int width);
void ed_visual_right(Editor *e, int width);
void ed_visual_home(Editor *e, int width);
void ed_visual_end(Editor *e, int width);
void ed_visual_move_vertical(Editor *e, int delta, int width);
void ed_visual_set_cell(Editor *e, int row, int col, int width);

/* Paint base+combining groups in visual order. Coordinates are zero-based
 * in the complete wrapped editor, not relative to first_row. The callback
 * receives a temporary wide string and its cell width/source scalar span.
 * Return 1 when visual layout was used, 0 for the caller's logical fallback.
 * No callback is made before the complete temporary layout is available. */
typedef void (*EdVisualPaint)(void *context, int row, int col,
                              const wchar_t *text, size_t count, int cells,
                              size_t logical_first, size_t logical_past);

/* A frame-local snapshot keeps caret geometry and painting on one successful
 * layout. Creation prepares all temporary storage before exposing the result;
 * NULL requests the caller's complete logical fallback. Layout and render do
 * not allocate and cannot switch presentation modes partway through a frame.
 * The snapshot owns no Editor pointer and never changes the logical draft.
 * Free it after painting; do not store it inside or copy it with an Editor. */
typedef struct EdVisualSnapshot EdVisualSnapshot;
EdVisualSnapshot *ed_visual_snapshot(const Editor *e, int width);
void ed_visual_snapshot_layout(const EdVisualSnapshot *snapshot, int *rows,
                               int *cursor_row, int *cursor_col);
void ed_visual_snapshot_render(EdVisualSnapshot *snapshot, int first_row,
                               int row_count, EdVisualPaint paint, void *context);
void ed_visual_snapshot_free(EdVisualSnapshot *snapshot);

/* Compatibility helper for callers needing paint only. Frontends that also
 * place a caret or scroll the editor must use one snapshot for the frame. */
int ed_visual_render(const Editor *e, int width, int first_row, int row_count,
                      EdVisualPaint paint, void *context);

/* Strict UTF-8, independent of the active locale. from_utf8 is transactional;
 * on failure the original editor is unchanged. to_utf8 returns SIZE_MAX if
 * cap cannot hold the complete string including its terminating NUL. */
size_t ed_to_utf8(const Editor *e, char *out, size_t cap);
int ed_from_utf8(Editor *e, const char *text);

#endif
