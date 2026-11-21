/* Logical drafts with optional visual Arabic caret/rendering geometry.
 * No terminal, server, or presentation forms are stored in the Editor. */
#define _XOPEN_SOURCE 700
#include "editor.h"
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "test_editor_visual.c:%d: %s\n", __LINE__, #x); abort(); \
} } while (0)

static const char beh_pair[] = "\xd8\xa8\xd8\xa8";
static const char beh_teh[] = "\xd8\xa8\xd8\xaa";
static const char five_beh[] = "\xd8\xa8\xd8\xa8\xd8\xa8\xd8\xa8\xd8\xa8";

static void enabled(int on)
{
    CHECK(!setenv("ONEKEYIRC_ARABIC_VISUAL", on ? "1" : "0", 1));
    CHECK(!setenv("VIPIRC_ARABIC_VISUAL", on ? "1" : "0", 1));
}

static void text_is(const Editor *e, const char *expected)
{
    char actual[ED_MAX_BYTES + 1];
    CHECK(ed_to_utf8(e, actual, sizeof(actual)) == strlen(expected));
    CHECK(!strcmp(actual, expected));
    CHECK(e->len <= ED_MAX_CHARS && e->pos <= e->len);
    CHECK(e->bytes == strlen(expected) && e->data[e->len] == 0);
}

static void caret_is(const Editor *e, int width, size_t pos, int row, int col)
{
    int actual_row = -1, actual_col = -1, rows = -1;
    ed_visual_layout(e, width, &rows, &actual_row, &actual_col);
    if (e->pos != pos || actual_row != row || actual_col != col)
        fprintf(stderr, "caret width=%d: got %zu@%d,%d expected %zu@%d,%d\n",
                width, e->pos, actual_row, actual_col, pos, row, col);
    CHECK(e->pos == pos && actual_row == row && actual_col == col);
    CHECK(rows > row && col >= 0 && col < (width > 0 ? width : 1));
}

typedef struct PaintProbe {
    const Editor *editor;
    int width, first_row, row_count, calls, marks, previous_row, previous_col;
    size_t letters;
    wchar_t first[4];
} PaintProbe;

static void paint(void *opaque, int row, int col, const wchar_t *text,
                  size_t count, int cells, size_t first, size_t past)
{
    PaintProbe *p = opaque;
    CHECK(row >= p->first_row && row - p->first_row < p->row_count);
    CHECK(col >= 0 && cells > 0 && col + cells <= p->width);
    CHECK(first < past && past <= p->editor->len);
    CHECK(count && text[count] == 0);
    CHECK(row > p->previous_row || (row == p->previous_row && col >= p->previous_col));
    p->previous_row = row; p->previous_col = col;
    if (p->calls < 4) p->first[p->calls] = text[0];
    ++p->calls;
    for (size_t i = 0; i < count; ++i) {
        CHECK(text[i] != 0xfeff && text[i] != 0x200e && text[i] != 0x200f);
        if (i) { CHECK(wcwidth(text[i]) == 0); ++p->marks; }
    }
    p->letters += past - first;
}

static PaintProbe render(const Editor *e, int width, int first, int rows)
{
    PaintProbe p = {.editor=e, .width=width, .first_row=first, .row_count=rows,
                    .previous_row=-1, .previous_col=-1};
    Editor copy = *e;
    CHECK(ed_visual_render(e, width, first, rows, paint, &p));
    CHECK(!memcmp(e, &copy, sizeof(copy)));
    return p;
}

static void fallback(void)
{
    Editor e, ordinary, visual;
    CHECK(ed_from_utf8(&e, "ab " "\xd8\xa8\xd9\x8e\xd8\xaa" "\nxyz"));
    for (int width = 0; width <= 20; ++width) {
        int r1, c1, n1, r2, c2, n2;
        CHECK(!ed_visual_active(&e, width));
        CHECK(!ed_visual_render(&e, width, 0, 10, NULL, NULL));
        for (size_t i = 0; i <= e.len; ++i) {
            ed_position(&e, i, width, &r1, &c1);
            ed_visual_position(&e, i, width, &r2, &c2);
            CHECK(r1 == r2 && c1 == c2);
        }
        ed_layout(&e, width, &n1, &r1, &c1);
        ed_visual_layout(&e, width, &n2, &r2, &c2);
        CHECK(n1 == n2 && r1 == r2 && c1 == c2);
        ordinary = e; visual = e;
        ed_home(&ordinary); ed_visual_home(&visual, width);
        CHECK(ordinary.pos == visual.pos);
        for (int i = 0; i < 4; ++i) {
            ed_right(&ordinary); ed_visual_right(&visual, width);
            CHECK(ordinary.pos == visual.pos);
        }
        ed_left(&ordinary); ed_visual_left(&visual, width);
        CHECK(ordinary.pos == visual.pos);
        ed_move_vertical(&ordinary, 1, width); ed_visual_move_vertical(&visual, 1, width);
        CHECK(ordinary.pos == visual.pos && ordinary.goal_col == visual.goal_col);
        ed_set_cell(&ordinary, 2, 3, width); ed_visual_set_cell(&visual, 2, 3, width);
        CHECK(ordinary.pos == visual.pos);
        ed_end(&ordinary); ed_visual_end(&visual, width);
        CHECK(ordinary.pos == visual.pos);
        CHECK(!memcmp(ordinary.data, visual.data, sizeof(ordinary.data)));
    }
    text_is(&e, "ab " "\xd8\xa8\xd9\x8e\xd8\xaa" "\nxyz");
}

static void editing(void)
{
    Editor e;
    PaintProbe p;
    CHECK(ed_from_utf8(&e, beh_pair));
    ed_visual_home(&e, 10); caret_is(&e, 10, 0, 0, 9);
    ed_visual_left(&e, 10); caret_is(&e, 10, 1, 0, 8);
    ed_visual_left(&e, 10); caret_is(&e, 10, 2, 0, 7);
    ed_visual_left(&e, 10); caret_is(&e, 10, 2, 0, 7);
    ed_visual_right(&e, 10); caret_is(&e, 10, 1, 0, 8);
    ed_visual_right(&e, 10); caret_is(&e, 10, 0, 0, 9);
    text_is(&e, beh_pair);
    p = render(&e, 10, 0, 1);
    CHECK(p.calls == 2 && p.first[0] == 0xfe90 && p.first[1] == 0xfe91);

    CHECK(ed_from_utf8(&e, beh_teh));
    CHECK(ed_backspace(&e)); text_is(&e, "\xd8\xa8");
    CHECK(ed_from_utf8(&e, beh_teh));
    ed_visual_home(&e, 10);
    CHECK(ed_delete(&e)); text_is(&e, "\xd8\xaa");
    CHECK(ed_from_utf8(&e, beh_teh));
    ed_visual_home(&e, 10); ed_visual_left(&e, 10);
    CHECK(e.pos == 1 && ed_insert(&e, (wchar_t)0x062c));
    text_is(&e, "\xd8\xa8\xd8\xac\xd8\xaa");
    CHECK(!e.visual_affinity);

    CHECK(ed_from_utf8(&e, "\xd8\xa8\xd9\x8e\xd8\xaa"));
    ed_visual_home(&e, 10); ed_visual_left(&e, 10);
    caret_is(&e, 10, 2, 0, 8);
    ed_visual_left(&e, 10); caret_is(&e, 10, 3, 0, 7);
    ed_visual_right(&e, 10); caret_is(&e, 10, 2, 0, 8);
    p = render(&e, 10, 0, 1);
    CHECK(p.calls == 2 && p.marks == 1 && p.letters == 3);
    CHECK(ed_backspace(&e)); text_is(&e, "\xd8\xaa");

    CHECK(ed_from_utf8(&e, "\xd9\x84\xd8\xa7"));
    p = render(&e, 10, 0, 1); CHECK(p.calls == 2);
    ed_visual_home(&e, 10); ed_visual_left(&e, 10); CHECK(e.pos == 1);
    ed_visual_left(&e, 10); CHECK(e.pos == 2);
    CHECK(ed_backspace(&e)); text_is(&e, "\xd9\x84");

    CHECK(ed_from_utf8(&e, five_beh));
    ed_select_all(&e); ed_visual_left(&e, 4); CHECK(e.pos == e.len && !e.selection_all);
    ed_select_all(&e); ed_visual_right(&e, 4); CHECK(e.pos == 0 && !e.selection_all);
    ed_select_all(&e); CHECK(ed_insert(&e, (wchar_t)0x062a)); text_is(&e, "\xd8\xaa");
}

static void wrapping(void)
{
    Editor e;
    static const size_t pos4[] = {0,1,2,3,3,4,5};
    static const int row4[] = {0,0,0,0,1,1,1};
    static const int col4[] = {3,2,1,0,3,2,1};
    CHECK(ed_from_utf8(&e, five_beh));
    ed_visual_home(&e, 4);
    for (size_t i = 0; i < sizeof(pos4)/sizeof(pos4[0]); ++i) {
        caret_is(&e, 4, pos4[i], row4[i], col4[i]);
        if (i + 1 < sizeof(pos4)/sizeof(pos4[0])) ed_visual_left(&e, 4);
    }
    for (size_t i = sizeof(pos4)/sizeof(pos4[0]); i-- > 0;) {
        caret_is(&e, 4, pos4[i], row4[i], col4[i]);
        if (i) ed_visual_right(&e, 4);
    }
    for (int width = 1; width <= 2; ++width) {
        ed_visual_home(&e, width);
        for (size_t i = 0; i < 5; ++i) {
            caret_is(&e, width, i, (int)i, width - 1);
            ed_visual_left(&e, width); caret_is(&e, width, i + 1, (int)i, 0);
            if (i < 4) ed_visual_left(&e, width);
        }
        for (size_t i = 5; i-- > 0;) {
            caret_is(&e, width, i + 1, (int)i, 0);
            ed_visual_right(&e, width); caret_is(&e, width, i, (int)i, width - 1);
            if (i) ed_visual_right(&e, width);
        }
    }
    text_is(&e, five_beh);

    /* A fully Latin physical row still belongs to its RTL paragraph. */
    CHECK(ed_from_utf8(&e, "\xd8\xa8 /ab \xd8\xa8"));
    ed_visual_home(&e, 4);
    ed_visual_left(&e, 4); caret_is(&e, 4, 1, 0, 2);
    ed_visual_left(&e, 4); caret_is(&e, 4, 2, 0, 1);
    ed_visual_left(&e, 4); caret_is(&e, 4, 5, 1, 3);
    ed_visual_left(&e, 4); caret_is(&e, 4, 4, 1, 2);
    ed_visual_left(&e, 4); caret_is(&e, 4, 3, 1, 1);
    ed_visual_left(&e, 4); caret_is(&e, 4, 2, 1, 0);
    ed_visual_left(&e, 4); caret_is(&e, 4, 5, 2, 3);

    CHECK(ed_from_utf8(&e, "\xd8\xa8\xd8\xa8\nabc\n\xd8\xa8\xd8\xa8"));
    ed_visual_home(&e, 4);
    ed_visual_left(&e, 4); ed_visual_left(&e, 4); caret_is(&e, 4, 2, 0, 1);
    ed_visual_left(&e, 4); caret_is(&e, 4, 3, 1, 0);
    ed_visual_right(&e, 4); ed_visual_right(&e, 4); ed_visual_right(&e, 4);
    caret_is(&e, 4, 6, 1, 3);
    ed_visual_right(&e, 4); caret_is(&e, 4, 7, 2, 3);
    ed_visual_right(&e, 4); caret_is(&e, 4, 6, 1, 3);
    ed_visual_left(&e, 4); ed_visual_left(&e, 4); ed_visual_left(&e, 4);
    caret_is(&e, 4, 3, 1, 0);
    ed_visual_left(&e, 4); caret_is(&e, 4, 2, 0, 1);
}

static void mouse_vertical_resize(void)
{
    Editor e;
    CHECK(ed_from_utf8(&e, five_beh));
    ed_visual_home(&e, 4); ed_visual_left(&e, 4);
    ed_visual_move_vertical(&e, 1, 4); caret_is(&e, 4, 4, 1, 2);
    CHECK(e.goal_col == 2);
    ed_visual_move_vertical(&e, INT_MIN, 4); caret_is(&e, 4, 1, 0, 2);
    ed_visual_move_vertical(&e, INT_MAX, 4); caret_is(&e, 4, 4, 1, 2);
    ed_visual_set_cell(&e, 0, 3, 4); caret_is(&e, 4, 0, 0, 3);
    ed_visual_set_cell(&e, INT_MAX, INT_MIN, 4); caret_is(&e, 4, 5, 1, 1);
    ed_visual_set_cell(&e, INT_MIN, INT_MAX, 4); caret_is(&e, 4, 0, 0, 3);
    ed_visual_home(&e, 4); ed_visual_left(&e, 4); ed_visual_left(&e, 4); ed_visual_left(&e, 4);
    caret_is(&e, 4, 3, 0, 0);
    /* Width changes discard the old soft-wrap affinity, keeping logical pos. */
    caret_is(&e, 10, 3, 0, 6);
    (void)render(&e, 4, 1, 1);
    CHECK(ed_visual_render(&e, 4, 0, 0, NULL, NULL));

    /* Several insertion stops can occupy the same one-column cell. Clamped
     * Up/Down must not replace the chosen stop with another equal-cell tie. */
    ed_visual_home(&e, 1);
    ed_visual_move_vertical(&e, -1, 1); caret_is(&e, 1, 0, 0, 0);
    ed_visual_move_vertical(&e, INT_MIN, 1); caret_is(&e, 1, 0, 0, 0);
    ed_visual_end(&e, 1); ed_visual_right(&e, 1);
    caret_is(&e, 1, 4, 4, 0);
    size_t affinity = e.visual_caret;
    ed_visual_move_vertical(&e, 1, 1); caret_is(&e, 1, 4, 4, 0);
    ed_visual_move_vertical(&e, INT_MAX, 1); caret_is(&e, 1, 4, 4, 0);
    CHECK(e.visual_affinity && e.visual_caret == affinity && e.goal_col == 0);
    ed_select_all(&e);
    ed_visual_move_vertical(&e, 1, 1);
    CHECK(!e.selection_all && e.pos == e.len);
    text_is(&e, five_beh);

    CHECK(ed_from_utf8(&e, "/msg #room \xd8\xa8\xd9\x8e\xd8\xaa 123"));
    for (int width = 4; width <= 40; width += 9) {
        for (size_t i = 0; i <= e.len; i = ed_next(&e, i)) {
            int row, col, after_row, after_col;
            ed_visual_position(&e, i, width, &row, &col);
            ed_visual_set_cell(&e, row, col, width);
            ed_visual_position(&e, e.pos, width, &after_row, &after_col);
            CHECK(row == after_row && col == after_col);
            CHECK(!e.pos || wcwidth(e.data[e.pos]) != 0 || e.pos == e.len);
            if (i == e.len) break;
        }
        (void)render(&e, width, 0, ED_MAX_CHARS + 1);
    }
    text_is(&e, "/msg #room \xd8\xa8\xd9\x8e\xd8\xaa 123");
}

static void maximum_input(void)
{
    Editor e;
    char text[ED_MAX_BYTES + 1];
    for (size_t i = 0; i < ED_MAX_CHARS; ++i) {
        text[2*i] = (char)0xd8; text[2*i+1] = (char)0xa8;
    }
    text[ED_MAX_BYTES] = 0;
    CHECK(ed_from_utf8(&e, text));
    CHECK(e.len == ED_MAX_CHARS && e.bytes == ED_MAX_BYTES);
    PaintProbe p = render(&e, 80, 0, ED_MAX_CHARS + 1);
    CHECK(p.calls == ED_MAX_CHARS && p.letters == ED_MAX_CHARS);
    ed_visual_home(&e, 80); ed_visual_move_vertical(&e, INT_MAX, 80);
    int rows, row, col;
    ed_visual_layout(&e, 80, &rows, &row, &col);
    CHECK(row == rows - 1 && col >= 0 && col < 80);
    ed_visual_end(&e, 80); CHECK(e.pos == ED_MAX_CHARS);
    text_is(&e, text);
}

int main(void)
{
    if (!setlocale(LC_ALL, "C.UTF-8")) (void)setlocale(LC_ALL, "");
    CHECK(!setenv("ONEKEYIRC_LANG", "en", 1));
    CHECK(!setenv("VIPIRC_LANG", "en", 1));
    enabled(0); fallback();
    enabled(1);
    Editor e;
    CHECK(ed_from_utf8(&e, beh_pair));
    if (!ed_visual_active(&e, 10)) {
        fallback();
#ifdef IRC_NO_ARABIC_VISUAL
        printf("Arabic editor compile-disabled fallback: PASS (%lu checks)\n", checks);
        return 0;
#else
        printf("Arabic editor optional converter: UNAVAILABLE (fallback passed)\n");
        return 77;
#endif
    }
    CHECK(ed_from_utf8(&e, "ordinary ASCII /msg #room 123"));
    CHECK(!ed_visual_active(&e, 10));
    editing(); wrapping(); mouse_vertical_resize(); maximum_input();
    enabled(0); fallback();
    printf("Arabic visual editor: PASS (%lu checks; logical bytes, clusters, wrap affinity, mouse, resize)\n", checks);
    return 0;
}
