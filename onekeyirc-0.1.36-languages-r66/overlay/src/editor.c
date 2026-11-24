/* Logical draft editing and shared display geometry.
 *
 * data/pos/len count wchar_t scalar slots; bytes tracks encoded UTF-8 bytes.
 * Screen wrapping is computed from that logical draft and never truncates it.
 * Read scalar validation and mutations first, then geometry, then UTF-8 I/O.
 * The grouping rule is a base plus zero-width followers, not full grapheme
 * segmentation; renderers use these same helpers to agree with caret motion. */
#define _XOPEN_SOURCE 700
#include "editor.h"
#include "arabic_visual.h"

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

static void visual_forget(Editor *e)
{
    e->visual_affinity = 0;
}

void ed_init(Editor *e)
{
    memset(e, 0, sizeof(*e));
    e->goal_col = -1;
}

void ed_select_all(Editor *e)
{
    visual_forget(e);
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
    visual_forget(e);
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
    visual_forget(e);
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
    visual_forget(e);
    e->pos = e->selection_all ? 0 : ed_prev(e, e->pos);
    ed_unselect(e);
    e->goal_col = -1;
}

void ed_right(Editor *e)
{
    visual_forget(e);
    e->pos = e->selection_all ? e->len : ed_next(e, e->pos);
    ed_unselect(e);
    e->goal_col = -1;
}

void ed_home(Editor *e)
{
    visual_forget(e);
    ed_unselect(e);
    e->pos = 0;
    e->goal_col = -1;
}

void ed_end(Editor *e)
{
    visual_forget(e);
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
    visual_forget(e);
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
    visual_forget(e);
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

/* The optional visual map owns only temporary presentation data. It is built
 * from the logical editor on demand, so copying an Editor never shares a
 * cache/pointer and secret editors can keep their existing masked path. */
static IrcArabicLayout *visual_map(const Editor *e, int width)
{
    if (e->len > ED_MAX_CHARS || e->bytes > ED_MAX_BYTES) return NULL;
    return irc_arabic_layout_wide(e->data, e->len, width > 0 ? width : 1,
                                  IRC_ARABIC_EDIT);
}

static size_t visual_index(const IrcArabicLayout *map, size_t index)
{
    if (index > map->count) index = map->count;
    while (index && !map->boundary[index]) --index;
    return index;
}

static size_t visual_current(const Editor *e, const IrcArabicLayout *map)
{
    size_t index = visual_index(map, e->pos), first = SIZE_MAX;
    if (e->visual_affinity && e->visual_pos == e->pos &&
        e->visual_width == map->width && e->visual_caret < map->caret_count &&
        map->carets[e->visual_caret].index == index)
        return e->visual_caret;
    for (size_t i = 0; i < map->caret_count; ++i) {
        const IrcArabicCaret *caret = &map->carets[i];
        if (caret->index != index) continue;
        if (first == SIZE_MAX) first = i;
        if (caret->row == map->logical_row[index] &&
            caret->col == map->logical_col[index]) return i;
    }
    return first;
}

static void visual_choose(Editor *e, const IrcArabicLayout *map, size_t caret)
{
    if (caret >= map->caret_count) return;
    e->pos = map->carets[caret].index;
    e->visual_pos = e->pos;
    e->visual_width = map->width;
    e->visual_caret = caret;
    e->visual_affinity = 1;
    e->selection_all = 0;
}

int ed_visual_active(const Editor *e, int width)
{
    IrcArabicLayout *map = visual_map(e, width);
    int active = map != NULL;
    irc_arabic_layout_free(map);
    return active;
}

void ed_visual_position(const Editor *e, size_t index, int width, int *row, int *col)
{
    IrcArabicLayout *map = visual_map(e, width);
    if (!map) { ed_position(e, index, width, row, col); return; }
    size_t logical = visual_index(map, index);
    size_t caret = index == e->pos ? visual_current(e, map) : SIZE_MAX;
    if (row) *row = caret < map->caret_count ? map->carets[caret].row : map->logical_row[logical];
    if (col) *col = caret < map->caret_count ? map->carets[caret].col : map->logical_col[logical];
    irc_arabic_layout_free(map);
}

void ed_visual_layout(const Editor *e, int width, int *rows,
                      int *cursor_row, int *cursor_col)
{
    IrcArabicLayout *map = visual_map(e, width);
    if (!map) { ed_layout(e, width, rows, cursor_row, cursor_col); return; }
    size_t logical = visual_index(map, e->pos), caret = visual_current(e, map);
    if (rows) *rows = map->rows;
    if (cursor_row) *cursor_row = caret < map->caret_count ? map->carets[caret].row : map->logical_row[logical];
    if (cursor_col) *cursor_col = caret < map->caret_count ? map->carets[caret].col : map->logical_col[logical];
    irc_arabic_layout_free(map);
}

static void visual_horizontal(Editor *e, int direction, int width)
{
    IrcArabicLayout *map = visual_map(e, width);
    if (!map || !map->caret_count) {
        irc_arabic_layout_free(map);
        if (direction < 0) ed_left(e); else ed_right(e);
        return;
    }
    size_t current = visual_current(e, map), next = current;
    if (e->selection_all) {
        int rtl = map->lines[0].rtl;
        e->pos = (direction < 0) == rtl ? e->len : 0;
        visual_forget(e);
        next = visual_current(e, map);
    }
    else if (current == SIZE_MAX) next = direction < 0 ? 0 : map->caret_count - 1;
    else {
        int row = map->carets[current].row, moved = 0;
        size_t first = current, last = current;
        while (first && map->carets[first - 1].row == row) --first;
        while (last + 1 < map->caret_count && map->carets[last + 1].row == row) ++last;
        /* Keep distinct insertion positions at bidi junctions, including
         * positions sharing a screen cell. Skip only an identical stop. */
        while (direction < 0 ? next > first : next < last) {
            if (direction < 0) --next; else ++next;
            if (map->carets[next].index != map->carets[current].index ||
                map->carets[next].col != map->carets[current].col) { moved = 1; break; }
        }
        if (!moved) {
            /* Physical arrows follow the paragraph's flow at a soft wrap.
             * An Arabic paragraph may wrap through an entirely Latin row;
             * that row still uses its paragraph's RTL flow, not its glyphs. */
            int forward = (direction < 0) == map->lines[row].rtl;
            int target = row + (forward ? 1 : -1);
            next = current;
            if (target >= 0 && target < map->rows) {
                size_t begin = forward ? last + 1 : first - 1, end = begin;
                while (begin && map->carets[begin - 1].row == target) --begin;
                while (end + 1 < map->caret_count && map->carets[end + 1].row == target) ++end;
                const IrcArabicLine *before = &map->lines[forward ? row : target];
                int paragraph_break = before->past && e->data[before->past - 1] == L'\n';
                int right_edge = paragraph_break ?
                    (forward ? map->lines[target].rtl : !map->lines[target].rtl) : direction < 0;
                next = right_edge ? end : begin;
            }
        }
    }
    visual_choose(e, map, next);
    e->goal_col = -1;
    irc_arabic_layout_free(map);
}

void ed_visual_left(Editor *e, int width) { visual_horizontal(e, -1, width); }
void ed_visual_right(Editor *e, int width) { visual_horizontal(e, 1, width); }
void ed_visual_home(Editor *e, int width) { (void)width; ed_home(e); }
void ed_visual_end(Editor *e, int width) { (void)width; ed_end(e); }

/* Prefer a caret in the same directional run when two logical insertion
 * points occupy the same clicked/target cell. Visual order is the final tie
 * breaker, giving deterministic movement without mutating the logical text. */
static size_t visual_nearest(const IrcArabicLayout *map, size_t current,
                              int row, int col)
{
    size_t best = SIZE_MAX;
    int best_row = INT_MAX, best_col = INT_MAX, best_direction = INT_MAX;
    unsigned direction = current < map->caret_count ? map->carets[current].level & 1u : 0;
    for (size_t i = 0; i < map->caret_count; ++i) {
        const IrcArabicCaret *caret = &map->carets[i];
        int dr = caret->row > row ? caret->row - row : row - caret->row;
        int dc = caret->col > col ? caret->col - col : col - caret->col;
        int different = (caret->level & 1u) != direction;
        if (dr < best_row || (dr == best_row && dc < best_col) ||
            (dr == best_row && dc == best_col && different < best_direction)) {
            best = i; best_row = dr; best_col = dc; best_direction = different;
        }
    }
    return best;
}

void ed_visual_move_vertical(Editor *e, int delta, int width)
{
    if (!delta) return;
    IrcArabicLayout *map = visual_map(e, width);
    if (!map || !map->caret_count) {
        irc_arabic_layout_free(map); ed_move_vertical(e, delta, width); return;
    }
    size_t current = visual_current(e, map);
    size_t logical = visual_index(map, e->pos);
    int row = current < map->caret_count ? map->carets[current].row : map->logical_row[logical];
    int col = current < map->caret_count ? map->carets[current].col : map->logical_col[logical];
    int goal = e->goal_col >= 0 ? e->goal_col : col, target;
    if (goal >= map->width) goal = map->width - 1;
    if (delta < 0 && delta < -row) target = 0;
    else if (delta > 0 && delta >= map->rows - row) target = map->rows - 1;
    else target = row + delta;
    if (target == row) {
        /* At the top/bottom boundary, keep the same insertion stop. A bidi
         * junction or one-column row can have several logical stops in one
         * cell; re-running the nearest-cell tie break would silently move it. */
        ed_unselect(e);
        e->goal_col = goal;
        irc_arabic_layout_free(map);
        return;
    }
    visual_choose(e, map, visual_nearest(map, current, target, goal));
    e->goal_col = goal;
    irc_arabic_layout_free(map);
}

void ed_visual_set_cell(Editor *e, int row, int col, int width)
{
    IrcArabicLayout *map = visual_map(e, width);
    if (!map || !map->caret_count) {
        irc_arabic_layout_free(map); ed_set_cell(e, row, col, width); return;
    }
    if (row < 0) row = 0;
    if (row >= map->rows) row = map->rows - 1;
    if (col < 0) col = 0;
    if (col >= map->width) col = map->width - 1;
    visual_choose(e, map, visual_nearest(map, visual_current(e, map), row, col));
    e->goal_col = -1;
    irc_arabic_layout_free(map);
}

struct EdVisualSnapshot {
    IrcArabicLayout *map;
    wchar_t *cluster;
    int rows, row, col;
};

static size_t visual_cluster_past(const IrcArabicLayout *map, size_t first)
{
    const IrcArabicGlyph *glyph = &map->glyphs[first];
    size_t past = first + 1;
    while (past < map->glyph_count && map->glyphs[past].row == glyph->row &&
           map->glyphs[past].col == glyph->col &&
           map->glyphs[past].first == glyph->first && map->glyphs[past].past == glyph->past)
        ++past;
    return past;
}

EdVisualSnapshot *ed_visual_snapshot(const Editor *e, int width)
{
    if (!e) return NULL;
    IrcArabicLayout *map = visual_map(e, width);
    if (!map) return NULL;
    EdVisualSnapshot *snapshot = malloc(sizeof *snapshot);
    if (!snapshot) { irc_arabic_layout_free(map); return NULL; }
    size_t largest = 1;
    for (size_t i = 0; i < map->glyph_count;) {
        size_t past = visual_cluster_past(map, i);
        if (past - i > largest) largest = past - i;
        i = past;
    }
    if (largest > SIZE_MAX / sizeof(wchar_t) - 1) {
        free(snapshot); irc_arabic_layout_free(map); return NULL;
    }
    snapshot->cluster = malloc((largest + 1) * sizeof(*snapshot->cluster));
    if (!snapshot->cluster) {
        free(snapshot); irc_arabic_layout_free(map); return NULL;
    }
    size_t logical = visual_index(map, e->pos), caret = visual_current(e, map);
    snapshot->map = map;
    snapshot->rows = map->rows;
    snapshot->row = caret < map->caret_count ? map->carets[caret].row : map->logical_row[logical];
    snapshot->col = caret < map->caret_count ? map->carets[caret].col : map->logical_col[logical];
    return snapshot;
}

void ed_visual_snapshot_layout(const EdVisualSnapshot *snapshot, int *rows,
                               int *cursor_row, int *cursor_col)
{
    if (!snapshot) return;
    if (rows) *rows = snapshot->rows;
    if (cursor_row) *cursor_row = snapshot->row;
    if (cursor_col) *cursor_col = snapshot->col;
}

void ed_visual_snapshot_render(EdVisualSnapshot *snapshot, int first_row,
                               int row_count, EdVisualPaint paint, void *context)
{
    if (!snapshot || !paint || row_count <= 0) return;
    const IrcArabicLayout *map = snapshot->map;
    wchar_t *cluster = snapshot->cluster;
    if (first_row < 0) first_row = 0;
    for (size_t i = 0; i < map->glyph_count;) {
        const IrcArabicGlyph *glyph = &map->glyphs[i];
        size_t end = visual_cluster_past(map, i), count = 0;
        int cells = glyph->cells;
        for (size_t j = i + 1; j < end; ++j)
            if (map->glyphs[j].cells > cells) cells = map->glyphs[j].cells;
        if (glyph->row >= first_row && glyph->row - first_row < row_count) {
            for (size_t j = i; j < end; ++j) cluster[count++] = (wchar_t)map->glyphs[j].codepoint;
            cluster[count] = 0;
            paint(context, glyph->row, glyph->col, cluster, count, cells, glyph->first, glyph->past);
        }
        i = end;
    }
}

void ed_visual_snapshot_free(EdVisualSnapshot *snapshot)
{
    if (!snapshot) return;
    free(snapshot->cluster);
    irc_arabic_layout_free(snapshot->map);
    free(snapshot);
}

int ed_visual_render(const Editor *e, int width, int first_row, int row_count,
                      EdVisualPaint paint, void *context)
{
    EdVisualSnapshot *snapshot = ed_visual_snapshot(e, width);
    if (!snapshot) return 0;
    ed_visual_snapshot_render(snapshot, first_row, row_count, paint, context);
    ed_visual_snapshot_free(snapshot);
    return 1;
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
