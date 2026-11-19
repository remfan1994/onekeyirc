/* Minimalist output ownership and recovery under real short writes. The
 * private file descriptor is presentation only; no IRC connection is made. */
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif
#include <unistd.h>
static ssize_t recovery_write(int, const void *, size_t);
#define write recovery_write
#include "idle.c"
#undef write
#include <assert.h>

static ssize_t write_budget = -1;
static int write_error;

static ssize_t recovery_write(int fd, const void *bytes, size_t length)
{
    if (fd == STDOUT_FILENO) {
        if (write_error) { errno = write_error; return -1; }
        if (!write_budget) { errno = EAGAIN; return -1; }
        if (write_budget > 0 && length > (size_t)write_budget) length = (size_t)write_budget;
    }
    ssize_t count = write(fd, bytes, length);
    if (fd == STDOUT_FILENO && count > 0 && write_budget > 0) write_budget -= count;
    return count;
}

typedef struct Fixture {
    Idle *idle;
    int saved_stdout, output_fd;
    char directory[128];
} Fixture;

static Fixture fixture(void)
{
    Fixture f = {0};
    strcpy(f.directory, "/tmp/irc-recovery-XXXXXX");
    assert(mkdtemp(f.directory));
    char path[256], error[512];
    snprintf(path, sizeof path, "%s/output", f.directory);
    f.output_fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    assert(f.output_fd >= 0 && unlink(path) == 0);
    f.saved_stdout = dup(STDOUT_FILENO);
    assert(f.saved_stdout >= 0 && dup2(f.output_fd, STDOUT_FILENO) >= 0);
    f.idle = calloc(1, sizeof *f.idle);
    assert(f.idle);
    f.idle->client = calloc(1, sizeof *f.idle->client);
    assert(f.idle->client);
    Config config;
    config_minimal_defaults(&config);
    strcpy(config.server, "fixture.invalid");
    strcpy(config.nick, "tester"); strcpy(config.username, "tester");
    snprintf(path, sizeof path, "%s/config.ini", f.directory);
    assert(client_init(f.idle->client, &config, path, "tester", 1, 0, error, sizeof error) == 1);
    App *app = client_get_app(f.idle->client, 0, error, sizeof error);
    assert(app);
    app->registered = 1; /* A local plan needs no transport writes. */
    size_t query = app_buffer(app, "friend", B_QUERY, 1);
    assert(query != SIZE_MAX); app_switch(app, query);
    f.idle->program = "irc-recovery";
    f.idle->commands = (IdleCommands){f.idle->client, "irc-recovery", command_print, f.idle, 0, 1, NULL};
    f.idle->tty.rows = 24; f.idle->tty.cols = 80; f.idle->tty.utf8 = 1;
    f.idle->catchup_fd = -1;
    f.idle->input_batch = 1;
    f.idle->client->session_notice_shown = 1;
    write_budget = -1; write_error = 0;
    config_wipe(&config, sizeof config);
    return f;
}

static void cleanup(Fixture *f)
{
    assert(dup2(f->saved_stdout, STDOUT_FILENO) >= 0);
    assert(close(f->saved_stdout) == 0 && close(f->output_fd) == 0);
    while (f->idle->count) stream_pop(f->idle);
    response_reset(f->idle);
    client_free(f->idle->client); free(f->idle->client);
    config_wipe(f->idle, sizeof *f->idle); free(f->idle);
    assert(rmdir(f->directory) == 0);
}

static void edited_review_releases_pending_redraw(void)
{
    for (int command = 0; command < 2; ++command) {
        Fixture f = fixture(); Idle *idle = f.idle;
        begin_editor(idle, command ? COMMAND : COMPOSE);
        assert(ed_from_utf8(&idle->editor, command ? "/msg friend queued draft" : "queued draft"));
        draw_editor(idle);
        size_t queued = idle->output_length;
        assert(queued && idle->editor_frame);
        write_budget = 7;
        assert(output_flush(idle) && idle->output_length == queued - 7);
        assert(app_plan(current_app(idle), "friend", "queued draft", 0, &idle->pending));
        begin_review(idle, current_app(idle), command);
        draw_review(idle);
        assert(idle->mode == REVIEW && idle->pending_review_draw && !idle->review_drawn);
        ++idle->input_batch;
        key_event(idle, 'x');
        assert(idle->mode == (command ? COMMAND : COMPOSE));
        char text[ED_MAX_BYTES + 1];
        assert(ed_to_utf8(&idle->editor, text, sizeof text) != SIZE_MAX);
        assert(!strcmp(text, command ? "/msg friend queued draftx" : "queued draftx"));
        fprintf(stderr, "queued-review return: command=%d pending_review_draw=%d review_drawn=%d output_pending=%zu\n",
                command, idle->pending_review_draw, idle->review_drawn, idle->output_length);
        assert(!idle->pending_review_draw && !idle->review_drawn);
        assert(!idle->pending_token && !idle->pending.text[0]);
        /* Release the real descriptor's short-write limit. Only the editor
         * owns further output; no obsolete review can request a busy poll. */
        write_budget = -1;
        assert(output_flush(idle) && !idle->output_length);
        draw_editor(idle);
        assert(idle->editor_frame && !idle->redraw && idle->output_length);
        assert(output_flush(idle) && !idle->output_length && !idle->pending_review_draw);
        cleanup(&f);
    }
}

static void pending_bytes_survive_transient_and_fatal_write_errors(void)
{
    Fixture f = fixture(); Idle *idle = f.idle;
    char expected[OUTPUT_BYTES], actual[OUTPUT_BYTES];
    for (size_t i = 0; i < sizeof expected; ++i) expected[i] = (char)('a' + i % 26);
    assert(output_add(idle, expected, sizeof expected));
    write_budget = 31;
    assert(output_flush(idle));
    assert(idle->output_start == 31 && idle->output_length == sizeof expected - 31);
    write_error = EINTR;
    assert(output_flush(idle));
    write_error = EIO;
    assert(!output_flush(idle));
    assert(idle->output_start == 31 && idle->output_length == sizeof expected - 31);
    write_error = 0; write_budget = -1;
    while (idle->output_length) assert(output_flush(idle));
    assert(!idle->output_start);
    assert(lseek(f.output_fd, 0, SEEK_SET) == 0);
    size_t used = 0;
    while (used < sizeof actual) {
        ssize_t count = read(f.output_fd, actual + used, sizeof actual - used);
        assert(count > 0); used += (size_t)count;
    }
    assert(!memcmp(actual, expected, sizeof actual));
    cleanup(&f);
}

int main(void)
{
    edited_review_releases_pending_redraw();
    pending_bytes_survive_transient_and_fatal_write_errors();
    puts("idle recovery: queued-review edits retire their pending frame; short writes, EAGAIN, EINTR and EIO preserve exact remaining output");
    return 0;
}
