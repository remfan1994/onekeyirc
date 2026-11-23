/* Fail each temporary allocation in the real shared editor implementation.
 * A successful snapshot must paint and place its caret without another
 * allocation; failure must release everything and retain the logical draft.
 * This fixture includes editor.c and must not link a second editor object. */
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_calls, live_allocations, fail_at;
static int failure_seen;

static int refuse(void)
{
    ++allocation_calls;
    if (allocation_calls != fail_at) return 0;
    failure_seen = 1;
    return 1;
}

static void *injected_malloc(size_t bytes)
{
    if (refuse()) return NULL;
    void *result = malloc(bytes);
    if (result) ++live_allocations;
    return result;
}

static void *injected_calloc(size_t count, size_t size)
{
    if (refuse()) return NULL;
    void *result = calloc(count, size);
    if (result) ++live_allocations;
    return result;
}

static void injected_free(void *pointer)
{
    if (pointer) {
        assert(live_allocations);
        --live_allocations;
    }
    free(pointer);
}

#define malloc injected_malloc
#define calloc injected_calloc
#define free injected_free
#include "../src/editor.c"
#undef malloc
#undef calloc
#undef free

typedef struct Painted {
    int calls, marks, first_row, last_row, last_col;
    wchar_t forms[4];
} Painted;

static void painted(void *opaque, int row, int col, const wchar_t *text,
                    size_t count, int cells, size_t first, size_t past)
{
    Painted *p = opaque;
    assert(row >= p->first_row && col >= 0 && cells > 0);
    assert(first < past && count && text[count] == 0);
    if (!p->calls) p->first_row = row;
    else assert(row > p->last_row || (row == p->last_row && col >= p->last_col));
    p->last_row = row; p->last_col = col;
    if (p->calls < 4) p->forms[p->calls] = text[0];
    ++p->calls;
    for (size_t i = 1; i < count; ++i) {
        assert(wcwidth(text[i]) == 0);
        ++p->marks;
    }
}

static void arm(size_t nth)
{
    allocation_calls = 0;
    fail_at = nth;
    failure_seen = 0;
}

static void check_snapshot(EdVisualSnapshot *snapshot, int expected_row,
                           int expected_col, int expected_calls, int expected_marks)
{
    int rows = -1, row = -1, col = -1;
    Painted p = {0};
    size_t before = allocation_calls;
    fail_at = before + 1;
    ed_visual_snapshot_layout(snapshot, &rows, &row, &col);
    assert(rows > row && row == expected_row && col == expected_col);
    ed_visual_snapshot_render(snapshot, 0, ED_MAX_CHARS + 1, painted, &p);
    assert(p.calls == expected_calls && p.marks == expected_marks);
    assert(allocation_calls == before && !failure_seen);
}

static void failure_matrix(Editor *editor, int width, int row, int col,
                           int calls, int marks)
{
    Editor original = *editor;
    arm(0);
    EdVisualSnapshot *snapshot = ed_visual_snapshot(editor, width);
    assert(snapshot && live_allocations);
    size_t total = allocation_calls;
    check_snapshot(snapshot, row, col, calls, marks);
    ed_visual_snapshot_free(snapshot);
    assert(!live_allocations && !memcmp(editor, &original, sizeof original));
    for (size_t nth = 1; nth <= total; ++nth) {
        arm(nth);
        snapshot = ed_visual_snapshot(editor, width);
        assert(failure_seen && !snapshot && !live_allocations);
        assert(!memcmp(editor, &original, sizeof original));
        /* The next attempt succeeds as a complete unit, instead of combining
         * fallback caret geometry with a later successful visual paint. */
        arm(0);
        snapshot = ed_visual_snapshot(editor, width);
        assert(snapshot);
        check_snapshot(snapshot, row, col, calls, marks);
        ed_visual_snapshot_free(snapshot);
        assert(!live_allocations && !memcmp(editor, &original, sizeof original));
    }
    printf("Arabic editor snapshot: %zu allocation failures released cleanly\n", total);
}

int main(void)
{
    if (!setlocale(LC_ALL, "C.UTF-8")) (void)setlocale(LC_ALL, "");
    assert(!setenv("ONEKEYIRC_ARABIC_VISUAL", "1", 1));
    assert(!setenv("VIPIRC_ARABIC_VISUAL", "1", 1));
    Editor editor;
    assert(ed_from_utf8(&editor, "\xd8\xa8\xd8\xa8"));
    ed_home(&editor);
    arm(0);
    EdVisualSnapshot *snapshot = ed_visual_snapshot(&editor, 10);
    if (!snapshot) {
        assert(!live_allocations);
        const IrcArabicApi *detected = irc_arabic_api();
        int capable = detected && detected->layout_ready && irc_arabic_utf8_locale() &&
                      wcwidth((wchar_t)0xfe91) == 1 && wcwidth((wchar_t)0x25cc) == 1 &&
                      wcwidth((wchar_t)0xfffd) == 1;
        /* A functional converter returning no snapshot is a regression,
         * never an optional-dependency skip. */
        assert(!capable);
#ifdef IRC_NO_ARABIC_VISUAL
        puts("Arabic editor snapshot: PASS (compile-disabled fallback)");
        return 0;
#else
        puts("Arabic editor snapshot: UNAVAILABLE (converter or UTF-8 locale)");
        return 77;
#endif
    }
    ed_visual_snapshot_free(snapshot);
    assert(!live_allocations);

    /* A partially available runtime must request a complete logical fallback,
     * even when the legacy one-line conversion symbol remains usable. */
    IrcArabicApi *api = (IrcArabicApi *)irc_arabic_api();
    assert(api && api->log2vis && api->layout_ready);
    api->layout_ready = 0;
    arm(0);
    assert(!ed_visual_snapshot(&editor, 10) && !live_allocations);
    api->layout_ready = 1;
    assert(!ed_visual_snapshot(NULL, 10) && !live_allocations);
    failure_matrix(&editor, 10, 0, 9, 2, 0);

    assert(ed_from_utf8(&editor, "\xd8\xa8\xd9\x8e\xd8\xaa\n\xd8\xa8\xd9\x8f\xd8\xaa"));
    ed_home(&editor);
    failure_matrix(&editor, 4, 0, 3, 4, 2);

    /* A snapshot represents the requested frame even if the logical editor
     * changes later. It owns glyph storage, never a live Editor pointer. */
    arm(0);
    snapshot = ed_visual_snapshot(&editor, 4);
    assert(snapshot && ed_from_utf8(&editor, "ASCII replacement"));
    check_snapshot(snapshot, 0, 3, 4, 2);
    ed_visual_snapshot_free(snapshot);
    assert(!live_allocations);

    arm(0);
    assert(!ed_visual_snapshot(&editor, 10));
    ed_visual_snapshot_free(NULL);
    assert(!live_allocations);
    puts("Arabic editor snapshot: PASS (coherent geometry/paint, allocation-free render, logical draft unchanged)");
    return 0;
}
