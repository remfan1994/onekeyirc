/* Explicit-detach boundary regression. No AF_UNIX endpoint or fork is used.
 * Build normally for a native state-preservation test. Define
 * IDLE_BOUNDARY_FIXTURE and link main.c for the real-PTY Python fixture. */
#define background_prepare test_background_prepare
#define background_detach test_background_detach
#include "idle.c"
#undef background_prepare
#undef background_detach
#include <assert.h>
#include <sys/stat.h>

static unsigned prepare_calls, detach_calls;
#ifndef IDLE_BOUNDARY_FIXTURE
static int prepare_succeeds;
#endif
static const char refusal[] = "Fixture refused background preparation.";

int test_background_prepare(Background *background, const Client *client,
                            char *error, size_t capacity)
{
    (void)background;
    ++prepare_calls;
#ifdef IDLE_BOUNDARY_FIXTURE
    const char *path = getenv("VIPIRC_TEST_PREPARE_MARKER");
    size_t history = 0;
    int retained = 0;
    if (client && client->current < CLIENT_MAX_SERVERS) {
        const App *app = client->slots[client->current].app;
        if (app && app->current < app->buffer_count) {
            const Buffer *buffer = &app->buffers[app->current];
            history = buffer->line_count;
            for (size_t i = 0; i < history; ++i)
                if (strstr(buffer->lines[(buffer->line_start + i) % buffer->line_cap],
                           "DETACH-HISTORY-MARKER")) retained = 1;
        }
    }
    if (path && *path) {
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd >= 0) {
            char record[96];
            int n = snprintf(record, sizeof record, "prepare=%u history=%zu marker=%d\n",
                             prepare_calls, history, retained);
            if (n > 0 && (size_t)n < sizeof record) {
                ssize_t written = write(fd, record, (size_t)n);
                assert(written == n);
            }
            (void)close(fd);
        }
    }
#else
    (void)client;
#endif
    (void)snprintf(error, capacity, "%s", refusal);
#ifdef IDLE_BOUNDARY_FIXTURE
    return 0;
#else
    return prepare_succeeds;
#endif
}

int test_background_detach(Background *background, const volatile sig_atomic_t *cancel,
                           char *error, size_t capacity)
{
    (void)background; (void)cancel; (void)error; (void)capacity;
    ++detach_calls;
    return 1;
}

#ifndef IDLE_BOUNDARY_FIXTURE
int main(void)
{
    Idle *idle = calloc(1, sizeof *idle);
    Client *client = calloc(1, sizeof *client);
    App *app = calloc(1, sizeof *app);
    assert(idle && client && app);
    idle->client = client;
    client->slots[0].used = 1;
    client->slots[0].app = app;
    app->buffer_count = 1;
    app->buffers[0].line_cap = app->buffers[0].line_count = 1;
    app->buffers[0].lines = calloc(1, sizeof(char *));
    assert(app->buffers[0].lines);
    app->buffers[0].lines[0] = strdup("canonical history must remain");
    assert(app->buffers[0].lines[0]);
    char *canonical = app->buffers[0].lines[0];
    ed_init(&idle->editor);
    assert(ed_from_utf8(&idle->editor, "retained unsent draft"));
    strcpy(idle->pending.text, "retained pending send");
    idle->pending_token = 77;
    idle->lines[0].text = strdup("retained stream record");
    assert(idle->lines[0].text);
    char *stream = idle->lines[0].text;
    idle->lines[0].bytes = strlen(stream) + 1;
    idle->lines[0].sequence = 40;
    idle->lines[0].status = 1;
    idle->count = 1;
    idle->bytes = idle->lines[0].bytes;
    idle->shown_sequence = 3;
    idle->next_sequence = 41;
    memset(idle->output, 'x', sizeof idle->output);
    idle->output_length = sizeof idle->output;
    idle->response = strdup("stale command response");
    assert(idle->response);
    idle->response_capacity = strlen(idle->response) + 1;
    idle->response_length = strlen(idle->response);
    idle->response_active = 1;
    idle->background_requested = 1;

    process_background_request(idle);
    assert(prepare_calls == 1 && !idle->background_requested);
    assert(idle->output_length == sizeof idle->output && idle->response && idle->response_active);
    assert(!detach_calls);
    assert(idle->notice_pending && !strcmp(idle->notice, refusal));
    assert(app->buffers[0].line_count == 1 && app->buffers[0].lines[0] == canonical);
    assert(!strcmp(canonical, "canonical history must remain"));
    assert(idle->count == 1 && idle->lines[0].text == stream);
    assert(idle->shown_sequence == 3 && idle->next_sequence == 41);
    char draft[128];
    assert(ed_to_utf8(&idle->editor, draft, sizeof draft) != SIZE_MAX);
    assert(!strcmp(draft, "retained unsent draft"));
    assert(!strcmp(idle->pending.text, "retained pending send") && idle->pending_token == 77);
    /* Model previously queued terminal bytes draining. The failure notice
     * must be the next output, ahead of additional retained chat. */
    idle->output_start = idle->output_length = 0;
    stream_output(idle);
    assert(idle->output_length > strlen(refusal));
    assert(!memcmp(idle->output, refusal, strlen(refusal)));
    assert(!idle->notice_pending);
    process_background_request(idle);
    assert(prepare_calls == 1);
    idle->background_requested = 1;
    idle->output_length = sizeof idle->output;
    prepare_succeeds = 1;
    process_background_request(idle);
    assert(prepare_calls == 2 && detach_calls == 1);
    assert(!idle->output_length && !idle->response && !idle->response_active);
    assert(app->buffers[0].line_count == 1 && app->buffers[0].lines[0] == canonical);
    assert(idle->count == 1 && idle->lines[0].text == stream);
    assert(ed_to_utf8(&idle->editor, draft, sizeof draft) != SIZE_MAX);
    assert(!strcmp(draft, "retained unsent draft"));
    assert(!strcmp(idle->pending.text, "retained pending send") && idle->pending_token == 77);

    free(canonical);
    free(app->buffers[0].lines);
    stream_pop(idle);
    free(app);
    free(client);
    config_wipe(idle, sizeof *idle);
    free(idle);
    puts("idle detach: blocked-output request reaches prepare; canonical history/drafts retained and failure prioritized");
    return 0;
}
#endif
