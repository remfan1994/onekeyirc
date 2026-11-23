/* Allocation-failure regressions for real App state transitions.
 * Only app.c is compiled with app_test_* allocator substitutions.
 * The existing transport fixture is reused; no network socket is opened. */
#define main app_existing_regressions
#include "test_app.c"
#undef main

#include <errno.h>
#include <time.h>

struct Allocation { void *pointer; size_t size; };
static struct Allocation allocations[8192];
static size_t live_allocations, live_bytes, allocation_calls, fail_at, failures_hit, checked_cases;

static int fail_now(void)
{
    ++allocation_calls;
    if (fail_at && allocation_calls == fail_at) { ++failures_hit; errno = ENOMEM; return 1; }
    return 0;
}
static size_t allocation_index(const void *pointer)
{
    for (size_t i = 0; i < live_allocations; ++i)
        if (allocations[i].pointer == pointer) return i;
    assert(!"free/realloc of an untracked App allocation");
    return 0;
}
static void track(void *pointer, size_t size)
{
    if (!pointer) return;
    assert(live_allocations < sizeof allocations / sizeof allocations[0]);
    allocations[live_allocations++] = (struct Allocation){pointer, size};
    live_bytes += size;
}
void *app_test_malloc(size_t size)
{
    if (fail_now()) return NULL;
    void *pointer = malloc(size); track(pointer, size); return pointer;
}
void *app_test_calloc(size_t count, size_t size)
{
    assert(!count || size <= SIZE_MAX / count);
    if (fail_now()) return NULL;
    void *pointer = calloc(count, size); track(pointer, count * size); return pointer;
}
void *app_test_realloc(void *old, size_t size)
{
    assert(size);
    if (fail_now()) return NULL;
    int had_old = old != NULL;
    size_t index = had_old ? allocation_index(old) : 0;
    void *pointer = realloc(old, size);
    if (!pointer) return NULL;
    if (!had_old) track(pointer, size);
    else {
        live_bytes -= allocations[index].size;
        allocations[index] = (struct Allocation){pointer, size};
        live_bytes += size;
    }
    return pointer;
}
void app_test_free(void *pointer)
{
    if (!pointer) return;
    size_t index = allocation_index(pointer);
    live_bytes -= allocations[index].size;
    allocations[index] = allocations[--live_allocations];
    free(pointer);
}
static void inject(size_t nth) { allocation_calls = 0; fail_at = nth; }
static void restored(void) { fail_at = 0; }
static void check_history(const App *app)
{
    size_t bytes = 0;
    assert(app->buffer_count >= 3 && app->buffer_count <= APP_BUFFERS);
    assert(app->current < app->buffer_count);
    for (size_t i = 0; i < app->buffer_count; ++i) {
        const Buffer *b = &app->buffers[i];
        assert(b->lines && b->line_cap && b->line_count <= b->line_cap);
        assert(b->line_start < b->line_cap && b->user_count <= b->user_cap && b->user_count <= APP_USERS);
        for (size_t j = 0; j < b->line_count; ++j) {
            const char *line = b->lines[(b->line_start + j) % b->line_cap];
            assert(line); bytes += strlen(line) + 1;
        }
    }
    assert(bytes == app->log_bytes && bytes <= APP_LOG_BYTES);
}
static void empty_allocations(void) { assert(!live_allocations && !live_bytes); }

static void initialization_failures(void)
{
    Config config; memset(&config, 0, sizeof config);
    config.history_lines = 50;
    for (size_t nth = 1; nth <= 3; ++nth) {
        App app;
        inject(nth);
        assert(!app_init(&app, &config, "/unused/config"));
        restored(); empty_allocations();
        app_free(&app); empty_allocations();
        ++checked_cases;
    }
}

static void resize_failures(void)
{
    for (size_t nth = 1; nth <= 10; ++nth) {
        App *app = fresh(0);
        for (size_t i = 0; i < 7; ++i) {
            char name[32]; snprintf(name, sizeof name, "#fault%zu", i);
            size_t view = app_buffer(app, name, B_CHANNEL, 1);
            assert(view != SIZE_MAX);
            for (size_t j = 0; j < 12; ++j) app_log(app, view, "history %zu/%zu", i, j);
        }
        Config desired = app->cfg;
        desired.history_lines = 3000; /* All ten ring capacities change. */
        App before = *app;
        size_t old_count = live_allocations, old_bytes = live_bytes;
        char error[256];
        int saves = config_saves;
        inject(nth);
        assert(!app_config_apply(app, &desired, 1, error, sizeof error));
        restored();
        assert(strstr(error, "Cannot resize"));
        assert(!memcmp(app, &before, sizeof before));
        assert(config_saves == saves && live_allocations == old_count && live_bytes == old_bytes);
        check_history(app);
        assert(app_config_apply(app, &desired, 0, error, sizeof error));
        check_history(app);
        destroy(app); empty_allocations(); ++checked_cases;
    }
}

static void complete_queue_failure(void)
{
    App *app = fresh(0);
    register_plain(app);
    SendPlan existing, plan;
    char text[4097]; memset(text, 'x', sizeof text - 1); text[sizeof text - 1] = 0;
    assert(app_plan(app, "#test", "existing message", 0, &existing));
    assert(app_send_plan(app, &existing));
    assert(app_plan(app, "#test", text, 0, &plan) && plan.count > 3);
    for (size_t nth = 1; nth <= plan.count; ++nth) {
        size_t old_count = live_allocations, old_bytes = live_bytes, sent = app->net->output_len;
        OutLine *head = app->chat_head, *tail = app->chat_tail;
        inject(nth);
        assert(!app_send_plan(app, &plan));
        restored();
        assert(app->chat_count == 1 && app->chat_head == head && app->chat_tail == tail);
        assert(app->net->output_len == sent);
        assert(live_allocations == old_count && live_bytes == old_bytes);
        check_history(app); ++checked_cases;
    }
    assert(app_send_plan(app, &plan));
    assert(app->chat_count == 1 + plan.count);
    destroy(app); empty_allocations();
}

static void views_rosters_and_history(void)
{
    App *app = fresh(0); register_plain(app);
    size_t before = app->buffer_count;
    inject(1);
    assert(app_buffer(app, "#recover", B_CHANNEL, 1) == SIZE_MAX);
    restored();
    assert(app->buffer_count == before);
    size_t view = app_buffer(app, "#recover", B_CHANNEL, 1);
    assert(view == before);
    for (size_t i = 0; i < 100; ++i) {
        inject(1); app_log(app, view, "drop one line safely %zu", i); restored();
        check_history(app);
        app_log(app, view, "keep next line %zu", i);
        check_history(app); ++checked_cases;
    }
    inject(1);
    feed(app, ":tester!u@localhost JOIN :#recover\r\n");
    restored(); check_history(app);
    for (size_t i = 0; i < 160; ++i) {
        char line[128]; snprintf(line, sizeof line, ":member%zu!u@host JOIN :#recover\r\n", i);
        inject(1); feed(app, line); restored(); check_history(app);
        feed(app, line); check_history(app); ++checked_cases;
    }
    assert(app->buffers[view].user_count > 0);
    assert(app_close_view(app, view)); check_history(app);
    destroy(app); empty_allocations();
}

int main(void)
{
    for (size_t cycle = 0; cycle < 40; ++cycle) {
        initialization_failures(); resize_failures(); complete_queue_failure();
        views_rosters_and_history();
    }
    assert(failures_hit > 10000);
    printf("App allocation faults: %zu checked cases, %zu injected ENOMEM failures; "
           "history/accounting invariants, save-before-mutation, complete-send rollback, "
           "roster/view recovery and exact allocation balance passed.\n", checked_cases, failures_hit);
    return 0;
}
