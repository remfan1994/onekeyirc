/* Read actual wide ncurses cells and dispatch production keys/mouse events.
 * The existing transport fixture refuses all socket writes. No screenshots,
 * user configuration, IRC server, or real terminal are involved. */
/* Fault-inject ui.c's optional map factory and frame creation. The editor
 * and transport retain their production allocators and behavior. */
#define irc_arabic_layout_utf8 unfailing_arabic_layout_utf8
#include "../src/arabic_visual.h"
#undef irc_arabic_layout_utf8
static int arabic_layout_failure = -1;
static int arabic_layout_calls;
static inline IrcArabicLayout *irc_arabic_layout_utf8(const char *source, size_t bytes,
                                                     int width, unsigned flags)
{
    ++arabic_layout_calls;
    if (!arabic_layout_failure) { arabic_layout_failure = -1; return NULL; }
    if (arabic_layout_failure > 0) --arabic_layout_failure;
    return unfailing_arabic_layout_utf8(source, bytes, width, flags);
}
#include "../src/editor.h"
static int editor_snapshot_fail_next, editor_snapshot_calls;
static EdVisualSnapshot *test_editor_snapshot(const Editor *editor, int width)
{
    ++editor_snapshot_calls;
    if (editor_snapshot_fail_next) {
        editor_snapshot_fail_next = 0;
        return NULL;
    }
    return ed_visual_snapshot(editor, width);
}
#define ed_visual_snapshot test_editor_snapshot
#define main status_mouse_existing_suite
#include "test_status_mouse.c"
#undef main
#undef ed_visual_snapshot
#include <locale.h>

#if UI_MOUSE
static const char beh_pair[] = "\330\250\330\250";

static wchar_t screen_scalar(int y, int x)
{
    cchar_t cell;
    wchar_t text[CCHARW_MAX + 1];
    attr_t attrs;
    short pair;
    int saved_y, saved_x; getyx(stdscr, saved_y, saved_x);
    assert(mvwin_wch(stdscr, y, x, &cell) != ERR);
    assert(getcchar(&cell, text, &attrs, &pair, NULL) != ERR);
    assert(move(saved_y, saved_x) != ERR);
    return text[0];
}

static int screen_mark(int y, int x, wchar_t mark)
{
    cchar_t cell;
    wchar_t text[CCHARW_MAX + 1];
    attr_t attrs;
    short pair;
    int saved_y, saved_x; getyx(stdscr, saved_y, saved_x);
    assert(mvwin_wch(stdscr, y, x, &cell) != ERR);
    assert(getcchar(&cell, text, &attrs, &pair, NULL) != ERR);
    assert(move(saved_y, saved_x) != ERR);
    for (size_t i = 1; i < CCHARW_MAX && text[i]; ++i)
        if (text[i] == mark) return 1;
    return 0;
}

static void screen_map(const IrcArabicLayout *map, int y, int x, int skip, int rows)
{
    for (size_t i = 0; i < map->glyph_count; ++i) {
        const IrcArabicGlyph *g = &map->glyphs[i];
        if (g->row < skip || g->row - skip >= rows) continue;
        int py = y + g->row - skip, px = x + g->col;
        if (g->cells) assert(screen_scalar(py, px) == (wchar_t)g->codepoint);
        else assert(screen_mark(py, px, (wchar_t)g->codepoint));
    }
}

static void joined_labels_and_clipping(void)
{
    assert(resizeterm(24, 80) != ERR);
    erase();
    mvaddch(2, 2, 'L'); mvaddch(2, 11, 'R');
    text_at(2, 3, 8, beh_pair, A_BOLD);
    /* Two joining beh letters: the final form is physically on the left. */
    assert(screen_scalar(2, 9) == 0xfe90);
    assert(screen_scalar(2, 10) == 0xfe91);
    assert(screen_scalar(2, 2) == 'L' && screen_scalar(2, 11) == 'R');
    assert(mvinch(2, 9) & A_BOLD);
    text_at(3, 3, 8, "\330\250\331\216\330\250", A_NORMAL);
    assert(screen_scalar(3, 9) == 0xfe90);
    assert(screen_scalar(3, 10) == 0xfe91);
    assert(screen_mark(3, 10, 0x064e));
    mvaddch(4, 4, 'R');
    text_at(4, 3, 1, beh_pair, A_NORMAL);
    assert(screen_scalar(4, 3) == 0xfe8f);
    assert(screen_scalar(4, 4) == 'R');

    assert(!setenv("VIPIRC_ARABIC_VISUAL", "0", 1));
    text_at(5, 3, 8, beh_pair, A_NORMAL);
    assert(screen_scalar(5, 3) == 0x0628 && screen_scalar(5, 4) == 0x0628);
    assert(!setenv("VIPIRC_ARABIC_VISUAL", "1", 1));
}

static void marked_margin_cells(void)
{
    static const char marked_beh[] = "\330\250\331\216\331\221\331\223\331\224";
    static const wchar_t marked_form[] = {0xfe8f, 0x064e, 0x0651, 0x0653, 0x0654, 0};
    assert(resizeterm(4, 10) != ERR);
    for (int bottom = 0; bottom <= 1; ++bottom) {
        int y = bottom ? LINES - 1 : 0;
        erase();
        assert(mvaddch(y, COLS - 2, 'L') != ERR);
        text_at(y, COLS - 1, 1, marked_beh, A_BOLD);
        assert(screen_scalar(y, COLS - 1) == 0xfe8f);
        assert(screen_mark(y, COLS - 1, 0x064e));
        assert(screen_mark(y, COLS - 1, 0x0651));
        assert(screen_mark(y, COLS - 1, 0x0653));
        assert(screen_mark(y, COLS - 1, 0x0654));
        assert(mvinch(y, COLS - 1) & A_BOLD);
        assert(screen_scalar(y, COLS - 2) == 'L');
        assert(!screen_mark(y, COLS - 2, 0x064e));
        if (!bottom) assert(screen_scalar(y + 1, 0) == ' ');

        erase();
        assert(mvaddch(y, COLS - 2, 'L') != ERR);
        IrcCursesEditorPaint paint = {
            y, COLS - 1, 1, 1, 0, 0, 0, 1, A_REVERSE | A_UNDERLINE
        };
        assert(attrset(A_BOLD) != ERR);
        irc_curses_editor_cluster(&paint, 0, 0, marked_form, 5, 1, 0, 5);
        assert(screen_scalar(y, COLS - 1) == 0xfe8f);
        assert(screen_mark(y, COLS - 1, 0x064e));
        assert(screen_mark(y, COLS - 1, 0x0651));
        assert(screen_mark(y, COLS - 1, 0x0653));
        assert(screen_mark(y, COLS - 1, 0x0654));
        assert((mvinch(y, COLS - 1) & (A_REVERSE | A_UNDERLINE)) ==
                (A_REVERSE | A_UNDERLINE));
        assert(!(mvinch(y, COLS - 1) & A_BOLD));
        assert(screen_scalar(y, COLS - 2) == 'L');
        assert(!screen_mark(y, COLS - 2, 0x064e));
        if (!bottom) assert(screen_scalar(y + 1, 0) == ' ');
    }
    /* A clipped two-cell base cannot leave an orphan mark on the preceding
     * cell, even when the requested viewport extends past the screen edge. */
    static const char wide_mark[] = "\344\270\255\314\201\330\250";
    IrcArabicLayout *map = visual_text_layout(wide_mark, 3, IRC_ARABIC_SINGLE_LINE);
    assert(map);
    erase();
    assert(mvaddch(0, COLS - 2, 'L') != ERR);
    irc_curses_arabic_paint(map, 0, COLS - 1, 3, 0, 1);
    assert(screen_scalar(0, COLS - 2) == 'L');
    assert(!screen_mark(0, COLS - 2, 0x0301));
    assert(screen_scalar(0, COLS - 1) == ' ');
    assert(!screen_mark(0, COLS - 1, 0x0301));
    assert(screen_scalar(1, 0) == ' ');
    irc_arabic_layout_free(map);
    assert(attrset(A_NORMAL) != ERR);
    assert(resizeterm(24, 80) != ERR);
}

static void wrapped_history_and_targets(void)
{
    static const char paragraph[] =
        "\330\250\330\250 123 (#room) /join "
        "\330\250\331\216\330\250 (abc) "
        "\330\250\330\250 \330\250\330\250";
    for (int width = 7; width <= 19; width += 6) {
        IrcArabicLayout *map = visual_text_layout(paragraph, width, 0);
        assert(map && map->rows > 1);
        assert(history_rows(paragraph, width, -1, 0, 0) == map->rows);
        erase();
        assert(history_rows(paragraph, width, 2, 1, 6) == map->rows);
        screen_map(map, 2, 0, 1, 6);
        irc_arabic_layout_free(map);
    }
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 80);
    size_t target = app_buffer(f.app, "#room", B_CHANNEL, 1);
    assert(target != SIZE_MAX);
    f.app->buffers[target].joined = 1;
    app_log(f.app, f.app->current, "%s", paragraph);
    draw(f.app, f.ui);
    MEVENT click = {0};
    int found = 0;
    for (size_t i = 0; i < f.ui->text_hit_count; ++i) {
        const TextHit *hit = &f.ui->text_hits[i];
        if (hit->channel && !strcmp(hit->target, "#room")) {
            assert(hit->width == 5);
            static const char name[] = "#room";
            for (int j = 0; j < 5; ++j) assert(screen_scalar(hit->y, hit->x + j) == name[j]);
            click.x = hit->x; click.y = hit->y; click.bstate = BUTTON1_PRESSED;
            found = 1;
            break;
        }
    }
    assert(found);
    apply_mouse(f.app, f.ui, click);
    assert(f.app->current == target && !f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void expected_editor(const Editor *editor, int width, int top, int left,
                            int first, int height)
{
    IrcArabicLayout *map = irc_arabic_layout_wide(editor->data, editor->len, width, IRC_ARABIC_EDIT);
    assert(map);
    screen_map(map, top, left, first, height);
    irc_arabic_layout_free(map);
}

static void metadata_allocation_failure(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 80);
    static const char line[] = "\330\250\330\250 #room \330\250\330\250";
    int visual_used;
    erase();
    (void)history_rows_mode(line, 60, 5, 0, 2, &visual_used);
    history_hits(f.ui, line, 60, 5, 0, 2, 0, visual_used);
    assert(visual_used && f.ui->text_hit_count);
    /* Fail paint, then fail metadata: neither inconsistent frame gets a
     * clickable target positioned according to the other ordering. */
    for (int fail = 0; fail < 2; ++fail) {
        f.ui->text_hit_count = 0;
        erase();
        arabic_layout_failure = fail;
        (void)history_rows_mode(line, 60, 5, 0, 2, &visual_used);
        history_hits(f.ui, line, 60, 5, 0, 2, 0, visual_used);
        assert(arabic_layout_failure == -1 && !f.ui->text_hit_count);
    }
    f.ui->manual_match_offset = 0;
    f.ui->manual_match_length = strlen(beh_pair);
    for (int fail = 0; fail < 2; ++fail) {
        erase();
        arabic_layout_failure = fail;
        (void)history_rows_mode(line, 60, 5, 0, 2, &visual_used);
        highlight_manual_match(line, f.ui, 60, 5, 0, 2, visual_used);
        assert(arabic_layout_failure == -1);
        for (int col = 0; col < 60; ++col) assert(!(mvinch(5, col) & A_REVERSE));
    }
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void history_frame_allocation_failure(void)
{
    /* Read-only lam-alef uses one cell; its logical fallback needs two.
     * Failing only the count must not switch presentation before paint. */
    char line[] = "\331\204\330\247\331\204\330\247";
    char *lines[] = {line};
    Buffer buffer = {0};
    buffer.lines = lines;
    buffer.line_count = buffer.line_cap = 1;
    assert(resizeterm(24, 80) != ERR);
    erase();
    arabic_layout_calls = 0;
    arabic_layout_failure = 0;
    draw_history(NULL, &buffer, 3, 2, 2);
    assert(arabic_layout_failure == -1);
    assert(screen_scalar(3, 0) == 0x0644 && screen_scalar(3, 1) == 0x0627);
    assert(screen_scalar(4, 0) == 0x0644 && screen_scalar(4, 1) == 0x0627);
    assert(arabic_layout_calls == 1);

    /* The successful map still anchors the single joined row at the bottom
     * of the same viewport and does not build a second map for painting. */
    erase();
    arabic_layout_calls = 0;
    draw_history(NULL, &buffer, 3, 2, 2);
    assert(arabic_layout_calls == 1);
    assert(screen_scalar(3, 0) == ' ' && screen_scalar(3, 1) == ' ');
    assert(screen_scalar(4, 0) == 0xfefb && screen_scalar(4, 1) == 0xfefb);
    assert(!strcmp(line, "\331\204\330\247\331\204\330\247"));
    assert(!buffer.scroll);
}

static void editor_frame_allocation_failure(void)
{
    Editor editor;
    ed_init(&editor);
    assert(ed_from_utf8(&editor, beh_pair));
    int scroll = 5;
    IrcCursesEditorPosition position = {7, 8, 9, 10, 11};
    erase();
    assert(mvaddch(2, 3, 'X') != ERR);
    editor_snapshot_calls = 0;
    editor_snapshot_fail_next = 1;
    assert(!irc_curses_editor_visual(&editor, 12, 2, 3, 1, &scroll,
                                     A_NORMAL, &position));
    assert(editor_snapshot_calls == 1 && !editor_snapshot_fail_next);
    assert(scroll == 5 && position.total == 7 && position.row == 8 &&
            position.col == 9 && position.first == 10 && position.span == 11);
    assert(screen_scalar(2, 3) == 'X' && screen_scalar(2, 12) == ' ');

    erase();
    editor_snapshot_calls = 0;
    assert(irc_curses_editor_visual(&editor, 12, 2, 3, 1, &scroll,
                                    A_NORMAL, &position));
    assert(editor_snapshot_calls == 1 && scroll == 0 && position.total == 1 &&
            position.row == 0 && position.col == 9 && position.first == 0);
    assert(screen_scalar(2, 12) == 0xfe90 && screen_scalar(2, 13) == 0xfe91);
    assert_editor_text(&editor, beh_pair);

    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 100);
    Editor *draft_editor = draft(f.app, f.ui);
    assert(ed_from_utf8(draft_editor, beh_pair));
    editor_snapshot_calls = 0;
    draw(f.app, f.ui);
    assert(editor_snapshot_calls == 1);
    int y, x;
    getyx(stdscr, y, x);
    assert(y == f.ui->input_top && x == f.ui->input_width - 2);
    char border[256], expected[64];
    assert(mvinnstr(f.ui->input_top - 1, 0, border, COLS) != ERR);
    border[COLS] = 0;
    snprintf(expected, sizeof expected, "row 1 col %d", x);
    assert(strstr(border, expected));

    /* A failed frame must paint the logical fallback, set its actual caret,
     * and report that same logical position in the border. */
    editor_snapshot_calls = 0;
    editor_snapshot_fail_next = 1;
    draw(f.app, f.ui);
    assert(editor_snapshot_calls == 1 && !editor_snapshot_fail_next);
    getyx(stdscr, y, x);
    assert(y == f.ui->input_top && x == 3);
    assert(screen_scalar(y, 1) == 0x0628 && screen_scalar(y, 2) == 0x0628);
    assert(mvinnstr(f.ui->input_top - 1, 0, border, COLS) != ERR);
    border[COLS] = 0;
    assert(strstr(border, "row 1 col 3"));
    assert_editor_text(draft_editor, beh_pair);
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void chat_and_command_editors(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 40);
    Editor *editor = draft(f.app, f.ui);
    assert(ed_from_utf8(editor, beh_pair));
    draw(f.app, f.ui);
    int row, col, y, x;
    ed_visual_position(editor, editor->pos, f.ui->input_width, &row, &col);
    getyx(stdscr, y, x);
    assert(y == f.ui->input_top + row && x == col + 1);
    expected_editor(editor, f.ui->input_width, f.ui->input_top, 1, 0, f.ui->input_rows);
    int before = x;
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    draw(f.app, f.ui); getyx(stdscr, y, x);
    assert(x == before + 1 && editor->pos == 1);
    assert_editor_text(editor, beh_pair);
    MEVENT click = {0}; click.y = f.ui->input_top; click.x = f.ui->input_width;
    click.bstate = BUTTON1_PRESSED;
    apply_mouse(f.app, f.ui, click);
    assert(editor->pos == 0);
    assert_editor_text(editor, beh_pair);

    char wrapped[2049];
    for (size_t i = 0; i < sizeof wrapped - 1; i += 4) memcpy(wrapped + i, beh_pair, 4);
    wrapped[sizeof wrapped - 1] = 0;
    assert(ed_from_utf8(editor, wrapped));
    draw(f.app, f.ui);
    /* Acknowledge the existing long-message warning before navigation. */
    assert(f.ui->confirming == 3);
    fixture_clock += 200;
    key_input(f.app, f.ui, '\n', 0);
    assert(!f.ui->confirming && !f.app->chat_count);
    draw(f.app, f.ui);
    assert(f.ui->input_scroll[f.app->current] > 0);
    expected_editor(editor, f.ui->input_width, f.ui->input_top, 1,
                    f.ui->input_scroll[f.app->current], f.ui->input_rows);
    ed_visual_position(editor, editor->pos, f.ui->input_width, &row, &col);
    int old_row = row;
    key_input(f.app, f.ui, KEY_UP, 1);
    draw(f.app, f.ui);
    ed_visual_layout(editor, f.ui->input_width, NULL, &row, &col);
    getyx(stdscr, y, x);
    assert(row == old_row - 1 && y == f.ui->input_top + row - f.ui->input_scroll[f.app->current]);
    assert(x == col + 1);
    assert(resizeterm(24, 24) != ERR);
    draw(f.app, f.ui);
    expected_editor(editor, f.ui->input_width, f.ui->input_top, 1,
                    f.ui->input_scroll[f.app->current], f.ui->input_rows);
    char saved[sizeof wrapped];
    assert(ed_to_utf8(editor, saved, sizeof saved) == sizeof wrapped - 1);
    assert(!strcmp(saved, wrapped));
    assert(resizeterm(24, 40) != ERR);

    static const char command[] = "/msg #room \330\250\330\250 123";
    assert(open_command(f.app, f.ui, command));
    draw(f.app, f.ui);
    Editor *prompt = prompt_editor(f.ui->command);
    assert(prompt);
    expected_editor(prompt, COLS - 2, 4, 1, 0, 3);
    for (int j = 0; j < 10; ++j) assert(screen_scalar(4, j + 1) == command[j]);
    assert_editor_text(prompt, command);
    assert(ed_from_utf8(prompt, beh_pair));
    draw(f.app, f.ui); getyx(stdscr, y, x); before = x;
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    draw(f.app, f.ui); getyx(stdscr, y, x);
    assert(prompt->pos == 1 && x == before + 1);
    assert_editor_text(prompt, beh_pair);
    click.y = 4; click.x = COLS - 2;
    apply_mouse(f.app, f.ui, click);
    assert(prompt->pos == 0);
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void manual_search_and_highlight(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 40);
    assert(help_command(f.app, f.ui, ""));
    app_clear_buffer(f.app, 2);
    for (int i = 0; i < 20; ++i) app_log(f.app, 2, "Earlier manual line %d", i);
    static const char manual[] = "\330\250\330\250 /join #room 123 "
        "\330\250\330\250 \330\250\330\250 "
        "\330\250\330\250 \330\250\330\250";
    app_log(f.app, 2, "%s", manual);
    key_input(f.app, f.ui, '/', 0);
    assert(f.ui->manual_searching && ed_from_utf8(f.ui->manual_search, beh_pair));
    draw(f.app, f.ui);
    assert(f.ui->manual_search_visual);
    expected_editor(f.ui->manual_search, COLS - 8, LINES - 2, 7, 0, 1);
    int y, x; getyx(stdscr, y, x); int before = x;
    assert(y == LINES - 2);
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    draw(f.app, f.ui); getyx(stdscr, y, x);
    assert(f.ui->manual_search->pos == 1 && x == before + 1);
    MEVENT click = {0}; click.y = LINES - 2; click.x = COLS - 2; click.bstate = BUTTON1_PRESSED;
    apply_mouse(f.app, f.ui, click);
    assert(f.ui->manual_search->pos == 0);
    key_input(f.app, f.ui, '\n', 0);
    assert(f.ui->manual_found && !strcmp(f.ui->manual_term, beh_pair));
    draw(f.app, f.ui);
    assert_manual_highlight();
    Buffer *buffer = &f.app->buffers[2];
    const char *line = buffer->lines[(buffer->line_start + f.ui->manual_match_line) % buffer->line_cap];
    assert(strstr(line, manual));
    /* Source offsets highlight the physically reordered joined cells. */
    IrcArabicLayout *map = visual_text_layout(line, COLS - 1, 0);
    assert(map);
    erase();
    history_rows(line, COLS - 1, 5, 0, 8);
    highlight_manual_match(line, f.ui, COLS - 1, 5, 0, 8, 1);
    size_t end = f.ui->manual_match_offset + f.ui->manual_match_length;
    for (size_t i = 0; i < map->glyph_count; ++i) {
        const IrcArabicGlyph *g = &map->glyphs[i];
        if (!g->cells) continue;
        int matched = g->byte_first < end && g->byte_past > f.ui->manual_match_offset;
        assert(((mvinch(5 + g->row, g->col) & A_REVERSE) != 0) == matched);
    }
    irc_arabic_layout_free(map);
    dispose(&f);
}

static void pair_field(int top, int left, int width)
{
    assert(screen_scalar(top, left + width - 3) == 0xfe90);
    assert(screen_scalar(top, left + width - 2) == 0xfe91);
}

static void settings_and_secret_fields(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 80);
    Settings *settings = settings_create();
    assert(settings);
    settings_open(settings, f.app);
    for (int i = 0; i < 5; ++i) settings_key(settings, f.app, KEY_DOWN, 1);
    settings_key(settings, f.app, '\n', 0); /* Real name */
    settings_key(settings, f.app, 0x0628, 0); settings_key(settings, f.app, 0x0628, 0);
    erase(); settings_draw(settings, f.app, text_at);
    pair_field(6, 2, COLS - 4);
    int y, x; getyx(stdscr, y, x); int before = x;
    settings_key(settings, f.app, KEY_RIGHT, 1);
    erase(); settings_draw(settings, f.app, text_at); getyx(stdscr, y, x);
    assert(y == 6 && x == before + 1);
    MEVENT click = {0}; click.y = 6; click.x = COLS - 3; click.bstate = BUTTON1_PRESSED;
    settings_mouse(settings, f.app, &click);
    erase(); settings_draw(settings, f.app, text_at); getyx(stdscr, y, x);
    assert(y == 6 && x == COLS - 3);

    settings_open(settings, f.app);
    for (int i = 0; i < 12; ++i) settings_key(settings, f.app, KEY_DOWN, 1);
    settings_key(settings, f.app, '\n', 0); /* Plaintext password */
    settings_key(settings, f.app, 0x0628, 0); settings_key(settings, f.app, 0x0628, 0);
    erase(); settings_draw(settings, f.app, text_at); getyx(stdscr, y, x);
    assert(y == 6 && x == 4);
    assert(screen_scalar(6, 2) == '*' && screen_scalar(6, 3) == '*');
    for (int col = 2; col < COLS - 2; ++col) {
        wchar_t wc = screen_scalar(6, col);
        assert(wc != 0x0628 && (wc < 0xfe70 || wc > 0xfeff));
    }
    settings_key(settings, f.app, KEY_LEFT, 1);
    erase(); settings_draw(settings, f.app, text_at); getyx(stdscr, y, x);
    assert(x == 3);
    click.x = 2;
    settings_mouse(settings, f.app, &click);
    erase(); settings_draw(settings, f.app, text_at); getyx(stdscr, y, x);
    assert(x == 2 && !*f.app->cfg.password);
    settings_destroy(settings);
    dispose(&f);
}

static void guided_and_transfer_fields(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 80);
    assert(submit_text(f.app, f.ui, "/quickstart", 1));
    draw(f.app, f.ui);
    key_input(f.app, f.ui, 0x0628, 0); key_input(f.app, f.ui, 0x0628, 0);
    draw(f.app, f.ui);
    pair_field(LINES - 5, 1, COLS - 2);
    int y, x; getyx(stdscr, y, x); int before = x;
    assert(y == LINES - 5);
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    draw(f.app, f.ui); getyx(stdscr, y, x);
    assert(x == before + 1);
    MEVENT click = {0}; click.y = LINES - 5; click.x = COLS - 2; click.bstate = BUTTON1_PRESSED;
    apply_mouse(f.app, f.ui, click);
    draw(f.app, f.ui); getyx(stdscr, y, x);
    assert(x == COLS - 2);
    key_input(f.app, f.ui, 27, 0);
    assert(!guided_active(f.all->guided));
    assert(!strcmp(f.app->cfg.server, "fixture.invalid"));
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
    f = fresh(CONFIG_FOCUS_REVERSE, 80);
    assert(submit_text(f.app, f.ui, "/dcc", 1));
    draw(f.app, f.ui);
    apply_mouse(f.app, f.ui, visible_control("Send...", 3));
    draw(f.app, f.ui);
    apply_mouse(f.app, f.ui, visible_control("Nickname:", 5));
    draw(f.app, f.ui);
    key_input(f.app, f.ui, 0x0628, 0); key_input(f.app, f.ui, 0x0628, 0);
    draw(f.app, f.ui);
    pair_field(6, 2, COLS - 4);
    getyx(stdscr, y, x); before = x;
    assert(y == 6);
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    draw(f.app, f.ui); getyx(stdscr, y, x);
    assert(x == before + 1);
    click.y = 6; click.x = COLS - 3;
    apply_mouse(f.app, f.ui, click);
    draw(f.app, f.ui); getyx(stdscr, y, x);
    assert(x == COLS - 3);
    assert(!strcmp(f.app->cfg.server, "fixture.invalid"));
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

int main(void)
{
    if (!setlocale(LC_ALL, "C.UTF-8") && !setlocale(LC_ALL, "en_US.UTF-8")) {
        puts("Arabic curses: UTF-8 locale unavailable; skipped");
        return 77;
    }
    assert(!setenv("VIPIRC_ARABIC_VISUAL", "1", 1));
    assert(!setenv("VIPIRC_LANG", "en", 1));
    IrcArabicLayout *probe = irc_arabic_layout_utf8(beh_pair, strlen(beh_pair), 8, 0);
    if (!probe) {
        puts("Arabic curses: optional system FriBidi unavailable; skipped");
        return 77;
    }
    irc_arabic_layout_free(probe);
    FILE *out = tmpfile(), *in = tmpfile();
    assert(out && in);
    SCREEN *screen = newterm("xterm-256color", out, in);
    assert(screen && keypad(stdscr, TRUE) != ERR);
    vipirc_focus_colors();
    terminal_utf8 = 1;
    joined_labels_and_clipping();
    marked_margin_cells();
    wrapped_history_and_targets();
    metadata_allocation_failure();
    history_frame_allocation_failure();
    editor_frame_allocation_failure();
    chat_and_command_editors();
    manual_search_and_highlight();
    settings_and_secret_fields();
    guided_and_transfer_fields();
    endwin(); delscreen(screen); fclose(out); fclose(in);
    puts("Arabic curses: real joined/marked cells including bottom-right and clipped clusters, coherent editor-frame/fallback geometry and history count/paint, paragraph wrapping, channel targets and allocation-failure consistency, manual search/highlights, chat/command/settings/guided/transfer caret and mouse, logical drafts and masked Arabic password passed");
    return 0;
}
#else
int main(void) { puts("Arabic curses: mouse-capable ncurses unavailable; skipped"); return 77; }
#endif
