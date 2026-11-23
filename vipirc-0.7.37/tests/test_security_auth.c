#define main legacy_app_test_main
#include "test_app.c"
#undef main

/* The transport is deterministic, but the plan, command, queue and send
 * functions below are the actual application implementation. */
static App *auth_ready(int tls)
{
    Config c;
    memset(&c, 0, sizeof c);
    strcpy(c.server, "localhost");
    strcpy(c.nick, "tester");
    strcpy(c.username, "tester");
    strcpy(c.realname, "Auth regression");
    strcpy(c.channels, "#test");
    strcpy(c.auth, "none");
    c.port = tls ? 6697 : 6667;
    c.tls = tls;
    c.history_lines = 50;
    c.program_messages = 1;
    App *a = calloc(1, sizeof *a);
    assert(a && app_init(a, &c, "/fake/auth-config.ini"));
    assert(app_connect(a));
    app_network(a, 0);
    assert(a->net && opened_tls == tls);
    register_plain(a);
    feed(a, ":tester!user@host.example JOIN #test\r\n");
    output_clear(a);
    return a;
}

static void no_auth_bytes(const App *a)
{
    assert(a->net && !a->net->output_len);
    assert(!a->chat_count && !a->chat_head && !a->chat_tail && !a->inflight);
    assert(strstr(app_status_text(a), "TLS"));
    assert(!strstr(app_status_text(a), "AUTH_SECRET_CANARY"));
    for (size_t i = 0; i < a->buffer_count; ++i) {
        const Buffer *b = &a->buffers[i];
        for (size_t j = 0; j < b->line_count; ++j)
            assert(!strstr(b->lines[(b->line_start + j) % b->line_cap],
                           "AUTH_SECRET_CANARY"));
    }
}

static void flush_chat(App *a)
{
    size_t steps = 0;
    while (a->chat_count || a->inflight || a->control_count) {
        assert(++steps < APP_QUEUE + 5u);
        clock_ms += 1300;
        app_network(a, 0);
        assert(a->net);
    }
}

static void plaintext_rejected(void)
{
    static const char *const commands[] = {
        "IDENTIFY AUTH_SECRET_CANARY",
        "   :iDeNtIfY account AUTH_SECRET_CANARY",
        "AUTH account AUTH_SECRET_CANARY",
        "AUTHENTICATE AUTH_SECRET_CANARY",
        "LOGIN account AUTH_SECRET_CANARY",
        "PASSWORD AUTH_SECRET_CANARY",
        "PASS AUTH_SECRET_CANARY",
        "REGISTER AUTH_SECRET_CANARY email@example.invalid",
        "GHOST account AUTH_SECRET_CANARY",
        "RECOVER account AUTH_SECRET_CANARY",
        "RELEASE account AUTH_SECRET_CANARY",
        "GROUP account AUTH_SECRET_CANARY",
        "DROP account AUTH_SECRET_CANARY",
        "RESETPASS account AUTH_SECRET_CANARY",
        "SETPASS AUTH_SECRET_CANARY",
        "CHANGEPASS AUTH_SECRET_CANARY",
        "VERIFY AUTH_SECRET_CANARY",
        "OPER account AUTH_SECRET_CANARY",
        "SET PASSWORD AUTH_SECRET_CANARY",
        "sEt PaSs AUTH_SECRET_CANARY",
        "SASET PASSWORD AUTH_SECRET_CANARY",
        "SASET someone PASSWORD AUTH_SECRET_CANARY"
    };
    App *a = auth_ready(0);
    SendPlan plan;
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
        memset(&plan, 0xa5, sizeof plan);
        assert(!app_plan(a, i % 2u ? "CustomAuthBot" : "NickServ",
                         commands[i], 0, &plan));
        assert(!plan.count && !plan.text[0]);
        no_auth_bytes(a);
    }
    assert(app_command(a, "/msg NickServ IDENTIFY AUTH_SECRET_CANARY", &plan) != 2);
    no_auth_bytes(a);
    destroy(a);
}

static void ordinary_plaintext_and_tls(void)
{
    App *a = auth_ready(0);
    SendPlan plan;
    assert(app_plan(a, "NickServ", "HELP", 0, &plan) && app_send_plan(a, &plan));
    assert(app_plan(a, "Bob", "ordinary private chat", 0, &plan) && app_send_plan(a, &plan));
    assert(app_plan(a, "NickServ", "IDENTIFYING a bug", 0, &plan) && app_send_plan(a, &plan));
    assert(app_plan(a, "Bob", "waves", 1, &plan) && app_send_plan(a, &plan));
    flush_chat(a);
    assert(strstr(a->net->output, "PRIVMSG NickServ :HELP\r\n"));
    assert(strstr(a->net->output, "PRIVMSG Bob :ordinary private chat\r\n"));
    assert(strstr(a->net->output, "PRIVMSG NickServ :IDENTIFYING a bug\r\n"));
    assert(strstr(a->net->output, "PRIVMSG Bob :\001ACTION waves\001\r\n"));
    destroy(a);

    a = auth_ready(1);
    assert(app_plan(a, "NickServ", "IDENTIFY AUTH_SECRET_CANARY", 0, &plan));
    assert(app_send_plan(a, &plan));
    flush_chat(a);
    assert(strstr(a->net->output, "PRIVMSG NickServ :IDENTIFY AUTH_SECRET_CANARY\r\n"));
    destroy(a);
}

static void split_and_send_revalidation(void)
{
    App *a = auth_ready(0);
    SendPlan plan;
    assert(!app_plan(a, "NickServ", "HELP\nIDENTIFY AUTH_SECRET_CANARY", 0, &plan));
    no_auth_bytes(a);
    size_t budget = app_budget(a, "NickServ", 0);
    assert(budget > 5u && budget + 100u < APP_TEXT);
    char text[APP_TEXT + 1];
    memset(text, 'x', budget);
    memcpy(text, "HELP ", 5u);
    strcpy(text + budget, "IDENTIFY AUTH_SECRET_CANARY");
    /* Its first logical line is ordinary, but splitting creates an auth command. */
    assert(!app_plan(a, "NickServ", text, 0, &plan));
    no_auth_bytes(a);
    a->wire_limit -= budget - 5u;
    assert(app_budget(a, "NickServ", 0) == 5u);
    /* The original auth verb remains sensitive even if every split cuts it. */
    assert(!app_plan(a, "NickServ", "IDENTIFY AUTH_SECRET_CANARY", 0, &plan));
    no_auth_bytes(a);
    destroy(a);

    a = auth_ready(1);
    assert(app_plan(a, "NickServ", "HELP\nIDENTIFY AUTH_SECRET_CANARY", 0, &plan));
    assert(plan.count == 2u);
    /* A caller cannot submit a formerly approved plan to a plain transport.
     * Real UI transport changes are staged; this deliberately changes the
     * fixture's live flag to exercise the final enforcement boundary. */
    a->cfg.tls = 0;
    assert(!app_send_plan(a, &plan));
    no_auth_bytes(a);
    destroy(a);
}

static void final_queue_revalidation(void)
{
    App *a = auth_ready(0);
    /* Exercise the last send boundary, bypassing the higher-level planner.
     * No preceding ordinary chunk may leak from the same pending batch. */
    assert(app_outgoing(a, 1, "PRIVMSG NickServ :HELP"));
    assert(app_outgoing(a, 1, "PRIVMSG NickServ :IDENTIFY AUTH_SECRET_CANARY"));
    app_network(a, 0);
    no_auth_bytes(a);
    assert(a->registered);
    feed(a, "PING :auth-guard-liveness\r\n");
    assert(strstr(a->net->output, "PONG :auth-guard-liveness\r\n"));
    destroy(a);

    a = auth_ready(1);
    SendPlan plan;
    assert(app_plan(a, "NickServ", "HELP\nIDENTIFY AUTH_SECRET_CANARY", 0, &plan));
    assert(app_send_plan(a, &plan));
    a->cfg.tls = 0;
    app_network(a, 0);
    no_auth_bytes(a);
    destroy(a);

    a = auth_ready(1);
    size_t budget = app_budget(a, "CustomAuthBot", 0);
    assert(budget > 5u);
    a->wire_limit -= budget - 5u;
    assert(app_plan(a, "CustomAuthBot", "IDENTIFY AUTH_SECRET_CANARY", 0, &plan));
    assert(app_send_plan(a, &plan));
    a->cfg.tls = 0;
    /* Even when no actual chunk contains the full verb, the queue retains
     * the sensitivity derived from the complete submitted plan. */
    app_network(a, 0);
    no_auth_bytes(a);
    destroy(a);
}

static void offline_behavior_preserved(void)
{
    Config c;
    memset(&c, 0, sizeof c);
    strcpy(c.server, "offline.invalid");
    strcpy(c.nick, "tester");
    strcpy(c.username, "tester");
    strcpy(c.auth, "none");
    c.port = 6667;
    c.history_lines = 50;
    App *a = calloc(1, sizeof *a);
    assert(a && app_init(a, &c, "/fake/offline.ini") && app_set_test_mode(a));
    offline_guard = 1;
    assert(app_connect(a));
    pump_offline(a);
    assert(a->registered && !a->net);
    SendPlan plan;
    assert(app_plan(a, "NickServ", "IDENTIFY simulated-text", 0, &plan));
    assert(app_send_plan(a, &plan));
    app_network(a, 0);
    destroy(a);
    offline_guard = 0;
}

static void credential_batches_not_persisted(void)
{
    char directory[] = "/tmp/vipirc-auth-log-XXXXXX";
    assert(mkdtemp(directory));
    char config_path[CONFIG_PATH_MAX], log_path[CONFIG_PATH_MAX + 5];
    assert(snprintf(config_path, sizeof config_path, "%s/config.ini", directory) > 0);
    assert(snprintf(log_path, sizeof log_path, "%s.log", config_path) > 0);
    App *a = auth_ready(1);
    strcpy(a->config_path, config_path);
    a->cfg.persistent_logging = 1;
    size_t view = app_buffer(a, "CustomAuthBot", B_QUERY, 1);
    assert(view != SIZE_MAX);

    size_t budget = app_budget(a, "CustomAuthBot", 0);
    assert(budget > 9u && budget + 100u < APP_TEXT);
    char text[APP_TEXT + 1];
    memset(text, 'x', budget);
    memcpy(text, "IDENTIFY ", 9u);
    strcpy(text + budget, "SPLIT_CREDENTIAL_FRAGMENT_CANARY");
    SendPlan plan;
    assert(app_plan(a, "CustomAuthBot", text, 0, &plan) && plan.count == 2u);
    assert(app_send_plan(a, &plan));
    flush_chat(a);
    assert(strstr(a->net->output, "SPLIT_CREDENTIAL_FRAGMENT_CANARY"));
    assert(buffer_contains(&a->buffers[view], "SPLIT_CREDENTIAL_FRAGMENT_CANARY"));

    assert(app_plan(a, "CustomAuthBot",
                    "HELP credential-batch-first-line\nIDENTIFY CREDENTIAL_SECOND_LINE_CANARY",
                    0, &plan) && plan.count == 2u);
    assert(app_send_plan(a, &plan));
    flush_chat(a);
    assert(buffer_contains(&a->buffers[view], "credential-batch-first-line"));
    assert(buffer_contains(&a->buffers[view], "CREDENTIAL_SECOND_LINE_CANARY"));

    assert(app_plan(a, "CustomAuthBot", "ordinary-persisted-message", 0, &plan));
    assert(app_send_plan(a, &plan));
    flush_chat(a);
    FILE *file = fopen(log_path, "rb");
    assert(file);
    char data[8192];
    size_t length = fread(data, 1, sizeof data - 1u, file);
    data[length] = 0;
    assert(fclose(file) == 0);
    assert(!strstr(data, "SPLIT_CREDENTIAL_FRAGMENT_CANARY"));
    assert(!strstr(data, "CREDENTIAL_SECOND_LINE_CANARY"));
    assert(!strstr(data, "credential-batch-first-line"));
    assert(strstr(data, "ordinary-persisted-message"));
    destroy(a);
    assert(unlink(log_path) == 0 && rmdir(directory) == 0);
}

int main(int argc, char **argv)
{
    const char *test = argc > 1 ? argv[1] : "all";
    if (!strcmp(test, "all") || !strcmp(test, "plain")) plaintext_rejected();
    if (!strcmp(test, "all") || !strcmp(test, "normal")) ordinary_plaintext_and_tls();
    if (!strcmp(test, "all") || !strcmp(test, "split")) split_and_send_revalidation();
    if (!strcmp(test, "all") || !strcmp(test, "queue")) final_queue_revalidation();
    if (!strcmp(test, "all") || !strcmp(test, "offline")) offline_behavior_preserved();
    if (!strcmp(test, "all") || !strcmp(test, "logging")) credential_batches_not_persisted();
    puts("security auth: plaintext guard, split/queue atomicity, TLS, credential-log privacy and offline behavior passed");
    return 0;
}
