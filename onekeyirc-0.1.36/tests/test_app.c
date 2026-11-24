#define _POSIX_C_SOURCE 200809L
#include "app.h"

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Deterministic in-memory transport exercises the actual application state
 * machine without contacting a public IRC network. */
struct Net {
    unsigned char input[65536];
    size_t input_len, input_pos;
    char output[131072];
    size_t output_len;
    const void *retry_pointer;
    size_t retry_length;
    unsigned char retry_copy[512];
    int retry, block;
    int failed, retryable, eof;
    size_t write_limit;
};
static int64_t clock_ms = 100000;
static char test_password[CONFIG_SECRET_MAX + 1] = "secret";
static int secret_reads, config_saves, network_calls, offline_guard;
static int network_opens;
static int credential_starts, credential_cancels, credential_result;
struct Credential { int marker; };
static int save_failure, opened_port, opened_tls;
static char opened_host[256], opened_ca[CONFIG_PATH_MAX], secret_path[CONFIG_PATH_MAX];
static Config saved_config;
static const App *save_observer;
static App save_before;
static const void *watched_wipe;
static const char *watched_secret;
static int watched_wipe_calls;
static int watch_saved_config;
static int memory_contains(const void *data, size_t size, const char *needle)
{
    const unsigned char *bytes = data;
    size_t length = strlen(needle);
    if (!length || length > size) return 0;
    for (size_t i = 0; i <= size - length; ++i)
        if (!memcmp(bytes + i, needle, length)) return 1;
    return 0;
}
static void network_touch(void)
{
    assert(!offline_guard);
    ++network_calls;
}
const char *const vipirc_manual[] = { "vipirc manual", "Second line", NULL };

Net *net_open(const char *host, int port, int tls, const char *ca,
              char *error, size_t cap)
{
    network_touch();
    ++network_opens;
    snprintf(opened_host, sizeof(opened_host), "%s", host);
    snprintf(opened_ca, sizeof(opened_ca), "%s", ca ? ca : "");
    opened_port = port; opened_tls = tls;
    (void)error; (void)cap;
    return calloc(1, sizeof(Net));
}
int net_fd(const Net *n) { network_touch(); (void)n; return -1; }
short net_events(const Net *n, int write) { network_touch(); (void)n; return (short)(POLLIN | (write ? POLLOUT : 0)); }
NetState net_state(const Net *n) { network_touch(); return n->failed ? NET_FAILED : NET_READY; }
int net_step(Net *n, short events, int64_t now) { network_touch(); (void)events; (void)now; return n->failed ? -1 : 1; }
int net_retryable(const Net *n) { network_touch(); return n && n->retryable; }
ssize_t net_read(Net *n, void *out, size_t cap)
{
    network_touch();
    if (n->eof) return 0;
    size_t count = n->input_len - n->input_pos;
    if (!count) return -2;
    if (count > cap) count = cap;
    memcpy(out, n->input + n->input_pos, count);
    n->input_pos += count;
    if (n->input_pos == n->input_len) n->input_pos = n->input_len = 0;
    return (ssize_t)count;
}
ssize_t net_write(Net *n, const void *data, size_t size)
{
    network_touch();
    if (n->retry) {
        assert(data == n->retry_pointer && size == n->retry_length);
        assert(!memcmp(data, n->retry_copy, size));
    }
    if (n->block) {
        n->retry = 1;
        n->retry_pointer = data;
        n->retry_length = size;
        assert(size <= sizeof(n->retry_copy));
        memcpy(n->retry_copy, data, size);
        return -2;
    }
    n->retry = 0;
    if (n->write_limit && size > n->write_limit) size = n->write_limit;
    assert(n->output_len + size < sizeof(n->output));
    memcpy(n->output + n->output_len, data, size);
    n->output_len += size;
    n->output[n->output_len] = 0;
    return (ssize_t)size;
}
int net_pending(const Net *n) { network_touch(); return n->input_len != n->input_pos; }
const char *net_error(const Net *n) { network_touch(); (void)n; return "fake transport error"; }
int64_t net_now_ms(void) { network_touch(); return clock_ms; }
void net_close(Net *n) { network_touch(); free(n); }
int config_validate(const Config *c, char *error, size_t cap)
{
    if ((!c->tls && (strcmp(c->auth, "none") || c->server_password_file[0])) ||
        c->history_lines < 50 || c->history_lines > 5000 || c->word_count > CONFIG_WORD_MAX ||
        c->ignore_count > CONFIG_IGNORE_MAX ||
        (c->focus_style != CONFIG_FOCUS_REVERSE && c->focus_style != CONFIG_FOCUS_BRACKETS)) {
        if (error && cap) snprintf(error, cap, "fixture validation failed");
        return -1;
    }
    for (size_t i = 0; i < c->ignore_count; ++i)
        if (!irc_valid_ignore_mask(c->ignores[i])) {
            if (error && cap) snprintf(error, cap, "fixture ignore validation failed");
            return -1;
        }
    return 1;
}
int config_save(const Config *c, const char *path, char *error, size_t cap)
{
    assert(!offline_guard);
    if (save_observer) assert(!memcmp(save_observer, &save_before, sizeof(save_before)));
    ++config_saves;
    memcpy(&saved_config, c, sizeof(saved_config));
    if (watch_saved_config) watched_wipe = c;
    (void)path;
    if (save_failure) {
        if (error && cap) snprintf(error, cap, "fixture save failure");
        return -1;
    }
    return 1;
}
/* The production formatter/validator has separate config.c integration tests.
 * This fixture supplies only identities used by the application state tests. */
int config_bouncer_identity(const Config *c, char *out, size_t cap)
{
    if (!cap) return -1;
    out[0] = 0;
    if (c->bouncer_type == CONFIG_BOUNCER_NONE) return 0;
    if (!c->bouncer_user[0] || !c->bouncer_network[0]) return -1;
    int n = c->bouncer_type == CONFIG_BOUNCER_ZNC ?
        snprintf(out, cap, "%s%s%s/%s", c->bouncer_user,
                 c->bouncer_client[0] ? "@" : "", c->bouncer_client, c->bouncer_network) :
        snprintf(out, cap, "%s/%s%s%s", c->bouncer_user, c->bouncer_network,
                 c->bouncer_client[0] ? "@" : "", c->bouncer_client);
    if (n < 0 || (size_t)n >= cap) { out[0] = 0; return -1; }
    return 1;
}
/* Parent-path policy is exercised against config.c in test_config. This
 * fixture keeps the App tests focused on opened-file checks and logging. */
int config_open_log(const char *config_path, char *error, size_t error_cap)
{
    assert(!offline_guard);
    char path[CONFIG_PATH_MAX + 5];
    int length = snprintf(path, sizeof path, "%s.log", config_path);
    if (length < 0 || (size_t)length >= sizeof path) {
        if (error && error_cap) snprintf(error, error_cap, "fixture log path is too long");
        return -1;
    }
    return open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
}
int config_read_secret(const char *path, char *out, size_t cap, char *error, size_t error_cap)
{
    (void)error; (void)error_cap;
    assert(!offline_guard);
    ++secret_reads;
    snprintf(secret_path, sizeof(secret_path), "%s", path);
    assert(strlen(test_password) < cap);
    strcpy(out, test_password);
    return 1;
}
Credential *credential_start(const char *path, int64_t now, char *error, size_t cap)
{
    (void)now; (void)error; (void)cap;
    assert(!offline_guard);
    ++credential_starts;
    snprintf(secret_path, sizeof secret_path, "%s", path);
    return calloc(1, sizeof(Credential));
}
int credential_poll(Credential *job, int64_t now, char *out, size_t cap, char *error, size_t error_cap)
{
    (void)now; assert(job && !offline_guard);
    memset(out, 0, cap);
    if (credential_result > 0) {
        assert(strlen(test_password) < cap);
        strcpy(out, test_password);
    } else if (credential_result < 0)
        snprintf(error, error_cap, "GPG fixture failed; nothing sent.");
    return credential_result;
}
void credential_cancel(Credential *job)
{
    assert(!offline_guard);
    ++credential_cancels;
    free(job);
}
void credential_reap(void) { assert(!offline_guard); }
int credential_cleanup_pending(void) { assert(!offline_guard); return 0; }
void config_wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;
    if (p == watched_wipe) {
        assert(watched_secret && memory_contains(p, n, watched_secret));
        ++watched_wipe_calls;
    }
    while (n--) *v++ = 0;
}

static App *fresh(int sasl)
{
    Config c;
    App *a = calloc(1, sizeof(*a));
    memset(&c, 0, sizeof(c));
    strcpy(c.server, "localhost");
    c.port = sasl ? 6697 : 6667;
    c.tls = sasl;
    strcpy(c.nick, "tester");
    strcpy(c.username, "tester");
    strcpy(c.realname, "Test User");
    strcpy(c.channels, "#test");
    strcpy(c.auth, sasl ? "sasl" : "none");
    strcpy(c.auth_user, "user");
    strcpy(c.password_file, "/fake/password");
    c.history_lines = 50;
    c.user_list = 1;
    c.mouse = 1;
    c.hints = c.shortcut_labels = 1;
    c.queue_notices = 1;
    c.program_messages = 1;
    c.autocommands.enabled = 1;
    c.autocommands.delay_seconds = 15;
    c.autocommands.interval_seconds = 2;
    assert(a && app_init(a, &c, "/fake/config.ini"));
    assert(a->buffer_count == 3);
    assert(a->buffers[2].line_cap >= APP_HELP_LINES);
    assert(app_connect(a));
    app_network(a, 0);
    assert(a->net && strstr(a->net->output, "CAP LS 302\r\nNICK tester\r\nUSER tester 0 * :Test User\r\n"));
    return a;
}

static void destroy(App *a) { app_free(a); free(a); }
static void output_clear(App *a)
{
    assert(a->net);
    a->net->output_len = 0;
    a->net->output[0] = 0;
}
static void feed_bytes(App *a, const void *bytes, size_t length)
{
    Net *n = a->net;
    assert(n && n->input_len + length <= sizeof(n->input));
    memcpy(n->input + n->input_len, bytes, length);
    n->input_len += length;
    app_network(a, POLLIN);
    while (a->net && net_pending(a->net)) app_network(a, POLLIN);
}
static void feed(App *a, const char *s) { feed_bytes(a, s, strlen(s)); }
static void register_plain(App *a)
{
    feed(a, ":server CAP tester LS :multi-prefix\r\n");
    assert(strstr(a->net->output, "CAP REQ :multi-prefix\r\n"));
    feed(a, ":server CAP tester ACK :multi-prefix\r\n");
    assert(strstr(a->net->output, "CAP END\r\n"));
    feed(a, ":server 001 tester :Welcome\r\n:server 005 tester CASEMAPPING=rfc1459 PREFIX=(ov)@+ CHANMODES=b,k,l,imnst :supported\r\n");
    assert(a->registered && strstr(a->net->output, "USERHOST tester\r\nJOIN #test\r\n"));
}
static size_t channel(App *a) { return app_buffer(a, "#test", B_CHANNEL, 0); }
static const char *last_line(const Buffer *b)
{
    assert(b->line_count);
    return b->lines[(b->line_start + b->line_count - 1) % b->line_cap];
}
static size_t user_index(const Buffer *b, const char *name)
{
    size_t i;
    for (i = 0; i < b->user_count; ++i)
        if (!strcmp(b->users[i].nick, name)) return i;
    return SIZE_MAX;
}

static void registration_membership_mentions(void)
{
    App *a = fresh(0);
    SendPlan plan;
    Buffer *b;
    size_t view, n;
    register_plain(a);
    assert(a->current == 0);
    feed(a, ":tester!~tester@host.example JOIN #test\r\n");
    view = channel(a);
    assert(view == 3 && a->current == view);
    b = &a->buffers[view];
    assert(b->joined && !strcmp(a->own_user, "~tester") && !strcmp(a->own_host, "host.example"));
    feed(a, ":server 353 tester = #test :@alice +bob tester\r\n:server 353 tester = #test :carol\r\n:server 366 tester #test :End\r\n");
    assert(b->user_count == 4 && !b->names_in_progress);
    assert(b->users[user_index(b, "alice")].modes == 1);
    assert(b->users[user_index(b, "bob")].modes == 2);
    feed(a, ":alice!u@h MODE #test +o bob\r\n");
    assert(b->users[user_index(b, "bob")].modes == 3);
    feed(a, ":bob!u@h NICK :Bobby\r\n");
    assert(user_index(b, "bob") == SIZE_MAX && user_index(b, "Bobby") != SIZE_MAX);
    feed(a, ":alice!u@h PRIVMSG #test :hello TeStEr!\r\n");
    assert(a->buffers[1].line_count == 1);
    assert(!strcmp(last_line(b), last_line(&a->buffers[1])));
    assert(strstr(last_line(b), "[#test] <alice> hello TeStEr!"));
    feed(a, ":alice!u@h PRIVMSG #test :testers are welcome\r\n");
    assert(a->buffers[1].line_count == 1);
    feed(a, ":alice!u@h PRIVMSG #test :te\00304st\002er\017: hi\r\n");
    assert(a->buffers[1].line_count == 2);
    assert(strstr(last_line(&a->buffers[1]), "tester: hi"));
    assert(app_command(a, "/watch add plant rights", &plan) == 1);
    feed(a, ":alice!u@h NOTICE #test :PLANT RIGHTS discussion\r\n");
    assert(a->buffers[1].line_count == 3);
    feed(a, ":alice!u@h PRIVMSG tester :a private message\r\n");
    assert(a->buffers[1].line_count == 4);
    assert(app_buffer(a, "alice", B_QUERY, 0) != SIZE_MAX);
    n = a->buffer_count;
    feed(a, ":alice!u@h PRIVMSG #test :\001VERSION\001\r\n");
    assert(a->buffer_count == n);
    assert(!strstr(a->net->output, "NOTICE alice"));
    feed(a, ":carol!u@h PART #test :bye\r\n:alice!u@h QUIT :gone\r\n");
    assert(user_index(b, "carol") == SIZE_MAX && user_index(b, "alice") == SIZE_MAX);
    assert(app_command(a, "/join #other", &plan) == 1);
    assert(!strcmp(a->cfg.channels, "#test,#other"));
    assert(app_command(a, "/part #other", &plan) == 1);
    assert(!strcmp(a->cfg.channels, "#test"));
    feed(a, ":operator!u@h KICK #test tester :reason\r\n");
    assert(!b->joined && !b->user_count);
    app_switch(a, view);
    assert(app_command(a, "should not send", &plan) == 0);
    app_help(a);
    assert(a->current == view && a->buffers[2].line_count == 2);
    destroy(a);
}

static void no_channel_server_view(void)
{
    App *a = fresh(0);
    a->cfg.channels[0] = 0;
    feed(a, ":server CAP tester LS :\r\n:server 001 tester :Welcome\r\n");
    assert(a->registered && a->current == 0 && a->buffer_count == 3);
    assert(!strstr(a->net->output, "JOIN "));
    feed(a, ":server 375 tester :- MOTD\r\n:server 372 tester :- hello server view\r\n:server 376 tester :End\r\n");
    assert(a->current == 0 && strstr(last_line(&a->buffers[0]), "] End"));
    assert(!strstr(last_line(&a->buffers[0]), "376 tester"));
    destroy(a);
}

static void disconnected_manual_commands(void)
{
    Config c;
    SendPlan plan;
    App *a = calloc(1, sizeof(*a));
    int calls = network_calls, reads = secret_reads, saves = config_saves;
    static const char *const commands[] = { "/manual", "/help", "/MANUAL", "/view help" };
    static const char *const invalid[] = { "/manual extra", "/help extra" };
    size_t origins[3], cycle[4];
    memset(&c, 0, sizeof(c));
    strcpy(c.server, "must-not-resolve.invalid");
    strcpy(c.nick, "tester");
    strcpy(c.username, "tester");
    strcpy(c.auth, "sasl");
    strcpy(c.password_file, "/must-not-read/password");
    c.port = 6697;
    c.tls = 1;
    c.history_lines = 50;
    offline_guard = 1;
    assert(a && app_init(a, &c, "/must-not-write/config.ini"));
    assert(!a->net && !a->sim && !a->registered);
    assert(app_command(a, "/prev", &plan) == 1 && a->current == 1);
    assert(app_command(a, "/next", &plan) == 1 && a->current == 0);
    app_switch(a, 2);
    assert(a->current == 0);
    origins[0] = 0;
    origins[1] = app_buffer(a, "#retained", B_CHANNEL, 1);
    origins[2] = app_buffer(a, "RetainedQuery", B_QUERY, 1);
    assert(origins[1] == 3 && origins[2] == 4);
    for (size_t origin = 0; origin < 3; ++origin) {
        app_switch(a, origins[origin]);
        for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
            assert(app_command(a, commands[i], &plan) == 5);
            assert(a->current == origins[origin]);
            assert(!a->net && !a->sim && !a->registered);
            app_help(a);
            assert(a->current == origins[origin] && a->buffers[2].line_count == 2);
        }
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
            assert(app_command(a, invalid[i], &plan) == 0);
            assert(strstr(a->status, "Usage:"));
            assert(a->current == origins[origin]);
        }
        assert(app_command(a, "/view help extra", &plan) == 0);
        assert(a->current == origins[origin] && strstr(a->status, "No such view"));
    }
    assert(app_command(a, "/unrecognized-manual-command", &plan) == 0);
    assert(strstr(a->status, "Unknown command /unrecognized-manual-command"));
    assert(app_command(a, "/join #requires-registration", &plan) == 0);
    assert(strstr(a->status, "registered") || strstr(a->status, "connect"));
    assert(app_buffer(a, "#requires-registration", B_CHANNEL, 0) == SIZE_MAX);

    /* Help is a secondary screen, not a conversation reached by cycling. */
    cycle[0] = 1; cycle[1] = 3; cycle[2] = 4; cycle[3] = 0;
    app_switch(a, 0);
    for (size_t i = 0; i < 4; ++i) {
        assert(app_command(a, "/next", &plan) == 1 && a->current == cycle[i]);
    }
    cycle[0] = 4; cycle[1] = 3; cycle[2] = 1; cycle[3] = 0;
    for (size_t i = 0; i < 4; ++i) {
        assert(app_command(a, "/prev", &plan) == 1 && a->current == cycle[i]);
    }
    destroy(a);
    assert(network_calls == calls && secret_reads == reads && config_saves == saves);
    offline_guard = 0;
}

static void send_plans_and_priority(void)
{
    App *a = fresh(0);
    SendPlan plan;
    char text[1001];
    size_t i;
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    output_clear(a);
    assert(app_command(a, "alpha\nbeta\n\n", &plan) == 2);
    assert(plan.count == 2 && plan.chunks[0].offset == 0 && plan.chunks[0].length == 5);
    assert(plan.chunks[1].offset == 6 && plan.chunks[1].length == 4);
    assert(!a->net->output_len && a->chat_count == 0);
    assert(app_send_plan(a, &plan));
    app_network(a, 0);
    assert(strstr(a->net->output, "PRIVMSG #test :alpha\r\n"));
    assert(!strstr(a->net->output, "PRIVMSG #test :beta"));
    feed(a, "PING :priority\r\n");
    assert(strstr(a->net->output, "PONG :priority\r\n"));
    assert(!strstr(a->net->output, "PRIVMSG #test :beta"));
    clock_ms += 1200;
    app_network(a, 0);
    assert(strstr(a->net->output, "PRIVMSG #test :beta\r\n"));
    memset(text, 'x', 1000); text[1000] = 0;
    assert(app_plan(a, "#test", text, 0, &plan) == 1 && plan.count == 3);
    assert(app_send_plan(a, &plan));
    assert(a->chat_count == 3);
    assert(app_command(a, "/cancel", &plan) == 1 && a->chat_count == 0);
    assert(app_plan(a, "#test", "a\rb", 0, &plan) == 0);
    assert(app_plan(a, "#test", "\n\n", 0, &plan) == 0);
    assert(app_plan(a, "#test", "one\ntwo", 1, &plan) == 1 && plan.count == 2);
    assert(app_send_plan(a, &plan));
    output_clear(a);
    clock_ms += 1200;
    app_network(a, 0);
    assert(strstr(a->net->output, "PRIVMSG #test :\001ACTION one\001\r\n"));
    assert(app_command(a, "/cancel", &plan) == 1);
    /* A changed relay identity invalidates an approved plan before queueing. */
    memset(text, 'x', 400); text[400] = 0;
    assert(app_plan(a, "#test", text, 0, &plan) && plan.count == 1);
    memset(a->own_host, 'h', 255); a->own_host[255] = 0;
    assert(!app_send_plan(a, &plan) && a->chat_count == 0);
    /* A protocol-injection mutation cannot bypass plan construction. */
    strcpy(a->own_host, "host");
    assert(app_plan(a, "#test", "safe", 0, &plan));
    strcpy(plan.text, "a\r\nQUIT");
    assert(!app_send_plan(a, &plan));
    /* Input drafts never cause multiple command dispatches. */
    assert(app_command(a, "/join #safe\nJOIN #unsafe", &plan) == 0);
    /* Saturated queue is all-or-nothing. */
    assert(app_plan(a, "#test", "one", 0, &plan));
    for (i = 0; i < APP_QUEUE; ++i) assert(app_send_plan(a, &plan));
    assert(!app_send_plan(a, &plan) && a->chat_count == APP_QUEUE);
    destroy(a);
}

/* Measuring a draft must never dispatch its command, create a view, update a
 * status, read credentials, or even consult the transport/clock. */
static int measure_pure(App *a, size_t view, const char *text, int literal,
                        AppDraftMeasure *measure)
{
    App *before = malloc(sizeof(*before));
    int result, net_before = network_calls, secret_before = secret_reads;
    int saves_before = config_saves;
    assert(before);
    memcpy(before, a, sizeof(*before));
    result = app_measure_draft(a, view, text, literal, measure);
    assert(!memcmp(before, a, sizeof(*before)));
    assert(network_calls == net_before && secret_reads == secret_before &&
           config_saves == saves_before);
    free(before);
    return result;
}

static void draft_measurement(void)
{
    static const struct {
        const char *draft, *payload, *target;
        int action, literal;
    } cases[] = {
        { "plain", "plain", "#test", 0, 0 },
        { " /msg bob x", " /msg bob x", "#test", 0, 0 },
        { "//help", "/help", "#test", 0, 0 },
        { "////help", "///help", "#test", 0, 0 },
        { "/me waves", "waves", "#test", 1, 0 },
        { "/  Me  waves  twice\nhi", "waves  twice\nhi", "#test", 1, 0 },
        { "/MsG   bob   one  two\nthree ", "one  two\nthree ", "bob", 0, 0 },
        { "/msg #test hi", "hi", "#test", 0, 0 },
        { "/help", "/help", "#test", 0, 1 },
        { "//literal", "//literal", "#test", 0, 1 },
        { "/msg bob hi", "/msg bob hi", "#test", 0, 1 },
        { "e\xcc\x81", "e\xcc\x81", "#test", 0, 0 }
    };
    static const char *const empty[] = {
        "", "\n\n", "/me", "/me    ", "/msg bob", "/msg bob   "
    };
    static const char *const invalid[] = {
        "/help", "/join #room", "/topic hello", "/msg", "/msg bo,b hi",
        "/m\ne hello", "/msg bo\nb hi", "x\ry", "x\ty", "x\033y",
        "\xff", "\xc0\xaf"
    };
    App *a = fresh(0);
    AppDraftMeasure m;
    SendPlan plan, dispatched;
    char text[APP_TEXT + 2];
    size_t view, budget, i, j, payload;
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    view = channel(a);
    budget = app_budget(a, "#test", 0);
    assert(budget > 10 && budget < 510);

    /* Exact fullness still fits one message; the very next byte needs two. */
    for (i = budget - 1; i <= budget + 1; ++i) {
        memset(text, 'x', i); text[i] = 0;
        assert(measure_pure(a, view, text, 0, &m));
        assert(m.budget == budget && m.payload_bytes == i &&
               m.longest_line_bytes == i && m.fits);
        assert(m.messages == (i <= budget ? 1u : 2u));
        assert(app_plan(a, "#test", text, 0, &plan));
        assert(m.messages == plan.count && m.budget == plan.budget);
    }

    /* A scalar cannot be divided to fill the last byte of a frame. */
    memset(text, 'x', budget - 1);
    memcpy(text + budget - 1, "\xc3\xa9", 3);
    assert(measure_pure(a, view, text, 0, &m));
    assert(m.payload_bytes == budget + 1 && m.messages == 2 && m.fits);
    assert(app_plan(a, "#test", text, 0, &plan));
    assert(plan.chunks[0].length == budget - 1 && plan.chunks[1].length == 2);
    memset(text, 'x', budget - 4);
    memcpy(text + budget - 4, "\xf0\x9f\x99\x82", 5);
    assert(measure_pure(a, view, text, 0, &m));
    assert(m.payload_bytes == budget && m.longest_line_bytes == budget &&
           m.messages == 1 && m.fits);

    /* Command syntax, optional ASCII spaces, /me framing, and literal pastes
     * are compared with actual plans and with the real command dispatcher. */
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const char *body = cases[i].payload;
        assert(measure_pure(a, view, cases[i].draft, cases[i].literal, &m));
        assert(!strcmp(m.target, cases[i].target) && m.action == cases[i].action);
        payload = 0;
        for (j = 0; body[j]; ++j) if (body[j] != '\n') ++payload;
        assert(m.payload_bytes == payload && m.fits);
        assert(app_plan(a, cases[i].target, body, cases[i].action, &plan));
        assert(m.messages == plan.count && m.budget == plan.budget);
        if (!cases[i].literal) {
            assert(app_command(a, cases[i].draft, &dispatched) == 2);
            assert(!strcmp(dispatched.text, body) &&
                   !strcmp(dispatched.target, m.target) &&
                   dispatched.count == m.messages && dispatched.budget == m.budget &&
                   dispatched.action == m.action);
        }
    }
    assert(app_budget(a, "#test", 1) == budget - 9);
    assert(measure_pure(a, 0, "/msg bob hello", 0, &m));
    assert(!strcmp(m.target, "bob") && m.payload_bytes == 5 && m.messages == 1);
    assert(!measure_pure(a, 0, "hello", 0, &m));
    assert(!measure_pure(a, 0, "/me waves", 0, &m));
    assert(!measure_pure(a, 0, "/help", 1, &m));
    assert(measure_pure(a, app_buffer(a, "bob", B_QUERY, 0), "hello", 0, &m));
    assert(!strcmp(m.target, "bob"));

    assert(measure_pure(a, view, "\nalpha\n\nbeta\n", 0, &m));
    assert(m.payload_bytes == 9 && m.longest_line_bytes == 5 &&
           m.messages == 2 && m.fits);
    for (i = 0; i < sizeof(empty) / sizeof(empty[0]); ++i) {
        assert(measure_pure(a, view, empty[i], 0, &m));
        assert(!m.payload_bytes && !m.longest_line_bytes && !m.messages && m.fits);
    }
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!measure_pure(a, view, invalid[i], 0, &m));
        assert(!m.target[0] && !m.messages && !m.budget);
    }
    assert(!measure_pure(a, SIZE_MAX, "hello", 0, &m));
    assert(!measure_pure(a, view, NULL, 0, &m));
    assert(!app_measure_draft(NULL, view, "hello", 0, &m));
    assert(!app_measure_draft(a, view, "hello", 0, NULL));
    memset(text, 'x', APP_TEXT + 1); text[APP_TEXT + 1] = 0;
    assert(!measure_pure(a, view, text, 0, &m));

    /* Explicit LF can exceed the queue even with a very small payload. */
    for (i = 0; i < APP_QUEUE + 1u; ++i) {
        text[i * 2] = 'x'; text[i * 2 + 1] = '\n';
    }
    text[APP_QUEUE * 2] = 0;
    assert(measure_pure(a, view, text, 0, &m));
    assert(m.messages == APP_QUEUE && m.payload_bytes == APP_QUEUE && m.fits);
    text[APP_QUEUE * 2] = 'x'; text[(APP_QUEUE + 1) * 2] = 0;
    assert(measure_pure(a, view, text, 0, &m));
    assert(m.messages == APP_QUEUE + 1 && m.payload_bytes == APP_QUEUE + 1 &&
           m.longest_line_bytes == 1 && !m.fits);
    assert(!app_plan(a, "#test", text, 0, &plan));

    /* A lower server limit can leave no usable payload, or less than one
     * scalar. Neither is misreported as a 65-message overflow. */
    a->wire_limit = 512 - budget + 1;
    assert(app_budget(a, "#test", 0) == 1);
    assert(measure_pure(a, view, "\xc3\xa9", 0, &m));
    assert(m.budget == 1 && m.payload_bytes == 2 && !m.fits && !m.messages);
    assert(measure_pure(a, view, "xx", 0, &m));
    assert(m.budget == 1 && m.messages == 2 && m.fits);
    --a->wire_limit;
    assert(measure_pure(a, view, "x", 0, &m));
    assert(!m.budget && !m.messages && !m.fits && m.payload_bytes == 1);
    a->wire_limit = 512;
    a->registered = 0;
    a->buffers[view].joined = 0;
    assert(measure_pure(a, view, "draft while offline", 0, &m));
    assert(m.messages == 1 && m.fits);
    destroy(a);
}

static void inflight_and_bounds(void)
{
    App *a = fresh(0);
    SendPlan plan;
    size_t i;
    char frame[600];
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    output_clear(a);
    assert(app_plan(a, "#test", "one\ntwo", 0, &plan));
    assert(app_send_plan(a, &plan));
    a->net->block = 1;
    app_network(a, 0);
    assert(a->inflight && a->chat_count == 1);
    assert(app_command(a, "/cancel", &plan) == 1);
    assert(a->inflight && a->chat_count == 0);
    a->net->block = 0;
    app_network(a, POLLOUT);
    assert(!a->inflight && strstr(a->net->output, "PRIVMSG #test :one\r\n"));
    assert(!strstr(a->net->output, "PRIVMSG #test :two"));
    /* History stays within its configured ring. */
    for (i = 0; i < 200; ++i) app_log(a, 0, "bounded line %zu", i);
    assert(a->buffers[0].line_count == 50 && a->log_bytes < APP_LOG_BYTES);
    /* Eight invalid body lengths close the connection. */
    strcpy(frame, "NOTICE tester :");
    memset(frame + 15, 'x', 500);
    strcpy(frame + 515, "\r\n");
    for (i = 0; i < 8 && a->net; ++i) feed(a, frame);
    assert(!a->net && a->chat_count == 0 && a->control_count == 0);
    destroy(a);
    a = fresh(0);
    feed_bytes(a, "PING :x\0QUIT\r\n", sizeof("PING :x\0QUIT\r\n") - 1);
    assert(!a->net);
    destroy(a);
    a = fresh(0);
    feed(a, "PING :x\rINJECT\r\n");
    assert(!a->net);
    destroy(a);
}

static void source_identity_bounds(void)
{
    App *a = fresh(0);
    char frame[512], own_nick[64], long_user[65], long_host[301];
    size_t views;
    register_plain(a);
    memset(own_nick, 'a', 63); own_nick[63] = 0;
    strcpy(a->nick, own_nick);
    views = a->buffer_count;
    (void)snprintf(frame, sizeof(frame), ":%sextra!u@h JOIN #spoof\r\n", own_nick);
    feed(a, frame);
    assert(a->net && a->buffer_count == views);
    assert(app_buffer(a, "#spoof", B_CHANNEL, 0) == SIZE_MAX);
    memset(long_user, 'u', 64); long_user[64] = 0;
    (void)snprintf(frame, sizeof(frame), ":%s!%s@h JOIN #test\r\n", own_nick, long_user);
    feed(a, frame);
    assert(!a->net);
    destroy(a);
    a = fresh(0);
    register_plain(a);
    memset(long_host, 'h', 300); long_host[300] = 0;
    (void)snprintf(frame, sizeof(frame), ":server 302 tester :tester=+u@%s\r\n", long_host);
    feed(a, frame);
    assert(!a->net);
    destroy(a);
}

static void private_sender_view_limit(void)
{
    App *a = fresh(0);
    SendPlan plan;
    char frame[256];
    size_t i, queries = 0;
    register_plain(a);
    for (i = 0; i < 20; ++i) {
        (void)snprintf(frame, sizeof(frame), ":sender%zu!u@h PRIVMSG tester :message %zu\r\n", i, i);
        feed(a, frame);
    }
    for (i = 0; i < a->buffer_count; ++i)
        if (a->buffers[i].kind == B_QUERY) ++queries;
    assert(queries == 8 && a->buffer_count == 11);
    assert(a->buffers[1].line_count == 20);
    assert(strstr(last_line(&a->buffers[0]), "<sender19> message 19"));
    assert(!strcmp(last_line(&a->buffers[0]), last_line(&a->buffers[1])));
    assert(app_command(a, "/join #new-channel", &plan) == 1);
    assert(app_command(a, "/msg sender19 reply", &plan) == 2);
    assert(app_buffer(a, "sender19", B_QUERY, 0) != SIZE_MAX);
    destroy(a);
}

static void global_memory_bounds(void)
{
    App *a = fresh(0);
    char *large = malloc(APP_TEXT + 1);
    char name[32];
    size_t i, j, view;
    assert(large);
    memset(large, 'x', APP_TEXT);
    large[APP_TEXT] = 0;
    app_help(a);
    for (i = 0; i < APP_BUFFERS - 3; ++i) {
        (void)snprintf(name, sizeof(name), "#bounded%zu", i);
        view = app_buffer(a, name, B_CHANNEL, 1);
        assert(view != SIZE_MAX);
        for (j = 0; j < 50; ++j) app_log(a, view, "%s", large);
        assert(a->log_bytes <= APP_LOG_BYTES);
    }
    assert(a->buffer_count == APP_BUFFERS);
    assert(app_buffer(a, "#extra", B_CHANNEL, 1) == SIZE_MAX);
    assert(a->buffers[2].line_count == 2);
    free(large);
    destroy(a);
}

typedef struct CloseRecord {
    App *app;
    size_t calls, last_index, old_count;
    char name[201];
} CloseRecord;

static void record_close(void *context, size_t index)
{
    CloseRecord *record = context;
    assert(index >= 3 && index < record->app->buffer_count);
    ++record->calls;
    record->last_index = index;
    record->old_count = record->app->buffer_count;
    strcpy(record->name, record->app->buffers[index].name);
    assert(record->app->buffers[index].lines);
}

static void check_log_accounting(const App *a)
{
    size_t bytes = 0, i, j;
    for (i = 0; i < a->buffer_count; ++i) {
        const Buffer *b = &a->buffers[i];
        for (j = 0; j < b->line_count; ++j)
            bytes += strlen(b->lines[(b->line_start + j) % b->line_cap]) + 1;
    }
    assert(bytes == a->log_bytes && bytes <= APP_LOG_BYTES);
}

static void close_and_channel_numbering(void)
{
    App *a = fresh(0);
    SendPlan plan;
    CloseRecord record;
    size_t i, count;
    char name[32];
    memset(&record, 0, sizeof(record));
    record.app = a;
    a->view_closed = record_close;
    a->view_context = &record;
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    output_clear(a);
    assert(app_command(a, "/pm alice", &plan) == 1);
    assert(a->current == 4 && a->net->output_len == 0);
    assert(app_command(a, "/join #second", &plan) == 1);
    feed(a, ":tester!u@host JOIN #second\r\n");
    assert(app_command(a, "/pm bob", &plan) == 1 && a->current == 6);
    assert(app_command(a, "/channels", &plan) == 3 && a->current == 6);
    assert(app_command(a, "/views", &plan) == 3 && a->current == 6);
    assert(app_command(a, "/ql", &plan) == 4 && a->current == 6);
    assert(app_command(a, "/channel 0", &plan) == 1 && a->current == 0);
    assert(app_command(a, "/channel 1", &plan) == 1 && a->current == 3);
    assert(app_command(a, "/channel 2", &plan) == 1 && a->current == 5);
    assert(app_command(a, "/view 2", &plan) == 1 && a->current == 5);
    assert(app_command(a, "/view alice", &plan) == 1 && a->current == 4);
    assert(app_command(a, "/channel alice", &plan) == 0 && a->current == 4);
    assert(app_command(a, "/channel #second", &plan) == 1 && a->current == 5);
    assert(app_command(a, "/pm bob", &plan) == 1 && a->current == 6);
    assert(app_plan(a, "#test", "cancel-one\ncancel-two", 0, &plan));
    assert(app_send_plan(a, &plan));
    assert(app_plan(a, "alice", "private-stays", 0, &plan));
    assert(app_send_plan(a, &plan));
    assert(app_plan(a, "#second", "other-channel-stays", 0, &plan));
    assert(app_send_plan(a, &plan));
    assert(a->chat_count == 4);
    output_clear(a);
    assert(app_command(a, "/close 1", &plan) == 1);
    assert(record.calls == 1 && record.last_index == 3 && record.old_count == 7);
    assert(!strcmp(record.name, "#test"));
    assert(a->buffer_count == 6 && a->current == 5);
    assert(!strcmp(a->buffers[a->current].name, "bob"));
    assert(a->chat_count == 2 && !strcmp(a->cfg.channels, "#second"));
    assert(!a->buffers[6].lines && !a->buffers[6].users);
    assert(app_buffer(a, "#test", B_CHANNEL, 0) == SIZE_MAX);
    check_log_accounting(a);
    app_network(a, 0);
    assert(strstr(a->net->output, "PART #test\r\n"));
    assert(!strstr(a->net->output, "PRIVMSG #test"));
    assert(strstr(a->net->output, "PRIVMSG alice :private-stays\r\n"));
    clock_ms += 1200;
    app_network(a, 0);
    assert(strstr(a->net->output, "PRIVMSG #second :other-channel-stays\r\n"));
    assert(app_command(a, "/channel 1", &plan) == 1);
    assert(!strcmp(a->buffers[a->current].name, "#second"));
    assert(app_command(a, "/pm bob", &plan) == 1);
    assert(app_command(a, "/close", &plan) == 1 && a->current == 0);
    assert(record.calls == 2 && !strcmp(record.name, "bob"));
    count = a->buffer_count;
    assert(!app_close_view(a, 0) && !app_close_view(a, 1) && !app_close_view(a, 2));
    assert(app_command(a, "/close 0", &plan) == 0);
    assert(a->buffer_count == count && record.calls == 2);
    /* Closed slots can be reused, including after reaching the hard limit. */
    while (a->buffer_count < APP_BUFFERS) {
        (void)snprintf(name, sizeof(name), "query%zu", a->buffer_count);
        assert(app_buffer(a, name, B_QUERY, 1) != SIZE_MAX);
    }
    assert(app_buffer(a, "cannotfit", B_QUERY, 1) == SIZE_MAX);
    for (i = 0; i < 10; ++i) assert(app_close_view(a, a->buffer_count - 1));
    assert(a->buffer_count == APP_BUFFERS - 10);
    for (i = 0; i < 10; ++i) {
        (void)snprintf(name, sizeof(name), "reused%zu", i);
        assert(app_buffer(a, name, B_QUERY, 1) != SIZE_MAX);
    }
    assert(a->buffer_count == APP_BUFFERS);
    check_log_accounting(a);
    destroy(a);
}

static void close_inflight_and_pending_join(void)
{
    App *a = fresh(0);
    SendPlan plan;
    size_t index;
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    assert(app_command(a, "/pm alice", &plan) == 1);
    index = a->current;
    assert(app_plan(a, "alice", "finish-inflight\ncancel-pending", 0, &plan));
    assert(app_send_plan(a, &plan));
    output_clear(a);
    a->net->block = 1;
    app_network(a, 0);
    assert(a->inflight && a->chat_count == 1);
    assert(app_close_view(a, index));
    assert(a->inflight && a->chat_count == 0);
    assert(strstr(a->status, "in-flight message will finish"));
    a->net->block = 0;
    app_network(a, POLLOUT);
    assert(!a->inflight && strstr(a->net->output, "PRIVMSG alice :finish-inflight\r\n"));
    assert(!strstr(a->net->output, "cancel-pending"));
    assert(app_buffer(a, "alice", B_QUERY, 0) == SIZE_MAX);
    assert(strstr(last_line(&a->buffers[0]), "finish-inflight"));
    /* PART is ordered after an unavoidable in-flight channel message. */
    assert(app_plan(a, "#test", "channel-inflight\nchannel-pending", 0, &plan));
    assert(app_send_plan(a, &plan));
    clock_ms += 1200;
    output_clear(a);
    a->net->block = 1;
    app_network(a, 0);
    assert(a->inflight && a->chat_count == 1);
    assert(app_close_view(a, channel(a)));
    a->net->block = 0;
    app_network(a, POLLOUT);
    assert(strstr(a->net->output, "PRIVMSG #test :channel-inflight\r\nPART #test\r\n"));
    assert(!strstr(a->net->output, "channel-pending"));
    assert(app_buffer(a, "#test", B_CHANNEL, 0) == SIZE_MAX);
    /* A queued JOIN and delayed state replies cannot resurrect closed views. */
    assert(app_command(a, "/join #pending", &plan) == 1);
    index = a->current;
    output_clear(a);
    assert(app_close_view(a, index));
    app_network(a, 0);
    assert(!strstr(a->net->output, "JOIN #pending"));
    feed(a, ":tester!u@host JOIN #pending\r\n:server 353 tester = #pending :tester alice\r\n:server 332 tester #pending :old topic\r\n:tester!u@host PART #pending\r\n");
    assert(app_buffer(a, "#pending", B_CHANNEL, 0) == SIZE_MAX);
    assert(a->buffer_count == 3);
    check_log_accounting(a);
    destroy(a);
}

static void close_failure_part_and_pm_names(void)
{
    App *a = fresh(0);
    SendPlan plan;
    size_t i, before;
    CloseRecord record;
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    memset(&record, 0, sizeof(record));
    record.app = a;
    a->view_closed = record_close;
    a->view_context = &record;
    assert(app_plan(a, "#test", "preserve-if-close-fails", 0, &plan));
    assert(app_send_plan(a, &plan));
    for (i = 0; i < APP_QUEUE; ++i) assert(app_outgoing(a, 0, "PING :full%zu", i));
    before = a->buffer_count;
    assert(!app_close_view(a, channel(a)));
    assert(a->buffer_count == before && a->chat_count == 1 && record.calls == 0);
    assert(!strcmp(a->cfg.channels, "#test"));
    destroy(a);
    a = fresh(0);
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    assert(app_plan(a, "#test", "no-send-after-part", 0, &plan));
    assert(app_send_plan(a, &plan));
    before = a->buffer_count;
    assert(app_command(a, "/part", &plan) == 1);
    assert(a->chat_count == 0 && a->buffer_count == before);
    feed(a, ":tester!u@host PART #test\r\n");
    assert(!a->buffers[channel(a)].joined);
    assert(strstr(a->status, "history retained"));
    assert(app_command(a, "/channel 1", &plan) == 1 && a->current == channel(a));
    assert(app_command(a, "/close 1", &plan) == 1 && a->buffer_count == 3);
    /* A PM named like a builtin can be opened and closed unambiguously. */
    assert(app_command(a, "/pm server", &plan) == 1);
    assert(a->buffers[a->current].kind == B_QUERY);
    assert(app_command(a, "/server", &plan) == 1 && a->current == 0);
    assert(app_command(a, "/close server", &plan) == 1);
    assert(a->buffer_count == 3 && a->current == 0);
    assert(app_command(a, "/close server", &plan) == 0);
    assert(app_command(a, "/pm help", &plan) == 1);
    assert(app_command(a, "/close help", &plan) == 1 && a->buffer_count == 3);
    assert(app_command(a, "/pm mentions", &plan) == 1);
    assert(app_command(a, "/close mentions", &plan) == 1 && a->buffer_count == 3);
    assert(app_command(a, "/channels\nquit", &plan) == 0);
    assert(app_command(a, "/close 1\n/quit", &plan) == 0);
    assert(app_command(a, "/pm alice\n/quit", &plan) == 0);
    assert(!a->quit);
    app_disconnect(a, "Test offline PM creation");
    assert(app_command(a, "/pm offline", &plan) == 1);
    assert(a->buffers[a->current].kind == B_QUERY && !a->net);
    assert(app_command(a, "/close", &plan) == 1);
    destroy(a);
}

static void sasl_tests(void)
{
    App *a;
    const char *encoded;
    size_t length;
    secret_reads = 0;
    strcpy(test_password, "secret");
    a = fresh(1);
    feed(a, ":server CAP tester LS * :multi-prefix\r\n");
    assert(!strstr(a->net->output, "CAP REQ"));
    feed(a, ":server CAP tester LS :sasl=EXTERNAL,PLAIN\r\n");
    assert(strstr(a->net->output, "CAP REQ :sasl multi-prefix\r\n"));
    feed(a, ":server CAP tester ACK :sasl multi-prefix\r\n");
    assert(strstr(a->net->output, "AUTHENTICATE PLAIN\r\n") && secret_reads == 0);
    feed(a, "AUTHENTICATE +\r\n");
    assert(secret_reads == 1 && strstr(a->net->output, "AUTHENTICATE AHVzZXIAc2VjcmV0\r\n"));
    assert(!strstr(a->net->output, "JOIN #test"));
    feed(a, ":server 903 tester :SASL success\r\n:server 001 tester :Welcome\r\n");
    assert(a->authenticated && a->registered && strstr(a->net->output, "JOIN #test\r\n"));
    assert(!strstr(last_line(&a->buffers[0]), "secret"));
    destroy(a);
    /* Exactly 400 base64 bytes requires a final AUTHENTICATE +. */
    memset(test_password, 'p', 294); test_password[294] = 0;
    a = fresh(1);
    feed(a, ":server CAP tester LS :sasl\r\n:server CAP tester ACK :sasl\r\n");
    output_clear(a);
    feed(a, "AUTHENTICATE +\r\n");
    encoded = strstr(a->net->output, "AUTHENTICATE ");
    assert(encoded);
    encoded += 13;
    length = (size_t)(strstr(encoded, "\r\n") - encoded);
    assert(length == 400);
    assert(strstr(encoded + 400, "\r\nAUTHENTICATE +\r\n"));
    feed(a, ":server 904 tester :failed\r\n");
    assert(!a->net && !a->registered);
    destroy(a);
    strcpy(test_password, "secret");
    secret_reads = 0;
    a = fresh(1);
    feed(a, ":server CAP tester LS :sasl=EXTERNAL multi-prefix\r\n");
    assert(!a->net && secret_reads == 0);
    destroy(a);
    a = fresh(1);
    feed(a, ":server 001 tester :Welcome without auth\r\n");
    assert(!a->net && secret_reads == 0);
    destroy(a);
    a = fresh(1);
    clock_ms += 16000;
    app_network(a, 0);
    assert(!a->net && secret_reads == 0);
    destroy(a);
    a = fresh(0);
    clock_ms += 16000;
    app_network(a, 0);
    assert(a->net && strstr(a->net->output, "CAP END\r\n"));
    clock_ms += 60000;
    app_network(a, 0);
    assert(!a->net);
    destroy(a);
}

static int buffer_contains(const Buffer *b, const char *text)
{
    size_t i;
    for (i = 0; i < b->line_count; ++i)
        if (strstr(b->lines[(b->line_start + i) % b->line_cap], text)) return 1;
    return 0;
}

static void pump_offline(App *a)
{
    size_t i;
    for (i = 0; i < 32; ++i) {
        assert(a->test_mode && !a->net);
        app_network(a, 0);
        assert(app_fd(a) == -1 && app_events(a) == 0);
        assert(app_timeout(a) >= 0 && app_timeout(a) <= 1000);
    }
}

static App *fresh_offline(const char *channels)
{
    Config c;
    App *a = calloc(1, sizeof(*a));
    memset(&c, 0, sizeof(c));
    strcpy(c.server, "must-not-resolve.invalid");
    c.port = 6697;
    c.tls = 1;
    strcpy(c.nick, "offline_tester");
    strcpy(c.username, "tester");
    strcpy(c.realname, "Offline test");
    strcpy(c.channels, channels);
    strcpy(c.auth, "sasl");
    strcpy(c.auth_user, "private-account");
    strcpy(c.password_file, "/unreadable/sasl-secret");
    strcpy(c.server_password_file, "/unreadable/server-secret");
    strcpy(c.ca_file, "/unreadable/ca-bundle");
    c.history_lines = 50;
    c.user_list = 1;
    c.mouse = 1;
    c.hints = c.shortcut_labels = 1;
    c.word_count = 2;
    strcpy(c.words[0], "custom-watch-phrase");
    strcpy(c.words[1], "second-watch-phrase");
    assert(a && app_init(a, &c, "/unwritable/config.ini"));
    assert(app_set_test_mode(a));
    assert(!memcmp(&a->cfg, &c, sizeof(c)));
    assert(strstr(app_connection_label(a), "OFFLINE TEST MODE (stopped)"));
    assert(!a->net && !a->sim);
    /* Exercise stopped-mode timer and readiness without any net_* call. */
    pump_offline(a);
    assert(app_connect(a));
    assert(!a->net && a->sim);
    pump_offline(a);
    assert(a->registered && !a->authenticated && !a->auth_started && !a->auth_sent);
    assert(strstr(app_connection_label(a), "OFFLINE TEST MODE (running)"));
    assert(buffer_contains(&a->buffers[0], "No network, DNS, TLS or authentication"));
    assert(!strcmp(a->cfg.server, "must-not-resolve.invalid"));
    assert(!strcmp(a->cfg.auth, "sasl"));
    assert(!strcmp(a->cfg.channels, channels));
    return a;
}

static size_t guest_view(const App *a, const char *nick)
{
    size_t i;
    for (i = 3; i < a->buffer_count; ++i)
        if (a->buffers[i].kind == B_CHANNEL && user_index(&a->buffers[i], nick) != SIZE_MAX)
            return i;
    return SIZE_MAX;
}

static void offline_mode_tests(void)
{
    App *a;
    SendPlan plan;
    int old_network = network_calls, old_secrets = secret_reads, old_saves = config_saves;
    size_t first, second, query, i, guest;
    int64_t event_time;
    char text[801], long_channel[201], long_nick[64], topic[256], command_text[512];
    offline_guard = 1;
    a = fresh_offline("#offline-a,#offline-b");
    first = app_buffer(a, "#offline-a", B_CHANNEL, 0);
    second = app_buffer(a, "#offline-b", B_CHANNEL, 0);
    assert(first != SIZE_MAX && second != SIZE_MAX);
    assert(app_buffer(a, "#test-one", B_CHANNEL, 0) == SIZE_MAX);
    assert(a->buffers[first].joined && a->buffers[second].joined);
    assert(a->buffers[first].user_count == 4 && a->buffers[second].user_count == 4);
    assert(!strcmp(a->own_user, "offline") && !strcmp(a->own_host, "sim.local"));
    assert(app_command(a, "/save", &plan) == 0);
    assert(strstr(a->status, "/save is disabled") && config_saves == old_saves);
    assert(app_command(a, "/set autoconnect on", &plan) == 1);
    assert(strstr(a->status, "test-session"));
    assert(app_command(a, "/users off", &plan) == 1 && strstr(a->status, "test-session"));
    /* Move the simulator's event clock forward explicitly. The application
     * still consumes each event through its actual IRC parser/state machine. */
    event_time = sim_now_ms() + 100000;
    for (i = 0; i < 11; ++i) {
        size_t word = sim_watch_index(a->sim) % a->cfg.word_count;
        assert(sim_step(a->sim, event_time + (int64_t)i * 2001, a->cfg.words[word]) == 1);
        pump_offline(a);
        if (i == 0)
            assert(buffer_contains(&a->buffers[first], "channel conversation is active"));
        if (i == 1)
            assert(buffer_contains(&a->buffers[1], "this is a simulated mention"));
        if (i == 2)
            assert(buffer_contains(&a->buffers[1], "Configured watch text: custom-watch-phrase"));
        if (i == 3)
            assert(app_buffer(a, "Aster", B_QUERY, 0) != SIZE_MAX);
        if (i == 4) assert(guest_view(a, "Sprout") != SIZE_MAX);
        if (i == 5) assert(guest_view(a, "Sprout") == SIZE_MAX && guest_view(a, "Leaf") != SIZE_MAX);
        if (i == 6) {
            guest = guest_view(a, "Leaf");
            assert(guest != SIZE_MAX);
            assert(a->buffers[guest].users[user_index(&a->buffers[guest], "Leaf")].modes & 2u);
        }
        if (i == 7) assert(guest_view(a, "Leaf") == SIZE_MAX);
        if (i == 10)
            assert(buffer_contains(&a->buffers[1], "Configured watch text: second-watch-phrase"));
    }
    assert(sim_watch_index(a->sim) == 2);
    assert(app_command(a, "/watch add later-watch", &plan) == 1 && strstr(a->status, "test-session"));
    assert(app_command(a, "/watch remove later-watch", &plan) == 1 && strstr(a->status, "test-session"));
    /* Plans do not send until explicitly accepted; normal pacing is retained. */
    memset(text, 'x', 800); text[800] = 0;
    assert(app_plan(a, "#offline-a", text, 0, &plan) && plan.count == 2);
    pump_offline(a);
    assert(a->chat_count == 0);
    assert(app_send_plan(a, &plan));
    pump_offline(a);
    assert(a->chat_count == 1);
    a->last_chat = app_now_ms(a) - 1200;
    pump_offline(a);
    assert(a->chat_count == 0 && buffer_contains(&a->buffers[first], "<offline_tester> x"));
    assert(app_command(a, "/pm Visitor", &plan) == 1);
    query = a->current;
    assert(app_command(a, "hello offline", &plan) == 2);
    assert(app_send_plan(a, &plan));
    a->last_chat = app_now_ms(a) - 1200;
    pump_offline(a);
    assert(buffer_contains(&a->buffers[query], "<offline_tester> hello offline"));
    assert(buffer_contains(&a->buffers[query], "Offline reply: this is a simulated query"));
    assert(app_command(a, "/whois Aster", &plan) == 1);
    pump_offline(a);
    assert(buffer_contains(&a->buffers[0], "Simulated participant"));
    assert(app_command(a, "/channel #offline-a", &plan) == 1);
    assert(app_command(a, "/topic changed offline topic", &plan) == 1);
    pump_offline(a);
    assert(buffer_contains(&a->buffers[first], "Topic: changed offline topic"));
    assert(app_command(a, "/nick renamed_tester", &plan) == 1);
    pump_offline(a);
    assert(!strcmp(a->nick, "renamed_tester"));
    assert(app_command(a, "/join #extra-offline", &plan) == 1);
    pump_offline(a);
    assert(a->buffers[a->current].joined);
    assert(strstr(a->test_channels, "#extra-offline") && !strstr(a->cfg.channels, "#extra-offline"));
    assert(app_command(a, "/close", &plan) == 1);
    pump_offline(a);
    assert(app_buffer(a, "#extra-offline", B_CHANNEL, 0) == SIZE_MAX);
    assert(!strstr(a->test_channels, "#extra-offline"));
    assert(app_command(a, "/part #offline-b", &plan) == 1);
    pump_offline(a);
    assert(!a->buffers[second].joined && a->buffers[second].line_count > 0);
    assert(!strcmp(a->cfg.channels, "#offline-a,#offline-b"));
    /* Oversized relayed topics get an explicit protocol error. */
    memset(long_nick, 'n', 63); long_nick[63] = 0;
    (void)snprintf(command_text, sizeof(command_text), "/nick %s", long_nick);
    assert(app_command(a, command_text, &plan) == 1);
    pump_offline(a);
    long_channel[0] = '#'; memset(long_channel + 1, 'c', 199); long_channel[200] = 0;
    (void)snprintf(command_text, sizeof(command_text), "/join %s", long_channel);
    assert(app_command(a, command_text, &plan) == 1);
    pump_offline(a);
    memset(topic, 't', 255); topic[255] = 0;
    (void)snprintf(command_text, sizeof(command_text), "/topic %s", topic);
    assert(app_command(a, command_text, &plan) == 1);
    pump_offline(a);
    assert(buffer_contains(&a->buffers[0], "Topic exceeds the simulated"));
    assert(app_command(a, "/disconnect", &plan) == 1);
    assert(a->test_mode && !a->net && !a->sim && !a->registered);
    assert(strstr(app_connection_label(a), "OFFLINE TEST MODE (stopped)"));
    pump_offline(a);
    assert(app_command(a, "/connect", &plan) == 1);
    pump_offline(a);
    assert(a->test_mode && !a->net && a->sim && a->registered);
    assert(a->buffers[first].joined && !a->buffers[second].joined);
    assert(app_command(a, "/save", &plan) == 0);
    destroy(a);
    /* Demo default channels never overwrite the configuration field. */
    a = fresh_offline("");
    assert(app_buffer(a, "#test-one", B_CHANNEL, 0) != SIZE_MAX);
    assert(app_buffer(a, "#test-two", B_CHANNEL, 0) != SIZE_MAX);
    assert(!a->cfg.channels[0] && !strcmp(a->test_channels, "#test-one,#test-two"));
    assert(app_command(a, "/quit", &plan) == 1 && a->quit);
    destroy(a);
    assert(network_calls == old_network && secret_reads == old_secrets && config_saves == old_saves);
    offline_guard = 0;
}

static void simulator_queue_bound(void)
{
    Sim *s;
    char error[128], bytes[1024];
    size_t i, total = 0;
    ssize_t n;
    static const char registration[] = "CAP LS 302\r\nNICK tester\r\nUSER tester 0 * :Test\r\nCAP END\r\n";
    offline_guard = 1;
    s = sim_open(error, sizeof(error));
    assert(s);
    assert(sim_write(s, registration, sizeof(registration) - 1) > 0);
    for (i = 0; i < 2000; ++i)
        assert(sim_write(s, "WHOIS Aster\r\n", sizeof("WHOIS Aster\r\n") - 1) > 0);
    while ((n = sim_read(s, bytes, sizeof(bytes))) > 0) total += (size_t)n;
    assert(n == -2 && total <= 65536 && !sim_failed(s));
    assert(sim_step(s, sim_now_ms() + 100000, "") > 0);
    n = sim_read(s, bytes, sizeof(bytes) - 1);
    assert(n > 0);
    bytes[n] = 0;
    assert(strstr(bytes, "bounded frame/output limits"));
    assert(sim_write(s, "PING :a\0b\r\n", sizeof("PING :a\0b\r\n") - 1) < 0);
    assert(sim_failed(s));
    sim_close(s);
    offline_guard = 0;
}

static void credential_source_tests(void)
{
    strcpy(test_password, "secret");
    secret_reads = credential_starts = credential_cancels = credential_result = 0;
    App *a = fresh(1);
    a->cfg.password_file[0] = 0;
    strcpy(a->cfg.password, "secret");
    feed(a, ":server CAP tester LS :sasl\r\n:server CAP tester ACK :sasl\r\nAUTHENTICATE +\r\n");
    assert(a->auth_sent && !secret_reads && !credential_starts);
    assert(strstr(a->net->output, "AUTHENTICATE AHVzZXIAc2VjcmV0\r\n"));
    assert(!buffer_contains(&a->buffers[0], "secret"));
    SendPlan plan;
    for (int failed = 0; failed <= 1; ++failed) {
        watch_saved_config = 1; watched_secret = "secret"; watched_wipe_calls = 0;
        save_failure = failed;
        assert(app_command(a, "/save", &plan) == !failed);
        assert(watched_wipe_calls == 1);
        assert(!strcmp(a->cfg.password, "secret"));
        watch_saved_config = 0; watched_wipe = NULL; watched_secret = NULL;
    }
    save_failure = 0;
    destroy(a);

    a = fresh(1);
    app_disconnect(a, "reset fixture");
    strcpy(a->cfg.auth, "none"); a->cfg.password_file[0] = 0;
    strcpy(a->cfg.server_password, " pass#;= ");
    assert(app_connect(a)); app_network(a, 0);
    assert(!strncmp(a->net->output, "PASS : pass#;= \r\nCAP LS 302\r\n", strlen("PASS : pass#;= \r\nCAP LS 302\r\n")));
    assert(!secret_reads && !credential_starts && !buffer_contains(&a->buffers[0], "pass#;="));
    destroy(a);

    a = fresh(1);
    a->cfg.password_file[0] = 0;
    strcpy(a->cfg.password_gpg_file, "/private/account.gpg");
    feed(a, ":server CAP tester LS :sasl\r\n:server CAP tester ACK :sasl\r\nAUTHENTICATE +\r\n");
    assert(a->credential && credential_starts == 1 && !a->auth_sent && !secret_reads);
    assert(app_timeout(a) == 50 && strstr(app_connection_label(a), "decrypting"));
    assert(!strstr(a->net->output, "AUTHENTICATE AHVzZXIA"));
    feed(a, "PING :responsive\r\n");
    assert(strstr(a->net->output, "PONG :responsive\r\n"));
    Config desired;
    app_config_effective(a, &desired);
    desired.password_gpg_file[0] = 0;
    strcpy(desired.password, "future-inline-secret");
    char error[256];
    assert(app_config_apply(a, &desired, 0, error, sizeof error));
    assert(!a->cfg.password[0] && !strcmp(a->cfg.password_gpg_file, "/private/account.gpg"));
    credential_result = 1; app_network(a, 0);
    assert(a->auth_sent && !a->credential && credential_cancels == 1);
    assert(strstr(a->net->output, "AUTHENTICATE AHVzZXIAc2VjcmV0\r\n") && !secret_reads);
    destroy(a);

    credential_result = 0;
    a = fresh(1); a->cfg.password_file[0] = 0;
    strcpy(a->cfg.password_gpg_file, "/private/account.gpg");
    feed(a, ":server CAP tester LS :sasl\r\n:server CAP tester ACK :sasl\r\nAUTHENTICATE +\r\n");
    assert(a->credential && !a->auth_sent);
    credential_result = -1; app_network(a, 0);
    assert(!a->net && !a->credential && !a->registered && !secret_reads);
    assert(strstr(a->status, "GPG fixture failed"));
    destroy(a);

    credential_result = 0;
    a = fresh(1); app_disconnect(a, "reset fixture");
    strcpy(a->cfg.auth, "none"); a->cfg.password_file[0] = 0;
    strcpy(a->cfg.server_password_gpg_file, "/private/server.gpg");
    assert(app_connect(a)); app_network(a, 0);
    assert(a->credential && !a->started && !a->net->output_len && app_fd(a) == -1 && !app_events(a));
    /* Registration cannot advance through a premature001 while PASS waits. */
    const char *early = ":server 001 tester :premature\r\n";
    memcpy(a->net->input, early, strlen(early)); a->net->input_len = strlen(early);
    app_network(a, POLLIN);
    assert(!a->registered && !a->started && !a->net->output_len);
    a->net->input_len = a->net->input_pos = 0;
    credential_result = 1; app_network(a, 0);
    assert(a->started && !a->credential && !strncmp(a->net->output, "PASS :secret\r\n", strlen("PASS :secret\r\n")));
    assert(!secret_reads);
    destroy(a);

    credential_result = 0;
    a = fresh(1); a->cfg.password_file[0] = 0;
    strcpy(a->cfg.password_gpg_file, "/private/account.gpg");
    feed(a, ":server CAP tester LS :sasl\r\n:server CAP tester ACK :sasl\r\nAUTHENTICATE +\r\n");
    int cancelled = credential_cancels;
    app_disconnect(a, "cancel fixture");
    assert(!a->credential && !a->credential_kind && credential_cancels == cancelled + 1);
    destroy(a);

    offline_guard = 1;
    a = fresh_offline("#offline");
    app_disconnect(a, "restart offline with GPG fields");
    a->cfg.password_file[0] = a->cfg.server_password_file[0] = 0;
    strcpy(a->cfg.password_gpg_file, "/must-not-open/account.gpg");
    strcpy(a->cfg.server_password_gpg_file, "/must-not-open/server.gpg");
    assert(app_connect(a)); pump_offline(a);
    assert(a->registered && !a->credential && !secret_reads);
    destroy(a); offline_guard = 0;
}

static void assert_startup_same(const Config *a, const Config *b)
{
    assert(!strcmp(a->server, b->server) && a->port == b->port && a->tls == b->tls);
    assert(!strcmp(a->nick, b->nick) && !strcmp(a->username, b->username));
    assert(!strcmp(a->realname, b->realname) && !strcmp(a->channels, b->channels));
    assert(!strcmp(a->auth, b->auth) && !strcmp(a->auth_user, b->auth_user));
    assert(!strcmp(a->password_file, b->password_file));
    assert(!strcmp(a->password_gpg_file, b->password_gpg_file));
    assert(!strcmp(a->password, b->password));
    assert(!strcmp(a->server_password_file, b->server_password_file));
    assert(!strcmp(a->server_password_gpg_file, b->server_password_gpg_file));
    assert(!strcmp(a->server_password, b->server_password));
    assert(!strcmp(a->ca_file, b->ca_file));
}

static void local_list_aliases(void)
{
    App *a = fresh(0); SendPlan plan;
    size_t controls = a->control_count, chats = a->chat_count, output = a->net->output_len;
    assert(app_command(a, "/channels", &plan) == 3);
    assert(app_command(a, "/cl", &plan) == 3);
    assert(app_command(a, "/CL", &plan) == 3);
    assert(!app_command(a, "/cl extra", &plan));
    assert(app_command(a, "/ql", &plan) == 4);
    assert(app_command(a, "/queries", &plan) == 4);
    assert(app_command(a, "/QUERIES", &plan) == 4);
    assert(!app_command(a, "/queries extra", &plan));
    assert(a->control_count == controls && a->chat_count == chats && a->net->output_len == output);
    destroy(a);
}

static void settings_config_transactions(void)
{
    App *a;
    App before;
    Config original, desired, effective;
    SendPlan plan;
    char error[256];
    int calls, reads, saves;
    size_t view, i, bytes_before;
    Net *connection;

    strcpy(test_password, "secret");
    a = fresh(1);
    memcpy(&original, &a->cfg, sizeof(original));
    feed(a, ":server CAP tester LS :sasl\r\n:server CAP tester ACK :sasl\r\n");
    assert(strstr(a->net->output, "AUTHENTICATE PLAIN\r\n"));
    memcpy(&desired, &a->cfg, sizeof(desired));
    strcpy(desired.server, "future.example.test");
    desired.port = 6669; desired.tls = 0;
    strcpy(desired.nick, "FutureNick");
    strcpy(desired.username, "future_user");
    strcpy(desired.realname, "Future real name");
    strcpy(desired.channels, "#future");
    strcpy(desired.auth, "none");
    strcpy(desired.auth_user, "future_account");
    strcpy(desired.password_file, "/future/password");
    strcpy(desired.ca_file, "/future/ca");
    desired.user_list = 0; desired.mouse = 0; desired.hints = desired.shortcut_labels = 0; desired.output_full = 1; desired.autoconnect = 1;
    desired.focus_style = CONFIG_FOCUS_BRACKETS;
    desired.history_lines = 120;
    desired.force_terminal_signals = 1;
    desired.word_count = 1; strcpy(desired.words[0], "live-watch");
    calls = network_calls; reads = secret_reads; saves = config_saves; connection = a->net;
    assert(app_config_apply(a, &desired, 0, error, sizeof(error)) == 1);
    assert_startup_same(&a->cfg, &original);
    assert(a->net == connection && network_calls == calls && secret_reads == reads && config_saves == saves);
    assert(a->cfg.focus_style == CONFIG_FOCUS_BRACKETS);
    assert(a->cfg.force_terminal_signals);
    assert(!a->cfg.user_list && !a->cfg.mouse && !a->cfg.hints && !a->cfg.shortcut_labels && a->cfg.output_full && a->cfg.autoconnect);
    assert(a->cfg.history_lines == 120 && a->cfg.word_count == 1 && !strcmp(a->cfg.words[0], "live-watch"));
    assert(a->buffers[0].line_cap == 120 && a->buffers[2].line_cap >= APP_HELP_LINES);
    app_config_effective(a, &effective);
    assert(!memcmp(&effective, &desired, sizeof(desired)) && a->config_pending_mask);
    /* Settings edited mid-authentication cannot downgrade TLS, replace the
     * account/password path, or change the channel joined after success. */
    feed(a, "AUTHENTICATE +\r\n");
    assert(strstr(a->net->output, "AUTHENTICATE AHVzZXIAc2VjcmV0\r\n"));
    assert(!strcmp(secret_path, "/fake/password"));
    feed(a, ":server 903 tester :SASL success\r\n:server 001 tester :Welcome\r\n");
    assert(a->registered && strstr(a->net->output, "JOIN #test\r\n") && !strstr(a->net->output, "JOIN #future"));
    desired.tls = 1; strcpy(desired.auth, "sasl");
    strcpy(desired.server_password_file, "/future/server-password");
    assert(app_config_apply(a, &desired, 0, error, sizeof(error)) == 1);
    assert_startup_same(&a->cfg, &original);
    assert(app_command(a, "/save", &plan) == 1);
    assert_startup_same(&saved_config, &desired);
    assert(saved_config.force_terminal_signals);
    assert(a->config_pending_mask && !strcmp(a->cfg.server, "localhost"));

    feed(a, ":tester!u@host NICK :LiveNow\r\n");
    assert(app_command(a, "/join #live", &plan) == 1);
    assert(!strcmp(a->cfg.nick, "LiveNow") && strstr(a->cfg.channels, "#live"));
    app_config_effective(a, &effective);
    assert(!strcmp(effective.nick, "FutureNick") && !strcmp(effective.channels, "#future"));
    /* Reverting just these future fields removes their overrides, without
     * discarding other pending connection edits. */
    strcpy(effective.nick, a->cfg.nick);
    strcpy(effective.channels, a->cfg.channels);
    assert(app_config_apply(a, &effective, 0, error, sizeof(error)) == 1);
    feed(a, ":LiveNow!u@host NICK :LiveLater\r\n");
    assert(app_command(a, "/save", &plan) == 1);
    assert(!strcmp(saved_config.nick, "LiveLater") && strstr(saved_config.channels, "#live"));
    assert(!strcmp(saved_config.server, "future.example.test"));
    app_config_effective(a, &effective);
    app_disconnect(a, "settings reconnect check");
    assert(app_connect(a));
    assert(!strcmp(opened_host, effective.server) && opened_port == effective.port && opened_tls == effective.tls);
    assert(!strcmp(opened_ca, effective.ca_file) && !strcmp(a->cfg.auth_user, effective.auth_user));
    assert(!a->config_pending_mask && !memcmp(&a->cfg, &effective, sizeof effective));
    destroy(a);

    a = fresh(0);
    register_plain(a);
    memcpy(&desired, &a->cfg, sizeof(desired));
    desired.history_lines = 120;
    assert(app_config_apply(a, &desired, 0, error, sizeof(error)) == 1);
    view = app_buffer(a, "settings-history", B_QUERY, 1);
    assert(view != SIZE_MAX);
    for (i = 0; i < 120; ++i) app_log(a, view, "row-%03zu", i);
    assert(a->buffers[view].line_count == 120);
    desired.history_lines = 50;
    desired.focus_style = CONFIG_FOCUS_BRACKETS;
    desired.force_terminal_signals = 1;
    desired.word_count = 1; strcpy(desired.words[0], "new-watch");
    strcpy(desired.server, "pending.example.test");
    memcpy(&before, a, sizeof(before));
    memcpy(&save_before, a, sizeof(save_before));
    save_observer = a; save_failure = 1;
    bytes_before = a->log_bytes;
    assert(!a->config_saved);
    assert(app_config_apply(a, &desired, 1, error, sizeof(error)) == 0);
    assert(strstr(error, "fixture save failure") && !memcmp(a, &before, sizeof(before)));
    assert(buffer_contains(&a->buffers[view], "row-000") && buffer_contains(&a->buffers[view], "row-119"));
    save_failure = 0;
    assert(app_config_apply(a, &desired, 1, error, sizeof(error)) == 1);
    assert(a->config_saved);
    assert(a->cfg.force_terminal_signals && saved_config.force_terminal_signals);
    save_observer = NULL;
    assert(a->buffers[view].line_cap == 50 && a->buffers[view].line_count == 50 && a->log_bytes < bytes_before);
    assert(!buffer_contains(&a->buffers[view], "row-069") && buffer_contains(&a->buffers[view], "row-070"));
    assert(strstr(last_line(&a->buffers[view]), "row-119") && a->buffers[2].line_cap >= APP_HELP_LINES);
    assert(!strcmp(a->cfg.server, "localhost") && !strcmp(saved_config.server, "pending.example.test"));
    feed(a, ":alice!u@host PRIVMSG #test :a new-watch message\r\n");
    assert(buffer_contains(&a->buffers[1], "a new-watch message"));
    app_config_effective(a, &effective);
    assert(effective.force_terminal_signals);
    effective.history_lines = 0;
    memcpy(&before, a, sizeof(before));
    saves = config_saves;
    assert(!app_config_apply(a, &effective, 1, error, sizeof(error)) && config_saves == saves);
    assert(!memcmp(a, &before, sizeof(before)));
    destroy(a);

    calls = network_calls; reads = secret_reads; saves = config_saves;
    offline_guard = 1;
    a = fresh_offline("#offline-a");
    memcpy(&original, &a->cfg, sizeof(original));
    app_config_effective(a, &desired);
    strcpy(desired.server, "next-start.invalid");
    strcpy(desired.nick, "NextOffline");
    strcpy(desired.channels, "#next-start");
    desired.focus_style = CONFIG_FOCUS_BRACKETS;
    desired.mouse = 0; desired.history_lines = 70;
    desired.word_count = 1; strcpy(desired.words[0], "offline-live-watch");
    assert(app_config_apply(a, &desired, 0, error, sizeof(error)) == 1);
    assert_startup_same(&a->cfg, &original);
    assert(a->cfg.history_lines == 70 && !a->cfg.mouse && a->cfg.focus_style == CONFIG_FOCUS_BRACKETS);
    app_config_effective(a, &effective);
    assert(!strcmp(effective.server, "next-start.invalid") && !strcmp(effective.channels, "#next-start"));
    desired.focus_style = CONFIG_FOCUS_REVERSE; desired.history_lines = 60;
    memcpy(&before, a, sizeof(before));
    assert(!app_config_apply(a, &desired, 1, error, sizeof(error)));
    assert(strstr(error, "Save is disabled") && !memcmp(a, &before, sizeof(before)));
    assert(app_command(a, "/save", &plan) == 0);
    app_disconnect(a, "offline settings reconnect check");
    assert(app_connect(a));
    pump_offline(a);
    assert(!strcmp(a->nick, "NextOffline") && app_buffer(a, "#next-start", B_CHANNEL, 0) != SIZE_MAX);
    assert(app_buffer(a, "#offline-a", B_CHANNEL, 0) != SIZE_MAX);
    assert(!a->buffers[app_buffer(a, "#offline-a", B_CHANNEL, 0)].joined);
    assert(!a->config_pending_mask);
    destroy(a);
    assert(network_calls == calls && secret_reads == reads && config_saves == saves);
    offline_guard = 0;
}

static int logs_contain(const App *a, const char *text)
{
    for (size_t i = 0; i < a->buffer_count; ++i) {
        const Buffer *b = &a->buffers[i];
        for (size_t j = 0; j < b->line_count; ++j)
            if (strstr(b->lines[(b->line_start + j) % b->line_cap], text)) return 1;
    }
    return 0;
}

static void readable_server_events(void)
{
    App *a = fresh(0);
    register_plain(a);
    assert(strstr(last_line(&a->buffers[0]), "] Welcome"));
    assert(!logs_contain(a, "001 tester") && !logs_contain(a, "005 tester"));
    size_t lines = a->buffers[0].line_count;
    feed(a, ":server 005 tester CASEMAPPING=ascii LINELEN=400 :supported\r\n"
            ":server 302 tester :tester=+learned@visible.example\r\n");
    assert(a->casemap == IRC_CASE_ASCII && a->wire_limit == 400);
    assert(!strcmp(a->own_user, "learned") && !strcmp(a->own_host, "visible.example"));
    assert(a->buffers[0].line_count == lines && !logs_contain(a, "302 tester"));
    feed(a, ":server 005 tester LINELEN=512 :supported\r\n"
            ":server 396 tester cloak.example :is your visible host\r\n");
    assert(!strcmp(a->own_host, "cloak.example") && a->wire_limit == 512);
    assert(a->buffers[0].line_count == lines);
    feed(a, ":server 306 tester :Away confirmed\r\n");
    assert(a->away && strstr(a->status, "Away status confirmed"));
    assert(a->buffers[0].line_count == lines);
    feed(a, ":server 305 tester :Back confirmed\r\n");
    assert(!a->away && strstr(a->status, "Away status cleared"));
    assert(a->buffers[0].line_count == lines);
    feed(a, ":server 433 tester BusyNick :Reserved for another person\r\n");
    assert(strstr(a->status, "BusyNick is already in use") &&
           strstr(a->status, "Reserved for another person"));
    feed(a, ":server 432 tester BadNick :Network nickname policy\r\n");
    assert(strstr(a->status, "BadNick was rejected") && strstr(a->status, "Network nickname policy"));
    assert(a->buffers[0].line_count == lines);

    static const struct { const char *wire, *visible; } replies[] = {
        {":server 375 tester :- Network message of the day\r\n", "] - Network message of the day"},
        {":server 372 tester :Welcome, everyone\r\n", "] Welcome, everyone"},
        {":server 376 tester :End of MOTD\r\n", "] End of MOTD"},
        {":server 301 tester alice :Out for lunch\r\n", "alice is away: Out for lunch"},
        {":server 311 tester alice user host.example * :Alice Example\r\n", "alice is user@host.example (Alice Example)"},
        {":server 312 tester alice irc.example :Regional server\r\n", "Server for alice: irc.example (Regional server)"},
        {":server 313 tester alice :is a local operator with network-specific duties\r\n", "alice: is a local operator with network-specific duties"},
        {":server 317 tester alice 12 1 :seconds idle, signon time\r\n", "alice has been idle for 12 seconds; connected since "},
        {":server 317 tester alice 7 :seconds idle\r\n", "alice has been idle for 7 seconds."},
        {":server 318 tester alice :End of WHOIS\r\n", "End of user information for alice: End of WHOIS"},
        {":server 319 tester alice :@#one +#two\r\n", "alice's channels: @#one +#two"},
        {":server 330 tester alice account :is logged in as\r\n", "alice is signed in as account."},
        {":server 322 tester #one 42 :Public discussion\r\n", "#one (42 users): Public discussion"},
        {":server 323 tester :End of LIST\r\n", "End of channel list: End of LIST"},
        {":server 331 tester #unjoined :No topic\r\n", "No topic is set for #unjoined: No topic"},
        {":server 332 tester #unjoined :A channel topic\r\n", "Topic for #unjoined: A channel topic"},
        {":server 333 tester #unjoined alice 1\r\n", "Topic for #unjoined was set by alice on "},
        {":server 401 tester Missing :No such nickname\r\n", "Cannot find Missing: No such nickname"},
        {":server 403 tester #missing :No such channel\r\n", "Cannot find #missing: No such channel"},
        {":server 404 tester #quiet :Cannot send while moderated\r\n", "Cannot send to #quiet: Cannot send while moderated"},
        {":server 471 tester #full :Channel is full\r\n", "Cannot join #full: Channel is full"},
        {":server 473 tester #private :An invitation is required\r\n", "Cannot join #private: An invitation is required"},
        {":server 474 tester #banned :Access denied by network policy\r\n", "Cannot join #banned: Access denied by network policy"},
        {":server 475 tester #locked :A correct key is required\r\n", "Cannot join #locked: A correct key is required"},
        {":server 482 tester #one :Channel operator privileges required\r\n", "Cannot change #one: Channel operator privileges required"},
        {":server 422 tester :No MOTD file\r\n", "No message of the day: No MOTD file"},
        {":server 799 tester #extension detail :Unfamiliar network reply\r\n", "Server: #extension detail Unfamiliar network reply"},
        {":server 338 tester alice real@host 192.0.2.1 :is actually using this host\r\n", "Server: alice real@host 192.0.2.1 is actually using this host"},
        {":server 477 tester #modeless :Channel does not support modes\r\n", "Server: #modeless Channel does not support modes"},
        {":server 464 :Password rejected before registration\r\n", "Server: Password rejected before registration"},
        {":server 317 tester alice 12 999999999999999999999 :unknown connection time\r\n", "Server: alice 12 999999999999999999999 unknown connection time"},
        {":server 317 tester alice -2 1 :invalid idle time\r\n", "Server: alice -2 1 invalid idle time"}
    };
    for (size_t i = 0; i < sizeof replies / sizeof replies[0]; ++i) {
        feed(a, replies[i].wire);
        assert(strstr(last_line(&a->buffers[0]), replies[i].visible));
        assert(a->net && a->registered && a->current == 0);
    }

    /* Unknown/malformed data keeps useful text, while terminal controls and
     * printf directives cannot become terminal commands or format strings. */
    feed(a, ":server 799 tester :literal %s %n \033]0;unsafe\007\002bold\017\r\n");
    const char *shown = last_line(&a->buffers[0]);
    assert(strstr(shown, "literal %s %n") && strstr(shown, "bold"));
    for (size_t i = 0; shown[i]; ++i)
        assert((unsigned char)shown[i] >= 0x20 && (unsigned char)shown[i] != 0x7f);
    static const char *const malformed[] = {
        ":server 311\r\n", ":server 312 tester\r\n", ":server 317 tester alice\r\n",
        ":server 318 tester\r\n", ":server 322 tester #one\r\n", ":server 331 tester\r\n",
        ":server 332 tester\r\n", ":server 333 tester #one alice not-a-time\r\n",
        ":server 401 tester\r\n", ":server 422\r\n", ":server 475 tester\r\n",
        ":server 799\r\n"
    };
    for (size_t i = 0; i < sizeof malformed / sizeof malformed[0]; ++i) {
        feed(a, malformed[i]);
        assert(a->net && a->registered && strstr(last_line(&a->buffers[0]), "Server:"));
    }

    lines = a->buffers[0].line_count;
    feed(a, ":server 900 tester tester!u@host private-account :private authentication detail\r\n");
    assert(a->buffers[0].line_count == lines && !logs_contain(a, "private authentication detail"));
    feed(a, ":server 324 tester #unjoined +ntk never-show-mode-key\r\n"
            ":operator!u@host MODE #unjoined +k another-private-mode-key\r\n");
    assert(logs_contain(a, "Modes for #unjoined: +ntk") && !logs_contain(a, "never-show-mode-key"));
    assert(strstr(last_line(&a->buffers[0]), "operator changed modes on #unjoined: +k"));
    assert(!logs_contain(a, "another-private-mode-key"));

    feed(a, ":tester!u@host JOIN #test\r\n");
    size_t room = channel(a);
    Buffer *b = &a->buffers[room];
    assert(strstr(last_line(b), "tester has joined #test"));
    feed(a, ":alice!u@host JOIN #test\r\n");
    assert(strstr(last_line(b), "alice has joined #test"));
    feed(a, ":alice!u@host NICK :Alicia\r\n");
    assert(strstr(last_line(b), "alice is now known as Alicia") && user_index(b, "Alicia") != SIZE_MAX);
    feed(a, ":Alicia!u@host PART #test :See you later\r\n");
    assert(strstr(last_line(b), "Alicia has left #test: See you later") && user_index(b, "Alicia") == SIZE_MAX);
    feed(a, ":alice!u@host JOIN #test\r\n:alice!u@host QUIT :Goodbye\r\n");
    assert(strstr(last_line(b), "alice has disconnected: Goodbye") && user_index(b, "alice") == SIZE_MAX);
    size_t views = a->buffer_count;
    feed(a, ":alice!u@host INVITE tester :#private\r\n");
    assert(strstr(last_line(&a->buffers[0]), "alice invited you to #private"));
    assert(a->buffer_count == views && a->current == room);
    feed(a, ":operator!u@host MODE #test +k known-channel-private-key\r\n");
    assert(!logs_contain(a, "known-channel-private-key"));
    feed(a, ":operator!u@host KICK #test tester :Please leave\r\n");
    assert(strstr(last_line(b), "operator removed tester from #test: Please leave"));
    assert(!b->joined && !b->user_count);
    destroy(a);
    puts("readable IRC: hidden identity/feature bookkeeping, welcome/MOTD/WHOIS/errors, preserved unknown details, membership/invites, malformed bounds, control sanitizing and mode/auth privacy passed");
}

static void away_and_keyed_join(void)
{
    App *a = fresh(0);
    SendPlan plan;
    char text[700], key[100], expected[700], old_channels[1024];
    Config effective;
    size_t views;
    assert(!app_command(a, "/away gone", &plan) && !a->away);
    assert(strstr(a->status, "registered connection"));
    assert(!app_command(a, "/back", &plan));
    register_plain(a);
    output_clear(a);
    assert(app_command(a, "/away Gone for lunch", &plan) == 1 && !a->away);
    app_network(a, 0);
    assert(!strcmp(a->net->output, "AWAY :Gone for lunch\r\n"));
    feed(a, ":server 306 someone-else :Away\r\n:peer!u@h 306 tester :Away\r\n");
    assert(!a->away);
    feed(a, ":server 306 TESTER :Away\r\n");
    assert(a->away && strstr(a->status, "confirmed") && strstr(app_connection_label(a), "(away)"));
    output_clear(a);
    assert(app_command(a, "/back", &plan) == 1 && a->away);
    app_network(a, 0);
    assert(!strcmp(a->net->output, "AWAY\r\n"));
    feed(a, ":server 305 tester :Back\r\n");
    assert(!a->away && strstr(a->status, "cleared") && !strstr(app_connection_label(a), "(away)"));
    output_clear(a);
    assert(app_command(a, "/away", &plan) == 1);
    app_network(a, 0);
    assert(!strcmp(a->net->output, "AWAY\r\n"));
    assert(!app_command(a, "/back extra", &plan));
    assert(!app_command(a, "/away gone\nJOIN #bad", &plan));
    assert(!app_command(a, "/away gone\rJOIN #bad", &plan));
    a->wire_limit = 64;
    memset(key, 'a', 56); key[56] = 0;
    snprintf(text, sizeof(text), "/away %s", key);
    output_clear(a);
    assert(app_command(a, text, &plan) == 1);
    app_network(a, 0);
    assert(a->net->output_len == 64 && !memcmp(a->net->output, "AWAY :", 6));
    key[56] = 'a'; key[57] = 0;
    snprintf(text, sizeof(text), "/away %s", key);
    assert(!app_command(a, text, &plan) && !a->control_count);
    feed(a, ":server 306 tester :Away\r\n");
    assert(a->away);
    app_disconnect(a, "away reset test");
    assert(!a->away);
    a->away = 1;
    assert(app_connect(a) && !a->away);
    destroy(a);

    a = fresh(0);
    register_plain(a);
    a->wire_limit = 64;
    memset(key, 'K', 50); key[50] = 0;
    snprintf(text, sizeof(text), "/join #keyed %s", key);
    output_clear(a);
    assert(app_command(a, text, &plan) == 1);
    assert(!strcmp(a->cfg.channels, "#test,#keyed"));
    assert(!strstr(a->status, key) && !logs_contain(a, key));
    watched_wipe = a->control_head; watched_secret = key; watched_wipe_calls = 0;
    app_network(a, 0);
    assert(watched_wipe_calls == 1);
    watched_wipe = NULL; watched_secret = NULL;
    snprintf(expected, sizeof(expected), "JOIN #keyed %s\r\n", key);
    assert(!strcmp(a->net->output, expected) && a->net->output_len == 64);
    app_config_effective(a, &effective);
    assert(!memory_contains(&effective, sizeof(effective), key));
    assert(app_command(a, "/save", &plan) == 1);
    assert(!memory_contains(&saved_config, sizeof(saved_config), key));
    views = a->buffer_count;
    strcpy(old_channels, a->cfg.channels);
    key[50] = 'K'; key[51] = 0;
    snprintf(text, sizeof(text), "/join #keyed %s", key);
    assert(!app_command(a, text, &plan) && !a->control_count);
    assert(a->buffer_count == views && !strcmp(a->cfg.channels, old_channels));
    static const char *const invalid_joins[] = {
        "/join #bad key extra", "/join #bad :key", "/join #bad a,b",
        "/join #bad \xc3\xa9", "/join #one,#two key", "/join #bad key\nPART #test"
    };
    for (size_t i = 0; i < sizeof(invalid_joins) / sizeof(invalid_joins[0]); ++i) {
        assert(!app_command(a, invalid_joins[i], &plan));
        assert(!a->control_count && a->buffer_count == views);
        assert(!strcmp(a->cfg.channels, old_channels));
    }
    /* Cancelling a pending keyed join removes and wipes its unsent frame. */
    output_clear(a);
    assert(app_command(a, "/join #pending private-key", &plan) == 1);
    watched_wipe = a->control_head; watched_secret = "private-key"; watched_wipe_calls = 0;
    assert(app_close_view(a, a->current));
    assert(watched_wipe_calls == 1);
    watched_wipe = NULL; watched_secret = NULL;
    app_network(a, 0);
    assert(!strstr(a->net->output, "JOIN #pending") && !strstr(a->net->output, "private-key"));
    assert(!strstr(a->cfg.channels, "#pending"));
    for (size_t i = 0; i < APP_QUEUE; ++i) assert(app_outgoing(a, 0, "PING :queue%zu", i));
    assert(!app_command(a, "/join #full never-queued-key", &plan));
    assert(!strstr(a->cfg.channels, "#full") && !logs_contain(a, "never-queued-key"));
    app_disconnect(a, "reconnect without retained keys");
    assert(app_connect(a)); app_network(a, 0); register_plain(a);
    assert(strstr(a->net->output, "JOIN #keyed\r\n"));
    assert(!strstr(a->net->output, "KKKK") && !strstr(a->net->output, "private-key"));
    destroy(a);
}

static void local_ignores(void)
{
    App *a = fresh(0);
    SendPlan plan;
    Config desired, effective;
    char text[256], error[256];
    size_t before, mentions, views, server_lines, room;
    int calls, reads, saves;
    app_disconnect(a, "local ignore commands");
    calls = network_calls; reads = secret_reads; saves = config_saves;
    assert(app_command(a, "/ignore Alice", &plan) == 1 && a->cfg.ignore_count == 1);
    assert(app_command(a, "/ignore add ALICE", &plan) == 1 && a->cfg.ignore_count == 1);
    assert(app_command(a, "/ignore add {Nick}", &plan) == 1);
    assert(app_command(a, "/unignore [NICK]", &plan) == 1 && a->cfg.ignore_count == 1);
    assert(app_command(a, "/ignore Sp?m*!U[ser]@Host[one]", &plan) == 1);
    assert(app_command(a, "/ignore sp?M*!u[SeR]@hOSt[ONE]", &plan) == 1 && a->cfg.ignore_count == 2);
    assert(app_command(a, "/ignore Sp?m*!U[ser]@Host{one}", &plan) == 1 && a->cfg.ignore_count == 3);
    assert(!app_command(a, "/unignore Sp?m*!U{ser}@Host[one]", &plan));
    assert(app_command(a, "/ignore remove SP?M*!U[SER]@HOST[ONE]", &plan) == 1 && a->cfg.ignore_count == 2);
    assert(app_command(a, "/ignore clear", &plan) == 1 && !a->cfg.ignore_count);
    assert(app_command(a, "/ignore add clear", &plan) == 1);
    assert(app_command(a, "/unignore clear", &plan) == 1 && !a->cfg.ignore_count);
    assert(!app_command(a, "/ignore bad!mask", &plan));
    assert(!app_command(a, "/ignore *", &plan));
    assert(!app_command(a, "/ignore list extra", &plan));
    assert(!app_command(a, "/unignore", &plan));
    for (size_t i = 0; i < CONFIG_IGNORE_MAX; ++i) {
        snprintf(text, sizeof(text), "/ignore User%zu", i);
        assert(app_command(a, text, &plan) == 1);
    }
    assert(a->cfg.ignore_count == CONFIG_IGNORE_MAX);
    assert(!app_command(a, "/ignore TooMany", &plan));
    assert(app_command(a, "/ignore list", &plan) == 1 && a->current == 0);
    assert(logs_contain(a, "Ignore masks: 32/32"));
    assert(app_command(a, "/ignore clear", &plan) == 1);
    assert(app_command(a, "/ignore Spammer", &plan) == 1);
    assert(network_calls == calls && secret_reads == reads && config_saves == saves);
    assert(app_command(a, "/save", &plan) == 1);
    assert(saved_config.ignore_count == 1 && !strcmp(saved_config.ignores[0], "Spammer"));
    assert(app_connect(a)); app_network(a, 0); register_plain(a);
    feed(a, ":tester!u@h JOIN #test\r\n");
    room = channel(a);
    strcpy(a->cfg.words[0], "watched"); a->cfg.word_count = 1;
    before = a->log_bytes; mentions = a->buffers[1].line_count; views = a->buffer_count;
    feed(a, ":Spammer!u@h PRIVMSG tester :tester watched ignored-pm\r\n"
            ":Spammer!u@h NOTICE tester :tester watched ignored-notice\r\n"
            ":Spammer!u@h PRIVMSG #test :tester watched ignored-channel\r\n"
            ":Spammer!u@h PRIVMSG tester :\001VERSION\001\r\n");
    assert(a->log_bytes == before && a->buffers[1].line_count == mentions && a->buffer_count == views);
    assert(app_buffer(a, "Spammer", B_QUERY, 0) == SIZE_MAX);
    feed(a, ":Spammer!u@h JOIN #test\r\n:server MODE #test +v Spammer\r\n");
    size_t user = user_index(&a->buffers[room], "Spammer");
    assert(user != SIZE_MAX && a->buffers[room].users[user].modes);
    feed(a, ":Spammer!u@h NICK :NewName\r\n");
    assert(user_index(&a->buffers[room], "Spammer") == SIZE_MAX && user_index(&a->buffers[room], "NewName") != SIZE_MAX);
    feed(a, ":NewName!u@h QUIT :leaving\r\n");
    assert(user_index(&a->buffers[room], "NewName") == SIZE_MAX);
    output_clear(a);
    feed(a, "PING :ignore-does-not-block-protocol\r\n:server NOTICE tester :server-notice-kept\r\n"
            ":server 372 tester :server-numeric-kept\r\n:Spammer NOTICE tester :bare-source-kept\r\n");
    assert(strstr(a->net->output, "PONG :ignore-does-not-block-protocol\r\n"));
    assert(logs_contain(a, "server-notice-kept") && logs_contain(a, "server-numeric-kept") && logs_contain(a, "bare-source-kept"));
    assert(app_command(a, "/ignore clear", &plan) == 1);
    assert(app_command(a, "/ignore Sp?m*!~guest@*.blocked", &plan) == 1);
    before = a->log_bytes; views = a->buffer_count;
    feed(a, ":Spam42!~GUEST@irc.BLOCKED PRIVMSG tester :tester watched masked\r\n");
    assert(a->log_bytes == before && a->buffer_count == views);
    feed(a, ":Spam42!~guest@other.example PRIVMSG tester :tester watched allowed\r\n");
    assert(app_buffer(a, "Spam42", B_QUERY, 0) != SIZE_MAX && logs_contain(a, "allowed"));
    assert(app_command(a, "/ignore add Incomplete", &plan) == 1);
    before = a->log_bytes;
    feed(a, ":Incomplete@host.example PRIVMSG tester :tester hidden by nick\r\n");
    assert(a->log_bytes == before);
    app_config_effective(a, &desired);
    desired.ignore_count = 1; strcpy(desired.ignores[0], "Later");
    assert(app_config_apply(a, &desired, 0, error, sizeof(error)) == 1);
    assert(a->cfg.ignore_count == 1 && !strcmp(a->cfg.ignores[0], "Later"));
    app_config_effective(a, &effective);
    assert(effective.ignore_count == 1 && !strcmp(effective.ignores[0], "Later"));
    before = a->log_bytes; server_lines = a->buffers[0].line_count;
    feed(a, ":Later!u@h PRIVMSG tester :tester ignored after Apply\r\n");
    assert(a->log_bytes == before && a->buffers[0].line_count == server_lines);
    assert(app_command(a, "/unignore Later", &plan) == 1);
    feed(a, ":Later!u@h PRIVMSG tester :visible after removal\r\n");
    assert(logs_contain(a, "visible after removal"));
    destroy(a);
}

static void offline_away_key_and_ignore(void)
{
    int calls = network_calls, reads = secret_reads, saves = config_saves;
    SendPlan plan;
    offline_guard = 1;
    App *a = fresh_offline("#offline-a");
    pump_offline(a);
    assert(app_command(a, "/away simulated absence", &plan) == 1 && !a->away);
    pump_offline(a);
    assert(a->away && strstr(a->status, "confirmed") && strstr(app_connection_label(a), "away"));
    assert(app_command(a, "/back", &plan) == 1);
    pump_offline(a);
    assert(!a->away && strstr(a->status, "cleared"));
    assert(app_command(a, "/join #keyed-test ephemeral-key", &plan) == 1);
    pump_offline(a);
    size_t joined = app_buffer(a, "#keyed-test", B_CHANNEL, 0);
    assert(joined != SIZE_MAX && a->buffers[joined].joined);
    assert(!strstr(a->cfg.channels, "ephemeral-key") && !strstr(a->test_channels, "ephemeral-key"));
    assert(!logs_contain(a, "ephemeral-key"));
    /* A fragmented command followed by another in the same write still
     * parses correctly when the simulator wipes consumed command storage. */
    const char *first = "JOIN #fragment another-key";
    const char *second = "\r\nAWAY :fragment test\r\n";
    assert(sim_write(a->sim, first, strlen(first)) == (ssize_t)strlen(first));
    assert(sim_write(a->sim, second, strlen(second)) == (ssize_t)strlen(second));
    pump_offline(a);
    assert(a->away && !logs_contain(a, "another-key"));
    assert(app_command(a, "/ignore Blocked", &plan) == 1);
    size_t mentions = a->buffers[1].line_count;
    assert(app_command(a, "/msg Blocked hello", &plan) == 2 && app_send_plan(a, &plan));
    pump_offline(a);
    size_t query = app_buffer(a, "Blocked", B_QUERY, 0);
    assert(query != SIZE_MAX && a->buffers[query].line_count == 1);
    assert(a->buffers[1].line_count == mentions);
    assert(!app_command(a, "/save", &plan));
    app_disconnect(a, "offline features complete");
    assert(!a->away);
    destroy(a);
    assert(network_calls == calls && secret_reads == reads && config_saves == saves);
    offline_guard = 0;
}

static void activity_and_visibility(void)
{
    App *a = fresh(0);
    SendPlan plan;
    size_t unread, mentions, room, query, other;
    register_plain(a);
    feed(a, ":tester!u@h JOIN #test\r\n");
    room = channel(a);
    assert(a->visible && a->current == room);
    for (size_t i = 0; i < a->buffer_count; ++i) app_clear_buffer(a, i);
    feed(a, ":alice!u@h PRIVMSG #test :tester, visible mention\r\n");
    assert(!a->buffers[room].unread && !a->buffers[room].unread_mentions);
    assert(a->buffers[1].unread == 1 && a->buffers[1].unread_mentions == 1);
    app_activity(a, &unread, &mentions);
    assert(!unread && !mentions);

    app_set_visible(a, 0);
    feed(a, ":alice!u@h PRIVMSG #test :ordinary hidden message\r\n"
            ":alice!u@h PRIVMSG #test :tester, hidden mention\r\n"
            ":bob!u@h PRIVMSG tester :private message\r\n");
    query = app_buffer(a, "bob", B_QUERY, 0);
    assert(query != SIZE_MAX);
    assert(a->buffers[room].unread == 2 && a->buffers[room].unread_mentions == 1);
    assert(a->buffers[query].unread == 1 && a->buffers[query].unread_mentions == 1);
    app_activity(a, &unread, &mentions);
    assert(unread == 3 && mentions == 2);
    /* A pending send may complete after switching to another session. Its
     * local echo and any own-nick echo are history, not unread activity. */
    assert(app_plan(a, "#test", "tester: my own message", 0, &plan));
    assert(app_send_plan(a, &plan));
    app_network(a, 0);
    feed(a, ":tester!u@h PRIVMSG #test :tester: echoed own message\r\n");
    app_help(a);
    app_activity(a, &unread, &mentions);
    assert(unread == 3 && mentions == 2);
    assert(!a->buffers[2].unread && !a->buffers[2].unread_mentions);

    /* Switching a covered or background App cannot prematurely mark a view
     * read; only revealing its main conversation does that. */
    app_switch(a, query);
    assert(a->buffers[query].unread == 1);
    app_set_visible(a, 1);
    assert(!a->buffers[query].unread && !a->buffers[query].unread_mentions);
    app_switch(a, 1);
    assert(!a->buffers[1].unread && !a->buffers[1].unread_mentions);
    app_activity(a, &unread, &mentions);
    assert(unread == 2 && mentions == 1);
    app_switch(a, room);
    app_activity(a, &unread, &mentions);
    assert(!unread && !mentions);

    app_set_visible(a, 0);
    a->buffers[room].unread = a->buffers[room].unread_mentions = INT_MAX - 1;
    feed(a, ":alice!u@h PRIVMSG #test :tester one\r\n"
            ":alice!u@h PRIVMSG #test :tester two\r\n");
    assert(a->buffers[room].unread == INT_MAX && a->buffers[room].unread_mentions == INT_MAX);
    a->buffers[0].unread = a->buffers[0].unread_mentions = INT_MAX;
    a->buffers[query].unread = a->buffers[query].unread_mentions = INT_MAX;
    a->buffers[1].unread = a->buffers[1].unread_mentions = INT_MAX;
    a->buffers[2].unread = a->buffers[2].unread_mentions = INT_MAX;
    uint64_t expected = (uint64_t)INT_MAX * 3u;
    size_t total = expected > SIZE_MAX ? SIZE_MAX : (size_t)expected;
    app_activity(a, &unread, &mentions);
    assert(unread == total && mentions == total);
    app_activity(a, NULL, NULL);
    for (size_t i = 0; i < a->buffer_count; ++i) app_clear_buffer(a, i);

    /* Compaction preserves the selected hidden view's counters and does not
     * mark the server read when closing that selected view returns to it. */
    other = app_buffer(a, "carol", B_QUERY, 1);
    assert(other == query + 1);
    a->buffers[0].unread = 4; a->buffers[0].unread_mentions = 2;
    a->buffers[other].unread = 5; a->buffers[other].unread_mentions = 3;
    app_switch(a, other);
    assert(app_close_view(a, query));
    assert(a->current == other - 1 && !strcmp(a->buffers[a->current].name, "carol"));
    assert(a->buffers[a->current].unread == 5 && a->buffers[a->current].unread_mentions == 3);
    assert(a->buffers[0].unread == 5 && a->buffers[0].unread_mentions == 2);
    assert(app_close_view(a, a->current));
    assert(a->current == 0 && a->buffers[0].unread == 6 && a->buffers[0].unread_mentions == 2);
    app_set_visible(a, 1);
    app_activity(a, &unread, &mentions);
    assert(!unread && !mentions);
    destroy(a);
}

static void independent_connections(void)
{
    App *first = fresh(0), *second = fresh(0);
    SendPlan one, two;
    size_t unread, mentions, first_room, second_room;
    register_plain(first); register_plain(second);
    feed(first, ":tester!first@first.example JOIN #test\r\n");
    feed(second, ":tester!second@second.example JOIN #test\r\n");
    first_room = channel(first); second_room = channel(second);
    for (size_t i = 0; i < first->buffer_count; ++i) app_clear_buffer(first, i);
    for (size_t i = 0; i < second->buffer_count; ++i) app_clear_buffer(second, i);
    app_set_visible(second, 0);
    feed(second, ":alice!u@h PRIVMSG #test :tester, second connection only\r\n");
    app_activity(first, &unread, &mentions);
    assert(!unread && !mentions && !logs_contain(first, "second connection only"));
    app_activity(second, &unread, &mentions);
    assert(unread == 1 && mentions == 1);
    assert(!strcmp(first->own_host, "first.example") && !strcmp(second->own_host, "second.example"));
    assert(app_command(second, "/ignore bob", &two) == 1);
    feed(first, ":bob!u@h PRIVMSG tester :first connection query\r\n");
    feed(second, ":bob!u@h PRIVMSG tester :second connection ignored\r\n");
    assert(app_buffer(first, "bob", B_QUERY, 0) != SIZE_MAX);
    assert(app_buffer(second, "bob", B_QUERY, 0) == SIZE_MAX);
    assert(!first->cfg.ignore_count && second->cfg.ignore_count == 1);
    assert(!logs_contain(second, "first connection query"));

    output_clear(first); output_clear(second);
    assert(app_plan(first, "#test", "wire-first-only", 0, &one) && app_send_plan(first, &one));
    assert(app_plan(second, "#test", "wire-second-only", 0, &two) && app_send_plan(second, &two));
    first->net->block = 1;
    app_network(first, 0);
    assert(first->inflight && first->net->retry && !first->net->output_len);
    app_network(second, 0);
    assert(strstr(second->net->output, "PRIVMSG #test :wire-second-only\r\n"));
    assert(!strstr(second->net->output, "wire-first-only"));
    assert(second->buffers[second_room].unread == 1 && second->buffers[second_room].unread_mentions == 1);
    feed(second, "PING :while-other-session-blocked\r\n");
    assert(strstr(second->net->output, "PONG :while-other-session-blocked\r\n"));
    first->net->block = 0;
    app_network(first, 0);
    assert(!first->inflight && strstr(first->net->output, "PRIVMSG #test :wire-first-only\r\n"));
    assert(!strstr(first->net->output, "wire-second-only") && !first->buffers[first_room].unread);
    int calls = network_calls;
    assert(app_fd(first) == -1 && network_calls == calls + 1); /* fixture descriptor */
    app_disconnect(second, "disconnect only the second connection");
    calls = network_calls;
    assert(app_fd(second) == -1 && !app_events(second) && app_timeout(second) == 1000);
    assert(network_calls == calls && first->registered && first->net);
    feed(first, ":alice!u@h PRIVMSG #test :first still receives\r\n");
    assert(logs_contain(first, "first still receives"));
    assert(!logs_contain(second, "first still receives"));
    destroy(second); destroy(first);
}

typedef struct SaveHook {
    int calls, fail;
    Config saved;
    const App *app, *before;
} SaveHook;

static int save_hook(void *context, const Config *cfg, char *error, size_t cap)
{
    SaveHook *hook = context;
    assert(!offline_guard);
    if (hook->before) assert(!memcmp(hook->app, hook->before, sizeof(App)));
    ++hook->calls;
    hook->saved = *cfg;
    if (hook->fail) {
        if (error && cap) snprintf(error, cap, "fixture session save failure");
        return -1;
    }
    return 1;
}

static void session_save_hook(void)
{
    App *a = fresh(0), before;
    Config desired;
    SendPlan plan;
    SaveHook hook = { 0 };
    char error[256];
    int saves = config_saves;
    a->save_config = save_hook; a->save_context = &hook;
    assert(!a->config_saved);
    assert(app_command(a, "/save", &plan) == 1 && hook.calls == 1);
    assert(a->config_saved);
    assert(!memcmp(&hook.saved, &a->cfg, sizeof(Config)) && config_saves == saves);
    app_config_effective(a, &desired);
    desired.mouse = !desired.mouse;
    desired.history_lines = 70;
    strcpy(desired.server, "future-session.example");
    before = *a; hook.app = a; hook.before = &before; hook.fail = 1;
    assert(!app_config_apply(a, &desired, 1, error, sizeof(error)));
    assert(hook.calls == 2 && strstr(error, "session save failure"));
    assert(!memcmp(a, &before, sizeof(App)) && config_saves == saves);
    hook.fail = 0;
    assert(app_config_apply(a, &desired, 1, error, sizeof(error)));
    hook.before = NULL;
    assert(hook.calls == 3 && !strcmp(hook.saved.server, "future-session.example"));
    assert(a->cfg.mouse == desired.mouse && a->cfg.history_lines == 70);
    assert(!strcmp(a->cfg.server, "localhost") && config_saves == saves);
    assert(app_command(a, "/save", &plan) == 1 && hook.calls == 4);
    assert(!strcmp(hook.saved.server, "future-session.example") && config_saves == saves);
    destroy(a);

    offline_guard = 1;
    a = fresh_offline("#offline-a");
    a->save_config = save_hook; a->save_context = &hook;
    app_config_effective(a, &desired);
    assert(!app_command(a, "/save", &plan));
    assert(!app_config_apply(a, &desired, 1, error, sizeof(error)));
    assert(hook.calls == 4 && config_saves == saves);
    destroy(a);
    offline_guard = 0;
}

typedef struct OfferHook { int calls; char nick[64], body[IRC_BODY_MAX + 1]; } OfferHook;
static int offer_hook(void *context, const char *nick, const char *body)
{
    OfferHook *hook = context;
    ++hook->calls;
    snprintf(hook->nick, sizeof(hook->nick), "%s", nick);
    snprintf(hook->body, sizeof(hook->body), "%s", body);
    return 1;
}

static void direct_dcc_callback(void)
{
    App *a = fresh(0);
    SendPlan plan;
    OfferHook hook = { 0 };
    register_plain(a);
    a->dcc_offer = offer_hook; a->dcc_context = &hook;
    size_t views = a->buffer_count, mentions = a->buffers[1].line_count;
    output_clear(a);
    feed(a, ":sender!u@h PRIVMSG tester :\001DCC SEND file 2130706433 9000 1\001\r\n");
    assert(hook.calls == 1 && !strcmp(hook.nick, "sender"));
    assert(!strcmp(hook.body, "DCC SEND file 2130706433 9000 1"));
    assert(a->buffer_count == views && a->buffers[1].line_count == mentions && !a->net->output_len);
    assert(app_command(a, "/ignore sender", &plan) == 1);
    feed(a, ":sender!u@h PRIVMSG tester :\001DCC SEND ignored 2130706433 9000 1\001\r\n"
            ":server PRIVMSG tester :\001DCC SEND server 2130706433 9000 1\001\r\n"
            ":peer!u@h NOTICE tester :\001DCC SEND notice 2130706433 9000 1\001\r\n"
            ":peer!u@h PRIVMSG #test :\001DCC SEND channel 2130706433 9000 1\001\r\n"
            ":peer!u@h PRIVMSG tester :\001DCC SEND unclosed 2130706433 9000 1\r\n"
            ":peer!u@h PRIVMSG tester :\001DCC SEND embedded\001 2130706433 9000 1\001\r\n"
            ":peer!u@h PRIVMSG tester :\001VERSION\001\r\n");
    assert(hook.calls == 1 && !a->net->output_len);
    destroy(a);
}

static void cancellable_dcc_offers(void)
{
    static const char first[] = "DCC SEND first.bin 2130706433 9000 8";
    static const char second[] = "DCC SEND second.bin 2130706433 9001 8";
    App *a = fresh(0);
    SendPlan plan;
    assert(!app_queue_dcc_offer(a, 10, "peer", first));
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    output_clear(a);
    assert(app_plan(a, "#test", "before-offer", 0, &plan) && app_send_plan(a, &plan));
    assert(app_queue_dcc_offer(a, 10, "peer", first));
    assert(app_plan(a, "#test", "between-offers", 0, &plan) && app_send_plan(a, &plan));
    assert(app_queue_dcc_offer(a, 20, "peer", second));
    assert(app_plan(a, "#test", "after-offers", 0, &plan) && app_send_plan(a, &plan));
    assert(!app_queue_dcc_offer(a, 20, "peer", second));
    assert(a->chat_count == 5 && app_cancel_dcc_offer(a, 10) == 1 && a->chat_count == 4);
    assert(app_cancel_dcc_offer(a, 10) == 0 && app_cancel_dcc_offer(a, 0) == 0);
    for (size_t i = 0; i < 4; ++i) { app_network(a, 0); clock_ms += 1200; }
    const char *before = strstr(a->net->output, "before-offer");
    const char *between = strstr(a->net->output, "between-offers");
    const char *offer = strstr(a->net->output, second);
    const char *after = strstr(a->net->output, "after-offers");
    assert(before && between && offer && after && before < between && between < offer && offer < after);
    assert(!strstr(a->net->output, first) && !a->chat_count);
    assert(!app_queue_dcc_offer(a, 0, "peer", first));
    assert(!app_queue_dcc_offer(a, 30, "bad nick", first));
    assert(!app_queue_dcc_offer(a, 30, "peer", "DCC SEND bad\001 2130706433 9000 8"));

    /* TLS can retain a pointer after a zero-byte retry. Neither cancellation
     * nor removing an offer behind it may change that buffer or retry size. */
    output_clear(a);
    assert(app_queue_dcc_offer(a, 30, "peer", first));
    assert(app_queue_dcc_offer(a, 31, "peer", second));
    a->net->block = 1;
    app_network(a, 0);
    assert(a->inflight && a->net->retry && app_cancel_dcc_offer(a, 30) == 2);
    assert(app_cancel_dcc_offer(a, 31) == 1 && !a->chat_count);
    assert(!app_queue_dcc_offer(a, 30, "peer", first));
    app_network(a, 0);
    a->net->block = 0;
    app_network(a, 0);
    assert(!a->inflight && strstr(a->net->output, first) && !strstr(a->net->output, second));

    /* An actual partially written IRC frame also finishes intact. */
    output_clear(a); clock_ms += 1200;
    assert(app_queue_dcc_offer(a, 40, "peer", second));
    a->net->write_limit = 7;
    app_network(a, 0);
    assert(a->inflight && a->net->output_len > 0 && a->net->output_len < strlen(second) &&
           app_cancel_dcc_offer(a, 40) == 2);
    a->net->write_limit = 0;
    app_network(a, 0);
    assert(!a->inflight && strstr(a->net->output, "PRIVMSG peer :\001DCC SEND second.bin 2130706433 9001 8\001\r\n"));
    destroy(a);
}

typedef struct StreamHook {
    unsigned source_lines, mention_copies, statuses;
    int source_mention;
    char last_source[1024], last_status[512];
} StreamHook;

static void stream_line(void *context, const App *a, size_t index,
                        const char *line, int mention, int mine)
{
    (void)mine;
    StreamHook *hook = context;
    assert(index < a->buffer_count);
    for (const unsigned char *p = (const unsigned char *)line; *p; ++p)
        assert(*p >= 32 && *p != 127);
    if (a->buffers[index].kind == B_MENTIONS) {
        ++hook->mention_copies;
        assert(mention);
    } else {
        ++hook->source_lines;
        hook->source_mention = mention;
        snprintf(hook->last_source, sizeof hook->last_source, "%s", line);
    }
}

static void stream_status(void *context, const App *a, const char *text)
{
    StreamHook *hook = context;
    assert(!strcmp(text, a->status));
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        assert(*p >= 32 && *p != 127);
    ++hook->statuses;
    snprintf(hook->last_status, sizeof hook->last_status, "%s", text);
}

static void stream_output_contract(void)
{
    App *a = fresh(0);
    StreamHook hook = { 0 };
    size_t counts[APP_BUFFERS], bytes, view;
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    view = channel(a);
    bytes = a->log_bytes;
    for (size_t i = 0; i < a->buffer_count; ++i) counts[i] = a->buffers[i].line_count;
    a->line_event = stream_line;
    a->status_event = stream_status;
    a->event_context = &hook;
    a->stream_only = 1;
    app_set_visible(a, 0);
    feed(a, ":alice!u@host PRIVMSG #test :te\00304st\002er\017: hello\033[31m\r\n");
    assert(hook.source_lines == 1 && hook.mention_copies == 1 && hook.source_mention);
    assert(strstr(hook.last_source, "[#test] <alice> tester: hello"));
    assert(a->buffers[view].unread == 1 && a->buffers[view].unread_mentions == 1);
    assert(a->log_bytes == bytes);
    for (size_t i = 0; i < a->buffer_count; ++i) assert(a->buffers[i].line_count == counts[i]);
    app_status(a, "a status with %s", "\033[2Jterminal controls\007");
    assert(hook.statuses == 1 && strstr(hook.last_status, "terminal controls"));
    output_clear(a);
    feed(a, "PING :stream-still-alive\r\n");
    assert(strstr(a->net->output, "PONG :stream-still-alive"));
    for (size_t i = 0; i < 10000; ++i) app_log(a, view, "bounded stream record %zu", i);
    assert(hook.source_lines == 10001 && a->log_bytes == bytes);
    assert(a->buffers[view].line_count == counts[view]);
    app_set_visible(a, 1);
    assert(!a->buffers[view].unread && !a->buffers[view].unread_mentions);
    a->stream_only = 0;
    app_log(a, view, "ordinary retained history still works");
    assert(hook.source_lines == 10002 && a->log_bytes > bytes);
    assert(a->buffers[view].line_count == counts[view] + 1);
    destroy(a);
}

static void explicit_connection_settings_and_errors(void)
{
    App *a = fresh(0);
    Config desired;
    SendPlan old_plan, new_plan;
    char error[512];
    int calls;
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    assert(app_plan(a, "#test", "old connection draft", 0, &old_plan));
    app_config_effective(a, &desired);
    strcpy(desired.server, "replacement.example.test");
    desired.port = 6697;
    assert(app_config_apply(a, &desired, 0, error, sizeof error));
    calls = network_calls;
    assert(!app_connect(a) && !strcmp(a->cfg.server, "localhost"));
    assert(network_calls == calls && a->config_pending_mask);
    app_disconnect(a, "explicit reconnect with edited settings");
    assert(app_connect(a));
    assert(!strcmp(opened_host, desired.server) && opened_port == 6697 && !opened_tls);
    assert(!a->config_pending_mask && a->connection_id != old_plan.connection_id);
    assert(!a->cfg.password[0] && !a->cfg.server_password[0]);
    assert(logs_contain(a, "Connection encryption: OFF (unencrypted IRC; TLS disabled)"));
    assert(!logs_contain(a, "plaintext password"));
    assert(logs_contain(a, "Port 6697 normally expects TLS"));
    app_network(a, 0);
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    assert(!app_send_plan(a, &old_plan) && !a->chat_count && strstr(a->status, "Connection changed"));
    assert(app_plan(a, "#test", "fresh connection draft", 0, &new_plan));
    assert(app_send_plan(a, &new_plan) && a->chat_count == 1);
    app_disconnect(a, "field diagnostics");
    calls = network_calls;
    a->cfg.server[0] = 0;
    assert(!app_connect(a) && strstr(a->status, "server is empty"));
    strcpy(a->cfg.server, "localhost"); a->cfg.nick[0] = 0;
    assert(!app_connect(a) && strstr(a->status, "nick is empty"));
    strcpy(a->cfg.nick, "tester"); a->cfg.username[0] = 0;
    assert(!app_connect(a) && strstr(a->status, "username is empty"));
    strcpy(a->cfg.username, "tester"); strcpy(a->cfg.channels, "#one,,#two");
    assert(!app_connect(a) && strstr(a->status, "comma-separated"));
    assert(network_calls == calls);
    assert(logs_contain(a, "Cannot connect: server is empty"));
    destroy(a);

    a = fresh(0);
    static const unsigned char alert[] = { 21, 3, 3, 0, 2, 2, 10 };
    feed_bytes(a, alert, sizeof alert);
    assert(!a->net && strstr(a->status, "unencrypted IRC connection") && strstr(a->status, "TLS record"));
    assert(strstr(a->status, "Transport and Port"));
    destroy(a);
    a = fresh(1);
    feed_bytes(a, "invalid\0text", 12);
    assert(!a->net && strstr(a->status, "inside verified TLS") && strstr(a->status, "IRC service"));
    destroy(a);
}

static void mention_source_identity(void)
{
    App *a = fresh(0);
    Config desired;
    char error[256];
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    size_t room = channel(a);
    feed(a, ":alice!u@host PRIVMSG #test :tester: channel mention\r\n");
    assert(a->buffers[1].line_count == 1 && app_mention_view(a, 0) == room);
    feed(a, ":alice!u@host PRIVMSG tester :private mention\r\n");
    size_t query = app_buffer(a, "alice", B_QUERY, 0);
    assert(query != SIZE_MAX);
    uint64_t query_id = a->buffers[query].id;
    assert(app_mention_view(a, 1) == query);
    feed(a, ":alice!u@host NICK :renamed\r\n");
    assert(app_mention_view(a, 1) == query && !strcmp(a->buffers[query].name, "renamed"));
    assert(app_close_view(a, room));
    assert(app_mention_view(a, 0) == SIZE_MAX);
    query = app_mention_view(a, 1);
    assert(query != SIZE_MAX && a->buffers[query].id == query_id);
    assert(app_buffer(a, "#replacement", B_CHANNEL, 1) != SIZE_MAX);
    assert(app_mention_view(a, 0) == SIZE_MAX);
    app_config_effective(a, &desired); desired.history_lines = 70;
    assert(app_config_apply(a, &desired, 0, error, sizeof error));
    assert(app_mention_view(a, 1) == query);
    for (size_t i = 0; i < 100; ++i)
        feed(a, ":renamed!u@host PRIVMSG tester :retained origin after ring wrap\r\n");
    assert(a->buffers[1].line_count == 70);
    for (size_t i = 0; i < a->buffers[1].line_count; ++i)
        assert(app_mention_view(a, i) == query);
    app_config_effective(a, &desired); desired.history_lines = 50;
    assert(app_config_apply(a, &desired, 0, error, sizeof error));
    assert(a->buffers[1].line_count == 50);
    for (size_t i = 0; i < a->buffers[1].line_count; ++i)
        assert(app_mention_view(a, i) == query);
    assert(app_mention_view(a, 50) == SIZE_MAX);
    app_clear_buffer(a, 1);
    assert(app_mention_view(a, 0) == SIZE_MAX);
    assert(a->buffers[query].line_count);
    destroy(a);
}

static void closed_query_mentions_reopen_locally(void)
{
    App *a = fresh(0);
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    size_t room = channel(a);
    feed(a, ":alice!u@host PRIVMSG tester :first retained private line\r\n"
            ":alice!u@host PRIVMSG tester :second retained private line\r\n");
    size_t query = app_buffer(a, "alice", B_QUERY, 0);
    assert(query != SIZE_MAX && a->buffers[1].line_count == 2);
    uint64_t origin = a->buffers[query].id;
    feed(a, ":alice!u@host NICK :renamed\r\n");
    assert(!strcmp(a->buffers[query].name, "renamed"));
    /* Compaction must not make a mention identify the next array occupant. */
    assert(app_close_view(a, room));
    query = app_mention_view(a, 0);
    assert(query != SIZE_MAX && a->buffers[query].id == origin);
    assert(app_close_view(a, query));
    Buffer *mentions = &a->buffers[1];
    assert(mentions->line_queries && mentions->line_query_count == 2);
    assert(app_mention_origin(a, 0) == origin && app_mention_view(a, 0) == SIZE_MAX);
    size_t unrelated = app_buffer(a, "unrelated", B_QUERY, 1);
    assert(unrelated == query && a->buffers[unrelated].id != origin);
    size_t controls = a->control_count, chats = a->chat_count, written = a->net->output_len;
    size_t current = a->current;
    query = app_open_mention(a, origin);
    assert(query != SIZE_MAX && query != unrelated && !strcmp(a->buffers[query].name, "renamed"));
    uint64_t reopened = a->buffers[query].id;
    assert(reopened != origin && app_mention_origin(a, 0) == reopened && app_mention_view(a, 1) == query);
    assert(!mentions->line_queries && !mentions->line_query_count);
    assert(a->current == current && a->control_count == controls && a->chat_count == chats);
    assert(a->net->output_len == written && strstr(a->status, "No message was sent"));
    assert(app_open_mention(a, origin) == SIZE_MAX); /* A stale rendered identity is not rebound. */

    /* Reuse an already-open query with the same validated name, without
     * duplicating it or adopting the ID of an unrelated reused buffer slot. */
    assert(app_close_view(a, query));
    assert(mentions->line_query_count == 2);
    size_t existing = app_buffer(a, "RENAMED", B_QUERY, 1);
    assert(existing != SIZE_MAX && a->buffers[existing].id != reopened);
    size_t views = a->buffer_count;
    assert(app_open_mention(a, reopened) == existing && a->buffer_count == views);
    assert(app_mention_origin(a, 1) == a->buffers[existing].id);
    assert(!mentions->line_queries && !mentions->line_query_count);
    assert(a->control_count == controls && a->chat_count == chats && a->net->output_len == written);
    uint64_t existing_id = a->buffers[existing].id;
    assert(app_close_view(a, unrelated));
    existing = app_mention_view(a, 0);
    assert(existing != SIZE_MAX && a->buffers[existing].id == existing_id);
    assert(app_open_mention(a, existing_id) == existing);
    destroy(a);
}

static void closed_query_mentions_resize_and_evict(void)
{
    App *a = fresh(0);
    Config desired;
    char error[256];
    register_plain(a);
    app_config_effective(a, &desired); desired.history_lines = 70;
    assert(app_config_apply(a, &desired, 0, error, sizeof error));
    for (size_t i = 0; i < 20; ++i)
        feed(a, ":alice!u@host PRIVMSG tester :old query mention\r\n");
    size_t query = app_buffer(a, "alice", B_QUERY, 0);
    assert(query != SIZE_MAX);
    uint64_t origin = a->buffers[query].id;
    assert(app_close_view(a, query));
    Buffer *mentions = &a->buffers[1];
    assert(mentions->line_queries && mentions->line_query_count == 20);
    for (size_t i = 0; i < 40; ++i)
        feed(a, ":bob!u@host PRIVMSG tester :new live query mention\r\n");
    size_t bob = app_buffer(a, "bob", B_QUERY, 0);
    assert(bob != SIZE_MAX && mentions->line_count == 60);
    app_config_effective(a, &desired); desired.history_lines = 50;
    assert(app_config_apply(a, &desired, 0, error, sizeof error));
    assert(mentions->line_count == 50 && mentions->line_cap == 50);
    assert(mentions->line_queries && mentions->line_query_count == 10);
    for (size_t i = 0; i < 10; ++i) {
        assert(app_mention_origin(a, i) == origin && app_mention_view(a, i) == SIZE_MAX);
        assert(!strcmp(mentions->line_queries[(mentions->line_start + i) % mentions->line_cap], "alice"));
    }
    for (size_t i = 10; i < 50; ++i) assert(app_mention_view(a, i) == bob);
    app_config_effective(a, &desired); desired.history_lines = 80;
    assert(app_config_apply(a, &desired, 0, error, sizeof error));
    assert(mentions->line_count == 50 && mentions->line_query_count == 10);
    /* Fill, wrap, and evict all but one closed-query record. The allocation
     * disappears exactly when the last such record leaves the ring. */
    for (size_t i = 0; i < 39; ++i)
        feed(a, ":bob!u@host PRIVMSG tester :ring wrap replacement\r\n");
    assert(mentions->line_count == 80 && mentions->line_queries && mentions->line_query_count == 1);
    assert(app_mention_origin(a, 0) == origin);
    feed(a, ":bob!u@host PRIVMSG tester :last old query record evicted\r\n");
    assert(!mentions->line_queries && !mentions->line_query_count);
    for (size_t i = 0; i < mentions->line_count; ++i) assert(app_mention_view(a, i) == bob);
    size_t views = a->buffer_count, written = a->net->output_len;
    assert(app_open_mention(a, origin) == SIZE_MAX && app_open_mention(a, 0) == SIZE_MAX);
    assert(a->buffer_count == views && a->net->output_len == written);

    /* Shrinking can also discard every lazy record at once, while live
     * mention identities remain intact in the resized ring. */
    assert(app_close_view(a, bob));
    assert(mentions->line_queries && mentions->line_query_count == 80);
    for (size_t i = 0; i < 60; ++i)
        feed(a, ":carol!u@host PRIVMSG tester :live records after closed query\r\n");
    assert(mentions->line_query_count == 20);
    app_config_effective(a, &desired); desired.history_lines = 50;
    assert(app_config_apply(a, &desired, 0, error, sizeof error));
    assert(!mentions->line_queries && !mentions->line_query_count && mentions->line_count == 50);
    size_t carol = app_buffer(a, "carol", B_QUERY, 0);
    for (size_t i = 0; i < 50; ++i) assert(app_mention_view(a, i) == carol);
    destroy(a);
}

static void clear_mentions_preserves_conversations(void)
{
    App *a = fresh(0);
    SendPlan plan;
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    size_t room = channel(a);
    app_set_visible(a, 0);
    feed(a, ":alice!u@host PRIVMSG #test :tester: channel record\r\n"
            ":alice!u@host PRIVMSG tester :private record\r\n");
    size_t query = app_buffer(a, "alice", B_QUERY, 0);
    uint64_t origin = a->buffers[query].id;
    assert(app_close_view(a, query) && a->buffers[1].line_queries);
    assert(app_plan(a, "#test", "waiting chat survives clearing mentions", 0, &plan));
    assert(app_send_plan(a, &plan) && a->chat_count);
    a->buffers[room].unread = 7; a->buffers[room].unread_mentions = 3;
    size_t counts[APP_BUFFERS];
    int unread[APP_BUFFERS], highlighted[APP_BUFFERS];
    const char *last[APP_BUFFERS];
    for (size_t i = 0; i < a->buffer_count; ++i) {
        counts[i] = a->buffers[i].line_count;
        unread[i] = a->buffers[i].unread;
        highlighted[i] = a->buffers[i].unread_mentions;
        last[i] = counts[i] ? last_line(&a->buffers[i]) : NULL;
    }
    size_t current = a->current, views = a->buffer_count, chats = a->chat_count;
    size_t controls = a->control_count, written = a->net->output_len;
    OutLine *chat_head = a->chat_head, *control_head = a->control_head, *inflight = a->inflight;
    assert(app_clear_mentions(a) == counts[1]);
    assert(!a->buffers[1].line_count && !a->buffers[1].line_queries && !a->buffers[1].line_query_count);
    assert(app_clear_mentions(a) == 0 && app_open_mention(a, origin) == SIZE_MAX);
    assert(a->current == current && a->buffer_count == views);
    assert(a->chat_count == chats && a->chat_head == chat_head);
    assert(a->control_count == controls && a->control_head == control_head && a->inflight == inflight);
    assert(a->net->output_len == written);
    for (size_t i = 0; i < a->buffer_count; ++i) {
        if (i == 1) continue;
        assert(a->buffers[i].line_count == counts[i]);
        assert(a->buffers[i].unread == unread[i] && a->buffers[i].unread_mentions == highlighted[i]);
        if (last[i]) assert(last_line(&a->buffers[i]) == last[i]);
    }
    destroy(a);
}

static void set_automatic(App *a, const char *one, const char *two, int delay)
{
    config_wipe(&a->cfg.autocommands, sizeof a->cfg.autocommands);
    a->cfg.autocommands.enabled = 1;
    a->cfg.autocommands.delay_seconds = delay;
    a->cfg.autocommands.interval_seconds = 2;
    assert(config_autocommand_parse(one, NULL, NULL, 0) == 1);
    strcpy(a->cfg.autocommands.lines[0], one); a->cfg.autocommands.count = 1;
    if (two) {
        assert(config_autocommand_parse(two, NULL, NULL, 0) == 1);
        strcpy(a->cfg.autocommands.lines[1], two); ++a->cfg.autocommands.count;
    }
}

static App *fresh_automatic(const char *one, const char *two, int delay)
{
    App *a = fresh(0);
    app_disconnect(a, "Prepare verified TLS fixture");
    a->cfg.tls = 1; a->cfg.port = 6697;
    set_automatic(a, one, two, delay);
    assert(app_connect(a)); app_network(a, 0);
    return a;
}

static void automatic_timing_and_authentication(void)
{
    App *a = fresh_automatic("/msg NickServ IDENTIFY account private-startup-value",
                             "/msg ChanServ OP #test tester", 15);
    size_t buffers = a->buffer_count;
    int64_t began = clock_ms;
    app_network(a, 0);
    assert(!a->autocommand_active && !strstr(a->net->output, "private-startup-value"));
    register_plain(a);
    assert(a->autocommand_active && a->autocommand_due == began + 15000);
    buffers = a->buffer_count;
    clock_ms += 14999; app_network(a, 0);
    assert(!strstr(a->net->output, "private-startup-value") && app_timeout(a) == 1);
    ++clock_ms; assert(app_timeout(a) == 0); app_network(a, 0);
    assert(strstr(a->net->output, "PRIVMSG NickServ :IDENTIFY account private-startup-value\r\n"));
    assert(!strstr(a->net->output, "PRIVMSG ChanServ"));
    assert(!logs_contain(a, "private-startup-value") && !strstr(a->status, "private-startup-value"));
    assert(a->buffer_count == buffers && a->current == 0 && a->autocommand_next == 1);
    clock_ms += 1999; app_network(a, 0);
    assert(!strstr(a->net->output, "PRIVMSG ChanServ") && app_timeout(a) == 1);
    ++clock_ms; app_network(a, 0);
    assert(strstr(a->net->output, "PRIVMSG ChanServ :OP #test tester\r\n"));
    assert(!a->autocommand_active && !a->autocommand_waiting && !a->chat_count);
    output_clear(a); feed(a, ":server 001 tester :duplicate welcome\r\n");
    clock_ms += 60000; app_network(a, 0);
    assert(!a->net->output_len && !a->autocommand_active);
    destroy(a);

    /* Delayed wakes send only one command; the next interval starts on write completion. */
    a = fresh_automatic("/whois one", "/whois two", 15); register_plain(a); output_clear(a);
    clock_ms += 60000; app_network(a, 0); app_network(a, 0);
    assert(!strcmp(a->net->output, "WHOIS one\r\n") && a->autocommand_due == clock_ms + 2000);
    assert(app_autocommands_cancel(a) == 1 && !a->autocommand_active);
    destroy(a);

    strcpy(test_password, "secret");
    a = fresh(1); set_automatic(a, "/msg NickServ post-auth-private-value", NULL, 2);
    feed(a, ":server CAP tester LS :sasl\r\n:server CAP tester ACK :sasl\r\nAUTHENTICATE +\r\n");
    assert(a->auth_sent && !a->autocommand_active && !strstr(a->net->output, "post-auth-private-value"));
    feed(a, ":server 903 tester :authenticated\r\n");
    clock_ms += 2000; app_network(a, 0);
    assert(!a->registered && !a->autocommand_active && !strstr(a->net->output, "post-auth-private-value"));
    feed(a, ":server 001 tester :Welcome\r\n");
    assert(a->autocommand_due == clock_ms + 2000);
    clock_ms += 2000; app_network(a, 0);
    assert(strstr(a->net->output, "PRIVMSG NickServ :post-auth-private-value\r\n"));
    destroy(a);
    a = fresh(1); set_automatic(a, "/msg NickServ never-before-auth", NULL, 0);
    feed(a, ":server 001 tester :premature registration\r\n");
    assert(!a->net && !a->autocommand_active && !logs_contain(a, "never-before-auth")); destroy(a);

    offline_guard = 1;
    a = fresh_offline("#offline"); app_disconnect(a, "Restart offline fixture");
    set_automatic(a, "/msg NickServ never-in-test-mode", NULL, 0);
    assert(app_connect(a)); pump_offline(a);
    assert(a->registered && !a->autocommand_active && !logs_contain(a, "never-in-test-mode"));
    destroy(a); offline_guard = 0;
}

static void automatic_writes_and_cancellation(void)
{
    App *a = fresh_automatic("/msg NickServ private-retry-value", "/whois later", 1);
    SendPlan plan;
    register_plain(a); output_clear(a);
    a->net->block = 1; clock_ms += 1000; app_network(a, 0);
    assert(a->inflight && a->autocommand_waiting && a->autocommand_next == 1 && !a->net->output_len);
    assert(app_timeout(a) == 1000);
    clock_ms += 30000; app_network(a, 0);
    assert(a->autocommand_next == 1 && !a->net->output_len && app_timeout(a) == 1000);
    a->net->block = 0; a->net->write_limit = 7;
    for (size_t i = 0; i < 30 && a->inflight; ++i) app_network(a, 0);
    assert(!a->inflight && strstr(a->net->output, "PRIVMSG NickServ :private-retry-value\r\n"));
    assert(a->autocommand_due == clock_ms + 2000 && !strstr(a->net->output, "WHOIS later"));
    assert(!logs_contain(a, "private-retry-value"));
    a->net->write_limit = 0; clock_ms += 1999; app_network(a, 0);
    assert(!strstr(a->net->output, "WHOIS later"));
    ++clock_ms; app_network(a, 0); assert(strstr(a->net->output, "WHOIS later\r\n")); destroy(a);

    a = fresh_automatic("/msg NickServ private-inflight-value", "/whois cancelled", 1);
    register_plain(a); output_clear(a); a->net->block = 1;
    clock_ms += 1000; app_network(a, 0);
    watched_wipe = a->inflight; watched_secret = "private-inflight-value"; watched_wipe_calls = 0;
    assert(app_autocommands_cancel(a) == 1 && !a->autocommand_active && a->inflight && !watched_wipe_calls);
    clock_ms += 5000; app_network(a, 0);
    assert(!watched_wipe_calls && !a->net->output_len && app_timeout(a) == 1000);
    a->net->block = 0; app_network(a, 0);
    assert(watched_wipe_calls == 1 && strstr(a->net->output, "private-inflight-value"));
    watched_wipe = NULL; watched_secret = NULL;
    clock_ms += 5000; app_network(a, 0);
    assert(!strstr(a->net->output, "WHOIS cancelled") && !a->autocommand_waiting);
    destroy(a);

    /* A normal TLS write may own output while one automatic frame waits behind it. */
    a = fresh_automatic("/msg NickServ private-unsent-value", "/whois cancelled", 1);
    register_plain(a); output_clear(a);
    assert(app_outgoing(a, 0, "PING :blocking-control")); a->net->block = 1; app_network(a, 0);
    clock_ms += 1000; app_network(a, 0);
    assert(a->chat_count == 1 && a->autocommand_waiting && app_timeout(a) == 1000);
    watched_wipe = a->chat_head; watched_secret = "private-unsent-value"; watched_wipe_calls = 0;
    assert(app_command(a, "/cancel", &plan) == 1);
    assert(watched_wipe_calls == 1 && !a->chat_count && !a->autocommand_active && a->inflight);
    watched_wipe = NULL; watched_secret = NULL;
    a->net->block = 0; app_network(a, 0);
    assert(!strcmp(a->net->output, "PING :blocking-control\r\n"));
    assert(!logs_contain(a, "private-unsent-value")); destroy(a);
}

static void automatic_limits_focus_and_settings(void)
{
    char large[512], error[512];
    Config desired, effective;
    SendPlan plan;
    App *a = fresh_automatic("/msg n private-queue-limit-value", "/whois cancelled", 1);
    register_plain(a); output_clear(a);
    assert(app_outgoing(a, 0, "PING :block")); a->net->block = 1; app_network(a, 0);
    for (size_t i = 0; i < APP_QUEUE; ++i) assert(app_outgoing(a, 1, "PRIVMSG n :ordinary chat"));
    clock_ms += 1000; app_network(a, 0);
    assert(!a->autocommand_active && a->chat_count == APP_QUEUE && strstr(a->status, "Automatic command 1"));
    assert(!logs_contain(a, "private-queue-limit-value")); destroy(a);
    memset(large, 'x', sizeof large); memcpy(large, "/msg n budget-private-value", 27); large[511] = 0;
    a = fresh_automatic(large, "/whois cancelled", 0); register_plain(a);
    assert(!a->autocommand_active && !strstr(a->net->output, "budget-private-value"));
    assert(strstr(a->status, "one-message byte limit") && !logs_contain(a, "budget-private-value")); destroy(a);

    /* A queued automatic message is checked again after a smaller server limit. */
    a = fresh_automatic("/msg n a-message-that-will-not-fit-a-much-smaller-relay-budget", "/whois cancelled", 1);
    register_plain(a); output_clear(a); a->last_chat = clock_ms;
    clock_ms += 1000; app_network(a, 0); assert(a->chat_count == 1 && !a->inflight);
    feed(a, ":server 005 tester LINELEN=80 :supported\r\n");
    clock_ms += 200; app_network(a, 0);
    assert(!a->chat_count && !a->autocommand_active && !a->net->output_len && strstr(a->status, "limit changed"));
    destroy(a);

    a = fresh_automatic("/join #background private-channel-key", "/msg #background later", 1);
    register_plain(a); output_clear(a); clock_ms += 1000; app_network(a, 0);
    size_t room = app_buffer(a, "#background", B_CHANNEL, 0);
    assert(room != SIZE_MAX && a->buffers[room].background_join && a->current == 0);
    feed(a, ":tester!u@host JOIN #background\r\n");
    assert(a->current == 0 && a->buffers[room].joined && !a->buffers[room].background_join);
    assert(!logs_contain(a, "private-channel-key"));
    assert(app_command(a, "/part #background", &plan) == 1 && !a->autocommand_active);
    clock_ms += 5000; app_network(a, 0);
    assert(!strstr(a->net->output, "PRIVMSG #background")); destroy(a);
    a = fresh_automatic("/msg helper cancelled-by-close", "/whois cancelled", 5);
    register_plain(a); assert(app_command(a, "/query helper", &plan) == 1);
    assert(app_close_view(a, a->current) && !a->autocommand_active);
    clock_ms += 5000; app_network(a, 0);
    assert(!strstr(a->net->output, "cancelled-by-close")); destroy(a);

    a = fresh_automatic("/msg n old-private-value", NULL, 1); register_plain(a);
    app_config_effective(a, &desired); strcpy(desired.autocommands.lines[0], "/msg n staged-private-value");
    assert(app_config_apply(a, &desired, 0, error, sizeof error));
    assert(!strcmp(a->cfg.autocommands.lines[0], "/msg n old-private-value"));
    clock_ms += 1000; app_network(a, 0);
    assert(strstr(a->net->output, "old-private-value") && !strstr(a->net->output, "staged-private-value"));
    app_disconnect(a, "Reconnect fixture"); assert(!a->autocommand_active && !a->autocommand_waiting);
    assert(app_connect(a)); app_network(a, 0); register_plain(a); output_clear(a);
    clock_ms += 1000; app_network(a, 0);
    assert(strstr(a->net->output, "staged-private-value") && !strstr(a->net->output, "old-private-value"));
    app_config_effective(a, &desired); strcpy(desired.server, "other.invalid");
    assert(app_config_apply(a, &desired, 0, error, sizeof error));
    app_config_effective(a, &effective);
    assert(!effective.autocommands.count && !effective.autocommands.lines[0][0]);
    assert(a->cfg.autocommands.count == 1); /* Current connection's snapshot remains untouched. */
    destroy(a); config_wipe(&desired, sizeof desired); config_wipe(&effective, sizeof effective);

    App *first = fresh_automatic("/msg n first-profile-only", NULL, 1);
    App *second = fresh_automatic("/msg n second-profile-only", NULL, 3);
    register_plain(first); register_plain(second); output_clear(first); output_clear(second);
    clock_ms += 1000; app_network(first, 0); app_network(second, 0);
    assert(strstr(first->net->output, "first-profile-only") && !second->net->output_len);
    clock_ms += 2000; app_network(second, 0);
    assert(strstr(second->net->output, "second-profile-only") && !strstr(second->net->output, "first-profile-only"));
    destroy(first); destroy(second);
    puts("automatic commands: registration/SASL gate, offline exclusion, timing/no catch-up, blocked/partial TLS writes, cancellation/wiping, limits, background joins and staged isolated reconnects passed");
}

static void numbered_queries_and_queue_notices(void)
{
    App *a = fresh(0);
    SendPlan plan;
    assert(app_buffer(a, "#channel-first", B_CHANNEL, 1) == 3);
    assert(app_command(a, "/query Alice", &plan) == 1); size_t alice = a->current;
    assert(app_buffer(a, "#between-queries", B_CHANNEL, 1) != SIZE_MAX);
    assert(app_command(a, "/pm Bob", &plan) == 1); size_t bob = a->current;
    size_t views = a->buffer_count, written = a->net->output_len;
    assert(app_command(a, "/query 1", &plan) == 1 && a->current == alice);
    assert(app_command(a, "/pm 2", &plan) == 1 && a->current == bob);
    assert(app_command(a, "/query aLICE", &plan) == 1 && a->current == alice);
    const char *bad[] = {"/query", "/pm", "/query 0", "/query 3", "/pm -1", "/pm +1",
                         "/query 99999999999999999999999999999999999", "/query 1 extra"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i)
        assert(app_command(a, bad[i], &plan) == 0 && a->buffer_count == views && a->net->output_len == written);
    assert(app_close_view(a, alice));
    assert(app_command(a, "/query 1", &plan) == 1 && !strcmp(a->buffers[a->current].name, "Bob"));
    assert(app_command(a, "/query 2", &plan) == 0 && a->net->output_len == written);
    register_plain(a); feed(a, ":tester!u@host JOIN #test\r\n");
    a->cfg.queue_notices = 0;
    assert(app_plan(a, "Bob", "ordinary message", 0, &plan) && app_send_plan(a, &plan));
    assert(a->status_kind == APP_STATUS_QUEUE_NOTICE && !*app_status_text(a));
    a->cfg.queue_notices = 1;
    assert(strstr(app_status_text(a), "queued"));
    a->cfg.queue_notices = 0;
    assert(!app_plan(a, "Bob", "invalid\rmessage", 0, &plan));
    assert(a->status_kind == APP_STATUS_GENERAL && *app_status_text(a));
    assert(app_command(a, "/cancel", &plan) == 1 && *app_status_text(a));
    destroy(a);
    puts("query/queue preferences: separate numbered/name selection, strict invalid numbers, renumbering after close, hidden success notices and visible errors/cancellation passed");
}


static void network_channel_list(void)
{
    App *a = fresh(0);
    SendPlan plan;
    assert(!app_command(a, "/list *offtopic --min-users 10", &plan));
    register_plain(a);
    output_clear(a);
    assert(app_command(a, "/list *offtopic --min-users 10 --max-users 100 --name room --topic topic --sort users --desc --limit 2", &plan) == 6);
    assert(a->current == 0 && a->list.active && !a->list.complete);
    app_network(a, 0);
    assert(!strcmp(a->net->output, "LIST *offtopic\r\n"));
    output_clear(a);
    assert(!app_command(a, "/list", &plan));
    assert(strstr(a->status, "already in progress"));
    feed(a, ":server 321 tester Channel :Users Name\r\n");
    feed(a, ":server 322 tester #room1offtopic 5 :topic low\r\n");
    feed(a, ":server 322 tester #room2offtopic 20 :topic twenty\r\n");
    feed(a, ":server 322 tester #otherofftopic 30 :topic thirty\r\n");
    feed(a, ":server 322 tester #room3offtopic 100 :topic hundred\r\n");
    feed(a, ":server 322 tester #room4offtopic 101 :topic high\r\n");
    feed(a, ":server 322 tester #room5offtopic 50 :other subject\r\n");
    feed(a, ":server 322 tester #room6offtopic 40 :topic forty\r\n");
    assert(a->list.received == 7 && a->list.matched == 3 && a->list.count == 3);
    assert(app_list_visible_count(a) == 2);
    feed(a, ":server 323 tester :End of LIST\r\n");
    assert(!a->list.active && a->list.complete);
    assert(a->list.entries[0].users == 100 && a->list.entries[1].users == 40 && a->list.entries[2].users == 20);
    assert(strstr(last_line(&a->buffers[0]), "3 matched"));
    output_clear(a);
    assert(app_command(a, "/list show", &plan) == 6 && a->net->output_len == 0);
    assert(app_command(a, "/list show --sort channel --asc --limit 1", &plan) == 6);
    assert(a->net->output_len == 0 && a->list.sort == APP_LIST_SORT_CHANNEL && !a->list.descending);
    assert(a->list.display_limit == 1 && app_list_visible_count(a) == 1);
    assert(!strcmp(a->list.entries[0].channel, "#room2offtopic"));
    assert(app_command(a, "/list show --sort users --desc --limit 0", &plan) == 6);
    assert(a->list.sort == APP_LIST_SORT_USERS && a->list.descending && a->list.display_limit == 0);
    assert(a->list.entries[0].users == 100);

    const char *invalid[] = {
        "/list a b", "/list :all", "/list --min-users", "/list --min-users nope",
        "/list --max-users 4 --min-users 5", "/list --sort bogus", "/list --limit 9000",
        "/list --unknown 1", "/list show --min-users 2", "/list show --sort bogus",
        "/list show --limit 9000", "/list x\nQUIT", "/list x\rQUIT", "/list x\t"
    };
    output_clear(a);
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        size_t queued = a->control_count;
        assert(!app_command(a, invalid[i], &plan));
        assert(a->control_count == queued && a->net->output_len == 0);
    }
    char long_mask[230];
    memcpy(long_mask, "/list ", 6); memset(long_mask + 6, '*', 201); long_mask[207] = 0;
    assert(!app_command(a, long_mask, &plan));
    for (size_t i = 0; i < APP_QUEUE; ++i) assert(app_outgoing(a, 0, "PING :full%zu", i));
    assert(!app_command(a, "/list *offtopic", &plan) && a->control_count == APP_QUEUE);
    assert(strstr(a->status, "queue is full"));
    assert(a->list.count == 3 && a->list.complete && !a->list.active);
    assert(app_command(a, "/list clear", &plan) == 1 && !a->list.count && !a->list.complete);
    destroy(a);
    puts("network LIST: structured cache, client-side user/name/topic filters, display limit, sorting, cached show re-sort/re-limit, clear, exact server mask, invalid grammar, failed-refresh preservation and queue saturation passed");
}

static void reconnect_scheduler(void)
{
    App *a = fresh(0);
    assert(!a->reconnect_enabled);
    a->net->eof = 1;
    app_network(a, POLLIN);
    assert(!a->net && !a->reconnect_at);
    int opens = network_opens;
    clock_ms += 600000;
    app_network(a, 0);
    assert(network_opens == opens);
    app_set_reconnect(a, 1);
    app_network(a, 0);
    assert(network_opens == opens); /* Opt-in alone does not connect. */
    destroy(a);

    a = fresh(0);
    register_plain(a);
    app_set_reconnect(a, 1);
    assert(app_outgoing(a, 1, "PRIVMSG #test :do-not-replay"));
    a->net->block = 1;
    a->net->eof = 1;
    app_network(a, POLLIN);
    assert(!a->net && a->reconnect_at == clock_ms + 5000);
    assert(!a->chat_count && !a->control_count && !a->inflight);
    assert(strstr(app_connection_label(a), "waiting"));
    opens = network_opens;
    clock_ms += 4999;
    assert(app_timeout(a) == 1);
    app_network(a, 0);
    assert(!a->net && network_opens == opens);
    ++clock_ms;
    assert(app_timeout(a) == 0);
    app_network(a, 0);
    assert(a->net && network_opens == opens + 1 && !a->reconnect_at);
    app_network(a, 0);
    register_plain(a);
    assert(!strstr(a->net->output, "do-not-replay"));

    const unsigned delays[] = {10, 20, 40, 80, 160, 300, 300};
    for (size_t i = 0; i < sizeof delays / sizeof delays[0]; ++i) {
        a->net->failed = a->net->retryable = 1;
        app_network(a, 0);
        assert(!a->net && a->reconnect_at == clock_ms + (int64_t)delays[i] * 1000);
        clock_ms = a->reconnect_at;
        app_network(a, 0);
        assert(a->net && !a->reconnect_at);
    }
    app_network(a, 0);
    register_plain(a);
    clock_ms += 60000; /* A stable registered minute resets backoff. */
    a->net->eof = 1;
    app_network(a, POLLIN);
    assert(a->reconnect_at == clock_ms + 5000);
    app_disconnect(a, "Disconnected by user");
    assert(!a->reconnect_at);
    opens = network_opens;
    clock_ms += 600000;
    app_network(a, 0);
    assert(!a->net && network_opens == opens);
    assert(app_connect(a));
    a->net->failed = a->net->retryable = 1;
    app_network(a, 0);
    assert(a->reconnect_at == clock_ms + 5000);
    app_set_reconnect(a, 0);
    assert(!a->reconnect_at);
    destroy(a);

    a = fresh(0);
    app_set_reconnect(a, 1);
    a->net->failed = 1; /* A certificate/protocol failure is not transient. */
    a->net->retryable = 0;
    app_network(a, 0);
    assert(!a->net && !a->reconnect_at);
    destroy(a);

    const char *fatal[] = {
        ":server 432 tester tester :Erroneous nickname\r\n",
        ":server 463 tester :Host not permitted\r\n",
        ":server 464 tester :Password incorrect\r\n",
        ":server 465 tester :Access denied\r\n",
        "ERROR :Rejected before registration\r\n"
    };
    for (size_t i = 0; i < sizeof fatal / sizeof fatal[0]; ++i) {
        a = fresh(0);
        app_set_reconnect(a, 1);
        feed(a, fatal[i]);
        assert(!a->net && !a->reconnect_at);
        destroy(a);
    }
    a = fresh(1);
    app_set_reconnect(a, 1);
    feed(a, ":server 904 tester :Authentication failed\r\n");
    assert(!a->net && !a->reconnect_at);
    destroy(a);
    a = fresh(0);
    app_set_reconnect(a, 1);
    feed_bytes(a, "x\0y\r\n", 5);
    assert(!a->net && !a->reconnect_at);
    destroy(a);
    a = fresh(0);
    register_plain(a);
    app_set_reconnect(a, 1);
    feed(a, "ERROR :Server restarting\r\n");
    assert(!a->net && a->reconnect_at == clock_ms + 5000);
    destroy(a);

    a = fresh(0);
    app_set_reconnect(a, 1);
    Config future = a->cfg;
    strcpy(future.server, "next-explicit-connect.invalid");
    char error[256];
    assert(app_config_apply(a, &future, 0, error, sizeof error));
    assert(a->config_pending_mask);
    a->net->failed = a->net->retryable = 1;
    app_network(a, 0);
    clock_ms = a->reconnect_at;
    app_network(a, 0);
    assert(a->net && !strcmp(opened_host, "localhost") && a->config_pending_mask);
    app_disconnect(a, "Apply pending settings explicitly");
    assert(app_connect(a));
    assert(!strcmp(opened_host, "next-explicit-connect.invalid") && !a->config_pending_mask);
    destroy(a);

    a = fresh_offline("#offline");
    opens = network_opens;
    offline_guard = 1;
    app_set_reconnect(a, 1);
    assert(!a->reconnect_enabled);
    app_disconnect(a, "Offline remains offline");
    app_network(a, 0);
    assert(!a->net && !a->reconnect_at && network_opens == opens);
    offline_guard = 0;
    destroy(a);
    puts("reconnect: explicit opt-in, bounded deadlines/backoff, stable reset, queue clearing, manual cancellation, fatal holds, staged settings and offline isolation passed");
}

static void autojoin_pending_retry_and_authentication(void)
{
    App *a = fresh(0);
    register_plain(a);
    Config next;
    app_config_effective(a, &next);
    strcpy(next.channels, "#nastycode,#vipirc");
    char error[512];
    assert(app_config_apply(a, &next, 1, error, sizeof error));
    assert(!strcmp(saved_config.channels, "#nastycode,#vipirc"));
    assert(!strcmp(a->cfg.channels, "#test") && a->config_pending_mask);
    app_set_reconnect(a, 1);
    a->net->eof = 1;
    app_network(a, POLLIN);
    assert(!a->net && a->reconnect_at);
    clock_ms = a->reconnect_at;
    app_network(a, 0); app_network(a, 0);
    register_plain(a);
    assert(!strstr(a->net->output, "JOIN #nastycode") && !strstr(a->net->output, "JOIN #vipirc"));
    assert(a->config_pending_mask && !strcmp(a->cfg.channels, "#test"));
    app_disconnect(a, "explicit reconnect");
    assert(app_connect(a)); app_network(a, 0);
    feed(a, ":server CAP tester LS :\r\n:server 001 tester :Welcome\r\n");
    assert(strstr(a->net->output, "JOIN #nastycode\r\nJOIN #vipirc\r\n"));
    assert(!a->config_pending_mask);
    output_clear(a);
    feed(a, ":server 001 tester :Duplicate welcome\r\n");
    assert(!strstr(a->net->output, "JOIN "));
    destroy(a); config_wipe(&next, sizeof next);

    /* A visible configured list does not imply a completed registration.
     * GPG stays asynchronous, and joins wait for both SASL and numeric 001. */
    for (int success = 0; success <= 1; ++success) {
        credential_result = 0;
        a = fresh(1);
        strcpy(a->cfg.channels, "#nastycode,#vipirc");
        a->cfg.password_file[0] = 0;
        strcpy(a->cfg.password_gpg_file, "/private/autojoin-account.gpg");
        feed(a, ":server CAP tester LS :sasl\r\n:server CAP tester ACK :sasl\r\nAUTHENTICATE +\r\n");
        assert(a->credential && !a->registered && !strstr(a->net->output, "JOIN "));
        feed(a, "PING :autojoin-auth-wait\r\n");
        assert(strstr(a->net->output, "PONG :autojoin-auth-wait\r\n"));
        credential_result = success ? 1 : -1;
        app_network(a, 0);
        if (success) {
            assert(a->auth_sent && !a->registered && !strstr(a->net->output, "JOIN "));
            feed(a, ":server 903 tester :SASL succeeded\r\n");
            assert(a->authenticated && !a->registered && !strstr(a->net->output, "JOIN "));
            feed(a, ":server 001 tester :Welcome\r\n");
            assert(a->registered && strstr(a->net->output, "JOIN #nastycode\r\nJOIN #vipirc\r\n"));
        } else {
            assert(!a->net && !a->registered && !a->credential);
            assert(strstr(a->status, "GPG fixture failed"));
        }
        destroy(a);
    }
    credential_result = 0;
    puts("autojoin: saved pending list, unchanged automatic retry, explicit reconnect, duplicate welcome and SASL/GPG registration gate passed");
}



static void presence_departure_and_autoaway(void)
{
    SendPlan plan;
    App *a = fresh(0);
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    size_t view = app_buffer(a, "#test", B_CHANNEL, 0);
    assert(view != SIZE_MAX); app_switch(a, view);
    strcpy(a->cfg.part_message, "Client part");
    output_clear(a);
    assert(app_command(a, "/part", &plan) == 1);
    app_network(a, 0);
    assert(strstr(a->net->output, "PART #test :Client part\r\n"));
    destroy(a);

    a = fresh(0); register_plain(a); feed(a, ":tester!u@host JOIN #test\r\n");
    output_clear(a);
    assert(app_command(a, "/part #test Custom part reason", &plan) == 1);
    app_network(a, 0);
    assert(strstr(a->net->output, "PART #test :Custom part reason\r\n"));
    destroy(a);

    a = fresh(0); register_plain(a); feed(a, ":tester!u@host JOIN #test\r\n");
    strcpy(a->cfg.part_message, "Close default");
    output_clear(a);
    view = app_buffer(a, "#test", B_CHANNEL, 0);
    assert(view != SIZE_MAX && app_close_view(a, view) == 1);
    app_network(a, 0);
    assert(strstr(a->net->output, "PART #test :Close default\r\n"));
    destroy(a);

    a = fresh(0); register_plain(a); strcpy(a->cfg.quit_message, "Client quit");
    output_clear(a);
    assert(app_command(a, "/quit", &plan) == 1 && a->quit);
    assert(strstr(a->net->output, "QUIT :Client quit\r\n"));
    destroy(a);

    a = fresh(0); register_plain(a);
    a->cfg.background_away = 1;
    strcpy(a->cfg.background_away_message, "Detached from terminal");
    output_clear(a);
    app_set_backgrounded(a, 1);
    app_network(a, 0);
    assert(a->automatic_away == APP_AUTO_AWAY_BACKGROUND);
    assert(strstr(a->net->output, "AWAY :Detached from terminal\r\n"));
    feed(a, ":server 306 tester :You have been marked as being away\r\n");
    output_clear(a);
    app_set_backgrounded(a, 0);
    app_network(a, 0);
    assert(a->automatic_away == APP_AUTO_AWAY_NONE);
    assert(strstr(a->net->output, "AWAY\r\n"));
    destroy(a);

    a = fresh(0); register_plain(a);
    a->cfg.background_away = 1;
    strcpy(a->cfg.background_away_message, "Detached from terminal");
    assert(app_command(a, "/away Manual away", &plan) == 1);
    app_network(a, 0);
    feed(a, ":server 306 tester :You have been marked as being away\r\n");
    assert(a->away && a->automatic_away == APP_AUTO_AWAY_NONE);
    output_clear(a);
    app_set_backgrounded(a, 1);
    app_network(a, 0);
    assert(a->away && a->automatic_away == APP_AUTO_AWAY_NONE && a->net->output_len == 0);
    app_set_backgrounded(a, 0);
    app_network(a, 0);
    assert(a->away && a->net->output_len == 0);
    destroy(a);

    a = fresh(0); register_plain(a);
    a->cfg.autoaway_minutes = 1;
    strcpy(a->cfg.autoaway_message, "Idle");
    app_user_activity(a); output_clear(a);
    clock_ms += 59999; app_network(a, 0);
    assert(!strstr(a->net->output, "AWAY :Idle\r\n"));
    ++clock_ms; app_network(a, 0);
    assert(a->automatic_away == APP_AUTO_AWAY_IDLE);
    assert(strstr(a->net->output, "AWAY :Idle\r\n"));
    feed(a, ":server 306 tester :You have been marked as being away\r\n");
    output_clear(a);
    app_user_activity(a); app_network(a, 0);
    assert(a->automatic_away == APP_AUTO_AWAY_NONE);
    assert(strstr(a->net->output, "AWAY\r\n"));
    destroy(a);
    puts("presence: PART/close/QUIT reasons, background-away, manual-away preservation and idle auto-away passed");
}

static void program_messages_and_persistent_logging(void)
{
    App *a = fresh(0);
    a->cfg.program_messages = 0;
    app_program_notice(a, "Draft cancelled; nothing sent.");
    assert(!*app_status_text(a));
    app_status(a, "A required error remains visible.");
    assert(strstr(app_status_text(a), "required error"));
    a->cfg.queue_notices = 0;
    app_queue_notice(a, "Message queued; paced send pending.");
    assert(!*app_status_text(a));
    destroy(a);

    char directory[] = "/tmp/onekeyirc-log-test-XXXXXX";
    assert(mkdtemp(directory));
    char config_path[CONFIG_PATH_MAX], log_path[CONFIG_PATH_MAX + 5];
    assert(snprintf(config_path, sizeof config_path, "%s/config.ini", directory) > 0);
    assert(snprintf(log_path, sizeof log_path, "%s.log", config_path) > 0);
    Config c; memset(&c, 0, sizeof c);
    strcpy(c.server, "fixture.invalid"); strcpy(c.nick, "tester"); strcpy(c.username, "tester");
    strcpy(c.auth, "none"); c.port = 6667; c.history_lines = 50; c.program_messages = 1;
    c.persistent_logging = 1;
    a = calloc(1, sizeof *a); assert(a && app_init(a, &c, config_path));
    app_log(a, 0, "PERSISTENT_LOG_RECORD");
    struct stat st; assert(stat(log_path, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0777) == 0600);
    FILE *f = fopen(log_path, "rb"); assert(f); char data[4096]; size_t n = fread(data, 1, sizeof data - 1, f); data[n] = 0; fclose(f);
    assert(strstr(data, "[server]") && strstr(data, "PERSISTENT_LOG_RECORD"));
    destroy(a);

    /* Local chat echo is useful in the log, but obvious service/password traffic is not. */
    c.tls = 1; c.port = 6697;
    strcpy(c.channels, "#test");
    a = calloc(1, sizeof *a); assert(a && app_init(a, &c, config_path));
    assert(app_connect(a)); app_network(a, 0); assert(opened_tls);
    register_plain(a);
    feed(a, ":tester!u@host JOIN #test\r\n");
    output_clear(a);
    strcpy(a->config_path, config_path); a->cfg.persistent_logging = 1;
    SendPlan sensitive, ordinary;
    assert(app_plan(a, "NickServ", "IDENTIFY should-never-hit-disk", 0, &sensitive) && app_send_plan(a, &sensitive));
    app_network(a, 0);
    clock_ms += 2000;
    assert(app_plan(a, "Bob", "ordinary-log-message", 0, &ordinary) && app_send_plan(a, &ordinary));
    app_network(a, 0);

    /* A composition split by the IRC wire limit is one user activity for
     * catch-up purposes. Anchor its first chunk, then count later chunks from
     * that anchor so history never reopens halfway through the composition. */
    char split_text[1001]; memset(split_text, 'L', 1000); split_text[1000] = 0;
    SendPlan split; clock_ms += 2000;
    assert(app_plan(a, "#test", split_text, 0, &split) && split.count > 1 && app_send_plan(a, &split));
    size_t split_count = split.count;
    for (size_t sent = 0; sent < split_count + 1u; ++sent) {
        app_network(a, 0);
        clock_ms += 1200;
    }
    assert(a->chat_count == 0);
    size_t channel = app_buffer(a, "#test", B_CHANNEL, 0);
    assert(channel != SIZE_MAX && a->buffers[channel].last_own_retained);
    assert(a->buffers[channel].last_own_from_end == split_count - 1u);
    assert(a->buffers[channel].last_own_log_valid && a->buffers[channel].last_own_log_offset >= 0);
    int catchup_fd = open(log_path, O_RDONLY); assert(catchup_fd >= 0);
    char anchored[768];
    ssize_t anchored_n = pread(catchup_fd, anchored, sizeof anchored - 1u,
                               (off_t)a->buffers[channel].last_own_log_offset);
    assert(anchored_n > 0); anchored[anchored_n] = 0;
    assert(strstr(anchored, "[#test]") && strstr(anchored, "<tester> LLLLLLLLLLLLLLLL"));
    Buffer *anchor = &a->buffers[channel];
    assert(fstat(catchup_fd, &st) == 0 && anchor->last_own_log_device == st.st_dev &&
           anchor->last_own_log_inode == st.st_ino);
    const char *record_end = strchr(anchored, '\n');
    assert(record_end && anchor->last_own_log_length == (size_t)(record_end - anchored) + 1u);
    assert(app_catchup_log_end(anchor, catchup_fd) == (int64_t)st.st_size);
    int64_t original_end = (int64_t)st.st_size, original_offset = anchor->last_own_log_offset;
    uint64_t original_checksum = anchor->last_own_log_checksum;
    app_log(a, channel, "later incoming append retains the sent-message anchor");
    assert(fstat(catchup_fd, &st) == 0 && (int64_t)st.st_size > original_end);
    assert(anchor->last_own_log_offset == original_offset && anchor->last_own_log_checksum == original_checksum);
    assert(app_catchup_log_end(anchor, catchup_fd) == (int64_t)st.st_size);
    close(catchup_fd);

    destroy(a);
    f = fopen(log_path, "rb"); assert(f); n = fread(data, 1, sizeof data - 1, f); data[n] = 0; fclose(f);
    assert(!strstr(data, "should-never-hit-disk") && strstr(data, "ordinary-log-message"));

    assert(chmod(log_path, 0644) == 0);
    a = calloc(1, sizeof *a); assert(a && app_init(a, &c, config_path));
    app_log(a, 0, "MUST_NOT_APPEND");
    assert(a->persistent_log_failed && strstr(app_status_text(a), "Persistent logging unavailable"));
    destroy(a);
    f = fopen(log_path, "rb"); assert(f); n = fread(data, 1, sizeof data - 1, f); data[n] = 0; fclose(f);
    assert(!strstr(data, "MUST_NOT_APPEND"));

    assert(unlink(log_path) == 0);
    char target[CONFIG_PATH_MAX]; assert(snprintf(target, sizeof target, "%s/target", directory) > 0);
    f = fopen(target, "wb"); assert(f && fputs("KEEP", f) >= 0 && fclose(f) == 0); assert(chmod(target, 0600) == 0);
    assert(symlink(target, log_path) == 0);
    a = calloc(1, sizeof *a); assert(a && app_init(a, &c, config_path));
    app_log(a, 0, "SYMLINK_MUST_NOT_WRITE");
    assert(a->persistent_log_failed); destroy(a);
    f = fopen(target, "rb"); assert(f); n = fread(data, 1, sizeof data - 1, f); data[n] = 0; fclose(f);
    assert(!strcmp(data, "KEEP"));
    assert(unlink(log_path) == 0 && unlink(target) == 0 && rmdir(directory) == 0);
    puts("program messages/logging: low-priority suppression, mandatory status visibility, private 0600 append, service-secret filtering, unsafe-mode and symlink refusal passed");
}

int main(void)
{
    presence_departure_and_autoaway();
    program_messages_and_persistent_logging();
    autojoin_pending_retry_and_authentication();
    reconnect_scheduler();
    network_channel_list();
    no_channel_server_view();
    readable_server_events();
    automatic_timing_and_authentication();
    automatic_writes_and_cancellation();
    automatic_limits_focus_and_settings();
    numbered_queries_and_queue_notices();
    disconnected_manual_commands();
    registration_membership_mentions();
    send_plans_and_priority();
    draft_measurement();
    inflight_and_bounds();
    global_memory_bounds();
    private_sender_view_limit();
    source_identity_bounds();
    close_and_channel_numbering();
    close_inflight_and_pending_join();
    close_failure_part_and_pm_names();
    sasl_tests();
    credential_source_tests();
    offline_mode_tests();
    simulator_queue_bound();
    local_list_aliases();
    settings_config_transactions();
    away_and_keyed_join();
    local_ignores();
    offline_away_key_and_ignore();
    activity_and_visibility();
    independent_connections();
    session_save_hook();
    direct_dcc_callback();
    cancellable_dcc_offers();
    stream_output_contract();
    explicit_connection_settings_and_errors();
    mention_source_identity();
    closed_query_mentions_reopen_locally();
    closed_query_mentions_resize_and_evict();
    clear_mentions_preserves_conversations();
    puts("app: registration, memberships, mentions, plans, pacing, close/reclaim, TLS retries, hostile frames, SASL, offline simulation, settings, away, keyed joins, local ignores and independent connection activity passed");
    return 0;
}
