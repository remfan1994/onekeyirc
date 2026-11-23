/* Execute the actual minimalist direct and review paths against the offline
 * simulator. No terminal, network, credential file or persistent profile is
 * needed; history and the accepted outbound queue are inspected directly. */
#include "idle.c"
#include <assert.h>

static Idle *history_fixture(int review)
{
    Idle *idle = calloc(1, sizeof *idle);
    Config cfg;
    char error[512];
    char directory[] = "/tmp/irc-history-flow-XXXXXX", path[256];
    assert(idle);
    assert(mkdtemp(directory));
    assert(snprintf(path, sizeof path, "%s/config.ini", directory) > 0);
    idle->client = calloc(1, sizeof *idle->client);
    assert(idle->client);
    config_minimal_defaults(&cfg);
    strcpy(cfg.server, "fixture.invalid");
    strcpy(cfg.nick, "tester");
    strcpy(cfg.username, "tester");
    strcpy(cfg.channels, "#test");
    cfg.autoconnect = 0;
    cfg.send_review = review;
    cfg.persistent_logging = 0;
    assert(client_init(idle->client, &cfg, path, "tester", 1, 0, error, sizeof error) == 1);
    App *a = client_get_app(idle->client, 0, error, sizeof error);
    assert(a && app_connect(a));
    for (unsigned i = 0; i < 32; ++i) app_network(a, 0);
    assert(a->registered && a->sim && !a->net);
    idle->program = "onekeyirc";
    idle->commands = (IdleCommands){idle->client, idle->program, command_print, idle, 0, 0, NULL, NULL};
    idle->tty.rows = 24; idle->tty.cols = 80; idle->tty.utf8 = 1;
    idle->catchup_fd = -1;
    idle->mode = WATCH;
    attach_apps(idle);
    config_wipe(&cfg, sizeof cfg);
    return idle;
}

static void history_fixture_free(Idle *idle)
{
    char directory[CONFIG_PATH_MAX];
    snprintf(directory, sizeof directory, "%s", idle->client->primary_path);
    char *last = strrchr(directory, '/');
    assert(last); *last = 0;
    while (idle->count) stream_pop(idle);
    for (size_t i = 0; i < idle->history_count; ++i) {
        config_wipe(idle->history[i].text, strlen(idle->history[i].text));
        free(idle->history[i].text);
    }
    response_reset(idle);
    client_free(idle->client);
    free(idle->client);
    assert(rmdir(directory) == 0);
    config_wipe(idle, sizeof *idle);
    free(idle);
}

static unsigned usage_count(const Config *cfg, const char *name)
{
    for (size_t i = 0; i < cfg->command_usage_count; ++i)
        if (!strcmp(cfg->command_usage[i].name, name)) return cfg->command_usage[i].count;
    return 0;
}

static void execute_message(Idle *idle, const char *text, size_t messages,
                            int remembered, int review)
{
    App *a = current_app(idle);
    size_t queued = a->chat_count, history = idle->history_count;
    unsigned used = usage_count(&a->cfg, "msg");
    begin_editor(idle, COMMAND);
    assert(idle->mode == COMMAND && ed_from_utf8(&idle->editor, text));
    submit_editor(idle);
    if (review || messages > 1) {
        assert(idle->mode == REVIEW && idle->pending.count == messages);
        assert(a->chat_count == queued && idle->history_count == history);
        confirm_send(idle);
    }
    assert(idle->mode == WATCH && !idle->editor.len && !idle->pending.count);
    assert(a->chat_count == queued + messages);
    assert(usage_count(&a->cfg, "msg") == used + 1);
    assert(idle->history_count == history + (remembered ? 1u : 0u));
    if (remembered) assert(!strcmp(idle->history[0].text, text));
    for (size_t i = 0; i < idle->history_count; ++i)
        assert(!strstr(idle->history[i].text, "FLOW_SECRET_CANARY"));
}

static void direct_and_review(int review)
{
    Idle *idle = history_fixture(review);
    execute_message(idle, "/msg CustomAuthBot ::IDENTIFY FLOW_SECRET_CANARY", 1, 0, review);
    execute_message(idle, "/MeSsAgE CustomAuthBot : :LOGIN 123 FLOW_SECRET_CANARY", 1, 0, review);
    execute_message(idle, "/privmsg CustomAuthBot ordinary single message", 1, 1, review);

    char command[APP_TEXT + 1];
    const char prefix[] = "/msg CustomAuthBot ";
    size_t at = sizeof prefix - 1, budget = app_budget(current_app(idle), "CustomAuthBot", 0);
    assert(budget > 8 && at + budget + 128 < sizeof command);
    memcpy(command, prefix, at);
    memset(command + at, 'x', budget);
    memcpy(command + at, "HELP ", 5);
    strcpy(command + at + budget, "IDENTIFY FLOW_SECRET_CANARY");
    execute_message(idle, command, 2, 0, review);
    strcpy(command + at + budget, "ordinary split message");
    execute_message(idle, command, 2, 1, review);

    /* A private pending plan must not suppress an unrelated later command. */
    begin_editor(idle, COMMAND);
    assert(ed_from_utf8(&idle->editor, "/whois Alice"));
    size_t before = idle->history_count;
    submit_editor(idle);
    assert(idle->history_count == before + 1);
    assert(!strcmp(idle->history[0].text, "/whois Alice"));
    config_wipe(command, sizeof command);
    history_fixture_free(idle);
}

int main(void)
{
    direct_and_review(0);
    direct_and_review(1);
    puts("idle history flow: direct/review accepted queues exclude prefixed and split credential text; ordinary single/split messages and later commands retain history; usage counts stay canonical");
    return 0;
}
