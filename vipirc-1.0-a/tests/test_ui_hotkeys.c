/* Production full-UI hotkey/table state regressions. Normal builds use
 * curses with private temporary streams. UI_HOTKEY_STATE_ONLY uses an
 * explicitly non-ABI test header and aborting renderer; it validates no
 * terminal rendering, terminal ABI, or physical key delivery. */
#include "../src/ui.c"
#include <assert.h>
#include <sys/stat.h>

/* Compile the real guided-state machine here so asynchronous completion
 * can be staged without public test hooks, helper processes or credentials. */
#define changed hotkey_guided_changed
#define now_ms hotkey_guided_now_ms
#define notice hotkey_guided_notice
#define reset_editor hotkey_guided_reset_editor
#define question hotkey_guided_question
#define finished hotkey_guided_finished
#define back hotkey_guided_back
#define submit hotkey_guided_submit
#define insert hotkey_guided_insert
#define scroll_body hotkey_guided_scroll_body
#define append_lines hotkey_guided_append_lines
#define make_body hotkey_guided_make_body
#define body_lines hotkey_guided_body_lines
#define draw_actions hotkey_guided_draw_actions
#define draw_editor hotkey_guided_draw_editor
#include "../src/guided.c"
#undef changed
#undef now_ms
#undef notice
#undef reset_editor
#undef question
#undef finished
#undef back
#undef submit
#undef insert
#undef scroll_body
#undef append_lines
#undef make_body
#undef body_lines
#undef draw_actions
#undef draw_editor


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

typedef struct HotkeyFixture {
    Client *client;
    UiSet *all;
    Ui *ui;
    App *app;
    char directory[160], path[200];
} HotkeyFixture;

static HotkeyFixture fresh_hotkeys(void)
{
    HotkeyFixture f = {0};
    Config cfg;
    char error[512];
    config_defaults(&cfg);
    strcpy(cfg.server, "fixture.invalid");
    strcpy(cfg.nick, "tester");
    strcpy(cfg.username, "tester");
    cfg.autoconnect = cfg.mouse = cfg.user_list = 0;
    cfg.history_lines = 50;
    strcpy(f.directory, "/tmp/vipirc-hotkeys-XXXXXX");
    assert(mkdtemp(f.directory));
    snprintf(f.path, sizeof f.path, "%s/config.ini", f.directory);
    f.client = calloc(1, sizeof *f.client);
    f.all = calloc(1, sizeof *f.all);
    assert(f.client && f.all);
    assert(client_init(f.client, &cfg, f.path, "tester", 0, 0, error, sizeof error) > 0);
    f.app = client_get_app(f.client, 0, error, sizeof error);
    assert(f.app);
    f.all->client = f.client;
    f.all->switch_to = f.all->switch_view = f.all->close_slot = SIZE_MAX;
    f.all->input_batch = 1;
    f.all->dcc = dcc_create(0);
    f.all->transfers = transfers_create(f.client, f.all->dcc);
    assert(f.all->dcc && f.all->transfers);
    f.ui = get_ui(f.all, 0, error, sizeof error);
    assert(f.ui);
    f.app->net = calloc(1, sizeof(Net));
    assert(f.app->net);
    f.app->registered = f.app->started = f.app->cap_done = 1;
    f.app->last_receive = f.app->began = fixture_clock;
    size_t query = app_buffer(f.app, "Al", B_QUERY, 1);
    assert(query != SIZE_MAX);
    app_switch(f.app, query);
    config_wipe(&cfg, sizeof cfg);
    return f;
}

static void dispose_hotkeys(HotkeyFixture *f)
{
    guided_free(f->all->guided);
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) destroy_ui(f->all->states[i]);
    transfers_free(f->all->transfers);
    dcc_free(f->all->dcc);
    client_free(f->client);
    free(f->all); free(f->client);
    (void)unlink(f->path);
    char sidecar[240];
    snprintf(sidecar, sizeof sidecar, "%s.commands", f->path); (void)unlink(sidecar);
    assert(rmdir(f->directory) == 0);
}

static void settled(HotkeyFixture *f)
{
    fixture_clock += 200;
    ++f->all->input_batch;
#ifdef UI_HOTKEY_STATE_ONLY
    /* Model a completed redraw explicitly; drawing functions abort in this
     * private fixture, so a pass cannot be mistaken for renderer validation. */
    f->ui->bindings.drawn = 1;
    input_guard_drawn(f->app, f->ui, 1);
    if (prompt_active(f->ui->command) && !f->ui->context_help.active &&
        !(f->ui->summary.active && !f->ui->summary.hidden) && !f->ui->bindings.active) {
        if (f->ui->command_guard && !f->ui->command_guard_drawn)
            f->ui->command_guard_after = fixture_clock + 100;
        f->ui->command_guard_drawn = 1;
    }
#else
    draw(f->app, f->ui);
#endif
    fixture_clock += 200;
}

static void bind_key(HotkeyFixture *f, const char *key, int action)
{
    Config cfg;
    char error[512];
    app_config_effective(f->app, &cfg);
    assert(config_hotkey_set(&cfg, config_hotkey_key(key), action, error, sizeof error) > 0);
    assert(client_interface_apply(f->client, &cfg, 0, error, sizeof error) > 0);
    config_wipe(&cfg, sizeof cfg);
}

static void type_name(HotkeyFixture *f, const char *text)
{
    for (; *text; ++text) key_input(f->app, f->ui, (wint_t)(unsigned char)*text, 0);
}

static void feed_key(HotkeyFixture *f, const char *sequence)
{
    for (; *sequence; ++sequence) terminal_byte(f->app, f->ui, (unsigned char)*sequence);
}

static void assert_text(const Editor *editor, const char *text)
{
    char actual[ED_MAX_BYTES + 1];
    assert(editor && ed_to_utf8(editor, actual, sizeof actual) != SIZE_MAX);
    assert(!strcmp(actual, text));
    config_wipe(actual, sizeof actual);
}

static void table_apply_save_cancel(void)
{
    HotkeyFixture f = fresh_hotkeys();
    assert(!f.app->cfg.hotkey_count);
    Editor *editor = draft(f.app, f.ui);
    assert(ed_from_utf8(editor, "KEEP CHAT DRAFT"));
    editor->pos = 4;
    Editor before = *editor;
    key_input(f.app, f.ui, KEY_F(7), 1);
    assert(main_screen(f.ui));
    key_input(f.app, f.ui, KEY_F(1), 1);
    assert(f.ui->app_menu);
    f.ui->app_selection = APP_BINDINGS;
    key_input(f.app, f.ui, '\r', 0);
    assert(f.ui->bindings.active && !f.ui->bindings.count);
    settled(&f); key_input(f.app, f.ui, 'a', 0);
    assert(f.ui->bindings.stage == 1);
    type_name(&f, "F7");
    key_input(f.app, f.ui, '\r', 0);
    assert(f.ui->bindings.stage == 1); /* no unseen-stage acceptance */
    settled(&f); key_input(f.app, f.ui, '\r', 0);
    assert(f.ui->bindings.stage == 2);
    for (size_t i = 0; config_hotkey_action_at(i); ++i)
        if (config_hotkey_action_at(i)->id == CONFIG_ACTION_SUMMARY) f.ui->bindings.action = i;
    key_input(f.app, f.ui, '\r', 0);
    assert(f.ui->bindings.stage == 2);
    settled(&f); key_input(f.app, f.ui, '\r', 0);
    assert(f.ui->bindings.stage == 0 && f.ui->bindings.count == 1);
    assert(!f.app->cfg.hotkey_count);
    /* Applying a stale table must not copy stale unrelated preferences. */
    f.app->cfg.hints = 0;
    settled(&f); key_input(f.app, f.ui, 'p', 0);
    assert(!f.app->cfg.hints);
    assert(config_hotkey_lookup(&f.app->cfg, config_hotkey_key("F7")) == CONFIG_ACTION_SUMMARY);
    assert(access(f.path, F_OK) < 0);
    settled(&f); key_input(f.app, f.ui, 's', 0);
    Config saved;
    char error[512];
    config_defaults(&saved);
    assert(config_load(&saved, f.path, error, sizeof error) > 0);
    assert(saved.hotkey_count == 1 && !saved.hints);
    config_wipe(&saved, sizeof saved);
    settled(&f); key_input(f.app, f.ui, 'd', 0);
    assert(!f.ui->bindings.count);
    key_input(f.app, f.ui, 27, 0);
    assert(!f.ui->bindings.active && f.ui->app_menu);
    assert(f.app->cfg.hotkey_count == 1);
    assert(!memcmp(editor, &before, sizeof before));
    assert(!f.app->chat_count && !f.app->control_count);
    dispose_hotkeys(&f);
}

static void decoder_and_paste_boundaries(void)
{
    HotkeyFixture f = fresh_hotkeys();
    Editor *editor = draft(f.app, f.ui);
    assert(ed_from_utf8(editor, "KEEP"));
    bind_key(&f, "F7", CONFIG_ACTION_BINDINGS);
    bind_key(&f, "Alt-x", CONFIG_ACTION_BINDINGS);
    bind_key(&f, "Ctrl-K", CONFIG_ACTION_QUIT);
    feed_key(&f, "\033z");
    assert(main_screen(f.ui)); assert_text(editor, "KEEP");
    feed_key(&f, "\033[999~");
    assert(main_screen(f.ui)); assert_text(editor, "KEEP");
    /* The bytewise decoder retains a complete fixed key across arbitrary
     * input boundaries; no prefix opens a menu or edits the draft. */
    terminal_byte(f.app, f.ui, 27); ++f.all->input_batch;
    terminal_byte(f.app, f.ui, '['); ++f.all->input_batch;
    terminal_byte(f.app, f.ui, '1'); ++f.all->input_batch;
    terminal_byte(f.app, f.ui, '8');
    assert(!f.ui->bindings.active); assert_text(editor, "KEEP");
    terminal_byte(f.app, f.ui, '~');
    assert(f.ui->bindings.active);
    settled(&f); key_input(f.app, f.ui, 27, 0);
    settled(&f); feed_key(&f, "\033x");
    assert(f.ui->bindings.active);
    settled(&f);
    feed_key(&f, "\033[200~asp\013\033[201~s");
    assert(f.ui->bindings.active && !f.ui->bindings.stage && f.app->cfg.hotkey_count == 3);
    assert(access(f.path, F_OK) < 0);
    settled(&f); key_input(f.app, f.ui, 27, 0);
    settled(&f); feed_key(&f, "\033[200~\013\033[18~\033x\033[201~\013");
    assert(!f.ui->quit_confirm && !f.ui->bindings.active && !f.app->quit);
    settled(&f); terminal_byte(f.app, f.ui, 11);
    assert(f.ui->quit_confirm && !f.app->quit);
    key_input(f.app, f.ui, 'y', 0);
    assert(!f.app->quit); /* same action burst must not answer the dialog */
    settled(&f); key_input(f.app, f.ui, 27, 0);
    assert(!f.ui->quit_confirm);
    bind_key(&f, "Ctrl-D", CONFIG_ACTION_BINDINGS);
    settled(&f); terminal_byte(f.app, f.ui, 4);
    assert(f.ui->bindings.active);
    dispose_hotkeys(&f);
}

static void preserved_command_and_target(void)
{
    HotkeyFixture f = fresh_hotkeys();
    Editor *chat = draft(f.app, f.ui);
    assert(ed_from_utf8(chat, "CHAT"));
    bind_key(&f, "F7", CONFIG_ACTION_SUMMARY);
    bind_key(&f, "F8", CONFIG_ACTION_COMPOSE);
    bind_key(&f, "F9", CONFIG_ACTION_COMMAND);
    bind_key(&f, "F10", CONFIG_ACTION_NEXT);
    assert(open_command(f.app, f.ui, "/msg Al PRIVATE DRAFT"));
    Editor *command = prompt_editor(f.ui->command);
    command->pos = 8;
    Editor before = *command;
    uint64_t origin = f.app->buffers[f.app->current].id;
    key_input(f.app, f.ui, KEY_F(10), 1);
    assert(f.app->buffers[f.app->current].id == origin);
    assert(prompt_active(f.ui->command) && !memcmp(prompt_editor(f.ui->command), &before, sizeof before));
    settled(&f); key_input(f.app, f.ui, KEY_F(7), 1);
    assert(f.ui->summary.active && f.ui->hotkey_command && !f.ui->command);
    settled(&f); key_input(f.app, f.ui, 27, 0);
    assert(prompt_active(f.ui->command) && !f.ui->hotkey_command);
    assert(!memcmp(prompt_editor(f.ui->command), &before, sizeof before));
    key_input(f.app, f.ui, '\r', 0);
    assert(prompt_active(f.ui->command) && !f.app->chat_count && !f.app->control_count);
    settled(&f); key_input(f.app, f.ui, KEY_F(8), 1);
    assert(f.ui->hotkey_command && main_screen(f.ui));
    size_t other = app_buffer(f.app, "Other", B_QUERY, 1);
    assert(other != SIZE_MAX);
    app_switch(f.app, other);
    settled(&f); key_input(f.app, f.ui, KEY_F(9), 1);
    assert(prompt_active(f.ui->command) && f.app->buffers[f.app->current].id == origin);
    assert(!memcmp(prompt_editor(f.ui->command), &before, sizeof before));
    settled(&f); key_input(f.app, f.ui, KEY_F(8), 1);
    size_t old = f.app->current;
    assert(app_close_view(f.app, old));
    settled(&f); key_input(f.app, f.ui, KEY_F(9), 1);
    assert(prompt_active(f.ui->command) && f.ui->command_target_lost);
    assert(!memcmp(prompt_editor(f.ui->command), &before, sizeof before));
    settled(&f); key_input(f.app, f.ui, '\r', 0);
    assert(!f.app->chat_count && !f.app->control_count && prompt_active(f.ui->command));
    assert(!strcmp(app_status_text(f.app), "Original conversation closed. This preserved command cannot run; Esc cancels it."));
    /* Visiting another shortcut page must not rebind a lost command to the
     * replacement view that currently occupies its old array index. */
    settled(&f); key_input(f.app, f.ui, KEY_F(8), 1);
    settled(&f); key_input(f.app, f.ui, KEY_F(9), 1);
    assert(f.ui->command_target_lost);
    settled(&f); key_input(f.app, f.ui, '\r', 0);
    assert(!f.app->chat_count && !f.app->control_count);
    dispose_hotkeys(&f);
}

static void modal_and_action_burst_guards(void)
{
    HotkeyFixture f = fresh_hotkeys();
    bind_key(&f, "F7", CONFIG_ACTION_BINDINGS);
    bind_key(&f, "Ctrl-K", CONFIG_ACTION_CANCEL);
    Editor *editor = draft(f.app, f.ui);
    assert(ed_from_utf8(editor, "UNSENT"));
    Editor before = *editor;
    key_input(f.app, f.ui, 11, 0);
    key_input(f.app, f.ui, '\r', 0);
    assert(!f.app->chat_count && !f.app->control_count);
    assert(!memcmp(editor, &before, sizeof before));
    settled(&f); open_settings(f.app, f.ui);
    key_input(f.app, f.ui, KEY_F(7), 1);
    assert(settings_is_open(f.ui) && !f.ui->bindings.active);
    return_to_chat(f.app, f.ui);
    f.ui->confirming = 1;
    f.ui->confirm_at = fixture_clock;
    key_input(f.app, f.ui, KEY_F(7), 1);
    assert(f.ui->confirming == 1 && !f.ui->bindings.active);
    f.ui->confirming = 0;
    assert(open_command(f.app, f.ui, "/msg Al retained"));
    prompt_paste(f.ui->command, f.app, 1);
    key_input(f.app, f.ui, KEY_F(7), 1);
    assert(prompt_pasting(f.ui->command) && !f.ui->bindings.active);
    prompt_paste(f.ui->command, f.app, 0);
    assert(!f.app->chat_count && !f.app->control_count);
    dispose_hotkeys(&f);
}

static void held_enter_cannot_run_restored_command(void)
{
    HotkeyFixture f = fresh_hotkeys();
    bind_key(&f, "F7", CONFIG_ACTION_SUMMARY);
    assert(open_command(f.app, f.ui, "/quit"));
    key_input(f.app, f.ui, KEY_F(7), 1);
    assert(f.ui->summary.active);
    f.ui->summary.action = 1;
    settled(&f); key_input(f.app, f.ui, '\r', 0);
    assert(prompt_active(f.ui->command) && f.ui->command_guard && !f.app->quit);
    key_input(f.app, f.ui, '\r', 0);
    assert(!f.app->quit);
    settled(&f); key_input(f.app, f.ui, '\r', 0);
    assert(!f.app->quit && f.ui->command_guard_enter_seen);
    for (unsigned i = 0; i < 40; ++i) {
        ++f.all->input_batch;
        fixture_clock += 25;
        key_input(f.app, f.ui, '\r', 0);
        assert(!f.app->quit && f.ui->command_guard);
    }
    /* An explicit later activation still works. Disconnect the in-memory
     * transport so this expected /quit remains a local action. */
    net_close(f.app->net); f.app->net = NULL; f.app->registered = 0;
    settled(&f); key_input(f.app, f.ui, '\r', 0);
    assert(f.app->quit);
    dispose_hotkeys(&f);
}

static void command_context_help_has_return_guard(void)
{
    HotkeyFixture f = fresh_hotkeys();
    bind_key(&f, "F7", CONFIG_ACTION_HELP);
    assert(open_command(f.app, f.ui, "/quit"));
    key_input(f.app, f.ui, KEY_DOWN, 1);
    assert(prompt_help_command(f.ui->command, f.app));
    key_input(f.app, f.ui, KEY_F(7), 1);
    assert(f.ui->context_help.active && prompt_active(f.ui->command) && f.ui->command_guard);
    settled(&f); key_input(f.app, f.ui, 27, 0);
    assert(!f.ui->context_help.active && prompt_active(f.ui->command));
    /* The Run button and keyboard share this entry point. Even a direct
     * activation cannot execute a prompt that was just exposed. */
    f.ui->command_guard_drawn = 0;
    command_action(f.app, f.ui, PROMPT_RUN);
    assert(!f.app->quit);
    settled(&f);
    key_input(f.app, f.ui, KEY_LEFT, 1); /* leave suggestion selection */
    key_input(f.app, f.ui, '\r', 0);
    assert(!f.app->quit && f.ui->command_guard_enter_seen);
    for (unsigned i = 0; i < 40; ++i) {
        ++f.all->input_batch;
        fixture_clock += 25;
        key_input(f.app, f.ui, '\r', 0);
        assert(!f.app->quit && f.ui->command_guard);
    }
    dispose_hotkeys(&f);
}

static void category_labels_follow_binding_scope(void)
{
    HotkeyFixture f = fresh_hotkeys();
    StatusLayout layout;
    f.app->cfg.shortcut_labels = 1;
    status_layout(f.app, f.ui, &layout);
    assert(!strncmp(layout.labels[FOCUS_APP], "F1 ", 3));
    bind_key(&f, "F1", CONFIG_ACTION_BINDINGS);
    status_layout(f.app, f.ui, &layout);
    assert(strcmp(layout.labels[FOCUS_APP], "vipirc") == 0);
    assert(!strncmp(layout.labels[FOCUS_SERVERS], "F2 ", 3));
    key_input(f.app, f.ui, KEY_F(1), 1);
    assert(f.ui->bindings.active && !f.ui->app_menu);
    status_layout(f.app, f.ui, &layout);
    assert(strcmp(layout.labels[FOCUS_APP], "vipirc") == 0);
    settled(&f); key_input(f.app, f.ui, 27, 0);
    f.ui->app_menu = 1;
    status_layout(f.app, f.ui, &layout);
    assert(!strncmp(layout.labels[FOCUS_APP], "F1 ", 3));
    dispose_hotkeys(&f);
}

static void all_actions_preserve_unrelated_chat(void)
{
    for (size_t i = 0; config_hotkey_action_at(i); ++i) {
        const ConfigHotkeyAction *action = config_hotkey_action_at(i);
        HotkeyFixture f = fresh_hotkeys();
        Editor *editor = draft(f.app, f.ui);
        assert(ed_from_utf8(editor, "KEEP UNLESS EXPLICITLY CLEARED"));
        Editor before = *editor;
        bind_key(&f, "F7", action->id);
        key_input(f.app, f.ui, KEY_F(7), 1);
        assert(!f.app->quit && !f.app->chat_count && !f.app->control_count);
        if (action->id == CONFIG_ACTION_CLEARINPUT) assert(!editor->len);
        else assert(!memcmp(editor, &before, sizeof before));
        if (action->id == CONFIG_ACTION_QUIT) assert(f.ui->quit_confirm);
        if (action->id == CONFIG_ACTION_BACKGROUND) assert(f.all->background_requested);
        dispose_hotkeys(&f);
    }
}

static void pasted_commands_need_fresh_activation(void)
{
    HotkeyFixture f = fresh_hotkeys();
    /* An offline /quit is a visible local action, so a regression cannot
     * accidentally write a network frame while reproducing this boundary. */
    net_close(f.app->net); f.app->net = NULL; f.app->registered = 0;
    assert(open_command(f.app, f.ui, "/"));
    feed_key(&f, "\033[200~quit\033[201~\r");
    assert(!f.app->quit && prompt_active(f.ui->command));
    assert_text(prompt_const_editor(f.ui->command), "/quit");
    fixture_clock += 1000; ++f.all->input_batch;
    key_input(f.app, f.ui, '\r', 0); /* time alone cannot authorize Run */
    f.app->dirty = 0;
    command_action(f.app, f.ui, PROMPT_RUN); /* mouse Run shares the gate */
    assert(!f.app->quit && f.app->dirty); /* redraw can recover without traffic */
#if UI_MOUSE
    MEVENT click = {0}; click.bstate = BUTTON1_PRESSED;
    f.app->dirty = 0;
    apply_mouse(f.app, f.ui, click);
    assert(!f.app->quit && f.app->dirty && f.ui->hotkey_guard);
#endif
    int rows = LINES, columns = COLS;
    LINES = 4; COLS = 12;
    input_guard_drawn(f.app, f.ui, 1);
    assert(!f.ui->hotkey_guard_drawn);
    LINES = rows; COLS = columns;
    input_guard_drawn(f.app, f.ui, 0); /* a failed refresh gets no credit */
    assert(!f.ui->hotkey_guard_drawn);
    input_guard_drawn(f.app, f.ui, 1);
    key_input(f.app, f.ui, '\r', 0);
    assert(!f.app->quit && f.ui->hotkey_guard);
    for (unsigned i = 0; i < 40; ++i) {
        ++f.all->input_batch; fixture_clock += 25;
        key_input(f.app, f.ui, '\r', 0);
        assert(!f.app->quit && f.ui->hotkey_guard);
    }
    settled(&f); key_input(f.app, f.ui, '\r', 0);
    assert(f.app->quit); /* a deliberate later activation remains usable */
    dispose_hotkeys(&f);
}

static void pasted_chat_and_confirmation_suffixes_wait(void)
{
    HotkeyFixture f = fresh_hotkeys();
    feed_key(&f, "\033[200~private draft\033[201~\r");
    assert(!f.app->chat_count && !f.app->control_count);
    assert_text(draft(f.app, f.ui), "private draft");
    submit(f.app, f.ui); /* direct activation cannot bypass the same gate */
    assert(!f.app->chat_count);
    settled(&f); key_input(f.app, f.ui, '\r', 0);
    assert(f.app->chat_count == 1 && !draft(f.app, f.ui)->len);
    dispose_hotkeys(&f);

    f = fresh_hotkeys();
    f.ui->confirming = 1;
    f.ui->confirm_at = fixture_clock - 1000;
    feed_key(&f, "\033[200~ignored\033[201~\r");
    assert(f.ui->confirming == 1);
    settled(&f); key_input(f.app, f.ui, '\r', 0);
    assert(!f.ui->confirming);
    net_close(f.app->net); f.app->net = NULL; f.app->registered = 0;
    f.ui->quit_confirm = 1;
    f.ui->quit_confirm_at = fixture_clock - 1000;
    feed_key(&f, "\033[200~ignored\033[201~y");
    assert(!f.app->quit && f.ui->quit_confirm);
    settled(&f); key_input(f.app, f.ui, 'y', 0);
    assert(f.app->quit);
    dispose_hotkeys(&f);
}

static void every_modal_paste_consumer_uses_the_gate(void)
{
    HotkeyFixture f = fresh_hotkeys();
    open_settings(f.app, f.ui);
    assert(settings_open_server(f.ui->settings, f.client, 0, 1));
    key_input(f.app, f.ui, '\r', 0);
    assert(settings_mouse_state(f.ui->settings) & 2u);
    feed_key(&f, "\033[200~reviewnick\033[201~\r");
    assert(settings_mouse_state(f.ui->settings) & 2u);
    assert(!strcmp(f.app->cfg.nick, "tester") && access(f.path, F_OK) < 0);
    settled(&f); key_input(f.app, f.ui, '\r', 0);
    assert(!(settings_mouse_state(f.ui->settings) & 2u));
    return_to_chat(f.app, f.ui);

    transfers_open(f.all->transfers, 0);
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    key_input(f.app, f.ui, KEY_RIGHT, 1);
    key_input(f.app, f.ui, '\r', 0);
    key_input(f.app, f.ui, '\r', 0);
    assert(transfers_mouse_state(f.all->transfers) & 8u);
    feed_key(&f, "\033[200~Peer\033[201~\r");
    assert(transfers_mouse_state(f.all->transfers) & 8u);
    int rows = LINES, columns = COLS;
    LINES = 10;
    input_guard_drawn(f.app, f.ui, 1);
    assert(!f.ui->hotkey_guard_drawn);
    LINES = rows;
    settled(&f); key_input(f.app, f.ui, '\r', 0);
    assert(!(transfers_mouse_state(f.all->transfers) & 8u));
    transfers_close(f.all->transfers);
    assert(!dcc_count(f.all->dcc) && !f.app->chat_count && !f.app->control_count);

    open_bindings(f.app, f.ui, 0);
    settled(&f); key_input(f.app, f.ui, 'a', 0);
    assert(f.ui->bindings.stage == 1);
    feed_key(&f, "\033[200~ignored\033[201~F7\r");
    assert(f.ui->bindings.stage == 1 && !f.ui->bindings.key_length);
    settled(&f); type_name(&f, "F7");
    key_input(f.app, f.ui, '\r', 0);
    assert(f.ui->bindings.stage == 2);
    close_bindings(f.app, f.ui);

    assert(open_summary(f.app, f.ui));
    feed_key(&f, "\033[200~ignored\033[201~\033");
    flush_escape(f.app, f.ui, 1);
    assert(f.ui->summary.active && !f.ui->summary.hidden);
    settled(&f); key_input(f.app, f.ui, 27, 0);
    assert(!f.ui->summary.active || f.ui->summary.hidden);

    assert(open_guided(f.app, f.ui, 1));
    feed_key(&f, "\033[200~ignored\033[201~");
    unsigned before = guided_mouse_state(f.all->guided);
    key_input(f.app, f.ui, '\r', 0);
    assert(guided_active(f.all->guided));
    assert(guided_mouse_state(f.all->guided) == before);
    LINES = 13; COLS = 31;
    input_guard_drawn(f.app, f.ui, 1);
    assert(!f.ui->hotkey_guard_drawn);
    LINES = rows; COLS = columns;
    guided_close(f.all->guided);
    assert(!f.app->chat_count && !f.app->control_count && access(f.path, F_OK) < 0);
    dispose_hotkeys(&f);
}

static void terminal_replacement_invalidates_review(void)
{
    HotkeyFixture f = fresh_hotkeys();
    assert(open_command(f.app, f.ui, "/"));
    feed_key(&f, "\033[200~quit");
    assert(prompt_pasting(f.ui->command));
    release_terminal(f.app, f.ui); /* no native terminal exists in this fixture */
    assert(!prompt_pasting(f.ui->command));
    assert(f.ui->hotkey_guard && !f.ui->hotkey_guard_drawn);
    net_close(f.app->net); f.app->net = NULL; f.app->registered = 0;
    fixture_clock += 1000; ++f.all->input_batch;
    key_input(f.app, f.ui, '\r', 0);
    assert(!f.app->quit);
    settled(&f); key_input(f.app, f.ui, '\r', 0);
    assert(f.app->quit);
    dispose_hotkeys(&f);
}

static void deferred_setup_keeps_discarded_input_owned(void)
{
    HotkeyFixture f = fresh_hotkeys();
    assert(open_guided(f.app, f.ui, 1));
    Guided *guided = f.all->guided;
    /* Model a successfully completed async job; actual timer, input owner,
     * result delivery and destination-switch paths remain production code. */
    guided->deferred = 1;
    snprintf(guided->deferred_message, sizeof guided->deferred_message, "fixture setup completed");
    guided->settle_at = fixture_clock + GUIDED_ENTER_DELAY_MS;
    feed_key(&f, "\033[200~ignored\033[201~");
    for (unsigned i = 0; i < 40; ++i) {
        ++f.all->input_batch; fixture_clock += 25;
        key_input(f.app, f.ui, '\r', 0);
        assert(!guided_tick(guided, fixture_clock));
        assert(guided_active(guided) && guided->deferred);
    }
    fixture_clock += GUIDED_ENTER_DELAY_MS + 1;
    assert(guided_tick(guided, fixture_clock));
    guided_changed(f.app, f.ui);
    assert(!guided_active(guided) && f.ui->hotkey_guard && !f.ui->hotkey_guard_drawn);
    key_input(f.app, f.ui, '/', 0);
    assert(!prompt_active(f.ui->command));
    settled(&f); key_input(f.app, f.ui, '/', 0);
    assert(prompt_active(f.ui->command));
    dispose_hotkeys(&f);
}

static void setup_switch_carries_the_review_gate(void)
{
    HotkeyFixture f = fresh_hotkeys();
    Config cfg; char error[512]; size_t added;
    config_defaults(&cfg); cfg.autoconnect = cfg.mouse = 0;
    strcpy(cfg.server, "second.invalid"); strcpy(cfg.nick, "tester"); strcpy(cfg.username, "tester");
    assert(client_add(f.client, &cfg, &added, error, sizeof error) > 0);
    App *second = client_get_app(f.client, added, error, sizeof error);
    assert(second); config_wipe(&cfg, sizeof cfg);
    assert(open_guided(f.app, f.ui, 1));
    f.all->guided->deferred = 1;
    f.all->guided->settle_at = fixture_clock;
    assert(guided_tick(f.all->guided, fixture_clock));
    /* A completed shared setup may have selected a different saved server. */
    f.client->current = added;
    guided_changed(f.app, f.ui);
    assert(f.all->switch_to == added && f.ui->hotkey_guard);
    assert(apply_session_actions(f.all, &f.app, &f.ui) == 1);
    assert(f.app == second && f.ui->hotkey_guard && !f.ui->hotkey_guard_drawn);
    key_input(f.app, f.ui, '/', 0);
    assert(!prompt_active(f.ui->command));
    settled(&f); key_input(f.app, f.ui, '/', 0);
    assert(prompt_active(f.ui->command));
    dispose_hotkeys(&f);
}

int main(void)
{
#ifndef UI_HOTKEY_STATE_ONLY
    FILE *out = tmpfile(), *in = tmpfile();
    assert(out && in);
    SCREEN *screen = newterm("xterm-256color", out, in);
    assert(screen && keypad(stdscr, TRUE) != ERR);
    vipirc_focus_colors();
#endif
    terminal_utf8 = 0;
    pasted_commands_need_fresh_activation();
    pasted_chat_and_confirmation_suffixes_wait();
    every_modal_paste_consumer_uses_the_gate();
    terminal_replacement_invalidates_review();
    deferred_setup_keeps_discarded_input_owned();
    setup_switch_carries_the_review_gate();
    table_apply_save_cancel();
    decoder_and_paste_boundaries();
    preserved_command_and_target();
    modal_and_action_burst_guards();
    held_enter_cannot_run_restored_command();
    command_context_help_has_return_guard();
    category_labels_follow_binding_scope();
    all_actions_preserve_unrelated_chat();
#ifndef UI_HOTKEY_STATE_ONLY
    endwin(); delscreen(screen);
    fclose(out); fclose(in);
    puts("full UI hotkeys: table, primary save, empty defaults, decoded keys, paste/modal guards and draft target preservation passed");
#else
    puts("full UI hotkey state fixture: table/config/dispatch/draft/paste/burst guards passed; curses rendering and ABI untested");
#endif
    return 0;
}
