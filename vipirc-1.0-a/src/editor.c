/* Logical draft editing and shared display geometry.
 *
 * data/pos/len count wchar_t scalar slots; bytes tracks encoded UTF-8 bytes.
 * Screen wrapping is computed from that logical draft and never truncates it.
 * Read scalar validation and mutations first, then geometry, then UTF-8 I/O.
 * The grouping rule is a base plus zero-width followers, not full grapheme
 * segmentation; renderers use these same helpers to agree with caret motion. */
#define _XOPEN_SOURCE 700
#include "editor.h"

#include <limits.h>
#include <string.h>

static size_t scalar_bytes(uint32_t c)
{
    if (c < 0x80U) return 1;
    if (c < 0x800U) return 2;
    if (c < 0x10000U) return 3;
    return 4;
}

static int allowed(uint32_t c)
{
    if (c == '\n') return 1;
    if (c < 0x20U || (c >= 0x7fU && c <= 0x9fU) || c > 0x10ffffU)
        return 0;
    if ((c >= 0xd800U && c <= 0xdfffU) || c == 0x061cU ||
        c == 0x200eU || c == 0x200fU ||
        (c >= 0x2028U && c <= 0x202eU) ||
        (c >= 0x2066U && c <= 0x2069U))
        return 0;
    return 1;
}

static int zero_width(wchar_t wc)
{
    return wc != L'\n' && wcwidth(wc) == 0;
}

void ed_init(Editor *e)
{
    memset(e, 0, sizeof(*e));
    e->goal_col = -1;
}

void ed_select_all(Editor *e)
{
    e->selection_all = e->len != 0;
    e->pos = e->len;
    e->goal_col = -1;
}

void ed_unselect(Editor *e)
{
    e->selection_all = 0;
}

int ed_insert(Editor *e, wchar_t wc)
{
    uint32_t c = (uint32_t)wc;
    size_t n;
    if (!allowed(c) || e->pos > e->len)
        return 0;
    n = scalar_bytes(c);
    if (e->selection_all) {
        /* No operation after this point can fail: one allowed scalar fits an
         * empty editor. In particular, full drafts remain replaceable. */
        ed_init(e);
    } else if (e->len >= ED_MAX_CHARS || e->bytes > ED_MAX_BYTES - n) {
        return 0;
    }
    memmove(e->data + e->pos + 1, e->data + e->pos,
            (e->len - e->pos + 1) * sizeof(*e->data));
    e->data[e->pos++] = wc;
    e->len++;
    e->bytes += n;
    if (wc != L'\n')
        while (e->pos < e->len && zero_width(e->data[e->pos])) e->pos++;
    e->goal_col = -1;
    return 1;
}

size_t ed_next(const Editor *e, size_t index)
{
    size_t i;
    if (index >= e->len) return e->len;
    i = index + 1;
    if (e->data[index] != L'\n')
        while (i < e->len && zero_width(e->data[i])) i++;
    return i;
}

size_t ed_prev(const Editor *e, size_t index)
{
    size_t i = index > e->len ? e->len : index;
    if (!i) return 0;
    i--;
    while (i && zero_width(e->data[i]) && e->data[i - 1] != L'\n') i--;
    return i;
}

/* Mutations keep scalar positions and encoded-byte accounting together. */
static int erase_range(Editor *e, size_t first, size_t end)
{
    size_t i;
    if (first >= end || end > e->len) return 0;
    for (i = first; i < end; i++)
        e->bytes -= scalar_bytes((uint32_t)e->data[i]);
    memmove(e->data + first, e->data + end,
            (e->len - end + 1) * sizeof(*e->data));
    e->len -= end - first;
    e->pos = first;
    e->goal_col = -1;
    return 1;
}

int ed_backspace(Editor *e)
{
    if (e->selection_all) {
        int had_text = e->len != 0;
        ed_init(e);
        return had_text;
    }
    if (!e->pos || e->pos > e->len) return 0;
    return erase_range(e, ed_prev(e, e->pos), e->pos);
}

int ed_delete(Editor *e)
{
    if (e->selection_all) {
        int had_text = e->len != 0;
        ed_init(e);
        return had_text;
    }
    if (e->pos >= e->len) return 0;
    return erase_range(e, e->pos, ed_next(e, e->pos));
}

void ed_left(Editor *e)
{
    e->pos = e->selection_all ? 0 : ed_prev(e, e->pos);
    ed_unselect(e);
    e->goal_col = -1;
}

void ed_right(Editor *e)
{
    e->pos = e->selection_all ? e->len : ed_next(e, e->pos);
    ed_unselect(e);
    e->goal_col = -1;
}

void ed_home(Editor *e)
{
    ed_unselect(e);
    e->pos = 0;
    e->goal_col = -1;
}

void ed_end(Editor *e)
{
    ed_unselect(e);
    e->pos = e->len;
    e->goal_col = -1;
}

int ed_glyph_width(wchar_t wc, int width)
{
    int n;
    if (width <= 0) width = 1;
    if (wc == L'\n') return 0;
    n = wcwidth(wc);
    if (n < 0 || n > width) return 1;
    return n;
}

int ed_cluster_width(const Editor *e, size_t index, int width)
{
    int n;
    if (index >= e->len || e->data[index] == L'\n') return 0;
    n = ed_glyph_width(e->data[index], width);
    return n ? n : 1;
}

static int advance_cell(int w, int width, int *row, int *col)
{
    *col += w;
    if (*col >= width) {
        (*row)++;
        *col = 0;
        return 1;
    }
    return 0;
}

/* Editor indices count logical scalars; this method
 * maps them to rows and terminal columns. Combining characters and wide
 * glyphs make byte offsets unsuitable for cursor placement. */
void ed_position(const Editor *e, size_t index, int width, int *row, int *col)
{
    size_t i = 0;
    int r = 0, c = 0, wrapped = 0;
    if (width <= 0) width = 1;
    if (index > e->len) index = e->len;
    while (i < e->len) {
        size_t next = ed_next(e, i);
        int w = ed_cluster_width(e, i, width);
        if (w && c + w > width) {
            r++;
            c = 0;
        }
        if (index < next) break;
        if (e->data[i] == L'\n') {
            if (!wrapped) r++;
            c = 0;
            wrapped = 0;
        } else {
            wrapped = advance_cell(w, width, &r, &c);
        }
        i = next;
    }
    if (row) *row = r;
    if (col) *col = c;
}

void ed_layout(const Editor *e, int width, int *rows,
               int *cursor_row, int *cursor_col)
{
    int r, c;
    ed_position(e, e->len, width, &r, &c);
    if (rows) *rows = r + 1;
    ed_position(e, e->pos, width, cursor_row, cursor_col);
}

void ed_move_vertical(Editor *e, int delta, int width)
{
    size_t i = 0, best = e->pos;
    int row, col, want_row, r = 0, c = 0, wrapped = 0;
    int best_distance = INT_MAX, goal, total_rows;
    if (!delta) return;
    ed_unselect(e);
    if (width <= 0) width = 1;
    ed_layout(e, width, &total_rows, &row, &col);
    goal = e->goal_col >= 0 ? e->goal_col : col;
    e->goal_col = goal;
    if (delta < 0 && delta < -row) want_row = 0;
    else if (delta > 0 && delta >= total_rows - row) want_row = total_rows - 1;
    else want_row = row + delta;
    if (want_row == row) return;
    while (1) {
        int w = ed_cluster_width(e, i, width);
        if (w && c + w > width) {
            r++;
            c = 0;
        }
        if (r == want_row) {
            int d = c > goal ? c - goal : goal - c;
            /* Prefer the earlier boundary if the desired column is inside
             * a wide glyph, so the cursor never lands in its second cell. */
            if (d < best_distance) {
                best_distance = d;
                best = i;
            }
        }
        if (i >= e->len || r > want_row) break;
        if (e->data[i] == L'\n') {
            if (!wrapped) r++;
            c = 0;
            wrapped = 0;
        } else {
            wrapped = advance_cell(w, width, &r, &c);
        }
        i = ed_next(e, i);
    }
    if (best_distance != INT_MAX) e->pos = best;
}

void ed_set_cell(Editor *e, int row, int col, int width)
{
    size_t i = 0, best = 0;
    int r = 0, c = 0, wrapped = 0;
    int best_row_distance = INT_MAX, best_col_distance = INT_MAX;
    if (width <= 0) width = 1;
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (col >= width) col = width - 1;
    while (1) {
        int w = ed_cluster_width(e, i, width);
        int row_distance, col_distance;
        if (w && c + w > width) {
            ++r;
            c = 0;
        }
        row_distance = r > row ? r - row : row - r;
        col_distance = c > col ? c - col : col - c;
        /* Prefer the requested visual row, then its closest caret column.
         * This also clamps clicks below the draft to its last visual row. */
        if (row_distance < best_row_distance ||
            (row_distance == best_row_distance && col_distance < best_col_distance)) {
            best_row_distance = row_distance;
            best_col_distance = col_distance;
            best = i;
        }
        if (i >= e->len) break;
        if (e->data[i] == L'\n') {
            if (!wrapped) ++r;
            c = 0;
            wrapped = 0;
        } else {
            wrapped = advance_cell(w, width, &r, &c);
        }
        i = ed_next(e, i);
    }
    e->pos = best;
    ed_unselect(e);
    e->goal_col = -1;
}

static size_t encode(uint32_t c, char *out)
{
    if (c < 0x80U) {
        out[0] = (char)c;
        return 1;
    }
    if (c < 0x800U) {
        out[0] = (char)(0xc0U | (c >> 6));
        out[1] = (char)(0x80U | (c & 0x3fU));
        return 2;
    }
    if (c < 0x10000U) {
        out[0] = (char)(0xe0U | (c >> 12));
        out[1] = (char)(0x80U | ((c >> 6) & 0x3fU));
        out[2] = (char)(0x80U | (c & 0x3fU));
        return 3;
    }
    out[0] = (char)(0xf0U | (c >> 18));
    out[1] = (char)(0x80U | ((c >> 12) & 0x3fU));
    out[2] = (char)(0x80U | ((c >> 6) & 0x3fU));
    out[3] = (char)(0x80U | (c & 0x3fU));
    return 4;
}

size_t ed_to_utf8(const Editor *e, char *out, size_t cap)
{
    size_t i, n = 0;
    if (!out || cap <= e->bytes) return SIZE_MAX;
    for (i = 0; i < e->len; i++) n += encode((uint32_t)e->data[i], out + n);
    out[n] = '\0';
    return n;
}

static int decode(const unsigned char **p, uint32_t *out)
{
    const unsigned char *s = *p;
    uint32_t c = *s++;
    uint32_t minimum;
    size_t remaining, i;
    if (c < 0x80U) {
        *out = c;
        *p = s;
        return 1;
    }
    if (c >= 0xc2U && c <= 0xdfU) {
        c &= 0x1fU;
        minimum = 0x80U;
        remaining = 1;
    } else if (c >= 0xe0U && c <= 0xefU) {
        c &= 0x0fU;
        minimum = 0x800U;
        remaining = 2;
    } else if (c >= 0xf0U && c <= 0xf4U) {
        c &= 0x07U;
        minimum = 0x10000U;
        remaining = 3;
    } else {
        return 0;
    }
    for (i = 0; i < remaining; i++) {
        unsigned char b = *s;
        if (b < 0x80U || b > 0xbfU) return 0;
        c = (c << 6) | (b & 0x3fU);
        s++;
    }
    if (c < minimum || c > 0x10ffffU || (c >= 0xd800U && c <= 0xdfffU))
        return 0;
    *out = c;
    *p = s;
    return 1;
}

/* Build a complete replacement first. A malformed or oversized input leaves
 * the original draft/caret untouched, including a selected complete draft. */
int ed_from_utf8(Editor *e, const char *text)
{
    Editor replacement;
    volatile unsigned char *bytes = (volatile unsigned char *)&replacement;
    const unsigned char *p = (const unsigned char *)text;
    int result = 0;
    if (!text) return 0;
    ed_init(&replacement);
    while (*p) {
        uint32_t c;
        if (!decode(&p, &c) || !ed_insert(&replacement, (wchar_t)c))
            goto done;
    }
    *e = replacement;
    result = 1;
done:
    /* Settings may contain a masked password. Clear the temporary on both
     * success and failure without adding a dependency to this small editor. */
    for (size_t i = 0; i < sizeof replacement; ++i) bytes[i] = 0;
    return result;
}
