/* Exercise production curses dispatch/rendering with the existing in-memory
 * transport fixture. Only ReadPage allocations are fault-injected. */
#define main status_mouse_existing_suite
#include "test_status_mouse.c"
#undef main

static long page_malloc_failure = -1, page_realloc_failure = -1;
static void *page_test_malloc(size_t bytes)
{
    if (!page_malloc_failure) return NULL;
    if (page_malloc_failure > 0) --page_malloc_failure;
    return malloc(bytes);
}
static void *page_test_realloc(void *memory, size_t bytes)
{
    if (!page_realloc_failure) return NULL;
    if (page_realloc_failure > 0) --page_realloc_failure;
    return realloc(memory, bytes);
}
#define malloc page_test_malloc
#define realloc page_test_realloc
#include "../src/readpage.c"
#undef malloc
#undef realloc

#if UI_MOUSE
static int feature_page_contains(const ReadPage *page, const char *text)
{
    for (size_t i = 0; i < page->count; ++i)
        if (strstr(page->lines[i], text)) return 1;
    return 0;
}

static int feature_screen_contains(const char *text)
{
    char row[512];
    assert(COLS < (int)sizeof row);
    for (int y = 0; y < LINES; ++y) {
        assert(mvinnstr(y, 0, row, COLS) != ERR); row[COLS] = 0;
        if (strstr(row, text)) return 1;
    }
    return 0;
}

static void feature_settings_help_and_parent(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    Editor *e = draft(f.app, f.ui);
    assert(ed_from_utf8(e, "Unfinished message")); e->pos = 4;
    Editor before = *e;
    key_input(f.app, f.ui, KEY_F(1), 1);
    f.ui->app_selection = APP_SETTINGS;
    key_input(f.app, f.ui, '?', 0);
    assert(f.ui->context_help.active && !f.ui->context_help.page);
    assert(!strcmp(f.ui->context_help.pages[0].lines[0],
                   "Settings is an option on the vipirc main screen."));
    draw(f.app, f.ui);
    assert(feature_screen_contains("About vipirc main screen"));
    key_input(f.app, f.ui, '\r', 0);
    assert(f.ui->context_help.active && f.ui->context_help.page == 1);
    key_input(f.app, f.ui, 27, 0);
    assert(f.ui->context_help.active && !f.ui->context_help.page);
    key_input(f.app, f.ui, 27, 0);
    assert(!f.ui->context_help.active && f.ui->app_menu && f.ui->app_selection == APP_SETTINGS);
    assert(!memcmp(e, &before, sizeof before));
    assert(!f.app->chat_count && !f.app->control_count);

    key_input(f.app, f.ui, '?', 0); draw(f.app, f.ui);
    apply_mouse(f.app, f.ui, visible_control("About vipirc main screen", 3));
    assert(f.ui->context_help.page == 1);
    draw(f.app, f.ui);
    apply_mouse(f.app, f.ui, visible_control("Back to item", 3));
    assert(!f.ui->context_help.page);
    dispose(&f);
}

static void feature_settings_draft_and_literal_question(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    Editor *e = draft(f.app, f.ui);
    assert(ed_from_utf8(e, "literal"));
    key_input(f.app, f.ui, '?', 0);
    assert_editor_text(e, "literal?");
    assert(!f.ui->context_help.active);
    assert(open_command(f.app, f.ui, "/"));
    key_input(f.app, f.ui, '?', 0);
    assert_editor_text(prompt_const_editor(f.ui->command), "/?");
    assert(!f.ui->context_help.active);
    key_input(f.app, f.ui, 27, 0);

    if (!f.ui->settings) f.ui->settings = settings_create();
    assert(f.ui->settings && settings_open_server(f.ui->settings, f.client, 0, 1));
    key_input(f.app, f.ui, '\r', 0);
    key_input(f.app, f.ui, KEY_HOME, 1);
    for (int i = 0; i < 64; ++i) key_input(f.app, f.ui, KEY_DC, 1);
    type_local_text(&f, "DraftNick?"); draw(f.app, f.ui);
    assert(!f.ui->context_help.active && feature_screen_contains("DraftNick?"));
    key_input(f.app, f.ui, 127, 0);
    key_input(f.app, f.ui, '\r', 0);
    assert(!strcmp(f.app->cfg.nick, "tester"));
    Editor before = *e;
    unsigned settings_state = settings_mouse_state(f.ui->settings);
    key_input(f.app, f.ui, '?', 0);
    assert(f.ui->context_help.active && feature_page_contains(&f.ui->context_help.pages[0], "Nickname"));
    key_input(f.app, f.ui, '\r', 0);
    key_input(f.app, f.ui, 27, 0);
    key_input(f.app, f.ui, 27, 0);
    assert(settings_mouse_state(f.ui->settings) == settings_state);
    assert(!memcmp(e, &before, sizeof before));
    draw(f.app, f.ui); assert(feature_screen_contains("DraftNick"));
    key_input(f.app, f.ui, KEY_RIGHT, 1); /* Apply the same retained staged field. */
    key_input(f.app, f.ui, '\r', 0);
    Config effective; app_config_effective(f.app, &effective);
    assert(!strcmp(effective.nick, "DraftNick") && !strcmp(f.app->cfg.nick, "tester"));
    config_wipe(&effective, sizeof effective);
    dispose(&f);
}

static void feature_interface_fields_remain_complete(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    if (!f.ui->settings) f.ui->settings = settings_create();
    assert(f.ui->settings);
    settings_open_client(f.ui->settings, f.client);
    settings_key(f.ui->settings, f.app, '\r', 0); /* Interface is the first group. */
    settings_key(f.ui->settings, f.app, KEY_END, 1);
    char screen[160], item[512], detail[1024];
    assert(settings_context_help(f.ui->settings, screen, sizeof screen,
                                 item, sizeof item, detail, sizeof detail));
    assert(!strcmp(screen, "Interface settings") && !strcmp(item, "Default interface"));
    settings_key(f.ui->settings, f.app, KEY_UP, 1);
    assert(settings_context_help(f.ui->settings, screen, sizeof screen,
                                 item, sizeof item, detail, sizeof detail));
    assert(!strcmp(screen, "Interface settings") && !strcmp(item, "Command suggestions"));
    assert(settings_open_server(f.ui->settings, f.client, 0, 0));
    int found_history = 0;
    for (size_t i = 0; i < 64; ++i) {
        assert(settings_context_help(f.ui->settings, screen, sizeof screen,
                                     item, sizeof item, detail, sizeof detail));
        if (!strcmp(item, "History lines")) { found_history = 1; break; }
        settings_key(f.ui->settings, f.app, KEY_DOWN, 1);
    }
    assert(found_history && !strcmp(screen, "Server settings"));
    dispose(&f);
}

static void feature_summary_draft_refresh_and_failure(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 80);
    size_t original = f.app->current;
    Editor *e = draft(f.app, f.ui);
    assert(ed_from_utf8(e, "DRAFT preserved through summary")); e->pos = 7;
    Editor before = *e;
    for (size_t i = 0; i < 12; ++i) {
        char name[64]; snprintf(name, sizeof name, "#summary-channel-%zu", i);
        size_t at = app_buffer(f.app, name, B_CHANNEL, 1); assert(at != SIZE_MAX);
        f.app->buffers[at].unread = (int)i;
    }
    key_input(f.app, f.ui, KEY_F(1), 1); f.ui->app_selection = APP_SETTINGS;
    f.app->buffers[original].unread = 5; f.app->buffers[original].unread_mentions = 2;
    assert(open_command(f.app, f.ui, "/summary"));
    key_input(f.app, f.ui, '\r', 0);
    assert(f.ui->summary.active && !f.ui->summary.hidden && !prompt_active(f.ui->command));
    assert(f.ui->app_menu && f.ui->app_selection == APP_SETTINGS && f.app->current == original);
    assert(!memcmp(e, &before, sizeof before));
    assert(f.app->buffers[original].unread == 5 && f.app->buffers[original].unread_mentions == 2);
    assert(feature_page_contains(&f.ui->summary.pages[0], "Unread: 71 | mentions: 2"));
    key_input(f.app, f.ui, KEY_END, 1);
    assert(f.ui->summary.scroll[0] > 0);
    size_t scroll = f.ui->summary.scroll[0];
    char **old_lines = f.ui->summary.pages[0].lines;
    key_input(f.app, f.ui, '/', 0);
    assert(prompt_active(f.ui->command) && f.ui->summary.hidden);
    type_local_text(&f, "join #never-sent");
    key_input(f.app, f.ui, 27, 0);
    assert(f.ui->summary.active && !f.ui->summary.hidden && !prompt_active(f.ui->command));
    assert(f.ui->summary.scroll[0] == scroll && f.ui->summary.pages[0].lines == old_lines);
    assert(f.ui->app_selection == APP_SETTINGS && !memcmp(e, &before, sizeof before));

    ReadView old = f.ui->summary;
    page_realloc_failure = 0;
    assert(!open_summary(f.app, f.ui)); page_realloc_failure = -1;
    assert(!memcmp(&old, &f.ui->summary, sizeof old));
    page_malloc_failure = 0;
    assert(!open_summary(f.app, f.ui)); page_malloc_failure = -1;
    assert(!memcmp(&old, &f.ui->summary, sizeof old));
    page_malloc_failure = 7;
    assert(!open_summary(f.app, f.ui)); page_malloc_failure = -1;
    assert(!memcmp(&old, &f.ui->summary, sizeof old));
    assert(strstr(app_status_text(f.app), "previous snapshot is preserved"));

    f.app->buffers[original].unread = 9;
    key_input(f.app, f.ui, 'r', 0);
    assert(feature_page_contains(&f.ui->summary.pages[0], "Unread: 75 | mentions: 2"));
    assert(f.ui->summary.scroll[0] == scroll && f.app->buffers[original].unread == 9);
    key_input(f.app, f.ui, KEY_HOME, 1); draw(f.app, f.ui);
#ifdef BUTTON5_PRESSED
    MEVENT wheel = {0}; wheel.x = 20; wheel.y = 8; wheel.bstate = BUTTON5_PRESSED;
    apply_mouse(f.app, f.ui, wheel);
    assert(f.ui->summary.scroll[0] > 0);
#endif
    f.app->buffers[original].unread = 10; draw(f.app, f.ui);
    apply_mouse(f.app, f.ui, visible_control("Refresh", 3));
    assert(feature_page_contains(&f.ui->summary.pages[0], "Unread: 76 | mentions: 2"));
    assert(f.app->current == original && f.app->buffers[original].unread == 10);
    assert(!memcmp(e, &before, sizeof before));
    key_input(f.app, f.ui, 27, 0);
    assert(!f.ui->summary.active && f.ui->app_menu && f.ui->app_selection == APP_SETTINGS);
    dispose(&f);
}

static void feature_inquiry_full_path(const ReadPage *page, const char *expected)
{
    char actual[CONFIG_PATH_MAX] = "";
    size_t used = 0, length = strlen(expected);
    for (size_t i = 0; i < page->count; ++i) {
        size_t first, last, total;
        int offset = 0;
        if (sscanf(page->lines[i], "SASL GPG file [bytes %zu-%zu of %zu]: %n",
                   &first, &last, &total, &offset) != 3 || !offset) continue;
        const char *part = page->lines[i] + offset;
        size_t size = strlen(part);
        assert(first == used + 1 && last == used + size && total == length);
        assert(size < sizeof actual - used);
        memcpy(actual + used, part, size + 1); used += size;
    }
    assert(used == length && !strcmp(actual, expected));
}

static void feature_inquiry_parent_refresh_and_failure(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 80);
    size_t original = f.app->current;
    Editor *e = draft(f.app, f.ui);
    assert(ed_from_utf8(e, "Unsent authentication setup question")); e->pos = 11;
    Editor before = *e;
    strcpy(f.app->cfg.auth, "sasl"); strcpy(f.app->cfg.auth_user, "fixture-account");
    f.app->cfg.tls = 1;
    memset(f.app->cfg.password_gpg_file, 'a', sizeof f.app->cfg.password_gpg_file - 1);
    f.app->cfg.password_gpg_file[0] = '/';
    f.app->cfg.password_gpg_file[sizeof f.app->cfg.password_gpg_file - 1] = 0;
    strcpy(f.app->cfg.channels, "#nastycode,#vipirc");
    key_input(f.app, f.ui, KEY_F(1), 1); f.ui->app_selection = APP_SETTINGS;
    f.app->buffers[original].unread = 7; f.app->buffers[original].unread_mentions = 2;
    assert(open_command(f.app, f.ui, "/autoauth status"));
    key_input(f.app, f.ui, '\r', 0);
    assert(f.ui->summary.active && !strcmp(f.ui->summary.inquiry, "/autoauth status"));
    assert(!strcmp(f.ui->summary.title, "Authentication settings"));
    feature_inquiry_full_path(&f.ui->summary.pages[0], f.app->cfg.password_gpg_file);
    assert(f.ui->app_menu && f.ui->app_selection == APP_SETTINGS && f.app->current == original);
    assert(!memcmp(e, &before, sizeof before));
    assert(f.app->buffers[original].unread == 7 && f.app->buffers[original].unread_mentions == 2);
    key_input(f.app, f.ui, KEY_END, 1);
    assert(f.ui->summary.scroll[0] > 0);
    ReadView old = f.ui->summary;
    page_malloc_failure = 0;
    key_input(f.app, f.ui, 'r', 0); page_malloc_failure = -1;
    assert(!memcmp(&old, &f.ui->summary, sizeof old));
    page_realloc_failure = 0;
    assert(!open_inquiry(f.app, f.ui, "/autojoin")); page_realloc_failure = -1;
    assert(!memcmp(&old, &f.ui->summary, sizeof old));
    assert(strstr(app_status_text(f.app), "previous snapshot is preserved"));
    f.app->cfg.password_gpg_file[1] = 'Z';
    key_input(f.app, f.ui, 'r', 0);
    feature_inquiry_full_path(&f.ui->summary.pages[0], f.app->cfg.password_gpg_file);
    assert(f.ui->summary.scroll[0] == old.scroll[0]);
    key_input(f.app, f.ui, '/', 0);
    assert(prompt_active(f.ui->command) && f.ui->summary.hidden);
    key_input(f.app, f.ui, 27, 0);
    assert(f.ui->summary.active && !f.ui->summary.hidden && !prompt_active(f.ui->command));
    assert(f.ui->summary.scroll[0] == old.scroll[0]);
    key_input(f.app, f.ui, 27, 0);
    assert(!f.ui->summary.active && f.ui->app_menu && f.ui->app_selection == APP_SETTINGS);

    assert(open_command(f.app, f.ui, "/autojoin list"));
    key_input(f.app, f.ui, '\r', 0);
    assert(f.ui->summary.active && !strcmp(f.ui->summary.inquiry, "/autojoin"));
    assert(!strcmp(f.ui->summary.title, "Auto-join settings"));
    assert(feature_page_contains(&f.ui->summary.pages[0], "Channel #nastycode: not joined"));
    size_t channel = app_buffer(f.app, "#nastycode", B_CHANNEL, 1); assert(channel != SIZE_MAX);
    f.app->buffers[channel].joined = 1;
    key_input(f.app, f.ui, 'r', 0);
    assert(feature_page_contains(&f.ui->summary.pages[0], "Channel #nastycode: joined"));
    assert(feature_page_contains(&f.ui->summary.pages[0], "Channel #vipirc: not joined"));
    key_input(f.app, f.ui, 27, 0);
    assert(!f.ui->summary.active && f.ui->app_menu && f.ui->app_selection == APP_SETTINGS);
    assert(f.app->current == original && !memcmp(e, &before, sizeof before));
    assert(f.app->buffers[original].unread == 7 && f.app->buffers[original].unread_mentions == 2);
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void feature_reader_paste_and_suspend(void)
{
    for (int help = 0; help <= 1; ++help) {
        Fixture f = fresh(CONFIG_FOCUS_REVERSE, 80);
        if (help) {
            key_input(f.app, f.ui, KEY_F(1), 1); f.ui->app_selection = APP_SETTINGS;
            key_input(f.app, f.ui, '?', 0);
        } else assert(open_summary(f.app, f.ui));
        ReadView *view = help ? &f.ui->context_help : &f.ui->summary;
        char **old_lines = view->pages[0].lines;
        key_input(f.app, f.ui, PASTE_BEGIN, 1);
        assert(capturing_paste(f.ui));
        static const char dangerous[] = "?\033/summary\r\n?\033\r\033[201x";
        for (size_t i = 0; i < sizeof dangerous - 1; ++i) {
            terminal_byte(f.app, f.ui, (unsigned char)dangerous[i]);
            assert(view->active && !view->page && view->paste && view->pages[0].lines == old_lines);
            assert(!prompt_active(f.ui->command) && !f.app->quit);
        }
        static const char ending[] = "\033[201~";
        for (size_t i = 0; i < sizeof ending - 1; ++i) {
            terminal_byte(f.app, f.ui, (unsigned char)ending[i]);
            fixture_clock += UI_ESCAPE_CONTINUATION_MS + 100;
            flush_escape(f.app, f.ui, 0);
            if (i < sizeof ending - 2) assert(capturing_paste(f.ui));
        }
        assert(!capturing_paste(f.ui) && view->active && !view->page);
        key_input(f.app, f.ui, PASTE_BEGIN, 1);
        terminal_byte(f.app, f.ui, 27); terminal_byte(f.app, f.ui, '[');
        assert(capturing_paste(f.ui) && f.ui->paste_end_len == 2);
        release_terminal(f.app, f.ui);
        assert(!capturing_paste(f.ui) && !f.ui->paste_end_len && !f.ui->escape_len);
        key_input(f.app, f.ui, 27, 0);
        assert(!view->active && !f.app->quit);
        dispose(&f);
    }
}

static void feature_compact_reader_mouse_and_main_origin(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 20);
    key_input(f.app, f.ui, KEY_F(1), 1); f.ui->app_selection = APP_SETTINGS;
    key_input(f.app, f.ui, '?', 0); draw(f.app, f.ui);
    MEVENT old_click = {0}; old_click.x = 6; old_click.y = 3; old_click.bstate = BUTTON1_PRESSED;
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    apply_mouse(f.app, f.ui, old_click);
    assert(f.ui->context_help.active && f.ui->context_help.action == 1);
    draw(f.app, f.ui); apply_mouse(f.app, f.ui, old_click);
    assert(!f.ui->context_help.active);
    assert(open_summary(f.app, f.ui)); draw(f.app, f.ui);
    /* At width 20, summary actions may both fit. Force the same keyboard
     * change, which must still invalidate queued old coordinates. */
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    apply_mouse(f.app, f.ui, old_click);
    assert(f.ui->summary.active);
    key_input(f.app, f.ui, 27, 0);
    assert(resizeterm(24, 132) != ERR);
    f.ui->app_selection = APP_COMMAND;
    key_input(f.app, f.ui, '\r', 0);
    assert(prompt_active(f.ui->command) && f.ui->command_origin.app_menu);
    assert(!f.ui->app_menu);
    key_input(f.app, f.ui, 27, 0);
    assert(f.ui->app_menu && f.ui->app_selection == APP_COMMAND);
    dispose(&f);
}

int main(void)
{
    FILE *out = tmpfile(), *in = tmpfile(); assert(out && in);
    SCREEN *screen = newterm("xterm-256color", out, in); assert(screen);
    assert(keypad(stdscr, TRUE) != ERR); vipirc_focus_colors(); terminal_utf8 = 0;
    feature_settings_help_and_parent();
    feature_settings_draft_and_literal_question();
    feature_interface_fields_remain_complete();
    feature_summary_draft_refresh_and_failure();
    feature_inquiry_parent_refresh_and_failure();
    feature_reader_paste_and_suspend();
    feature_compact_reader_mouse_and_main_origin();
    endwin(); delscreen(screen); fclose(in); fclose(out);
    puts("context help/summary: item and screen links, keyboard/mouse return, staged Settings and drafts, literal ?, snapshots and refresh, allocation rollback, cancelled commands, paste/suspend isolation, compact mouse invalidation and main-screen origin passed");
    return 0;
}
#else
int main(void) { puts("context help tests require curses mouse support"); return 0; }
#endif
