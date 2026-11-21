/* Focused display-layout regressions. No terminal, user account, or network is
 * used. Source text is always logical; only returned glyphs are visual. */
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fault only the layout's own allocations, not libc/FriBidi internals or the
 * fixture. Every successful block must be freed even when a later allocation
 * fails; no partial layout may escape to a renderer. */
static size_t allocation_calls, allocation_live;
static long allocation_fail_at = -1;

static int allocation_allowed(void)
{
    size_t current = allocation_calls++;
    return allocation_fail_at < 0 || current != (size_t)allocation_fail_at;
}

static void *layout_malloc(size_t bytes)
{
    if (!allocation_allowed()) return NULL;
    void *result = malloc(bytes);
    if (result) ++allocation_live;
    return result;
}

static void *layout_calloc(size_t count, size_t bytes)
{
    if (!allocation_allowed()) return NULL;
    void *result = calloc(count, bytes);
    if (result) ++allocation_live;
    return result;
}

static void layout_free(void *pointer)
{
    if (pointer) { assert(allocation_live); --allocation_live; }
    free(pointer);
}

#define malloc layout_malloc
#define calloc layout_calloc
#define free layout_free
#include "../src/arabic_visual.h"
#undef free
#undef calloc
#undef malloc

static const char beh_pair[] = "\xd8\xa8\xd8\xa8";
static const char lam_alef[] = "\xd9\x84\xd8\xa7";
static const char marked_beh[] = "\xd8\xa8\xd9\x8e\xd8\xa8";

static void enabled(const char *value)
{
    assert(!setenv("ONEKEYIRC_ARABIC_VISUAL", value, 1));
    assert(!setenv("VIPIRC_ARABIC_VISUAL", value, 1));
}

static void check_environment(void)
{
#ifdef ONEKEY_ONLY
    const char *primary = "ONEKEYIRC_ARABIC_VISUAL";
    const char *fallback = "VIPIRC_ARABIC_VISUAL";
#else
    const char *primary = "VIPIRC_ARABIC_VISUAL";
    const char *fallback = "ONEKEYIRC_ARABIC_VISUAL";
#endif
    assert(!setenv("ONEKEYIRC_LANG", "en", 1));
    assert(!setenv("VIPIRC_LANG", "en", 1));
    assert(!unsetenv(primary)); assert(!unsetenv(fallback));
    assert(!irc_arabic_visual_selected());
    assert(!setenv(fallback, "1", 1));
    assert(irc_arabic_visual_selected());
    assert(!setenv(primary, "0", 1));
    assert(!irc_arabic_visual_selected());
    assert(!setenv(primary, "", 1));
    assert(irc_arabic_visual_selected());
    assert(!setenv(primary, "ViSuAl", 1));
    assert(!setenv(fallback, "0", 1));
    assert(irc_arabic_visual_selected());
    assert(!setenv(primary, "yes", 1));
    assert(!irc_arabic_visual_selected());
    enabled("1");
}

static IrcArabicLayout *checked(const char *src, size_t bytes, int width,
                                unsigned flags)
{
    char *copy = malloc(bytes);
    IrcArabicLayout *layout;
    size_t previous_past = 0;
    assert(copy);
    memcpy(copy, src, bytes);
    layout = irc_arabic_layout_utf8(src, bytes, width, flags);
    assert(layout);
    assert(!memcmp(copy, src, bytes));
    free(copy);
    assert(layout->count <= IRC_ARABIC_MAX_SCALARS);
    assert(layout->rows > 0 && layout->rows <= (int)layout->count + 1);
    assert(layout->byte_offsets[0] == 0);
    assert(layout->byte_offsets[layout->count] == bytes);
    for (size_t i = 1; i <= layout->count; ++i)
        assert(layout->byte_offsets[i] > layout->byte_offsets[i - 1]);
    for (int row = 0; row < layout->rows; ++row) {
        const IrcArabicLine *line = &layout->lines[row];
        int cells = 0, previous_col = -1;
        assert(line->first == previous_past);
        assert(line->first <= line->past && line->past <= layout->count);
        assert(line->byte_first == layout->byte_offsets[line->first]);
        assert(line->byte_past == layout->byte_offsets[line->past]);
        assert(line->glyph_first <= line->glyph_past);
        assert(line->glyph_past <= layout->glyph_count);
        for (size_t i = line->glyph_first; i < line->glyph_past; ++i) {
            const IrcArabicGlyph *glyph = &layout->glyphs[i];
            assert(glyph->row == row);
            assert(glyph->first >= line->first && glyph->past <= line->past);
            assert(glyph->first < glyph->past);
            assert(glyph->byte_first == layout->byte_offsets[glyph->first]);
            assert(glyph->byte_past == layout->byte_offsets[glyph->past]);
            assert(glyph->col >= previous_col && glyph->col >= 0);
            assert(glyph->col < width && glyph->cells >= 0);
            assert(glyph->col + glyph->cells <= width);
            assert(glyph->codepoint != 0xfeffu && glyph->codepoint != 0x200eu);
            assert(glyph->codepoint != 0x200fu && glyph->codepoint != 0x200cu);
            assert(glyph->codepoint != 0x200du);
            if (glyph->cells == 0) {
                const IrcArabicGlyph *previous;
                assert(i > line->glyph_first);
                previous = &layout->glyphs[i - 1u];
                assert(previous->first == glyph->first && previous->past == glyph->past);
                assert(previous->row == glyph->row && previous->col == glyph->col);
            }
            previous_col = glyph->col;
            cells += glyph->cells;
        }
        assert(cells == line->cells);
        assert(cells <= width - ((flags & IRC_ARABIC_EDIT) && width > 1));
        previous_past = line->past;
    }
    if (!(flags & IRC_ARABIC_SINGLE_LINE)) assert(previous_past == layout->count);
    else assert(layout->rows == 1);
    for (size_t i = 0; i < layout->caret_count; ++i) {
        const IrcArabicCaret *caret = &layout->carets[i];
        assert(caret->index <= layout->count && layout->boundary[caret->index]);
        assert(caret->row >= 0 && caret->row < layout->rows);
        assert(caret->col >= 0 && caret->col < width);
        if (i) assert(irc_arabic_caret_compare(&layout->carets[i - 1u], caret) < 0);
    }
    for (size_t i = 0; i <= layout->count; ++i) if (layout->boundary[i]) {
        int found = 0;
        assert(layout->logical_row[i] >= 0 && layout->logical_row[i] < layout->rows);
        assert(layout->logical_col[i] >= 0 && layout->logical_col[i] < width);
        for (size_t j = 0; j < layout->caret_count; ++j) {
            const IrcArabicCaret *caret = &layout->carets[j];
            if (caret->index == i && caret->row == layout->logical_row[i] &&
                caret->col == layout->logical_col[i]) { found = 1; break; }
        }
        assert(found);
    }
    return layout;
}

static void check_known_forms(void)
{
    IrcArabicLayout *layout = checked(beh_pair, sizeof beh_pair - 1u, 10, 0);
    assert(layout->rows == 1 && layout->glyph_count == 2);
    assert(layout->glyphs[0].codepoint == 0xfe90u);
    assert(layout->glyphs[1].codepoint == 0xfe91u);
    assert(layout->glyphs[0].first == 1 && layout->glyphs[1].first == 0);
    assert(layout->glyphs[0].col == 8 && layout->glyphs[1].col == 9);
    irc_arabic_layout_free(layout);
    layout = checked(beh_pair, sizeof beh_pair - 1u, 1, 0);
    assert(layout->rows == 2);
    assert(layout->glyphs[0].codepoint == 0xfe8fu);
    assert(layout->glyphs[1].codepoint == 0xfe8fu);
    irc_arabic_layout_free(layout);
    layout = checked(beh_pair, sizeof beh_pair - 1u, 1, IRC_ARABIC_EDIT);
    assert(layout->caret_count == 4);
    assert(layout->carets[0].index == 1 && layout->carets[0].row == 0);
    assert(layout->carets[1].index == 0 && layout->carets[1].row == 0);
    assert(layout->carets[2].index == 2 && layout->carets[2].row == 1);
    assert(layout->carets[3].index == 1 && layout->carets[3].row == 1);
    irc_arabic_layout_free(layout);
    layout = checked(lam_alef, sizeof lam_alef - 1u, 10, 0);
    assert(layout->glyph_count == 1 && layout->lines[0].cells == 1);
    assert(layout->glyphs[0].codepoint == 0xfefbu);
    assert(layout->glyphs[0].first == 0 && layout->glyphs[0].past == 2);
    assert(layout->glyphs[0].byte_first == 0 && layout->glyphs[0].byte_past == 4);
    assert(!layout->boundary[1]);
    irc_arabic_layout_free(layout);
    layout = checked(lam_alef, sizeof lam_alef - 1u, 10, IRC_ARABIC_EDIT);
    assert(layout->glyph_count == 2 && layout->lines[0].cells == 2);
    assert(layout->glyphs[0].codepoint == 0xfe8eu);
    assert(layout->glyphs[1].codepoint == 0xfedfu);
    assert(layout->boundary[0] && layout->boundary[1] && layout->boundary[2]);
    assert(layout->logical_col[0] == 9 && layout->logical_col[2] == 7);
    irc_arabic_layout_free(layout);
    layout = checked(marked_beh, sizeof marked_beh - 1u, 10, IRC_ARABIC_EDIT);
    assert(layout->glyph_count == 3);
    assert(layout->glyphs[1].codepoint == 0xfe91u);
    assert(layout->glyphs[2].codepoint == 0x064eu);
    assert(layout->glyphs[1].first == 0 && layout->glyphs[1].past == 2);
    assert(!layout->boundary[1]);
    assert(layout->boundary[0] && layout->boundary[2] && layout->boundary[3]);
    irc_arabic_layout_free(layout);
}

static void check_paragraphs_and_tokens(void)
{
    static const char text[] =
        "\xd8\xa8\xd8\xa8 /help keys --resume #channel Nick_7 "
        "https://example.invalid/a?x=1&y=2 \xd9\x84\xd8\xa7\n"
        "[12:34] <Nick> \xd8\xa8\xd8\xa8\n\n/help keys\n";
    IrcArabicLayout *layout = checked(text, sizeof text - 1u, 200, 0);
    char ascii[512];
    size_t used = 0;
    assert(layout->rows == 5);
    assert(layout->lines[0].rtl && !layout->lines[1].rtl);
    assert(layout->lines[2].rtl && !layout->lines[3].rtl);
    assert(layout->lines[0].past == layout->lines[1].first);
    assert(text[layout->lines[0].byte_past - 1u] == '\n');
    for (size_t i = layout->lines[0].glyph_first; i < layout->lines[0].glyph_past; ++i)
        if (layout->glyphs[i].codepoint < 128u) ascii[used++] = (char)layout->glyphs[i].codepoint;
    ascii[used] = '\0';
    assert(strstr(ascii, "/help keys --resume #channel Nick_7 https://example.invalid/a?x=1&y=2"));
    used = 0;
    for (size_t i = layout->lines[3].glyph_first; i < layout->lines[3].glyph_past; ++i)
        ascii[used++] = (char)layout->glyphs[i].codepoint;
    ascii[used] = '\0';
    assert(!strcmp(ascii, "/help keys"));
    assert(layout->glyphs[layout->lines[3].glyph_first].col == 0);
    irc_arabic_layout_free(layout);
    for (int width = 1; width <= 24; ++width) {
        for (unsigned flags = 0; flags <= IRC_ARABIC_EDIT; ++flags) {
            layout = checked(text, sizeof text - 1u, width, flags);
            size_t first_newline = 0;
            while (text[first_newline] != '\n') ++first_newline;
            for (int row = 0; row < layout->rows; ++row)
                if (layout->lines[row].byte_first < first_newline) assert(layout->lines[row].rtl);
            irc_arabic_layout_free(layout);
        }
    }
    layout = checked(text, sizeof text - 1u, 2, IRC_ARABIC_SINGLE_LINE);
    assert(layout->rows == 1 && layout->lines[0].first == 0 && layout->lines[0].past == 2);
    assert(layout->glyph_count == 2 && layout->glyphs[0].first == 1);
    assert(layout->logical_row[3] == -1);
    irc_arabic_layout_free(layout);
}

static void check_bounds_and_fallback(void)
{
    char *maximum = malloc(IRC_ARABIC_MAX_BYTES + 2u);
    wchar_t *wide = calloc(IRC_ARABIC_MAX_SCALARS + 1u, sizeof *wide);
    IrcArabicLayout *layout;
    assert(maximum && wide);
    for (size_t i = 0; i < IRC_ARABIC_MAX_SCALARS; ++i) {
        maximum[i * 2u] = (char)0xd8;
        maximum[i * 2u + 1u] = (char)0xa8;
        wide[i] = (wchar_t)0x0628u;
    }
    layout = checked(maximum, IRC_ARABIC_MAX_BYTES, 80, IRC_ARABIC_EDIT);
    assert(layout->count == IRC_ARABIC_MAX_SCALARS);
    irc_arabic_layout_free(layout);
    layout = irc_arabic_layout_wide(wide, IRC_ARABIC_MAX_SCALARS, 80, IRC_ARABIC_EDIT);
    assert(layout && layout->count == IRC_ARABIC_MAX_SCALARS);
    irc_arabic_layout_free(layout);
    assert(!irc_arabic_layout_wide(wide, IRC_ARABIC_MAX_SCALARS + 1u, 80, 0));
    assert(!irc_arabic_layout_utf8(maximum, IRC_ARABIC_MAX_BYTES + 1u, 80, 0));
    free(maximum); free(wide);
    assert(!irc_arabic_layout_utf8("/help --resume", 14, 80, 0));
    assert(!irc_arabic_layout_utf8(NULL, 0, 80, 0));
    assert(!irc_arabic_layout_utf8(beh_pair, sizeof beh_pair - 1u, 0, 0));
    assert(!irc_arabic_layout_utf8(beh_pair, sizeof beh_pair - 1u, 80, 4u));
    assert(!irc_arabic_layout_utf8("\xd8\xa8\x1b[2J", 6, 80, 0));
    assert(!irc_arabic_layout_utf8("\xd8\xa8\xe2\x80\xae", 5, 80, 0));
    assert(!irc_arabic_layout_utf8("\xd8\xa8\xc0\xaf", 4, 80, 0));
    assert(!irc_arabic_layout_utf8("\xd8\xa8\xed\xa0\x80", 5, 80, 0));
    enabled("0");
    assert(!irc_arabic_layout_utf8(beh_pair, sizeof beh_pair - 1u, 80, 0));
    enabled("1");
}

static void check_cluster_matrix(void)
{
    static const uint32_t scalars[] = {
        0x0628u, 0x0644u, 0x0627u, 0x064eu, 0x0640u, 0x200du, 0x200cu,
        0x0301u, 0x05d0u, 0x1f642u, 0x3000u, 'a', '7', '/', ':', '?',
        '_', '-', '#', ' ', '\n', '(', ')'
    };
    uint32_t state = 0x384731u;
    for (unsigned trial = 0; trial < 160; ++trial) {
        char input[1024];
        size_t bytes = irc_arabic_encode(0x0628u, input);
        int width = (int)(trial % 19u) + 1;
        for (unsigned i = 0; i < 60; ++i) {
            state = state * 1664525u + 1013904223u;
            bytes += irc_arabic_encode(scalars[state % (sizeof scalars / sizeof scalars[0])], input + bytes);
        }
        for (unsigned flags = 0; flags < 4; ++flags) {
            IrcArabicLayout *layout = checked(input, bytes, width, flags);
            IrcArabicLayout *again = irc_arabic_layout_utf8(input, bytes, width, flags);
            assert(again && layout->rows == again->rows);
            assert(layout->glyph_count == again->glyph_count && layout->caret_count == again->caret_count);
            assert(!memcmp(layout->glyphs, again->glyphs, layout->glyph_count * sizeof *layout->glyphs));
            assert(!memcmp(layout->carets, again->carets, layout->caret_count * sizeof *layout->carets));
            assert(!memcmp(layout->lines, again->lines, (size_t)layout->rows * sizeof *layout->lines));
            irc_arabic_layout_free(again);
            irc_arabic_layout_free(layout);
        }
    }
}

static void check_wide_preflight(int available)
{
    wchar_t input[IRC_ARABIC_MAX_SCALARS];
    for (size_t i = 0; i < IRC_ARABIC_MAX_SCALARS; ++i) input[i] = 0x0628;
    allocation_calls = 0;
    assert(!irc_arabic_layout_wide(input, IRC_ARABIC_MAX_SCALARS, 80, 4u));
    assert(!allocation_calls && !allocation_live);
    if (!available) {
        allocation_calls = 0;
        assert(!irc_arabic_layout_wide(input, IRC_ARABIC_MAX_SCALARS, 80, IRC_ARABIC_EDIT));
        assert(!allocation_calls && !allocation_live);
    }
}

static void check_allocation_failures(void)
{
    static const wchar_t source[] = L"\u0628\u064e\u0628 /msg #room 123 \u0644\u0627";
    wchar_t unchanged[sizeof source / sizeof source[0]];
    memcpy(unchanged, source, sizeof source);
    allocation_calls = 0;
    IrcArabicLayout *layout = irc_arabic_layout_wide(source, wcslen(source), 5, IRC_ARABIC_EDIT);
    assert(layout);
    size_t count = allocation_calls;
    irc_arabic_layout_free(layout);
    assert(!allocation_live && count > 1);
    for (size_t failure = 0; failure < count; ++failure) {
        allocation_calls = 0;
        allocation_fail_at = (long)failure;
        assert(!irc_arabic_layout_wide(source, wcslen(source), 5, IRC_ARABIC_EDIT));
        assert(!allocation_live);
        assert(!memcmp(source, unchanged, sizeof source));
    }
    allocation_fail_at = -1;
    layout = irc_arabic_layout_wide(source, wcslen(source), 5, IRC_ARABIC_EDIT);
    assert(layout);
    irc_arabic_layout_free(layout);
    assert(!allocation_live);
}

int main(void)
{
    assert(setlocale(LC_ALL, "C"));
    enabled("1");
    assert(!irc_arabic_utf8_locale());
    assert(!irc_arabic_layout_utf8(beh_pair, sizeof beh_pair - 1u, 80, 0));
    if (!setlocale(LC_ALL, "C.UTF-8")) (void)setlocale(LC_ALL, "");
    check_environment();
    int available = irc_arabic_api() && irc_arabic_api()->layout_ready &&
                    irc_arabic_utf8_locale() && wcwidth((wchar_t)0xfe91u) == 1;
    check_wide_preflight(available);
    if (!available) {
        assert(!irc_arabic_layout_utf8(beh_pair, sizeof beh_pair - 1u, 80, 0));
        puts("Arabic layout: PASS (unavailable-library or locale fallback)");
        return 0;
    }
    /* An older runtime can expose log2vis while lacking a paragraph symbol.
     * This differs from a completely disabled loader and must be just as cheap. */
    IrcArabicApi *partial = (IrcArabicApi *)irc_arabic_api();
    partial->layout_ready = 0;
    check_wide_preflight(0);
    partial->layout_ready = 1;
    check_known_forms();
    check_paragraphs_and_tokens();
    check_bounds_and_fallback();
    check_cluster_matrix();
    check_allocation_failures();
    assert(!allocation_live);
    puts("Arabic layout: PASS (paragraphs, wrapping, source maps, carets, clusters, tokens, bounds, allocation failures, fallback)");
    return 0;
}
