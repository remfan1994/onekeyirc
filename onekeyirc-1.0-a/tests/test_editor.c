#define _XOPEN_SOURCE 700
#include "editor.h"

#include <assert.h>
#include <limits.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long checks;

#define CHECK(x) do { checks++; assert(x); } while (0)

static void text_is(const Editor *e, const char *expected)
{
    char out[ED_MAX_BYTES + 1];
    CHECK(ed_to_utf8(e, out, sizeof(out)) == strlen(expected));
    CHECK(!strcmp(out, expected));
    CHECK(e->bytes == strlen(expected));
    CHECK(e->data[e->len] == 0);
    CHECK(e->pos <= e->len);
}

static void position_is(const Editor *e, size_t pos, int width, int row, int col)
{
    int actual_row, actual_col;
    ed_position(e, pos, width, &actual_row, &actual_col);
    CHECK(actual_row == row);
    CHECK(actual_col == col);
}

static void test_flat_editing(void)
{
    Editor e;
    ed_init(&e);
    CHECK(e.len == 0 && e.pos == 0 && e.bytes == 0);
    CHECK(!ed_backspace(&e));
    CHECK(!ed_delete(&e));
    CHECK(ed_from_utf8(&e, "abcdefghij"));
    position_is(&e, 5, 5, 1, 0);
    e.pos = 5;
    CHECK(ed_backspace(&e));
    text_is(&e, "abcdfghij");
    CHECK(e.pos == 4);
    position_is(&e, e.pos, 5, 0, 4);
    CHECK(ed_insert(&e, L'e'));
    text_is(&e, "abcdefghij");
    CHECK(e.pos == 5);
    CHECK(ed_delete(&e));
    text_is(&e, "abcdeghij");
    CHECK(ed_insert(&e, L'f'));
    text_is(&e, "abcdefghij");
    ed_home(&e);
    CHECK(e.pos == 0);
    ed_left(&e);
    CHECK(e.pos == 0);
    ed_end(&e);
    CHECK(e.pos == e.len);
    ed_right(&e);
    CHECK(e.pos == e.len);
}

static void test_geometry(void)
{
    Editor e;
    int width, rows, row, col;
    CHECK(ed_from_utf8(&e, "abcdefghijklmnopqrstuvwxy"));
    for (width = 1; width <= 80; width++) {
        size_t i;
        for (i = 0; i <= e.len; i++)
            position_is(&e, i, width, (int)i / width, (int)i % width);
        ed_layout(&e, width, &rows, &row, &col);
        CHECK(rows == (int)e.len / width + 1);
        CHECK(row == rows - 1);
        CHECK(col == (int)e.len % width);
    }
    position_is(&e, 5, 0, 5, 0);
    position_is(&e, SIZE_MAX, 5, 5, 0);
    e.pos = 7;
    ed_move_vertical(&e, 1, 5);
    CHECK(e.pos == 12 && e.goal_col == 2);
    ed_move_vertical(&e, 1, 5);
    CHECK(e.pos == 17 && e.goal_col == 2);
    ed_move_vertical(&e, -1, 5);
    CHECK(e.pos == 12);
    ed_move_vertical(&e, INT_MAX, 5);
    CHECK(e.pos == 25);
    ed_move_vertical(&e, INT_MIN, 5);
    CHECK(e.pos == 2);
    ed_right(&e);
    CHECK(e.pos == 3 && e.goal_col == -1);
    ed_move_vertical(&e, 1, 5);
    CHECK(e.pos == 8 && e.goal_col == 3);
    CHECK(ed_from_utf8(&e, "abc\ndef\n\nx"));
    position_is(&e, 3, 80, 0, 3);
    position_is(&e, 4, 80, 1, 0);
    position_is(&e, 8, 80, 2, 0);
    position_is(&e, 9, 80, 3, 0);
    position_is(&e, e.len, 80, 3, 1);
    position_is(&e, 4, 3, 1, 0);
    position_is(&e, 8, 3, 2, 0);
    position_is(&e, 9, 3, 3, 0);
    ed_home(&e);
    ed_move_vertical(&e, 1, 80);
    CHECK(e.pos == 4);
    ed_move_vertical(&e, 1, 80);
    CHECK(e.pos == 8);
    ed_move_vertical(&e, 1, 80);
    CHECK(e.pos == 9);
}

static void test_unicode(void)
{
    Editor e;
    CHECK(wcwidth((wchar_t)0x4e2d) == 2);
    CHECK(wcwidth((wchar_t)0x0301) == 0);
    CHECK(ed_from_utf8(&e, "abcd\xe4\xb8\xad"));
    position_is(&e, 4, 5, 1, 0);
    position_is(&e, 5, 5, 1, 2);
    position_is(&e, 5, 1, 5, 0);
    CHECK(ed_glyph_width((wchar_t)0x4e2d, 1) == 1);
    CHECK(ed_backspace(&e));
    text_is(&e, "abcd");
    CHECK(ed_from_utf8(&e, "A\xcc\x81" "B"));
    CHECK(e.len == 3 && e.bytes == 4);
    position_is(&e, 0, 5, 0, 0);
    position_is(&e, 1, 5, 0, 0);
    position_is(&e, 2, 5, 0, 1);
    position_is(&e, 3, 5, 0, 2);
    ed_left(&e);
    CHECK(e.pos == 2);
    ed_left(&e);
    CHECK(e.pos == 0);
    ed_right(&e);
    CHECK(e.pos == 2);
    CHECK(ed_backspace(&e));
    text_is(&e, "B");
    CHECK(e.pos == 0);
    CHECK(ed_from_utf8(&e, "A\xcc\x81\xcc\x82" "B"));
    ed_home(&e);
    CHECK(ed_delete(&e));
    text_is(&e, "B");
    CHECK(ed_from_utf8(&e, "\xcc\x81\xcc\x82" "B"));
    position_is(&e, 2, 5, 0, 1);
    ed_home(&e);
    CHECK(ed_cluster_width(&e, 0, 5) == 1);
    CHECK(ed_insert(&e, L'A'));
    CHECK(e.pos == 3);
    text_is(&e, "A\xcc\x81\xcc\x82" "B");
    CHECK(ed_from_utf8(&e, "A\n\xcc\x81\xcc\x82"));
    CHECK(ed_backspace(&e));
    text_is(&e, "A\n");
    CHECK(ed_backspace(&e));
    text_is(&e, "A");
    CHECK(ed_from_utf8(&e, "\xf0\x9f\x98\x80"));
    CHECK(e.len == 1 && e.bytes == 4);
    text_is(&e, "\xf0\x9f\x98\x80");
}

static void test_invalid_and_limits(void)
{
    static const char *bad[] = {
        "\x80", "\xc0\x80", "\xc1\xbf", "\xc2", "\xe0\x80\x80",
        "\xe0\xa0", "\xed\xa0\x80", "\xef\xbf", "\xf0\x80\x80\x80",
        "\xf0\x90\x80", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80",
        "\xff", "a\r", "\t", "\x1b[31m", "\xc2\x80", "\xc2\x9b",
        "\xe2\x80\xae", "\xe2\x81\xa6", "\xd8\x9c", NULL
    };
    Editor e;
    size_t i;
    char out[4] = {'x', 'x', 'x', 'x'};
    CHECK(ed_from_utf8(&e, "abc"));
    for (i = 0; bad[i]; i++) {
        CHECK(!ed_from_utf8(&e, bad[i]));
        text_is(&e, "abc");
    }
    CHECK(!ed_from_utf8(&e, NULL));
    CHECK(ed_to_utf8(&e, out, 3) == SIZE_MAX);
    CHECK(out[0] == 'x');
    CHECK(ed_to_utf8(&e, out, sizeof(out)) == 3);
    CHECK(!strcmp(out, "abc"));
    CHECK(!ed_insert(&e, (wchar_t)0x110000));
    CHECK(!ed_insert(&e, (wchar_t)0xdfff));
    CHECK(!ed_insert(&e, (wchar_t)-1));
    ed_init(&e);
    for (i = 0; i < ED_MAX_CHARS; i++) CHECK(ed_insert(&e, L'x'));
    CHECK(!ed_insert(&e, L'x'));
    CHECK(e.len == ED_MAX_CHARS && e.bytes == ED_MAX_CHARS);
    CHECK(ed_backspace(&e));
    CHECK(ed_insert(&e, L'y'));
    ed_init(&e);
    for (i = 0; i < ED_MAX_BYTES / 4; i++)
        CHECK(ed_insert(&e, (wchar_t)0x1f600));
    CHECK(e.bytes == ED_MAX_BYTES);
    CHECK(!ed_insert(&e, L'x'));
    CHECK(ed_backspace(&e));
    CHECK(ed_insert(&e, L'y'));
}

static void test_selection_all(void)
{
    static const wchar_t invalid[] = {
        L'\0', L'\r', L'\t', (wchar_t)0x1b, (wchar_t)0x7f,
        (wchar_t)0x9b, (wchar_t)0x202e, (wchar_t)0xdfff,
        (wchar_t)0x110000, (wchar_t)-1
    };
    static const char mixed[] =
        "A\xc3\xa9\n\xe4\xb8\xad" "e\xcc\x81\xf0\x9f\x98\x80";
    Editor e, before;
    size_t i;
    int rows, row, col;
    ed_init(&e);
    CHECK(!e.selection_all);
    ed_select_all(&e);
    CHECK(!e.selection_all && !e.len && !e.pos);
    CHECK(!ed_backspace(&e) && !ed_delete(&e));
    CHECK(ed_insert(&e, L'a'));
    CHECK(!e.selection_all);

    CHECK(ed_from_utf8(&e, mixed));
    ed_home(&e);
    ed_select_all(&e);
    CHECK(e.selection_all && e.pos == e.len && e.goal_col == -1);
    text_is(&e, mixed);
    ed_select_all(&e);
    CHECK(e.selection_all && e.pos == e.len);
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        memcpy(&before, &e, sizeof(e));
        CHECK(!ed_insert(&e, invalid[i]));
        CHECK(!memcmp(&before, &e, sizeof(e)));
    }
    memcpy(&before, &e, sizeof(e));
    CHECK(!ed_from_utf8(&e, "bad\xff"));
    CHECK(!memcmp(&before, &e, sizeof(e)));
    CHECK(!ed_from_utf8(&e, NULL));
    CHECK(!memcmp(&before, &e, sizeof(e)));

    /* The first valid pasted scalar replaces everything; later scalars append.
     * Byte accounting follows the replacement, including composed newlines. */
    CHECK(ed_insert(&e, (wchar_t)0x1f600));
    CHECK(!e.selection_all && e.len == 1 && e.pos == 1 && e.bytes == 4);
    CHECK(ed_insert(&e, (wchar_t)0xe9));
    CHECK(ed_insert(&e, L'\n'));
    CHECK(ed_insert(&e, L'x'));
    text_is(&e, "\xf0\x9f\x98\x80\xc3\xa9\nx");
    CHECK(e.len == 4 && e.bytes == 8);

    /* Selection removal does not depend on the caret's position. */
    ed_select_all(&e);
    e.pos = 0;
    CHECK(ed_backspace(&e));
    CHECK(!e.selection_all && !e.len && !e.pos && !e.bytes);
    text_is(&e, "");
    CHECK(ed_from_utf8(&e, mixed));
    ed_select_all(&e);
    CHECK(ed_delete(&e)); /* Select All leaves the caret at the end. */
    CHECK(!e.selection_all && !e.len && !e.pos && !e.bytes);
    CHECK(ed_from_utf8(&e, mixed));
    ed_select_all(&e);
    e.pos = 2;
    CHECK(ed_delete(&e));
    text_is(&e, "");

    CHECK(ed_from_utf8(&e, "abcdefghij"));
    ed_select_all(&e);
    ed_left(&e);
    CHECK(!e.selection_all && e.pos == 0 && e.goal_col == -1);
    text_is(&e, "abcdefghij");
    ed_select_all(&e);
    ed_right(&e);
    CHECK(!e.selection_all && e.pos == e.len);
    ed_select_all(&e);
    ed_home(&e);
    CHECK(!e.selection_all && e.pos == 0);
    ed_select_all(&e);
    ed_end(&e);
    CHECK(!e.selection_all && e.pos == e.len);
    ed_select_all(&e);
    ed_move_vertical(&e, -1, 5);
    CHECK(!e.selection_all && e.pos == 5 && e.goal_col == 0);
    ed_select_all(&e);
    ed_move_vertical(&e, 1, 5);
    CHECK(!e.selection_all && e.pos == e.len);
    ed_select_all(&e);
    ed_move_vertical(&e, 0, 5);
    CHECK(e.selection_all && e.pos == e.len);
    e.goal_col = 3;
    memcpy(&before, &e, sizeof(e));
    before.selection_all = 0;
    ed_unselect(&e);
    CHECK(!memcmp(&before, &e, sizeof(e)));
    CHECK(ed_insert(&e, L'k'));
    text_is(&e, "abcdefghijk");
    ed_select_all(&e);
    CHECK(ed_from_utf8(&e, "new draft"));
    CHECK(!e.selection_all);
    text_is(&e, "new draft");
    ed_select_all(&e);
    CHECK(ed_from_utf8(&e, ""));
    CHECK(!e.selection_all && !e.len);

    /* Selecting a full draft includes the many rows outside any input
     * viewport, and capacity checks apply to the replacement, not old text. */
    for (i = 0; i < ED_MAX_CHARS; ++i) CHECK(ed_insert(&e, L'x'));
    CHECK(!ed_insert(&e, L'y'));
    ed_select_all(&e);
    ed_layout(&e, 80, &rows, &row, &col);
    CHECK(e.selection_all && e.pos == ED_MAX_CHARS && rows > 100 && row > 100);
    memcpy(&before, &e, sizeof(e));
    CHECK(!ed_insert(&e, (wchar_t)0x1b));
    CHECK(!memcmp(&before, &e, sizeof(e)));
    CHECK(ed_insert(&e, (wchar_t)0x1f600));
    CHECK(!e.selection_all && e.len == 1 && e.pos == 1 && e.bytes == 4);
    text_is(&e, "\xf0\x9f\x98\x80");
    ed_init(&e);
    for (i = 0; i < ED_MAX_BYTES / 4; ++i)
        CHECK(ed_insert(&e, (wchar_t)0x1f600));
    CHECK(e.bytes == ED_MAX_BYTES && !ed_insert(&e, L'x'));
    ed_select_all(&e);
    CHECK(ed_insert(&e, (wchar_t)0xe9));
    CHECK(!e.selection_all && e.len == 1 && e.pos == 1 && e.bytes == 2);
    text_is(&e, "\xc3\xa9");
    ed_select_all(&e);
    CHECK(ed_insert(&e, L'\n'));
    text_is(&e, "\n");
    CHECK(!e.selection_all && e.bytes == 1);
    ed_select_all(&e);
    ed_init(&e);
    CHECK(!e.selection_all && !e.len && !e.pos && !e.bytes);
}

static void cell_is(Editor *e, int row, int col, int width, size_t expected)
{
    Editor before;
    ed_select_all(e);
    e->goal_col = 7;
    memcpy(&before, e, sizeof(before));
    ed_set_cell(e, row, col, width);
    CHECK(e->pos == expected && !e->selection_all && e->goal_col == -1);
    CHECK(e->len == before.len && e->bytes == before.bytes);
    CHECK(!memcmp(e->data, before.data, sizeof(e->data)));
}

static void test_mouse_cells(void)
{
    Editor e;
    ed_init(&e);
    cell_is(&e, 100, 100, 80, 0);
    CHECK(ed_from_utf8(&e, "abcdefghij"));
    cell_is(&e, 0, 0, 5, 0);
    cell_is(&e, 0, 4, 5, 4);
    cell_is(&e, 1, 0, 5, 5);
    cell_is(&e, 1, 4, 5, 9);
    cell_is(&e, 2, 0, 5, 10);
    cell_is(&e, 2, 4, 5, 10);
    cell_is(&e, INT_MIN, INT_MIN, 5, 0);
    cell_is(&e, INT_MAX, INT_MAX, 5, 10);
    cell_is(&e, 2, 0, 0, 2);
    cell_is(&e, 2, 0, -1, 2);
    cell_is(&e, 0, INT_MAX, INT_MAX, 10);

    CHECK(ed_from_utf8(&e, "abc\ndefgh\n\nx"));
    cell_is(&e, 0, 77, 80, 3);  /* End of a short explicit line. */
    cell_is(&e, 1, 2, 80, 6);
    cell_is(&e, 1, 77, 80, 9);
    cell_is(&e, 2, 4, 80, 10); /* Empty explicit line. */
    cell_is(&e, 3, 0, 80, 11);
    cell_is(&e, 3, 1, 80, 12);
    cell_is(&e, 9, 0, 80, 11); /* Below text, retain the requested column. */
    cell_is(&e, 9, 77, 80, 12);

    CHECK(ed_from_utf8(&e, "a\xe4\xb8\xad" "b"));
    cell_is(&e, 0, 0, 80, 0);
    cell_is(&e, 0, 1, 80, 1);
    cell_is(&e, 0, 2, 80, 1);  /* Tie inside a wide glyph prefers its start. */
    cell_is(&e, 0, 3, 80, 2);
    cell_is(&e, 0, 4, 80, 3);
    cell_is(&e, 1, 0, 1, 1);   /* One-cell fallback in a one-column editor. */
    CHECK(ed_from_utf8(&e, "ab\xe4\xb8\xad" "c"));
    cell_is(&e, 0, 2, 3, 1);   /* Blank cell before a glyph forced to wrap. */
    cell_is(&e, 1, 0, 3, 2);
    cell_is(&e, 1, 1, 3, 2);
    cell_is(&e, 1, 2, 3, 3);
    cell_is(&e, 2, 0, 3, 4);   /* Real caret row after an exactly full line. */

    CHECK(ed_from_utf8(&e, "A\xcc\x81" "B"));
    cell_is(&e, 0, 0, 5, 0);
    cell_is(&e, 0, 1, 5, 2);   /* Never land inside a combining group. */
    cell_is(&e, 0, 2, 5, 3);
    CHECK(ed_from_utf8(&e, "\xcc\x81\xcc\x82" "B"));
    cell_is(&e, 0, 0, 5, 0);   /* Leading combining group has a placeholder. */
    cell_is(&e, 0, 1, 5, 2);
    CHECK(ed_from_utf8(&e, "abc\nx"));
    cell_is(&e, 1, 0, 3, 3);   /* Coincident LF boundaries prefer the earlier. */
    cell_is(&e, 1, 1, 3, 5);
    CHECK(ed_from_utf8(&e, "abc\n\nx"));
    cell_is(&e, 1, 0, 3, 3);
    cell_is(&e, 2, 0, 3, 5);
    cell_is(&e, 2, 1, 3, 6);
}

static uint32_t random_state = 0xc1e17U;

static uint32_t next_random(void)
{
    random_state = random_state * 1664525U + 1013904223U;
    return random_state;
}

static void test_reference_edits(void)
{
    Editor e;
    char ref[ED_MAX_CHARS + 1] = {0};
    size_t n = 0, pos = 0, step;
    ed_init(&e);
    for (step = 0; step < 50000; step++) {
        unsigned op = next_random() % 8;
        if (op == 0 || op == 1) {
            char c = (char)('a' + next_random() % 26);
            if (n < ED_MAX_CHARS) {
                CHECK(ed_insert(&e, (wchar_t)c));
                memmove(ref + pos + 1, ref + pos, n - pos + 1);
                ref[pos++] = c;
                n++;
            }
        } else if (op == 2) {
            CHECK(ed_backspace(&e) == (pos > 0));
            if (pos) {
                memmove(ref + pos - 1, ref + pos, n - pos + 1);
                pos--;
                n--;
            }
        } else if (op == 3) {
            CHECK(ed_delete(&e) == (pos < n));
            if (pos < n) {
                memmove(ref + pos, ref + pos + 1, n - pos);
                n--;
            }
        } else if (op == 4) {
            ed_left(&e);
            if (pos) pos--;
        } else if (op == 5) {
            ed_right(&e);
            if (pos < n) pos++;
        } else if (op == 6) {
            ed_home(&e);
            pos = 0;
        } else {
            ed_end(&e);
            pos = n;
        }
        CHECK(e.pos == pos && e.len == n);
        text_is(&e, ref);
        position_is(&e, pos, 7, (int)pos / 7, (int)pos % 7);
    }
}

static void test_private_value_replacement(void)
{
    Editor value, before;
    CHECK(ed_from_utf8(&value, "previous-secret-with-a-long-tail"));
    before = value;
    CHECK(!ed_from_utf8(&value, "partial-new-secret\xff"));
    CHECK(!memcmp(&value, &before, sizeof value));
    CHECK(!ed_from_utf8(&value, "partial-new-secret\033"));
    CHECK(!memcmp(&value, &before, sizeof value));
    CHECK(ed_from_utf8(&value, "new"));
    text_is(&value, "new");
    CHECK(value.goal_col == -1 && !value.selection_all);
    for (size_t i = value.len + 1; i < sizeof value.data / sizeof value.data[0]; ++i)
        CHECK(value.data[i] == 0);
    CHECK(ed_from_utf8(&value, ""));
    CHECK(!value.len && !value.bytes && value.goal_col == -1);
}

int main(void)
{
    if (!setlocale(LC_CTYPE, "C.UTF-8") &&
        !setlocale(LC_CTYPE, "en_US.UTF-8") && !setlocale(LC_CTYPE, "")) {
        fputs("A UTF-8 locale is required for editor width tests.\n", stderr);
        return 1;
    }
    test_flat_editing();
    test_private_value_replacement();
    test_geometry();
    test_unicode();
    test_invalid_and_limits();
    test_selection_all();
    test_mouse_cells();
    test_reference_edits();
    printf("editor: %lu checks passed\n", checks);
    return 0;
}
