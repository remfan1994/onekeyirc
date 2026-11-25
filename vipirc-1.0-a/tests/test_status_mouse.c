/* Deterministic real-render/status-mouse regressions. The only fake component
 * is an in-memory transport: IRC parsing, App state, drawing and mouse dispatch
 * use production code. No socket, user configuration, or real terminal needed. */
#include "../src/ui.c"
#include <assert.h>
#include <sys/stat.h>

struct Net {
    char input[1024];
    size_t size;
};
static int64_t fixture_clock = 100000;
Net *net_open(const char *host, int port, int tls, const char *ca, char *error, size_t cap)
{
    (void)host; (void)port; (void)tls; (void)ca; (void)error; (void)cap;
    assert(!"This test must not start a connection");
    return NULL;
}
int net_fd(const Net *n) { (void)n; return -1; }
short net_events(const Net *n, int write) { (void)n; (void)write; return POLLIN; }
NetState net_state(const Net *n) { (void)n; return NET_READY; }
int net_step(Net *n, short events, int64_t now) { (void)n; (void)events; (void)now; return 1; }
ssize_t net_read(Net *n, void *out, size_t cap)
{
    if (!n->size) return -2;
    assert(n->size <= cap);
    size_t size = n->size;
    memcpy(out, n->input, size);
    n->size = 0;
    return (ssize_t)size;
}
ssize_t net_write(Net *n, const void *bytes, size_t size)
{
    (void)n; (void)bytes;
    assert(!"Read/navigation-only regression must not send anything");
    return (ssize_t)size;
}
int net_pending(const Net *n) { return n->size != 0; }
int net_detach_ready(Net *n) { (void)n; return 1; }
const char *net_error(const Net *n) { (void)n; return "test transport"; }
int net_retryable(const Net *n) { (void)n; return 0; }
int64_t net_now_ms(void) { return fixture_clock; }
void net_close(Net *n) { free(n); }

#if UI_MOUSE
typedef struct Fixture { Client *client; UiSet *all; App *app; Ui *ui; } Fixture;

static Fixture fresh(int style, int width)
{
    Fixture f;
    Config cfg;
    char error[256];
    assert(resizeterm(24, width) != ERR);
    config_defaults(&cfg);
    strcpy(cfg.server, "fixture.invalid");
    strcpy(cfg.nick, "tester");
    strcpy(cfg.username, "tester");
    cfg.tls = 0; cfg.mouse = 1; cfg.user_list = 0; cfg.history_lines = 50;
    cfg.focus_style = style;
    f.client = calloc(1, sizeof *f.client);
    f.all = calloc(1, sizeof *f.all);
    f.app = calloc(1, sizeof *f.app);
    assert(f.client && f.all && f.app);
    assert(app_init(f.app, &cfg, "/unused/test-config"));
    f.app->net = calloc(1, sizeof(Net));
    assert(f.app->net);
    f.app->registered = f.app->started = f.app->cap_done = 1;
    f.app->last_receive = f.app->began = fixture_clock;
    f.client->slots[0].used = 1;
    f.client->slots[0].token = 1;
    f.client->slots[0].app = f.app;
    f.all->client = f.client;
    f.all->switch_to = f.all->switch_view = f.all->close_slot = SIZE_MAX;
    f.all->dcc = dcc_create(0);
    f.all->transfers = transfers_create(f.client, f.all->dcc);
    assert(f.all->dcc && f.all->transfers);
    f.ui = get_ui(f.all, 0, error, sizeof error);
    assert(f.ui);
    f.ui->mouse_active = 1;
    size_t query = app_buffer(f.app, "Al", B_QUERY, 1);
    assert(query != SIZE_MAX);
    app_switch(f.app, query);
    app_log(f.app, 1, "[12:00:00] <Al> tester mention fixture");
    f.app->buffers[1].unread = 1;
    return f;
}

/* A deliberate action after paste/setup completion follows a displayed
 * frame in a later terminal batch, separated from the discarded suffix. */
static void reviewed_input(Fixture *f)
{
    ++f->all->input_batch;
    draw(f->app, f->ui);
    fixture_clock += 200;
}

static void dispose(Fixture *f)
{
    guided_free(f->all->guided);
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        destroy_ui(f->all->states[i]);
        if (f->client->slots[i].app) {
            app_free(f->client->slots[i].app);
            free(f->client->slots[i].app);
        }
    }
    transfers_free(f->all->transfers);
    dcc_free(f->all->dcc);
    free(f->all); free(f->client);
}

static MEVENT visible_control(const char *label, int y)
{
    char row[512];
    assert(COLS < (int)sizeof row);
    assert(mvinnstr(y, 0, row, COLS) != ERR);
    row[COLS] = 0;
    char *at = strstr(row, label);
    if (!at) fprintf(stderr, "Missing control '%s' at row %d: %s\n", label, y, row);
    assert(at);
    MEVENT event = {0};
    event.x = (int)(at - row) + (int)strlen(label) / 2;
    event.y = y;
    event.bstate = BUTTON1_PRESSED;
    return event;
}

static void feed(Fixture *f, const char *line)
{
    assert(strlen(line) < sizeof f->app->net->input);
    strcpy(f->app->net->input, line);
    f->app->net->size = strlen(line);
    app_network(f->app, POLLIN);
}

static void nick_arrival_before_click(int style, int width)
{
    static const char nick[] = "longnick00000000000000000000000000000000000000000000000000";
    Fixture f = fresh(style, width);
    char wire[256];
    assert(irc_valid_nick(nick));
    draw(f.app, f.ui);
    MEVENT click = visible_control("Mentions", 0);
    snprintf(wire, sizeof wire, ":Al!u@localhost NICK :%s\r\n", nick);
    feed(&f, wire);
    assert(!strcmp(f.app->buffers[f.app->current].name, nick));
    /* Deliberately no redraw: the real event loop services sockets before
     * dispatching terminal input from the same poll result. */
    apply_mouse(f.app, f.ui, click);
    if (!f.all->unread || f.ui->channels) {
        fprintf(stderr, "Visible Mentions click was retargeted after NICK: style=%d width=%d x=%d current=%zu channels=%d\n",
                style, width, click.x, f.app->current, f.ui->channels);
    }
    assert(f.all->unread && !f.ui->channels);
    dispose(&f);
}

static void empty_query_arrival_before_activation(int mouse)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 96);
    size_t map[APP_BUFFERS];
    assert(app_close_view(f.app, f.app->current));
    assert(!pm_map(f.app, map));
    assert(f.app->current == 0);
    open_pms(f.app, f.ui);
    draw(f.app, f.ui);
    MEVENT click = visible_control("New query...", 4);
    feed(&f, ":FirstPeer!u@host PRIVMSG tester :arrived before activation\r\n");
    assert(pm_map(f.app, map) == 1);
    /* The key/click refers to the displayed virtual New-query row. A new
     * remote conversation must not silently become its activation target. */
    if (mouse) apply_mouse(f.app, f.ui, click);
    else key_input(f.app, f.ui, '\n', 0);
    assert(f.app->current == 0);
    if (prompt_active(f.ui->command)) {
        char text[32];
        assert(ed_to_utf8(prompt_const_editor(f.ui->command), text, sizeof text) == 4);
        assert(!strcmp(text, "/pm "));
    } else {
        /* Invalidating the stale input until a fresh draw is also safe. */
        assert(f.ui->channels == 2);
        draw(f.app, f.ui);
        if (mouse) apply_mouse(f.app, f.ui, visible_control("FirstPeer", 4));
        else key_input(f.app, f.ui, '\n', 0);
        assert(f.app->current == map[0] && !f.ui->channels);
    }
    dispose(&f);
}

static MEVENT draft_and_clear(Fixture *f)
{
    f->ui->input_mode = 1;
    Editor *e = draft(f->app, f->ui);
    assert(e);
    for (const char *p = "KEEP"; *p; ++p) assert(ed_insert(e, (wchar_t)*p));
    draw(f->app, f->ui);
    return visible_control("Clear", LINES - 1);
}

static void mention_counter_width_before_click(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    f.app->buffers[1].unread = 9;
    for (int i = 1; i < 9; ++i) app_log(f.app, 1, "[12:00:00] old mention %d", i);
    f.app->buffers[1].unread = 9;
    f.app->buffers[0].unread = 1;
    draw(f.app, f.ui);
    MEVENT click = visible_control("Mentions/unread", 0);
    feed(&f, ":Al!u@host PRIVMSG tester :tester counter width changes\r\n");
    assert(f.app->buffers[1].unread == 10);
    /* The combined category grows a digit before the click is dispatched.
     * Its already drawn hit still opens the same activity destination. */
    apply_mouse(f.app, f.ui, click);
    assert(f.all->unread && !f.ui->channels && !f.all->servers);
    assert(!transfers_active(f.all->transfers));
    dispose(&f);
}

static void stale_main_after_resize_or_mode(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 96);
    MEVENT clear = draft_and_clear(&f);
    Editor *e = draft(f.app, f.ui);
    /* Even a resize notification whose dimensions already returned must
     * invalidate the previous displayed hit coordinates. */
    key_input(f.app, f.ui, KEY_RESIZE, 1);
    apply_mouse(f.app, f.ui, clear);
    assert(e->bytes == 4);
    draw(f.app, f.ui);
    key_input(f.app, f.ui, KEY_F(1), 1);
    assert(f.ui->app_menu && !f.ui->status_selected);
    apply_mouse(f.app, f.ui, clear);
    assert(e->bytes == 4);
    draw(f.app, f.ui);
    apply_mouse(f.app, f.ui, clear);
    assert(e->bytes == 4);
    key_input(f.app, f.ui, 'b', 0);
    assert(main_screen(f.ui));
    apply_mouse(f.app, f.ui, clear);
    assert(e->bytes == 4);
    draw(f.app, f.ui);
    apply_mouse(f.app, f.ui, clear);
    assert(!e->bytes && f.ui->input_mode);
    dispose(&f);
}

static void stale_main_after_secondary(void)
{
    Fixture f = fresh(CONFIG_FOCUS_BRACKETS, 96);
    MEVENT clear = draft_and_clear(&f);
    Editor *e = draft(f.app, f.ui);
    open_settings(f.app, f.ui);
    assert(settings_is_open(f.ui));
    unsigned state = settings_mouse_state(f.ui->settings);
    apply_mouse(f.app, f.ui, clear);
    assert(settings_mouse_state(f.ui->settings) == state);
    draw(f.app, f.ui);
    key_input(f.app, f.ui, 27, 0);
    assert(!settings_is_open(f.ui));
    assert(f.ui->app_menu);
    apply_mouse(f.app, f.ui, clear);
    assert(e->bytes == 4);
    key_input(f.app, f.ui, 'b', 0);
    assert(main_screen(f.ui));
    apply_mouse(f.app, f.ui, clear);
    assert(e->bytes == 4);
    draw(f.app, f.ui);
    apply_mouse(f.app, f.ui, clear);
    assert(!e->bytes);
    dispose(&f);
}

static void stale_main_after_view_or_small_screen(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 96);
    MEVENT clear = draft_and_clear(&f);
    size_t original = f.app->current;
    Editor *e = draft(f.app, f.ui);
    app_switch(f.app, 0);
    apply_mouse(f.app, f.ui, clear);
    assert(e->bytes == 4 && !f.ui->drafts[0]);
    app_switch(f.app, original);
    draw(f.app, f.ui);
    size_t other = app_buffer(f.app, "Other", B_QUERY, 1);
    assert(other != SIZE_MAX);
    assert(app_close_view(f.app, other));
    apply_mouse(f.app, f.ui, clear);
    assert(e->bytes == 4);
    assert(resizeterm(5, 10) != ERR);
    draw(f.app, f.ui);
    assert(resizeterm(24, 96) != ERR);
    apply_mouse(f.app, f.ui, clear);
    assert(e->bytes == 4);
    draw(f.app, f.ui);
    apply_mouse(f.app, f.ui, clear);
    assert(!e->bytes);
    dispose(&f);
}

static void stale_main_after_session_switch(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 96);
    App *second = calloc(1, sizeof *second);
    char error[256];
    assert(second && app_init(second, &f.app->cfg, "/unused/second-config"));
    f.client->slots[1].used = 1;
    f.client->slots[1].token = 2;
    f.client->slots[1].app = second;
    Ui *other_ui = get_ui(f.all, 1, error, sizeof error);
    assert(other_ui);
    other_ui->mouse_active = 1;
    size_t query = app_buffer(second, "Other", B_QUERY, 1);
    assert(query != SIZE_MAX);
    app_switch(second, query);
    Fixture displayed = f;
    displayed.app = second; displayed.ui = other_ui;
    f.client->current = 1;
    MEVENT clear = draft_and_clear(&displayed);
    Editor *e = draft(second, other_ui);
    Ui *current_ui = other_ui;
    App *current_app = second;
    f.all->switch_to = 0;
    assert(apply_session_actions(f.all, &current_app, &current_ui) == 1);
    draw(current_app, current_ui);
    f.all->switch_to = 1;
    assert(apply_session_actions(f.all, &current_app, &current_ui) == 1);
    assert(current_app == second && current_ui == other_ui);
    apply_mouse(current_app, current_ui, clear);
    assert(e->bytes == 4);
    draw(current_app, current_ui);
    apply_mouse(current_app, current_ui, clear);
    assert(!e->bytes);
    dispose(&f);
}

static void divider_and_empty_categories(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    StatusLayout layout;
    draw(f.app, f.ui);
    for (int x = 0; x < COLS; ++x) {
        chtype cell = mvinch(1, x);
        assert(cell != (chtype)ERR);
        assert((cell & A_CHARTEXT) == (ACS_HLINE & A_CHARTEXT));
        assert(cell & A_BOLD);
        assert(PAIR_NUMBER(cell) == 0);
    }
    status_layout(f.app, f.ui, &layout);
    assert(layout.present & (1u << FOCUS_APP));
    assert(layout.present & (1u << FOCUS_SERVERS));
    assert(layout.present & (1u << FOCUS_QUERIES));
    assert(layout.present & (1u << FOCUS_ACTIVITY));
    assert(!(layout.present & (1u << FOCUS_CHANNELS)));
    assert(!(layout.present & (1u << FOCUS_TRANSFERS)));
    assert(!strcmp(layout.labels[FOCUS_SERVERS], "F2 Servers (1)"));
    assert(!strcmp(layout.labels[FOCUS_QUERIES], "F4 Queries (1)"));
    assert(!strcmp(layout.labels[FOCUS_ACTIVITY], "F5 Mentions/unread (1/0)"));
    f.ui->mouse_active = 0;
    status_layout(f.app, f.ui, &layout);
    assert(!(layout.present & ((1u << FOCUS_CLEAR) | (1u << FOCUS_SEND))));
    assert(app_close_view(f.app, f.app->current));
    app_clear_buffer(f.app, 1);
    f.app->cfg.server[0] = 0;
    f.app->buffers[0].unread = 0;
    status_layout(f.app, f.ui, &layout);
    assert(layout.present == (1u << FOCUS_APP));
    assert(!strcmp(layout.labels[FOCUS_APP], "F1 vipirc"));
    dispose(&f);
}

static void assert_default_selection(int y, int x)
{
    chtype cell = mvinch(y, x);
    assert(cell != (chtype)ERR && (cell & A_CHARTEXT) == '[');
    assert((cell & A_BOLD) && !(cell & (A_DIM | A_REVERSE)));
    assert(PAIR_NUMBER(cell) == 0);
}

static void selected_menu_rows_and_nearby_actions(int style)
{
    Fixture f = fresh(style, 132);
    open_servers(f.app, f.ui);
    draw(f.app, f.ui);
    assert_default_selection(4, 0);
    MEVENT action = visible_control("Open", 3);
    assert_default_selection(action.y, 0);
    char footer[160];
    assert(mvinnstr(LINES - 2, 0, footer, COLS) != ERR);
    footer[COLS] = 0;
    assert(strstr(footer, "Up/Down:") && !strstr(footer, "[Open]"));

    key_input(f.app, f.ui, KEY_F(3), 1);
    draw(f.app, f.ui);
    /* The fixture has no joined channel, so use the explicit local picker. */
    open_channels(f.app, f.ui);
    draw(f.app, f.ui);
    assert_default_selection(4, 0);
    (void)visible_control("Open", 3);

    key_input(f.app, f.ui, KEY_F(4), 1);
    draw(f.app, f.ui);
    assert(f.ui->channels == 2);
    assert_default_selection(4, 0);
    (void)visible_control("Open", 3);
    f.app->cfg.hints = 0;
    draw(f.app, f.ui);
    assert(mvinnstr(LINES - 2, 0, footer, COLS) != ERR);
    footer[COLS] = 0;
    for (int i = 0; i < COLS; ++i) assert(footer[i] == ' ');
    (void)visible_control("Open", 3);

    key_input(f.app, f.ui, KEY_F(1), 1);
    draw(f.app, f.ui);
    assert(f.ui->app_menu && f.ui->app_hits[APP_SERVER].y >= 3);
    assert_default_selection(f.ui->app_hits[APP_SERVER].y, 0);

    feed(&f, ":Al!u@host PRIVMSG tester :tester selected activity\r\n");
    key_input(f.app, f.ui, KEY_F(5), 1);
    draw(f.app, f.ui);
    assert(f.all->unread && f.all->activity_hits_count);
    int found = 0;
    for (size_t i = 0; i < f.all->activity_hits_count; ++i) {
        const ActivityHit *hit = &f.all->activity_hits[i];
        if (hit->item == f.all->unread_selection) {
            assert_default_selection(hit->y, 0);
            found = 1;
        }
    }
    assert(found);
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void query_escape_returns_to_list(void)
{
    for (int from_list = 0; from_list < 2; ++from_list) {
        Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
        size_t first = f.app->current;
        size_t second = app_buffer(f.app, "NickServ", B_QUERY, 1);
        assert(second != SIZE_MAX);
        Editor *first_draft = draft(f.app, f.ui);
        assert(first_draft && ed_from_utf8(first_draft, "OTHER_QUERY_DRAFT"));
        if (from_list) {
            key_input(f.app, f.ui, KEY_F(4), 1);
            draw(f.app, f.ui);
            key_input(f.app, f.ui, KEY_END, 1);
            draw(f.app, f.ui);
            key_input(f.app, f.ui, '\n', 0);
        } else app_switch(f.app, second);
        assert(f.app->current == second && main_screen(f.ui));
        Editor *second_draft = draft(f.app, f.ui);
        assert(second_draft && ed_from_utf8(second_draft, "NICKSERV_DRAFT"));
        second_draft->pos = 5;
        draw(f.app, f.ui);
        key_input(f.app, f.ui, 27, 0);
        assert(f.ui->channels == 2 && !f.ui->app_menu && f.app->current == second);
        draw(f.app, f.ui);
        assert(f.ui->chooser_map[f.ui->channel_selection] == second);
        assert(f.ui->chooser_action == 0);
        key_input(f.app, f.ui, '\n', 0);
        assert(main_screen(f.ui) && f.app->current == second);
        assert(draft(f.app, f.ui) == second_draft && second_draft->pos == 5);
        key_input(f.app, f.ui, 27, 0);
        draw(f.app, f.ui);
        key_input(f.app, f.ui, 27, 0);
        assert(main_screen(f.ui) && f.app->current == second && second_draft->pos == 5);
        key_input(f.app, f.ui, KEY_F(1), 1);
        assert(f.ui->app_menu && !f.ui->channels);
        key_input(f.app, f.ui, 'b', 0);
        assert(main_screen(f.ui) && f.app->current == second);
        char data[64];
        assert(ed_to_utf8(second_draft, data, sizeof data) == strlen("NICKSERV_DRAFT"));
        assert(!strcmp(data, "NICKSERV_DRAFT"));
        assert(f.ui->drafts[first] == first_draft);
        assert(ed_to_utf8(first_draft, data, sizeof data) == strlen("OTHER_QUERY_DRAFT"));
        assert(!strcmp(data, "OTHER_QUERY_DRAFT"));
        assert(!f.app->chat_count && !f.app->control_count);
        dispose(&f);
    }
}

static void server_open_enters_its_message_view(void)
{
    for (int cross_server = 0; cross_server < 2; ++cross_server) {
        Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
        App *target = f.app;
        Ui *target_ui = f.ui;
        char error[256];
        if (cross_server) {
            target = calloc(1, sizeof *target);
            assert(target && app_init(target, &f.app->cfg, "/unused/target-config"));
            f.client->slots[1].used = 1;
            f.client->slots[1].token = 2;
            f.client->slots[1].app = target;
            target_ui = get_ui(f.all, 1, error, sizeof error);
            assert(target_ui);
        }
        app_switch(target, 0);
        Editor *server_draft = draft(target, target_ui);
        assert(server_draft && ed_from_utf8(server_draft, "SERVER_ZERO_DRAFT"));
        server_draft->pos = 4;
        size_t channel = app_buffer(target, "#selected-server-channel", B_CHANNEL, 1);
        assert(channel != SIZE_MAX);
        target->buffers[channel].joined = 1;
        app_switch(target, channel);
        Editor *saved = draft(target, target_ui);
        assert(saved && ed_from_utf8(saved, "SELECTED_SERVER_DRAFT"));
        saved->pos = 6;
        key_input(f.app, f.ui, KEY_F(2), 1);
        f.all->server_selection = (size_t)cross_server;
        draw(f.app, f.ui);
        char toolbar[160];
        assert(mvinnstr(3, 0, toolbar, COLS) != ERR);
        toolbar[COLS] = 0;
        assert(strstr(toolbar, "Open") && !strstr(toolbar, "Channels"));
        if (cross_server) {
            MEVENT event = {0};
            event.x = 8; event.y = 5; event.bstate = BUTTON1_PRESSED;
            apply_mouse(f.app, f.ui, event);
        } else key_input(f.app, f.ui, '\n', 0);
        App *active = f.app; Ui *active_ui = f.ui;
        assert(apply_session_actions(f.all, &active, &active_ui) >= 0);
        assert(active == target && active_ui == target_ui);
        assert(f.client->current == (size_t)cross_server && !f.all->servers);
        assert(main_screen(active_ui) && active->current == 0 && active->buffers[0].kind == B_SERVER);
        assert(draft(active, active_ui) == server_draft && server_draft->pos == 4);
        assert(saved->pos == 6 && saved->bytes == strlen("SELECTED_SERVER_DRAFT"));
        key_input(active, active_ui, KEY_F(3), 1);
        draw(active, active_ui);
        assert(active_ui->channels == 1 && !active_ui->app_menu);
        assert(active_ui->chooser_count == 2 && active_ui->chooser_map[0] == 0);
        assert(active_ui->channel_selection == 0);
        key_input(active, active_ui, KEY_DOWN, 1);
        key_input(active, active_ui, '\n', 0);
        assert(main_screen(active_ui) && active->current == channel);
        assert(draft(active, active_ui) == saved && saved->pos == 6);
        assert(!active->control_count && !active->chat_count);
        dispose(&f);
    }
}

static void type_local_text(Fixture *f, const char *text)
{
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        key_input(f->app, f->ui, *p, 0);
}

static void assert_editor_text(const Editor *editor, const char *expected)
{
    char text[512];
    assert(ed_to_utf8(editor, text, sizeof text) == strlen(expected));
    assert(!strcmp(text, expected));
}

static void assert_manual_highlight(void)
{
    int highlighted = 0;
    for (int y = 5; y < LINES - 2; ++y)
        for (int x = 0; x < COLS - 1; ++x)
            highlighted += (mvinch(y, x) & A_REVERSE) != 0;
    assert(highlighted > 0);
}

static void manual_parent_search_and_editor(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    size_t query = f.app->current;
    Editor *saved = draft(f.app, f.ui);
    assert(saved && ed_from_utf8(saved, "QUERY_DRAFT_UNCHANGED"));
    saved->pos = 7;
    key_input(f.app, f.ui, KEY_F(1), 1);
    key_input(f.app, f.ui, 'g', 0);
    for (int i = 0; i < 3; ++i) key_input(f.app, f.ui, KEY_DOWN, 1);
    key_input(f.app, f.ui, '\n', 0);
    assert(f.ui->manual && f.ui->manual_from_app && f.ui->manual_view == query);
    draw(f.app, f.ui);
    (void)visible_control("Search", 3);
    (void)visible_control("Return to: vipirc", 4);
    char row[160];
    for (int y = 3; y < 5; ++y) {
        assert(mvinnstr(y, 0, row, COLS) != ERR); row[COLS] = 0;
        assert(!strstr(row, "Connection:") && !strstr(row, "Nick:") && !strstr(row, "Server:"));
    }
    /* Controlled manual content gives exact first/next/previous, overlapping
     * matches and wrap expectations independent of documentation wording. */
    app_clear_buffer(f.app, 2);
    app_log(f.app, 2, "First Alpha occurrence");
    for (int i = 0; i < 10; ++i) app_log(f.app, 2, "Intermediate documentation line %d", i);
    app_log(f.app, 2, "Later alpha and ALPHA occurrences");
    for (int i = 0; i < 20; ++i) app_log(f.app, 2, "More documentation line %d", i);
    app_log(f.app, 2, "Overlapping ababa literal a.b and UTF-8 caf\303\251");
    Buffer *manual = &f.app->buffers[2];
    const char *first = manual->lines[manual->line_start];
    const char *later = manual->lines[(manual->line_start + 11) % manual->line_cap];
    size_t first_offset = (size_t)(strstr(first, "Alpha") - first);
    size_t later_offset = (size_t)(strstr(later, "alpha") - later);
    size_t final_offset = (size_t)(strstr(later, "ALPHA") - later);

    key_input(f.app, f.ui, '/', 0);
    assert(f.ui->manual_searching && f.ui->manual_search);
    type_local_text(&f, "alpa");
    key_input(f.app, f.ui, KEY_LEFT, 1);
    key_input(f.app, f.ui, 'h', 0);
    key_input(f.app, f.ui, KEY_BACKSPACE, 1);
    key_input(f.app, f.ui, 'H', 0);
    assert_editor_text(f.ui->manual_search, "alpHa");
    assert(f.ui->manual_search->pos == 4);
    draw(f.app, f.ui);
    int y, x; getyx(stdscr, y, x);
    assert(y == LINES - 2 && x == 11 && curs_set(1) == 1);
    assert(mvinch(y, x) & A_REVERSE);
    key_input(f.app, f.ui, '\n', 0);
    assert(!f.ui->manual_searching && f.ui->manual_found);
    assert(!strcmp(f.app->status, "Match 1 of 3."));
    assert(f.ui->manual_match_line == 0 && f.ui->manual_match_offset == first_offset);
    draw(f.app, f.ui); assert_manual_highlight();
    key_input(f.app, f.ui, 'n', 0);
    assert(!strcmp(f.app->status, "Match 2 of 3."));
    assert(f.ui->manual_match_line == 11 && f.ui->manual_match_offset == later_offset);
    key_input(f.app, f.ui, 'n', 0);
    assert(!strcmp(f.app->status, "Match 3 of 3."));
    assert(f.ui->manual_match_offset == final_offset);
    key_input(f.app, f.ui, 'n', 0);
    assert(!strcmp(f.app->status, "Match 1 of 3. Wrapped."));
    assert(f.ui->manual_match_line == 0 && f.ui->manual_match_offset == first_offset);
    key_input(f.app, f.ui, 'N', 0);
    assert(!strcmp(f.app->status, "Match 3 of 3. Wrapped."));
    assert(f.ui->manual_match_line == 11 && f.ui->manual_match_offset == final_offset);
    size_t scroll = manual->scroll;

    key_input(f.app, f.ui, '/', 0);
    assert_editor_text(f.ui->manual_search, "alpHa");
    type_local_text(&f, "discarded");
    key_input(f.app, f.ui, PASTE_BEGIN, 1);
    type_local_text(&f, "/quit\r\n\033");
    key_input(f.app, f.ui, PASTE_END, 1);
    assert(f.ui->manual && f.ui->manual_searching);
    assert_editor_text(f.ui->manual_search, "alpHadiscarded");
    reviewed_input(&f);
    key_input(f.app, f.ui, 27, 0);
    assert(f.ui->manual && !f.ui->manual_searching && f.ui->manual_found);
    assert(!strcmp(f.ui->manual_term, "alpHa") && manual->scroll == scroll);
    assert(f.ui->manual_match_offset == final_offset && saved->pos == 7);

    static const char *const needles[] = {"absent-manual-search", "aba", "a.b", ""};
    static const char *const statuses[] = {
        "No matches. Press / to change the search.", "Match 1 of 2.", "Match 1 of 1.", "Search cleared."
    };
    for (size_t i = 0; i < sizeof needles / sizeof needles[0]; ++i) {
        key_input(f.app, f.ui, '/', 0);
        key_input(f.app, f.ui, KEY_HOME, 1);
        while (f.ui->manual_search->len) key_input(f.app, f.ui, KEY_DC, 1);
        type_local_text(&f, needles[i]);
        key_input(f.app, f.ui, '\n', 0);
        assert(!strcmp(f.app->status, statuses[i]));
        assert(f.ui->manual_found == (i == 1 || i == 2));
        if (!i) assert(manual->scroll == scroll);
        if (i == 1) {
            size_t position = f.ui->manual_match_offset;
            key_input(f.app, f.ui, 'n', 0);
            assert(f.ui->manual_match_offset == position + 2);
        }
    }

    /* The bound is UTF-8 bytes, including during repeat and narrow scrolling. */
    key_input(f.app, f.ui, '/', 0);
    for (size_t i = 0; i < 255; ++i) key_input(f.app, f.ui, 'X', 0);
    key_input(f.app, f.ui, 0x00e9, 0);
    assert(f.ui->manual_search->bytes == 255);
    key_input(f.app, f.ui, 'B', 0);
    for (size_t i = 0; i < 40; ++i) key_input(f.app, f.ui, 'Z', 0);
    assert(f.ui->manual_search->bytes == 256 && f.ui->manual_search->len == 256);
    assert(strstr(f.app->status, "limited to 256 UTF-8 bytes"));
    assert(resizeterm(9, 20) != ERR);
    key_input(f.app, f.ui, KEY_RESIZE, 1);
    draw(f.app, f.ui);
    getyx(stdscr, y, x);
    assert(y == 7 && x >= 7 && x < 19 && curs_set(1) == 1);
    assert(f.ui->manual_searching && f.ui->manual_search->pos == 256);
    key_input(f.app, f.ui, KEY_BACKSPACE, 1);
    key_input(f.app, f.ui, KEY_BACKSPACE, 1);
    key_input(f.app, f.ui, 0x00e9, 0);
    assert(f.ui->manual_search->bytes == 256 && f.ui->manual_search->len == 255);
    key_input(f.app, f.ui, 27, 0);
    assert(f.ui->manual && !f.ui->manual_searching && !f.ui->manual_term[0]);
    key_input(f.app, f.ui, 27, 0);
    assert(f.ui->app_menu && !f.ui->manual && f.app->current == query);
    key_input(f.app, f.ui, 'b', 0);
    assert(main_screen(f.ui) && draft(f.app, f.ui) == saved && saved->pos == 7);
    assert_editor_text(saved, "QUERY_DRAFT_UNCHANGED");

    /* Toolbar Back has the same app parent and status as Escape. */
    assert(resizeterm(24, 132) != ERR);
    for (int use_mouse = 0; use_mouse < 2; ++use_mouse) {
        key_input(f.app, f.ui, KEY_F(1), 1);
        key_input(f.app, f.ui, 'g', 0);
        for (int i = 0; i < 3; ++i) key_input(f.app, f.ui, KEY_DOWN, 1);
        key_input(f.app, f.ui, '\n', 0);
        assert(f.ui->manual && f.ui->manual_from_app);
        draw(f.app, f.ui);
        if (use_mouse)
            apply_mouse(f.app, f.ui, visible_control("Back", 3));
        else
            key_input(f.app, f.ui, '\n', 0);
        assert(f.ui->app_menu && !f.ui->manual && f.app->current == query);
        assert(!strcmp(f.app->status, "Returned to vipirc."));
        key_input(f.app, f.ui, 'b', 0);
        assert(main_screen(f.ui) && draft(f.app, f.ui) == saved && saved->pos == 7);
        assert_editor_text(saved, "QUERY_DRAFT_UNCHANGED");
    }

    /* A direct slash command has a different, explicit parent. */
    assert(open_command(f.app, f.ui, "/manual"));
    key_input(f.app, f.ui, '\n', 0);
    assert(f.ui->manual && !f.ui->manual_from_app && f.ui->manual_view == query);
    draw(f.app, f.ui);
    (void)visible_control("Return to: Query: Al", 4);
    apply_mouse(f.app, f.ui, visible_control("Back", 3));
    assert(main_screen(f.ui) && !f.ui->app_menu && f.app->current == query);
    assert(saved->pos == 7); assert_editor_text(saved, "QUERY_DRAFT_UNCHANGED");
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void help_describes_manual_search(void)
{
    Fixture f = fresh(CONFIG_FOCUS_BRACKETS, 132);
    size_t origin = f.app->current;
    assert(help_command(f.app, f.ui, ""));
    assert(f.ui->manual && !f.ui->manual_term[0]);
    assert(strstr(f.app->status, "/help TEXT searches it"));
    assert(strstr(f.app->status, "does not print topic help"));
    close_manual(f.app, f.ui);
    assert(f.app->current == origin);
    assert(help_command(f.app, f.ui, "GETTING HELP"));
    assert(f.ui->manual && f.ui->manual_found);
    assert(!strcmp(f.ui->manual_term, "GETTING HELP"));
    close_manual(f.app, f.ui);
    assert(help_command(f.app, f.ui, "all"));
    assert(f.ui->manual && !f.ui->manual_term[0]);
    assert(strstr(f.app->status, "searches it"));
    char oversized[258];
    memset(oversized, 'x', sizeof oversized - 1);
    oversized[sizeof oversized - 1] = 0;
    assert(!help_command(f.app, f.ui, oversized));
    assert(strstr(f.app->status, "Usage: /help [TEXT]"));
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void graphical_dcc_command_routing(void)
{
    Fixture f = fresh(CONFIG_FOCUS_BRACKETS, 132);
    size_t origin = f.app->current;
    Editor *saved = draft(f.app, f.ui);
    assert(saved && ed_from_utf8(saved, "DCC_COMMAND_CHAT_DRAFT"));
    saved->pos = 6;
    assert(submit_text(f.app, f.ui, "/dcc list", 1));
    assert(!transfers_active(f.all->transfers));
    assert(!strcmp(f.app->status, "No file transfers."));
    assert(f.app->current == origin && draft(f.app, f.ui) == saved && saved->pos == 6);
    assert_editor_text(saved, "DCC_COMMAND_CHAT_DRAFT");
    assert(!submit_text(f.app, f.ui, "/dcc recipient-only", 1));
    assert(strstr(f.app->status, "Usage: /dcc"));
    assert(!transfers_active(f.all->transfers));
    assert_editor_text(saved, "DCC_COMMAND_CHAT_DRAFT");
    assert(submit_text(f.app, f.ui, "/transfers list", 1));
    assert(!transfers_active(f.all->transfers));
    assert(submit_text(f.app, f.ui, "/dcc", 1));
    assert(transfers_active(f.all->transfers));
    transfers_close(f.all->transfers);
    assert(submit_text(f.app, f.ui, "/transfers", 1));
    assert(transfers_active(f.all->transfers));
    assert(f.app->current == origin && saved->pos == 6);
    assert_editor_text(saved, "DCC_COMMAND_CHAT_DRAFT");
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void graphical_dcc_browser_escape(void)
{
    Fixture f = fresh(CONFIG_FOCUS_BRACKETS, 132);
    f.client->test_mode = 1;
    assert(submit_text(f.app, f.ui, "/dcc", 1));
    draw(f.app, f.ui);
    apply_mouse(f.app, f.ui, visible_control("Send...", 3));
    draw(f.app, f.ui);
    apply_mouse(f.app, f.ui, visible_control("Source file:", 6));
    draw(f.app, f.ui);
    char row[160];
    assert(mvinnstr(2, 0, row, COLS) != ERR); row[COLS] = 0;
    assert(strstr(row, "Choose file"));
    assert(mvinnstr(LINES - 1, 0, row, COLS) != ERR); row[COLS] = 0;
    assert(strstr(row, "OFFLINE TEST MODE"));
    key_input(f.app, f.ui, 27, 0);
    draw(f.app, f.ui);
    assert(transfers_active(f.all->transfers));
    assert(mvinnstr(2, 0, row, COLS) != ERR); row[COLS] = 0;
    assert(strstr(row, "Send file"));
    apply_mouse(f.app, f.ui, visible_control("Nickname:", 5));
    draw(f.app, f.ui);
    key_input(f.app, f.ui, 'X', 0);
    key_input(f.app, f.ui, 27, 0);
    draw(f.app, f.ui);
    assert(mvinnstr(5, 0, row, COLS) != ERR); row[COLS] = 0;
    assert(strstr(row, "Nickname: Al") && !strstr(row, "Nickname: X"));
    key_input(f.app, f.ui, KEY_F(1), 1);
    assert(!transfers_active(f.all->transfers) && f.ui->app_menu);
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void guided_expect(Fixture *f, const char *question)
{
    assert(guided_active(f->all->guided));
    if (f->ui->hotkey_guard) reviewed_input(f);
    else draw(f->app, f->ui);
    char row[256];
    assert(COLS < (int)sizeof row);
    assert(mvinnstr(5, 0, row, COLS) != ERR); row[COLS] = 0;
    if (!strstr(row, question)) fprintf(stderr, "Expected guided question '%s', got %s\n", question, row);
    assert(strstr(row, question));
}

static void guided_type(Fixture *f, const char *answer)
{
    for (const unsigned char *p = (const unsigned char *)answer; *p; ++p)
        key_input(f->app, f->ui, *p, 0);
}

static void guided_answer(Fixture *f, const char *question, const char *answer)
{
    guided_expect(f, question);
    guided_type(f, answer);
    draw(f->app, f->ui);
    fixture_clock += 200;
    key_input(f->app, f->ui, '\r', 0);
}

static void assert_screen_excludes(const char *text)
{
    char row[256];
    assert(COLS < (int)sizeof row);
    for (int y = 0; y < LINES; ++y) {
        assert(mvinnstr(y, 0, row, COLS) != ERR); row[COLS] = 0;
        assert(!strstr(row, text));
    }
}

static void graphical_guided_editing_and_origin(void)
{
    Fixture f = fresh(CONFIG_FOCUS_BRACKETS, 40);
    size_t origin = f.app->current;
    Editor *saved = draft(f.app, f.ui);
    assert(saved && ed_from_utf8(saved, "GUIDED_CHAT_DRAFT"));
    saved->pos = 5;
    key_input(f.app, f.ui, KEY_F(1), 1);
    assert(f.ui->app_menu);
    assert(submit_text(f.app, f.ui, "/quickstart", 1));
    guided_expect(&f, "Server");
    key_input(f.app, f.ui, KEY_F(2), 1);
    assert(guided_active(f.all->guided) && !f.all->servers);
    for (size_t i = 0; i < 52; ++i) key_input(f.app, f.ui, 'x', 0);
    key_input(f.app, f.ui, KEY_HOME, 1);
    for (size_t i = 0; i < 38; ++i) key_input(f.app, f.ui, KEY_RIGHT, 1);
    draw(f.app, f.ui);
    int y, x; getyx(stdscr, y, x);
    assert(y == LINES - 4 && x == 1);
    assert(curs_set(1) == 1);
    key_input(f.app, f.ui, KEY_BACKSPACE, 1);
    draw(f.app, f.ui);
    getyx(stdscr, y, x);
    assert(y == LINES - 5 && x == 38);
    assert(PAIR_NUMBER(mvinch(y, x)) == 0);
    assert(mvinch(y, x) & A_UNDERLINE);
    key_input(f.app, f.ui, 27, 0);
    assert(!guided_active(f.all->guided) && f.ui->app_menu);
    assert(f.app->current == origin && saved->pos == 5);
    assert_editor_text(saved, "GUIDED_CHAT_DRAFT");
    assert(!strcmp(f.app->cfg.server, "fixture.invalid"));

    assert(submit_text(f.app, f.ui, "/quickstart", 1));
    guided_expect(&f, "Server");
    MEVENT stale_next = visible_control("Next", 3);
    fixture_clock += 200;
    apply_mouse(f.app, f.ui, stale_next);
    /* Neither a queued click nor an Enter may answer a not-yet-drawn question. */
    apply_mouse(f.app, f.ui, stale_next);
    key_input(f.app, f.ui, '\r', 0);
    guided_expect(&f, "Use TLS");
    key_input(f.app, f.ui, 27, 0);
    guided_expect(&f, "Server");
    assert(resizeterm(13, 31) != ERR);
    draw(f.app, f.ui);
    fixture_clock += 200;
    key_input(f.app, f.ui, '\r', 0);
    assert(resizeterm(24, 40) != ERR);
    guided_expect(&f, "Server");
    key_input(f.app, f.ui, PASTE_BEGIN, 1);
    guided_type(&f, "pasted.example");
    key_input(f.app, f.ui, '\r', 0);
    key_input(f.app, f.ui, 27, 0);
    key_input(f.app, f.ui, PASTE_END, 1);
    key_input(f.app, f.ui, '\r', 0);
    guided_expect(&f, "Server");
    key_input(f.app, f.ui, 27, 0);
    assert(f.ui->app_menu && !guided_active(f.all->guided));
    assert(submit_text(f.app, f.ui, "/settings", 1));
    assert(settings_is_open(f.ui) && !guided_active(f.all->guided));
    assert_editor_text(saved, "GUIDED_CHAT_DRAFT");
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void graphical_guided_gpg_path(Fixture *f)
{
    assert(submit_text(f->app, f->ui, "/panel", 1));
    guided_answer(f, "Choose a panel", "1");
    guided_answer(f, "Server", "");
    guided_answer(f, "Use TLS", "yes");
    guided_answer(f, "Port", "");
    guided_answer(f, "Nickname", "");
    guided_answer(f, "IRC username", "");
    guided_answer(f, "Real name", "");
    guided_answer(f, "Auto-connect", "");
    guided_answer(f, "Auto-join", "");
    guided_answer(f, "Auto-authentication", "sasl");
    guided_answer(f, "Authentication account", "test-account");
    guided_answer(f, "Authentication password source", "gpg");
    guided_answer(f, "Authentication GPG file", "create");
}

static void graphical_guided_application_and_secret(void)
{
    Fixture f = fresh(CONFIG_FOCUS_BRACKETS, 132);
    size_t origin = f.app->current;
    assert(submit_text(f.app, f.ui, "/panel", 1));
    guided_answer(&f, "Choose a panel", "0");
    guided_answer(&f, "History lines", "100");
    guided_answer(&f, "Send mode", "review");
    guided_answer(&f, "Composer mode", "line");
    guided_answer(&f, "Output format", "full");
    guided_answer(&f, "Queue", "no");
    guided_answer(&f, "Program messages", "yes");
    guided_answer(&f, "Persistent logging", "no");
    guided_answer(&f, "Single-session mode", "no");
    guided_answer(&f, "Command suggestions", "contextual");
    guided_answer(&f, "Default interface", "idle");
    assert(f.app->cfg.history_lines == 50 && !f.app->cfg.send_review && !f.app->cfg.output_full);
    guided_answer(&f, "Finish", "2");
    assert(!guided_active(f.all->guided) && main_screen(f.ui));
    assert(f.app->cfg.history_lines == 100 && f.app->cfg.send_review && f.app->cfg.output_full && !f.app->cfg.queue_notices);
    assert(f.app->cfg.command_suggestions == CONFIG_SUGGEST_CONTEXTUAL);
    assert(f.app->cfg.default_interface == CONFIG_INTERFACE_IDLE);
    assert(f.app->current == origin && f.client->current == 0);

    /* Model offline guards are independent of the fake App transport clock. */
    f.client->test_mode = 1;
    graphical_guided_gpg_path(&f);
    guided_answer(&f, "Authentication password file", "/never-open-vipirc-guided-test/secret.gpg");
    guided_expect(&f, "Authentication password file");
    char row[160];
    assert(mvinnstr(LINES - 1, 0, row, COLS) != ERR); row[COLS] = 0;
    assert(strstr(row, "disabled in --test-mode"));
    assert(guided_timeout(f.all->guided) == -1);
    assert(!f.app->cfg.password_gpg_file[0] && !strcmp(f.app->cfg.auth, "none"));
    apply_mouse(f.app, f.ui, visible_control("Cancel", 3));
    assert(!guided_active(f.all->guided) && main_screen(f.ui));

    /* Real preflight, but no helper execution: cancel before Finish. The
     * temporary executable only satisfies the availability check, so this
     * ordinary render test has no installed-GPG dependency. */
    char directory[] = "/tmp/vipirc-guided-secret-XXXXXX";
    assert(mkdtemp(directory));
    char program[256], destination[256];
    assert(snprintf(program, sizeof program, "%s/gpg", directory) > 0);
    assert(snprintf(destination, sizeof destination, "%s/secret.gpg", directory) > 0);
    FILE *helper = fopen(program, "w"); assert(helper);
    assert(fputs("#!/bin/sh\nexit 99\n", helper) >= 0 && !fclose(helper) && !chmod(program, 0700));
    char *old_path = getenv("PATH") ? strdup(getenv("PATH")) : NULL;
    assert(!setenv("PATH", directory, 1));
    f.client->test_mode = 0;
    graphical_guided_gpg_path(&f);
    guided_answer(&f, "Authentication password file", destination);
    guided_answer(&f, "Authentication GPG key fingerprint", "0123456789ABCDEF0123456789ABCDEF01234567");
    static const char secret[] = "GUIDED_SECRET_ONLY_IN_MEMORY";
    guided_expect(&f, "Authentication password to encrypt");
    guided_type(&f, secret);
    draw(f.app, f.ui);
    assert_screen_excludes(secret);
    assert(mvinnstr(LINES - 5, 0, row, COLS) != ERR); row[COLS] = 0;
    assert(strstr(row, "********"));
    assert(curs_set(1) == 1);
    fixture_clock += 200;
    key_input(f.app, f.ui, '\r', 0);
    guided_expect(&f, "Confirm authentication password");
    guided_type(&f, secret);
    draw(f.app, f.ui);
    assert_screen_excludes(secret);
    fixture_clock += 200;
    key_input(f.app, f.ui, '\r', 0);
    guided_answer(&f, "Server password source", "none");
    guided_answer(&f, "CA certificate file", "");
    guided_answer(&f, "Mention text", "");
    guided_answer(&f, "Ignored sender", "");
    guided_expect(&f, "Finish");
    assert(guided_timeout(f.all->guided) == -1);
    assert(!f.app->cfg.password_gpg_file[0] && !strcmp(f.app->cfg.auth, "none"));
    assert_screen_excludes(secret);
    apply_mouse(f.app, f.ui, visible_control("Cancel", 3));
    assert(!guided_active(f.all->guided) && main_screen(f.ui));
    assert(!f.app->chat_count && !f.app->control_count);
    assert(access(destination, F_OK) < 0);
    if (old_path) { assert(!setenv("PATH", old_path, 1)); free(old_path); }
    else assert(!unsetenv("PATH"));
    assert(!unlink(program) && !rmdir(directory));
    dispose(&f);
}

static void delayed_paste_byte(Fixture *f, int byte)
{
    terminal_byte(f->app, f->ui, byte);
    fixture_clock += 200;
    flush_escape(f->app, f->ui, 0);
    draw(f->app, f->ui);
}

static void all_editors_retain_fragmented_paste_end(void)
{
    static const char near[] = "literal-\033[201X-[201~-\033[20Y-tail";
    static const char literal[] = "literal-[201X-[201~-[20Y-tail";
    static const char ending[] = "\033[201~";
    for (int consumer = 0; consumer < 5; ++consumer) {
        Fixture f = fresh(CONFIG_FOCUS_BRACKETS, 132);
        if (consumer == 1) assert(open_command(f.app, f.ui, "/"));
        else if (consumer == 2) {
            open_settings(f.app, f.ui);
            assert(settings_open_server(f.ui->settings, f.client, 0, 0));
            draw(f.app, f.ui);
            key_input(f.app, f.ui, '\r', 0);
        } else if (consumer == 3) {
            assert(submit_text(f.app, f.ui, "/dcc", 1));
            draw(f.app, f.ui);
            apply_mouse(f.app, f.ui, visible_control("Send...", 3));
            draw(f.app, f.ui);
            apply_mouse(f.app, f.ui, visible_control("Nickname:", 5));
        } else if (consumer == 4) {
            assert(submit_text(f.app, f.ui, "/quickstart", 1));
            guided_expect(&f, "Server");
        }
        draw(f.app, f.ui);
        terminal_byte(f.app, f.ui, PASTE_BEGIN);
        assert(capturing_paste(f.ui));
        for (size_t i = 0; i < sizeof near - 1u; ++i) {
            delayed_paste_byte(&f, (unsigned char)near[i]);
            assert(capturing_paste(f.ui));
        }
        assert(!f.ui->paste_end_len);
        terminal_utf8 = 1;
        terminal_byte(f.app, f.ui, 0xc3);
        assert(f.ui->utf8_remaining == 1);
        for (size_t i = 0; i < sizeof ending - 1u; ++i) {
            delayed_paste_byte(&f, (unsigned char)ending[i]);
            assert(!f.ui->utf8_remaining);
            if (i + 1u < sizeof ending - 1u) {
                assert(capturing_paste(f.ui) && f.ui->paste_end_len == i + 1u);
                /* A resize can arrive between any two terminal bytes. */
                terminal_byte(f.app, f.ui, KEY_RESIZE);
                assert(f.ui->paste_end_len == i + 1u);
            }
        }
        assert(!capturing_paste(f.ui) && !f.ui->paste_end_len);
        terminal_byte(f.app, f.ui, 0xa9);
        assert(!f.ui->utf8_remaining);
        terminal_utf8 = 0;
        draw(f.app, f.ui);
        int found = 0;
        char row[160];
        for (int y = 0; y < LINES; ++y) {
            assert(mvinnstr(y, 0, row, COLS) != ERR); row[COLS] = 0;
            if (strstr(row, literal)) found = 1;
        }
        if (!found) fprintf(stderr, "Consumer %d did not retain literal paste near-matches\n", consumer);
        assert(found && !f.app->quit && !f.app->chat_count && !f.app->control_count);
        dispose(&f);
    }
}

static void delayed_opening_marker_and_escape_deadlines(void)
{
    Fixture f = fresh(CONFIG_FOCUS_BRACKETS, 132);
    draw(f.app, f.ui);
    terminal_byte(f.app, f.ui, 27);
    fixture_clock += 200; flush_escape(f.app, f.ui, 0);
    assert(f.ui->escape_len == 1 && main_screen(f.ui));
    terminal_byte(f.app, f.ui, '[');
    fixture_clock += 600; flush_escape(f.app, f.ui, 0);
    assert(f.ui->escape_len == 2 && main_screen(f.ui));
    terminal_byte(f.app, f.ui, '2');
    fixture_clock += 600; flush_escape(f.app, f.ui, 0);
    assert(f.ui->escape_len == 3 && main_screen(f.ui));
    terminal_byte(f.app, f.ui, KEY_RESIZE);
    assert(f.ui->escape_len == 3);
    static const char rest[] = "00~/quit\r\033[201~";
    for (size_t i = 0; i < sizeof rest - 1u; ++i)
        terminal_byte(f.app, f.ui, (unsigned char)rest[i]);
    assert(!f.ui->escape_len && !capturing_paste(f.ui) && !f.app->quit && main_screen(f.ui));
    assert_editor_text(draft(f.app, f.ui), "/quit\n");
    assert(f.ui->literal[f.app->current] && !f.app->chat_count && !f.app->control_count);
    reviewed_input(&f);
    terminal_byte(f.app, f.ui, 27);
    fixture_clock += UI_ESCAPE_MS - 1; flush_escape(f.app, f.ui, 0);
    assert(main_screen(f.ui));
    ++fixture_clock; flush_escape(f.app, f.ui, 0);
    assert(f.ui->channels == 2 && !f.app->quit);
    dispose(&f);
}

static void shortcut_labels_keep_keys_and_invalidate_old_mouse_positions(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    draw(f.app, f.ui);
    MEVENT old_server = visible_control("Servers", 0);
    f.app->cfg.shortcut_labels = 0;
    apply_mouse(f.app, f.ui, old_server);
    assert(!f.all->servers && main_screen(f.ui));
    key_input(f.app, f.ui, KEY_F(2), 1);
    assert(f.all->servers && !f.ui->app_menu);
    key_input(f.app, f.ui, 27, 0);
    draw(f.app, f.ui);
    StatusLayout layout; status_layout(f.app, f.ui, &layout);
    assert(!strcmp(layout.labels[FOCUS_APP], "vipirc"));
    assert(!strcmp(layout.labels[FOCUS_SERVERS], "Servers (1)"));
    assert(!strcmp(layout.labels[FOCUS_QUERIES], "Queries (1)"));
    assert(!strcmp(layout.labels[FOCUS_ACTIVITY], "Mentions/unread (1/0)"));
    MEVENT current_server = visible_control("Servers", 0);
    apply_mouse(f.app, f.ui, current_server);
    assert(f.all->servers);
    key_input(f.app, f.ui, 27, 0);
    key_input(f.app, f.ui, KEY_F(4), 1);
    assert(f.ui->channels == 2);
    key_input(f.app, f.ui, 27, 0);
    draw(f.app, f.ui);
    current_server = visible_control("Servers", 0);
    f.app->cfg.shortcut_labels = 1;
    apply_mouse(f.app, f.ui, current_server);
    assert(!f.all->servers && main_screen(f.ui));
    draw(f.app, f.ui);
    apply_mouse(f.app, f.ui, visible_control("Servers", 0));
    assert(f.all->servers);
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void list_arrows_select_actions_without_switching(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    assert(app_close_view(f.app, f.app->current));
    app_clear_buffer(f.app, 1);
    f.app->buffers[0].unread = 0;
    draw(f.app, f.ui);
    key_input(f.app, f.ui, KEY_F(1 + FOCUS_SERVERS), 1);
    draw(f.app, f.ui);
    assert(f.all->servers && f.all->server_action == 0);
    for (size_t round = 0; round < 4; ++round) {
        for (size_t action = 0; action < SERVER_ACTIONS; ++action) {
            key_input(f.app, f.ui, KEY_RIGHT, 1);
            draw(f.app, f.ui);
            assert(f.all->servers && !f.ui->channels && !f.all->unread);
            assert(f.all->server_action == (action + 1) % SERVER_ACTIONS);
        }
        for (size_t action = 0; action < SERVER_ACTIONS; ++action) {
            key_input(f.app, f.ui, 'h', 0);
            draw(f.app, f.ui);
            assert(f.all->servers && !f.ui->channels && !f.all->unread);
            assert(f.all->server_action == SERVER_ACTIONS - action - 1);
        }
    }
    for (size_t category = FOCUS_CHANNELS; category <= FOCUS_LAST; ++category) {
        key_input(f.app, f.ui, (wint_t)KEY_F(1 + (int)category), 1);
        draw(f.app, f.ui);
        assert(f.all->servers && !f.ui->channels && !f.all->unread);
        assert(!transfers_active(f.all->transfers));
    }
    key_input(f.app, f.ui, 'l', 0);
    key_input(f.app, f.ui, KEY_DOWN, 1);
    key_input(f.app, f.ui, KEY_UP, 1);
    key_input(f.app, f.ui, ' ', 0);
    assert(f.all->server_selection == 0 && f.all->server_action == 1);
    key_input(f.app, f.ui, '\n', 0);
    assert(settings_is_open(f.ui) && !f.all->servers);
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);

    f = fresh(CONFIG_FOCUS_REVERSE, 132);
    size_t first = f.app->current;
    uint64_t first_id = f.app->buffers[first].id;
    size_t second = app_buffer(f.app, "SecondPeer", B_QUERY, 1);
    assert(second != SIZE_MAX);
    open_pms(f.app, f.ui);
    draw(f.app, f.ui);
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    key_input(f.app, f.ui, KEY_DOWN, 1);
    draw(f.app, f.ui);
    assert(f.ui->channels == 2 && f.ui->channel_selection == 1 && f.ui->chooser_action == 1);
    key_input(f.app, f.ui, KEY_UP, 1);
    draw(f.app, f.ui);
    assert(f.ui->channel_selection == 0 && f.ui->chooser_action == 1);
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    key_input(f.app, f.ui, KEY_DOWN, 1);
    draw(f.app, f.ui);
    assert(f.ui->chooser_action == 2 && f.ui->channel_selection == 1);
    key_input(f.app, f.ui, '\n', 0);
    assert(f.ui->channels == 2 && f.ui->chooser_action == 0);
    assert(app_buffer(f.app, "SecondPeer", B_QUERY, 0) == SIZE_MAX);
    assert(f.app->buffers[first].id == first_id && !strcmp(f.app->buffers[first].name, "Al"));
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void stale_compact_list_clicks_after_keys(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 20);
    open_pms(f.app, f.ui);
    draw(f.app, f.ui);
    MEVENT open_click = visible_control("Open", 3);
    size_t original = f.app->current;
    uint64_t original_id = f.app->buffers[original].id;
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    assert(f.ui->chooser_action == 2);
    /* The displayed compact button is still Open. A queued click on those
     * coordinates must not execute Close before Close is actually drawn. */
    apply_mouse(f.app, f.ui, open_click);
    assert(f.ui->channels == 2 && f.app->buffers[original].id == original_id);
    assert(app_buffer(f.app, "Al", B_QUERY, 0) == original);
    draw(f.app, f.ui);
    apply_mouse(f.app, f.ui, visible_control("Close", 3));
    assert(app_buffer(f.app, "Al", B_QUERY, 0) == SIZE_MAX);
    dispose(&f);

    f = fresh(CONFIG_FOCUS_REVERSE, 20);
    open_servers(f.app, f.ui);
    draw(f.app, f.ui);
    open_click = visible_control("Open", 3);
    Net *connected = f.app->net;
    for (size_t i = 0; i < 3; ++i) key_input(f.app, f.ui, KEY_RIGHT, 1);
    assert(f.all->server_action == 3);
    apply_mouse(f.app, f.ui, open_click);
    assert(f.all->servers && f.app->net == connected);
    draw(f.app, f.ui);
    apply_mouse(f.app, f.ui, visible_control("Disconnect", 3));
    assert(!f.app->net);
    dispose(&f);

    f = fresh(CONFIG_FOCUS_REVERSE, 20);
    for (size_t i = 0; i < 24; ++i) {
        char nick[32];
        snprintf(nick, sizeof nick, "Peer%02zu", i);
        assert(app_buffer(f.app, nick, B_QUERY, 1) != SIZE_MAX);
    }
    assert(resizeterm(9, 20) != ERR);
    original = f.app->current;
    open_pms(f.app, f.ui);
    draw(f.app, f.ui);
    MEVENT first_row = visible_control("Al", 4);
    key_input(f.app, f.ui, KEY_END, 1);
    assert(f.ui->channel_selection == 24);
    /* End scrolls the list's selection. The old top row must not silently
     * become an unrelated bottom-of-list row before repaint. */
    apply_mouse(f.app, f.ui, first_row);
    assert(f.ui->channels == 2 && f.app->current == original);
    draw(f.app, f.ui);
    MEVENT new_first = {0};
    new_first.x = 8; new_first.y = 4; new_first.bstate = BUTTON1_PRESSED;
    apply_mouse(f.app, f.ui, new_first);
    assert(!f.ui->channels && f.app->current != original);
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void queued_compact_mouse_buttons_wait_for_redraw(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 20);
    configure_mouse(f.ui, 1);
    assert(f.ui->mouse_active);
    open_pms(f.app, f.ui);
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    draw(f.app, f.ui);
    assert(f.ui->chooser_action == 1);
    MEVENT next = visible_control(">", 3);
    MEVENT old_center = visible_control("New", 3);
    size_t original = f.app->current;
    uint64_t original_id = f.app->buffers[original].id;
    /* Queue in arrival order. handle_mouse must stop after the first event
     * changes the compact action; the old New coordinates cannot run Close. */
    assert(ungetmouse(&next) == OK);
    assert(ungetmouse(&old_center) == OK);
    key_input(f.app, f.ui, KEY_MOUSE, 1);
    assert(f.ui->channels == 2 && f.ui->chooser_action == 2);
    assert(f.app->buffers[original].id == original_id && app_buffer(f.app, "Al", B_QUERY, 0) == original);
    draw(f.app, f.ui);
    MEVENT close = visible_control("Close", 3);
    assert(ungetmouse(&close) == OK);
    key_input(f.app, f.ui, KEY_MOUSE, 1);
    assert(app_buffer(f.app, "Al", B_QUERY, 0) == SIZE_MAX);
    assert(flushinp() != ERR);
    dispose(&f);

    f = fresh(CONFIG_FOCUS_REVERSE, 20);
    configure_mouse(f.ui, 1);
    assert(f.ui->mouse_active);
    open_servers(f.app, f.ui);
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    draw(f.app, f.ui);
    assert(f.all->server_action == 2);
    next = visible_control(">", 3);
    old_center = visible_control("Connect", 3);
    Net *connected = f.app->net;
    assert(ungetmouse(&next) == OK);
    assert(ungetmouse(&old_center) == OK);
    key_input(f.app, f.ui, KEY_MOUSE, 1);
    assert(f.all->servers && f.all->server_action == 3 && f.app->net == connected);
    draw(f.app, f.ui);
    MEVENT disconnect = visible_control("Disconnect", 3);
    assert(ungetmouse(&disconnect) == OK);
    key_input(f.app, f.ui, KEY_MOUSE, 1);
    assert(!f.app->net);
    assert(flushinp() != ERR);
    dispose(&f);
}

static MEVENT rendered_target(const Ui *u, const char *target, int channel, int roster)
{
    for (size_t i = 0; i < u->text_hit_count; ++i) {
        const TextHit *hit = &u->text_hits[i];
        if (hit->channel != channel || strcmp(hit->target, target)) continue;
        if ((hit->x >= COLS - 22) != roster) continue;
        MEVENT event = {0};
        event.x = hit->x; event.y = hit->y; event.bstate = BUTTON1_PRESSED;
        return event;
    }
    fprintf(stderr, "Missing target '%s' (channel=%d roster=%d) among %zu rendered targets\n",
            target, channel, roster, u->text_hit_count);
    for (size_t i = 0; i < u->text_hit_count; ++i)
        fprintf(stderr, "  %d,%d channel=%d %s\n", u->text_hits[i].y, u->text_hits[i].x,
                u->text_hits[i].channel, u->text_hits[i].target);
    assert(!"Expected rendered nickname/channel target is missing");
    MEVENT event = {0};
    return event;
}

static void nick_roster_mutation_before_click(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    size_t channel = app_buffer(f.app, "#click", B_CHANNEL, 1);
    assert(channel != SIZE_MAX);
    f.app->buffers[channel].joined = 1;
    f.app->cfg.user_list = 1;
    app_switch(f.app, channel);
    feed(&f, ":srv 353 tester = #click :alice bob tester\r\n:srv 366 tester #click :End\r\n");
    draw(f.app, f.ui);
    MEVENT click = rendered_target(f.ui, "alice", 0, 1);
    /* Incoming state changes compact and re-sort the same roster storage
     * before a queued mouse press is dispatched. Its displayed nick wins. */
    feed(&f, ":alice!u@host NICK :Zoe\r\n");
    feed(&f, ":Aaron!u@host JOIN #click\r\n");
    feed(&f, ":bob!u@host PART #click :gone\r\n");
    apply_mouse(f.app, f.ui, click);
    assert(f.ui->nick_menu && !strcmp(f.ui->selected_nick, "alice"));
    assert(f.ui->text_hit_count <= UI_TEXT_HITS_MAX);
    dispose(&f);
}

static void history_eviction_before_click(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    size_t original = f.app->current;
    app_log(f.app, original, "[Al] -Notice-Peer- #retained.link sender snapshot");
    draw(f.app, f.ui);
    MEVENT click = rendered_target(f.ui, "Notice-Peer", 0, 0);
    for (size_t i = 0; i < 200; ++i)
        app_log(f.app, original, "[12:01:00] [Al] <Replacement> history rollover %zu", i);
    assert(f.app->buffers[original].line_count == 50);
    apply_mouse(f.app, f.ui, click);
    assert(f.ui->nick_menu && !strcmp(f.ui->selected_nick, "Notice-Peer"));
    dispose(&f);
}

static void retained_channel_target_before_history_change(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    size_t original = f.app->current;
    size_t prefix = app_buffer(f.app, "#foo", B_CHANNEL, 1);
    size_t dotted = app_buffer(f.app, "#foo.bar", B_CHANNEL, 1);
    size_t bracket = app_buffer(f.app, "#room]", B_CHANNEL, 1);
    assert(prefix != SIZE_MAX && dotted != SIZE_MAX && bracket != SIZE_MAX);
    f.app->buffers[prefix].joined = f.app->buffers[dotted].joined = f.app->buffers[bracket].joined = 1;
    app_log(f.app, original, "[12:00:00] [Al] <Al> (#foo.bar). (#room]).");
    draw(f.app, f.ui);
    MEVENT dot = rendered_target(f.ui, "#foo.bar", 1, 0);
    MEVENT close = rendered_target(f.ui, "#room]", 1, 0);
    for (size_t i = 0; i < 200; ++i) app_log(f.app, original, "new history %zu", i);
    apply_mouse(f.app, f.ui, dot);
    assert(f.app->current == dotted && !f.app->control_count);
    app_switch(f.app, original);
    /* A fresh frame with the bracketed name must still use the exact
     * retained target, including a legal closing bracket. */
    app_log(f.app, original, "[12:00:00] [Al] <Al> (#room]).");
    draw(f.app, f.ui);
    close = rendered_target(f.ui, "#room]", 1, 0);
    apply_mouse(f.app, f.ui, close);
    assert(f.app->current == bracket && !f.app->control_count);
    dispose(&f);
}

static void cross_server_unread_returns_to_selected_view(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    App *second = calloc(1, sizeof *second);
    char error[256];
    assert(second && app_init(second, &f.app->cfg, "/unused/second-config"));
    f.client->slots[1].used = 1; f.client->slots[1].token = 2; f.client->slots[1].app = second;
    Ui *second_ui = get_ui(f.all, 1, error, sizeof error);
    assert(second_ui);
    second_ui->mouse_active = 1;
    size_t original = f.app->current;
    size_t unread = app_buffer(f.app, "UnreadPeer", B_QUERY, 1);
    assert(unread != SIZE_MAX);
    draw(f.app, f.ui);
    key_input(f.app, f.ui, KEY_F(1 + FOCUS_SERVERS), 1);
    draw(f.app, f.ui);
    key_input(f.app, f.ui, KEY_DOWN, 1);
    key_input(f.app, f.ui, '\n', 0);
    App *active = f.app; Ui *active_ui = f.ui;
    assert(apply_session_actions(f.all, &active, &active_ui) == 1);
    assert(active == second);
    f.app->buffers[unread].unread = 1;
    /* The unread picker chooses both a different server and a new view.
     * That explicit choice supersedes the old server's abandoned preview. */
    f.all->switch_to = 0; f.all->switch_view = unread;
    assert(apply_session_actions(f.all, &active, &active_ui) == 1);
    assert(active == f.app && active->current == unread);
    Editor *composition = draft(active, active_ui);
    assert(composition && ed_from_utf8(composition, "UNREAD_QUERY_DRAFT"));
    composition->pos = 5;
    draw(active, active_ui);
    key_input(active, active_ui, 27, 0);
    key_input(active, active_ui, 27, 0);
    assert(active->current == unread && active->current != original);
    assert(draft(active, active_ui) == composition && composition->pos == 5);
    char bytes[64];
    assert(ed_to_utf8(composition, bytes, sizeof bytes) == strlen("UNREAD_QUERY_DRAFT"));
    assert(!strcmp(bytes, "UNREAD_QUERY_DRAFT"));
    assert(!active->chat_count && !active->control_count);
    dispose(&f);
}

static void activity_targets_survive_history_and_refuse_reused_views(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    size_t original = f.app->current;
    uint64_t original_id = f.app->buffers[original].id;
    feed(&f, ":Al!u@host PRIVMSG tester :tester stable activity source\r\n");
    open_unread(f.app, f.ui);
    draw(f.app, f.ui);
    ActivityHit selected = {0};
    int found = 0;
    for (size_t i = 0; i < f.all->activity_hits_count; ++i) {
        const ActivityHit *hit = &f.all->activity_hits[i];
        if (hit->item < f.all->activity_count - f.all->unread_count && hit->buffer_id == original_id) {
            selected = *hit; found = 1; break;
        }
    }
    assert(found);
    for (int i = 0; i < 70; ++i)
        feed(&f, ":DifferentPeer!u@host PRIVMSG tester :tester newer activity evicts the old mention\r\n");
    assert(f.app->buffers[1].line_count == f.app->buffers[1].line_cap);
    /* The same logical ring index now describes another peer. A queued click
     * must still use the source identity of the line that was displayed. */
    MEVENT click = {0}; click.y = selected.y; click.x = 7; click.bstate = BUTTON1_PRESSED;
    apply_mouse(f.app, f.ui, click);
    assert(f.all->switch_to == 0 && f.all->switch_view == original);
    f.all->switch_to = f.all->switch_view = SIZE_MAX;
    f.all->unread = 1;
    f.all->unread_selection = selected.item;
    assert(app_close_view(f.app, original));
    size_t replacement = app_buffer(f.app, "Al", B_QUERY, 1);
    assert(replacement != SIZE_MAX && f.app->buffers[replacement].id != original_id);
    activate_unread(f.app, f.ui);
    assert(f.all->switch_to == SIZE_MAX && f.all->switch_view == SIZE_MAX);
    assert(strstr(f.app->status, "no longer available"));
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void closed_query_mentions_reopen_and_clear(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    size_t old_query = f.app->current;
    uint64_t old_id = f.app->buffers[old_query].id;
    feed(&f, ":Al!u@host PRIVMSG tester :tester retained query mention\r\n");
    size_t channel = app_buffer(f.app, "#keep-history", B_CHANNEL, 1);
    assert(channel != SIZE_MAX);
    app_switch(f.app, channel);
    app_log(f.app, channel, "KEEP_ORDINARY_CHANNEL_HISTORY");
    Editor *saved = draft(f.app, f.ui);
    assert(saved && ed_from_utf8(saved, "KEEP_CHANNEL_DRAFT"));
    saved->pos = 4;
    assert(app_close_view(f.app, old_query));
    assert(f.app->buffers[1].line_queries && f.app->buffers[1].line_query_count);
    channel = app_buffer(f.app, "#keep-history", B_CHANNEL, 0);
    size_t replacement = app_buffer(f.app, "ReplacementPeer", B_QUERY, 1);
    assert(channel != SIZE_MAX && replacement != SIZE_MAX);
    uint64_t replacement_id = f.app->buffers[replacement].id;
    open_unread(f.app, f.ui);
    draw(f.app, f.ui);
    MEVENT click = {0}; int found = 0;
    for (size_t i = 0; i < f.all->activity_hits_count; ++i) {
        ActivityHit *hit = &f.all->activity_hits[i];
        if (hit->buffer_id == old_id) {
            click.y = hit->y; click.x = 8; click.bstate = BUTTON1_PRESSED;
            found = 1; break;
        }
    }
    assert(found);
    apply_mouse(f.app, f.ui, click);
    App *active = f.app; Ui *active_ui = f.ui;
    assert(apply_session_actions(f.all, &active, &active_ui) >= 0);
    size_t reopened = app_buffer(f.app, "Al", B_QUERY, 0);
    assert(reopened != SIZE_MAX && f.app->current == reopened && main_screen(f.ui));
    assert(f.app->buffers[reopened].id != old_id && f.app->buffers[reopened].id != replacement_id);
    assert(f.app->buffers[replacement].id == replacement_id);
    assert(!f.app->buffers[1].line_query_count && !f.app->buffers[1].line_queries);
    int rebound = 0;
    for (size_t i = 0; i < f.app->buffers[1].line_count; ++i)
        rebound |= app_mention_origin(f.app, i) == f.app->buffers[reopened].id;
    assert(rebound && !f.app->chat_count && !f.app->control_count);
    assert(f.ui->drafts[channel] == saved && saved->pos == 4);
    assert_editor_text(saved, "KEEP_CHANNEL_DRAFT");

    App *second = calloc(1, sizeof *second);
    char error[256];
    assert(second && app_init(second, &f.app->cfg, "/unused/clear-second-config"));
    f.client->slots[1].used = 1; f.client->slots[1].token = 2; f.client->slots[1].app = second;
    Ui *other_ui = get_ui(f.all, 1, error, sizeof error);
    assert(other_ui);
    second->net = calloc(1, sizeof(Net)); assert(second->net);
    second->registered = second->started = second->cap_done = 1;
    second->last_receive = second->began = fixture_clock;
    Fixture other = f; other.app = second; other.ui = other_ui;
    feed(&other, ":SecondPeer!u@host PRIVMSG tester :tester second retained mention\r\n");
    size_t other_query = app_buffer(second, "SecondPeer", B_QUERY, 0);
    assert(other_query != SIZE_MAX);
    size_t lines = f.app->buffers[channel].line_count, other_lines = second->buffers[other_query].line_count;
    f.app->buffers[channel].unread = 7; f.app->buffers[channel].unread_mentions = 3;
    second->buffers[other_query].unread = 5; second->buffers[other_query].unread_mentions = 2;
    assert(f.app->buffers[1].line_count && second->buffers[1].line_count);
    key_input(f.app, f.ui, KEY_F(5), 1);
    draw(f.app, f.ui);
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    draw(f.app, f.ui);
    (void)visible_control("Clear mentions", 3);
    key_input(f.app, f.ui, '\n', 0);
    assert(!f.app->buffers[1].line_count && !second->buffers[1].line_count);
    assert(f.app->buffers[channel].line_count == lines && second->buffers[other_query].line_count == other_lines);
    assert(f.app->buffers[channel].unread == 7 && f.app->buffers[channel].unread_mentions == 3);
    assert(second->buffers[other_query].unread == 5 && second->buffers[other_query].unread_mentions == 2);
    assert(saved->pos == 4); assert_editor_text(saved, "KEEP_CHANNEL_DRAFT");
    feed(&f, ":Al!u@host PRIVMSG tester :tester command clear mention\r\n");
    feed(&other, ":SecondPeer!u@host PRIVMSG tester :tester second command clear mention\r\n");
    assert(f.app->buffers[1].line_count && second->buffers[1].line_count);
    size_t other_kept = second->buffers[other_query].line_count;
    int other_unread = second->buffers[other_query].unread;
    key_input(f.app, f.ui, KEY_F(1), 1);
    key_input(f.app, f.ui, '/', 0);
    type_local_text(&f, "mentions clear");
    key_input(f.app, f.ui, '\n', 0);
    assert(!f.app->buffers[1].line_count && !second->buffers[1].line_count);
    assert(f.app->buffers[channel].line_count == lines && f.app->buffers[channel].unread == 7);
    assert(second->buffers[other_query].line_count == other_kept && second->buffers[other_query].unread == other_unread);
    assert_editor_text(saved, "KEEP_CHANNEL_DRAFT");
    assert(!f.app->chat_count && !f.app->control_count && !second->chat_count && !second->control_count);
    dispose(&f);
}

static void network_directory_sorting(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 96);
    app_list_clear(f.app);
    f.app->list.active = 1;
    f.app->list.min_set = 1;
    f.app->list.min_users = 10;
    f.app->list.max_set = 1;
    f.app->list.max_users = 100;
    feed(&f, ":server 321 tester Channel :Users Name\r\n");
    feed(&f, ":server 322 tester #tiny 3 :too small\r\n");
    feed(&f, ":server 322 tester #beta 40 :Beta topic\r\n");
    feed(&f, ":server 322 tester #alpha 20 :Alpha topic\r\n");
    feed(&f, ":server 322 tester #gamma 75 :Gamma topic\r\n");
    feed(&f, ":server 323 tester :End of LIST\r\n");
    assert(f.app->list.received == 4 && f.app->list.count == 3 && f.app->list.complete);
    open_directory(f.app, f.ui);
    draw(f.app, f.ui);
    assert(f.ui->channels == 3 && f.app->list.sort == APP_LIST_SORT_USERS && f.app->list.descending);
    assert(!strcmp(f.app->list.entries[0].channel, "#gamma"));
    (void)visible_control("Channel", 4);
    (void)visible_control("Users", 4);
    (void)visible_control("Topic", 4);

    directory_key(f.app, f.ui, KEY_LEFT, 1);
    assert(f.app->list.sort == APP_LIST_SORT_CHANNEL && f.app->list.descending);
    directory_key(f.app, f.ui, 's', 0);
    assert(!f.app->list.descending && !strcmp(f.app->list.entries[0].channel, "#alpha"));
    draw(f.app, f.ui);

    MEVENT users = visible_control("Users", 4);
    apply_mouse(f.app, f.ui, users);
    assert(f.app->list.sort == APP_LIST_SORT_USERS && !f.app->list.descending);
    assert(f.app->list.entries[0].users == 20);
    draw(f.app, f.ui);
    users = visible_control("Users", 4);
    apply_mouse(f.app, f.ui, users);
    assert(f.app->list.descending && f.app->list.entries[0].users == 75);

    f.ui->channel_selection = 1;
    size_t queued = f.app->control_count;
    directory_key(f.app, f.ui, '\n', 0);
    assert(f.ui->channels == 0 && f.app->control_count == queued + 1);
    dispose(&f);
    puts("network directory: live LIST cache, local user filtering, keyboard column sort, mouse header sort/direction and selected join passed");
}

static void app_quit_requires_fresh_yes(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    Editor *saved = draft(f.app, f.ui);
    assert(saved && ed_from_utf8(saved, "QUIT_QUESTION_KEEPS_DRAFT")); saved->pos = 4;
    key_input(f.app, f.ui, KEY_F(1), 1);
    static const wint_t cancelled[] = {'n', 'N', 'x', '\n', '\t', 27, 4, KEY_LEFT, KEY_F(2)};
    for (size_t i = 0; i < sizeof cancelled / sizeof cancelled[0]; ++i) {
        key_input(f.app, f.ui, 27, 0);
        assert(f.ui->quit_confirm && !f.app->quit);
        fixture_clock += 200;
        key_input(f.app, f.ui, cancelled[i], cancelled[i] >= KEY_MIN);
        assert(f.ui->app_menu && !f.ui->quit_confirm && !f.app->quit);
    }
    key_input(f.app, f.ui, 27, 0);
    key_input(f.app, f.ui, 'Y', 0);
    assert(f.ui->quit_confirm && !f.app->quit); /* Same key burst is not consent. */
    fixture_clock += 200;
    key_input(f.app, f.ui, KEY_RESIZE, 1);
    draw(f.app, f.ui);
    MEVENT click = {0}; click.y = LINES - 2; click.x = 2; click.bstate = BUTTON1_PRESSED;
    apply_mouse(f.app, f.ui, click);
    key_input(f.app, f.ui, PASTE_BEGIN, 1);
    type_local_text(&f, "Yy\r\n\033");
    key_input(f.app, f.ui, PASTE_END, 1);
    assert(f.ui->quit_confirm && f.ui->app_menu && !f.app->quit);
    reviewed_input(&f);
    key_input(f.app, f.ui, 'n', 0);
    key_input(f.app, f.ui, 'b', 0);
    assert(main_screen(f.ui) && draft(f.app, f.ui) == saved && saved->pos == 4);
    assert_editor_text(saved, "QUIT_QUESTION_KEEPS_DRAFT");
    for (int uppercase = 0; uppercase < 2; ++uppercase) {
        key_input(f.app, f.ui, KEY_F(1), 1);
        key_input(f.app, f.ui, 27, 0);
        fixture_clock += 200;
        key_input(f.app, f.ui, uppercase ? 'Y' : 'y', 0);
        assert(f.app->quit);
        f.app->quit = f.ui->quit_confirm = 0;
    }
    assert(!f.app->chat_count && !f.app->control_count);
    dispose(&f);
}

static void delete_uses_existing_close_guards(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 132);
    size_t query = f.app->current;
    Editor *saved = draft(f.app, f.ui);
    assert(saved && ed_from_utf8(saved, "DELETE_QUERY_DRAFT"));
    key_input(f.app, f.ui, KEY_F(4), 1); draw(f.app, f.ui);
    key_input(f.app, f.ui, KEY_DC, 1);
    assert(f.ui->confirming == 2 && f.ui->close_view == query);
    fixture_clock += 200; key_input(f.app, f.ui, '\n', 0);
    assert(!f.ui->confirming && app_buffer(f.app, "Al", B_QUERY, 0) == query);
    assert_editor_text(saved, "DELETE_QUERY_DRAFT");
    draw(f.app, f.ui); key_input(f.app, f.ui, KEY_DC, 1);
    fixture_clock += 200; key_input(f.app, f.ui, 'y', 0);
    assert(app_buffer(f.app, "Al", B_QUERY, 0) == SIZE_MAX);
    open_channels(f.app, f.ui); draw(f.app, f.ui);
    key_input(f.app, f.ui, KEY_DC, 1);
    assert(f.app->buffers[0].kind == B_SERVER && !f.ui->confirming);
    size_t channel = app_buffer(f.app, "#retained-channel", B_CHANNEL, 1);
    assert(channel != SIZE_MAX);
    open_channels(f.app, f.ui); draw(f.app, f.ui);
    key_input(f.app, f.ui, KEY_END, 1); draw(f.app, f.ui);
    key_input(f.app, f.ui, KEY_DC, 1);
    assert(app_buffer(f.app, "#retained-channel", B_CHANNEL, 0) == SIZE_MAX);
    key_input(f.app, f.ui, KEY_F(2), 1); draw(f.app, f.ui);
    key_input(f.app, f.ui, KEY_DC, 1);
    assert(f.client->slots[0].used && !f.all->close_confirm && f.all->close_slot == SIZE_MAX);
    App *second = calloc(1, sizeof *second);
    assert(second && app_init(second, &f.app->cfg, "/unused/delete-second-config"));
    f.client->slots[1].used = 1; f.client->slots[1].token = 2; f.client->slots[1].app = second;
    key_input(f.app, f.ui, KEY_DOWN, 1); draw(f.app, f.ui);
    key_input(f.app, f.ui, KEY_DC, 1);
    assert(f.all->close_confirm && f.all->close_slot == SIZE_MAX && f.client->slots[1].used);
    fixture_clock += 200; key_input(f.app, f.ui, '\n', 0);
    assert(!f.all->close_confirm && f.client->slots[1].used);
    key_input(f.app, f.ui, KEY_DC, 1);
    fixture_clock += 200; key_input(f.app, f.ui, 'Y', 0);
    assert(f.all->close_slot == 1 && f.client->slots[1].used);
    assert(!f.app->chat_count && !f.app->control_count && !second->chat_count && !second->control_count);
    dispose(&f);
}

int main(void)
{
    /* A warning can be drawn before the ordinary small-terminal guard.
     * Zero/negative widths must return; oversized widths must fit the row. */
    assert(dialog_paragraph("warning", 0, -1, 0, A_NORMAL) == 0);
    assert(dialog_paragraph("warning", -1, -1, 0, A_NORMAL) == 0);
    assert(dialog_paragraph("abc", 1, -1, 0, A_NORMAL) == 3);
    char wide_warning[513];
    memset(wide_warning, 'x', sizeof wide_warning - 1);
    wide_warning[sizeof wide_warning - 1] = 0;
    assert(dialog_paragraph(wide_warning, 4096, -1, 0, A_NORMAL) == 5);
    FILE *out = tmpfile(), *in = tmpfile();
    assert(out && in);
    SCREEN *screen = newterm("xterm-256color", out, in);
    assert(screen);
    assert(keypad(stdscr, TRUE) != ERR);
    vipirc_focus_colors();
    terminal_utf8 = 0;
    for (int style = CONFIG_FOCUS_REVERSE; style <= CONFIG_FOCUS_BRACKETS; ++style) {
        nick_arrival_before_click(style, 80);
        nick_arrival_before_click(style, 96);
        nick_arrival_before_click(style, 132);
        selected_menu_rows_and_nearby_actions(style);
    }
    query_escape_returns_to_list();
    server_open_enters_its_message_view();
    manual_parent_search_and_editor();
    help_describes_manual_search();
    graphical_dcc_command_routing();
    graphical_dcc_browser_escape();
    graphical_guided_editing_and_origin();
    graphical_guided_application_and_secret();
    all_editors_retain_fragmented_paste_end();
    delayed_opening_marker_and_escape_deadlines();
    puts("paste markers: delayed byte-by-byte terminators survive redraws/resizes in chat, commands, settings, DCC and guided fields; near-matches remain literal and never dispatch commands");
    puts("guided setup: shared quickstart/panel, origin and chat-draft retention, real wrapped caret/backspace, fresh Enter/paste/stale-click guards, explicit preferences commit, masked GPG confirmation and offline refusal passed; DCC browser/editor Escape stays local");
    stale_main_after_resize_or_mode();
    stale_main_after_secondary();
    stale_main_after_view_or_small_screen();
    stale_main_after_session_switch();
    empty_query_arrival_before_activation(0);
    empty_query_arrival_before_activation(1);
    mention_counter_width_before_click();
    divider_and_empty_categories();
    shortcut_labels_keep_keys_and_invalidate_old_mouse_positions();
    list_arrows_select_actions_without_switching();
    stale_compact_list_clicks_after_keys();
    queued_compact_mouse_buttons_wait_for_redraw();
    nick_roster_mutation_before_click();
    history_eviction_before_click();
    retained_channel_target_before_history_change();
    cross_server_unread_returns_to_selected_view();
    activity_targets_survive_history_and_refuse_reused_views();
    closed_query_mentions_reopen_and_clear();
    network_directory_sorting();
    app_quit_requires_fresh_yes();
    delete_uses_existing_close_guards();
    endwin(); delscreen(screen); fclose(out); fclose(in);
    static const char *const fallback_terms[] = {"screen", "vt100"};
    for (size_t i = 0; i < sizeof fallback_terms / sizeof fallback_terms[0]; ++i) {
        out = tmpfile(); in = tmpfile();
        assert(out && in);
        screen = newterm(fallback_terms[i], out, in);
        if (!screen) {
            printf("focus fallback: %s terminfo unavailable; that terminal case skipped\n", fallback_terms[i]);
            fclose(out); fclose(in);
            continue;
        }
        assert(keypad(stdscr, TRUE) != ERR);
        vipirc_focus_colors();
        selected_menu_rows_and_nearby_actions(CONFIG_FOCUS_BRACKETS);
        printf("focus fallback: %s selected rows retain bold brackets without dim/reverse attributes\n", fallback_terms[i]);
        endwin(); delscreen(screen); fclose(out); fclose(in);
    }
    puts("status mouse: terminal-default bold selected rows, nearby list actions, query Escape/draft/caret preservation, server Open to its message view, manual search description/parent/wrap/overlap/editor/256-byte limit/resize/paste/draft preservation, six NICK cases, parenthesized counts, local list arrows/selected-row actions, compact-action/scroll and batched-mouse click invalidation, counter width, empty-query activation, roster mutation/history eviction target snapshots, exact channel punctuation, divider/categories and resize/selector/secondary/view/compaction/session invalidation passed; redraw restores clicks");
    return 0;
}
#else
int main(void)
{
    puts("status mouse: skipped; this curses library has no mouse API");
    return 0;
}
#endif
