/* Real minimalist input decoder and action dispatch, without a public
 * network connection, controlling terminal, or background endpoint. */
#include "idle.c"
#include <assert.h>

typedef struct Fixture {
    Idle *idle;
    char directory[128], path[256];
} Fixture;

static Fixture fixture(void)
{
    Fixture f = {0};
    char error[512];
    Config cfg;
    strcpy(f.directory, "/tmp/irc-hotkeys-XXXXXX");
    assert(mkdtemp(f.directory));
    snprintf(f.path, sizeof f.path, "%s/config.ini", f.directory);
    f.idle = calloc(1, sizeof *f.idle);
    assert(f.idle);
    f.idle->client = calloc(1, sizeof *f.idle->client);
    assert(f.idle->client);
    config_minimal_defaults(&cfg);
    strcpy(cfg.server, "fixture.invalid");
    strcpy(cfg.nick, "tester"); strcpy(cfg.username, "tester");
    assert(client_init(f.idle->client, &cfg, f.path, "tester", 1, 0, error, sizeof error) == 1);
    assert(client_get_app(f.idle->client, 0, error, sizeof error));
    f.idle->program = "onekeyirc";
    f.idle->commands = (IdleCommands){f.idle->client, "onekeyirc", command_print, f.idle, 0, 1, NULL};
    f.idle->tty.rows = 24; f.idle->tty.cols = 100; f.idle->tty.utf8 = 1;
    f.idle->catchup_fd = -1;
    f.idle->input_batch = 1;
    ed_init(&f.idle->editor);
    config_wipe(&cfg, sizeof cfg);
    return f;
}

static void cleanup(Fixture *f)
{
    Idle *idle = f->idle;
    if (idle->setup) idle_setup_free(idle->setup);
    response_reset(idle);
    while (idle->count) stream_pop(idle);
    for (size_t i = 0; i < idle->history_count; ++i) {
        config_wipe(idle->history[i].text, strlen(idle->history[i].text));
        free(idle->history[i].text);
    }
    dcc_free(idle->dcc);
    client_free(idle->client); free(idle->client);
    config_wipe(idle, sizeof *idle); free(idle);
    assert(rmdir(f->directory) == 0);
}

static void bind_key(Idle *idle, const char *key, int action)
{
    char error[256];
    assert(config_hotkey_set(&idle->client->slots[0].app->cfg,
                             config_hotkey_key(key), action, error, sizeof error) == 1);
}

static void feed(Idle *idle, const char *bytes)
{
    ++idle->input_batch;
    for (size_t i = 0; bytes[i]; ++i) input_byte(idle, (unsigned char)bytes[i]);
}

static void assert_draft(const Idle *idle, const char *expected)
{
    char text[ED_MAX_BYTES + 1];
    assert(ed_to_utf8(&idle->editor, text, sizeof text) != SIZE_MAX);
    assert(!strcmp(text, expected));
    config_wipe(text, sizeof text);
}

static void no_defaults_and_terminal_decoding(void)
{
    static const char *const functions[] = {
        "\033OP", "\033OQ", "\033OR", "\033OS", "\033[15~", "\033[17~",
        "\033[18~", "\033[19~", "\033[20~", "\033[21~", "\033[23~", "\033[24~"
    };
    Fixture f = fixture(); Idle *idle = f.idle;
    assert(!idle->client->slots[0].app->cfg.hotkey_count);
    for (size_t i = 0; i < sizeof functions / sizeof functions[0]; ++i) feed(idle, functions[i]);
    feed(idle, "\033a\0339\004\014");
    assert(idle->mode == WATCH && !idle->quit && !idle->background_requested && !idle->editor.len);
    feed(idle, "\r");
    assert(idle->mode == COMPOSE); /* Existing Enter remains intact. */
    for (size_t i = 0; i < sizeof functions / sizeof functions[0]; ++i) {
        char key[16]; snprintf(key, sizeof key, "F%zu", i + 1);
        bind_key(idle, key, CONFIG_ACTION_COMPOSE);
        idle->mode = WATCH; reset_input(idle);
        /* Every sequence may arrive one byte per terminal read. */
        for (size_t j = 0; functions[i][j]; ++j) {
            char byte[] = {functions[i][j], 0}; feed(idle, byte);
        }
        assert(idle->mode == COMPOSE && !idle->editor.len);
    }
    idle->mode = WATCH; reset_input(idle);
    feed(idle, "\033[[A"); assert(idle->mode == COMPOSE);
    idle->mode = WATCH; reset_input(idle);
    bind_key(idle, "Alt-a", CONFIG_ACTION_COMMAND);
    feed(idle, "\033"); assert(idle->mode == WATCH);
    feed(idle, "a"); assert(idle->mode == COMMAND); assert_draft(idle, "/");
    idle->mode = WATCH; reset_input(idle);
    bind_key(idle, "Alt-9", CONFIG_ACTION_COMPOSE);
    feed(idle, "\0339"); assert(idle->mode == COMPOSE);
    idle->mode = WATCH; reset_input(idle);
    feed(idle, "\033[99~\033[1;9P\033A");
    assert(idle->mode == WATCH && !idle->editor.len);
    bind_key(idle, "Ctrl-D", CONFIG_ACTION_COMPOSE);
    feed(idle, "\004"); assert(idle->mode == COMPOSE);
    bind_key(idle, "Up", CONFIG_ACTION_COMMAND);
    feed(idle, "\033[A"); assert(idle->mode == COMMAND);
    bind_key(idle, "Tab", CONFIG_ACTION_COMPOSE);
    feed(idle, "\t"); assert(idle->mode == COMPOSE);
    cleanup(&f);
}

static void readers_preserve_editors(void)
{
    Fixture f = fixture(); Idle *idle = f.idle;
    bind_key(idle, "F7", CONFIG_ACTION_SUMMARY);
    for (int command = 0; command < 2; ++command) {
        begin_editor(idle, command ? COMMAND : COMPOSE);
        assert(ed_from_utf8(&idle->editor, command ? "/msg person unfinished text" : "unfinished chat text"));
        idle->editor.pos = 4;
        Editor before = idle->editor;
        uint64_t token = idle->compose_token;
        BufferKind kind = idle->compose_kind;
        char target[sizeof idle->compose_target]; strcpy(target, idle->compose_target);
        feed(idle, "\033[18~q\033[18~");
        assert(idle->mode == READING && idle->response_summary && idle->response_keeps_draft);
        assert(!memcmp(&idle->editor, &before, sizeof before));
        feed(idle, "/");
        assert(idle->mode == READING && !memcmp(&idle->editor, &before, sizeof before));
        feed(idle, "r");
        assert(idle->mode == READING && !memcmp(&idle->editor, &before, sizeof before));
        feed(idle, "q");
        assert(idle->mode == (command ? COMMAND : COMPOSE));
        assert(!memcmp(&idle->editor, &before, sizeof before));
        assert(idle->compose_token == token && idle->compose_kind == kind && !strcmp(idle->compose_target, target));
    }
    cleanup(&f);
}

static void actions_do_not_retarget_drafts(void)
{
    Fixture f = fixture(); Idle *idle = f.idle;
    begin_editor(idle, COMPOSE);
    assert(ed_from_utf8(&idle->editor, "keep destination"));
    size_t view = current_app(idle)->current;
    static const int actions[] = {CONFIG_ACTION_COMMAND, CONFIG_ACTION_NEXT,
        CONFIG_ACTION_PREV, CONFIG_ACTION_MENTIONS, CONFIG_ACTION_SETTINGS, CONFIG_ACTION_QUICKSTART};
    for (size_t i = 0; i < sizeof actions / sizeof actions[0]; ++i) {
        bind_key(idle, "F7", actions[i]); feed(idle, "\033[18~");
        assert(idle->mode == COMPOSE && current_app(idle)->current == view && !idle->setup);
        assert_draft(idle, "keep destination");
    }
    bind_key(idle, "F7", CONFIG_ACTION_CLEARINPUT); feed(idle, "\033[18~");
    assert(idle->mode == COMPOSE && !idle->editor.len);
    bind_key(idle, "F7", CONFIG_ACTION_COMMAND); feed(idle, "\033[18~");
    assert(idle->mode == COMMAND); assert_draft(idle, "/");
    bind_key(idle, "F7", CONFIG_ACTION_COMPOSE); feed(idle, "\033[18~");
    assert(idle->mode == COMPOSE && !idle->editor.len);
    bind_key(idle, "F7", CONFIG_ACTION_SETTINGS); feed(idle, "\033[18~");
    assert(idle->mode == SETUP && idle->setup);
    cleanup(&f);
}

static void paste_and_modal_boundaries(void)
{
    Fixture f = fixture(); Idle *idle = f.idle;
    bind_key(idle, "F7", CONFIG_ACTION_QUIT);
    bind_key(idle, "Ctrl-D", CONFIG_ACTION_BACKGROUND);
    feed(idle, "\033[200~retained\033[18~\004\033[201~\033[18~\004");
    assert(idle->mode == COMPOSE && !idle->paste && idle->paste_completed);
    assert(!idle->quit && !idle->background_requested && idle->editor.len);
    Editor pasted = idle->editor;
    feed(idle, "\033[18~\004");
    assert(!idle->quit && !idle->background_requested && !memcmp(&idle->editor, &pasted, sizeof pasted));
    idle->paste_drawn = 1;
    idle->limit_pending = 1;
    feed(idle, "\033[18~\004");
    assert(!idle->quit && !idle->background_requested && !memcmp(&idle->editor, &pasted, sizeof pasted));
    idle->limit_pending = 0;
    static const IdleMode modes[] = {SETUP, REVIEW, READING, CONTEXT_HELP, SCROLLBACK, CATCHUP};
    for (size_t i = 0; i < sizeof modes / sizeof modes[0]; ++i) {
        idle->mode = modes[i]; feed(idle, "\033[18~\004");
        assert(idle->mode == modes[i] && !idle->quit && !idle->background_requested);
        assert(!memcmp(&idle->editor, &pasted, sizeof pasted));
    }
    idle->mode = COMPOSE;
    /* A visible paste does not authorize a shortcut in the paste's read. */
    idle->paste_batch = idle->input_batch;
    input_byte(idle, 4); assert(!idle->background_requested);
    idle->paste_review_at = now_ms() - REVIEW_DELAY_MS;
    feed(idle, "\004"); assert(idle->background_requested && !idle->quit);
    assert(!memcmp(&idle->editor, &pasted, sizeof pasted));
    idle->background_requested = 0;
    feed(idle, "\033[18~"); assert(idle->quit);
    cleanup(&f);
}

static void a_shortcut_cannot_execute_buffered_enter(void)
{
    Fixture f = fixture(); Idle *idle = f.idle;
    begin_editor(idle, COMMAND);
    assert(ed_from_utf8(&idle->editor, "/quit"));
    bind_key(idle, "F7", CONFIG_ACTION_CANCEL);
    feed(idle, "\033[18~\r");
    assert(idle->mode == COMMAND && !idle->quit);
    assert_draft(idle, "/quit");
    bind_key(idle, "F7", CONFIG_ACTION_BACKGROUND);
    feed(idle, "\033[18~\r");
    assert(idle->mode == COMMAND && !idle->quit && idle->background_requested);
    assert_draft(idle, "/quit");
    cleanup(&f);
}

static void returning_to_a_draft_requires_a_fresh_enter(void)
{
    Fixture f = fixture(); Idle *idle = f.idle;
    begin_editor(idle, COMMAND);
    assert(ed_from_utf8(&idle->editor, "/quit"));
    bind_key(idle, "F7", CONFIG_ACTION_SUMMARY);
    feed(idle, "\033[18~");
    assert(idle->mode == READING && idle->response_keeps_draft);
    feed(idle, "q\r");
    assert(idle->mode == COMMAND && !idle->quit && idle->hotkey_return_guard);
    assert_draft(idle, "/quit");
    feed(idle, "\r"); /* A fresh read is still insufficient before display. */
    assert(!idle->quit);
    idle->hotkey_return_drawn = 1;
    idle->hotkey_return_at = now_ms();
    feed(idle, "\r"); /* Repeating Enter cannot approve immediately on draw. */
    assert(!idle->quit);
    idle->hotkey_return_at = now_ms() - REVIEW_DELAY_MS;
    feed(idle, "\r");
    assert(idle->quit);
    cleanup(&f);
}

static void a_paste_suffix_cannot_cancel_and_execute(void)
{
    Fixture f = fixture(); Idle *idle = f.idle;
    begin_editor(idle, COMMAND);
    feed(idle, "\033[200~help\033[201~\033\033/quit\r");
    assert(!idle->quit && idle->mode == COMMAND);
    assert_draft(idle, "/help");
    cleanup(&f);
}

static void a_paste_needs_a_complete_visible_editor(void)
{
    Fixture f = fixture(); Idle *idle = f.idle;
    begin_editor(idle, COMMAND);
    feed(idle, "\033[200~quit\033[201~");
    assert_draft(idle, "/quit");
    idle->tty.rows = 4; idle->tty.cols = 10;
    draw_editor(idle);
    assert(!idle->editor_frame && idle->output_length);
    idle->output_start = idle->output_length = 0; /* model completed drain */
    paste_frame_drawn(idle);
    assert(!idle->paste_drawn);
    idle->paste_review_at = now_ms() - REVIEW_DELAY_MS;
    feed(idle, "\r");
    assert(!idle->quit && idle->mode == COMMAND);
    idle->tty.rows = 24; idle->tty.cols = 100; idle->redraw = 1;
    draw_editor(idle);
    assert(idle->editor_frame && idle->output_length);
    paste_frame_drawn(idle); /* pending/failed output is not display credit */
    assert(!idle->paste_drawn);
    idle->output_start = idle->output_length = 0;
    paste_frame_drawn(idle);
    assert(idle->paste_drawn);
    feed(idle, "\r");
    assert(!idle->quit); /* a visible frame still needs a quiet interval */
    idle->paste_review_at = now_ms() - REVIEW_DELAY_MS;
    feed(idle, "\r");
    assert(idle->quit);
    cleanup(&f);
}

static void deferred_setup_keeps_the_paste_boundary(void)
{
    for (int success = 0; success < 2; ++success) {
        Fixture f = fixture(); Idle *idle = f.idle;
        assert(begin_setup(idle, 1));
        idle->setup_result = success ? 1 : -1;
        snprintf(idle->setup_message, sizeof idle->setup_message, "fixture setup %s", success ? "completed" : "failed");
        feed(idle, "\033[200~ignored\033[201~");
        /* A helper result may settle after 100 ms while paste review still
         * requires 250 ms. No network event or real helper is needed here. */
        idle->setup_settle_at = now_ms() - 1;
        idle->paste_review_at = now_ms() - 125;
        poll_setup(idle);
        assert(idle->mode == (success ? WATCH : SETUP));
        assert(idle->paste_completed && !idle->paste_drawn);
        feed(idle, "/quit\r");
        assert(!idle->quit && idle->mode == (success ? WATCH : SETUP));
        paste_frame_drawn(idle);
        assert(!idle->paste_drawn);
        if (success) {
            assert(idle->notice_pending);
            stream_output(idle);
            assert(!idle->notice_pending && idle->output_length);
        } else {
            draw_setup(idle);
            assert(idle->setup_frame && idle->output_length);
        }
        paste_frame_drawn(idle);
        assert(!idle->paste_drawn); /* the result is still queued */
        idle->output_start = idle->output_length = 0; /* model completed output */
        paste_frame_drawn(idle);
        assert(idle->paste_drawn);
        idle->paste_review_at = now_ms() - REVIEW_DELAY_MS;
        if (success) {
            feed(idle, "/quit\r");
            assert(idle->quit); /* disconnected WATCH needs no inbound traffic */
        } else {
            feed(idle, "reviewed");
            assert_draft(idle, "reviewed");
        }
        cleanup(&f);
    }
}

int main(void)
{
    a_paste_suffix_cannot_cancel_and_execute();
    a_paste_needs_a_complete_visible_editor();
    deferred_setup_keeps_the_paste_boundary();
    no_defaults_and_terminal_decoding();
    readers_preserve_editors();
    actions_do_not_retarget_drafts();
    paste_and_modal_boundaries();
    a_shortcut_cannot_execute_buffered_enter();
    returning_to_a_draft_requires_a_fresh_enter();
    puts("idle hotkeys: empty defaults, fragmented F/Alt, retained editors, target guards, paste/modal/transition boundaries passed");
    return 0;
}
