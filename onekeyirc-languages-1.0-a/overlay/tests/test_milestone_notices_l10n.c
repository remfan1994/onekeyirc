/* Exercise actual application notice and command paths with the fake transport. */
#define main legacy_app_test_main
#define force_terminal_signals use_signals
#include "test_app.c"
#undef main
#undef force_terminal_signals
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"

static void expect_status(App *a, const char *key, const IrcDisplayField *fields, size_t n)
{
    char expected[1024];
    assert(irc_display_render(expected, sizeof expected, key, fields, n));
    if (strcmp(a->status, expected)) fprintf(stderr, "status: %s\nexpected: %s\n", a->status, expected);
    assert(!strcmp(a->status, expected));
}
static void expect_log(App *a, const char *key, const IrcDisplayField *fields, size_t n)
{
    char expected[2048];
    assert(irc_display_render(expected, sizeof expected, key, fields, n));
    if (!logs_contain(a, expected)) fprintf(stderr, "missing log: %s\n", expected);
    assert(logs_contain(a, expected));
}
int main(void)
{
    App *a = fresh(0);
    SendPlan plan;
    register_plain(a);
    feed(a, ":server 306 tester :away\r\n");
    assert(a->away && !strcmp(a->status, irc_tr("Away status confirmed by the server.")));
    feed(a, ":server 305 tester :back\r\n");
    assert(!a->away && !strcmp(a->status, irc_tr("Away status cleared by the server.")));
    const char *aliases[] = {"channels", "cl", "ql", "queries"};
    for (size_t i = 0; i < sizeof aliases / sizeof aliases[0]; ++i) {
        char cmd[64]; snprintf(cmd, sizeof cmd, "/%s extra", aliases[i]);
        const IrcDisplayField usage[] = {{"command", aliases[i]}};
        assert(!app_command(a, cmd, &plan));
        expect_status(a, "Usage: /{command}", usage, 1);
    }
    feed(a, ":tester!u@host JOIN #test\r\n");
    const IrcDisplayField one[] = {{"count", "1"}}, two[] = {{"count", "2"}};
    assert(app_plan(a, "#test", "queued first", 0, &plan) && plan.count == 1 && app_send_plan(a, &plan));
    expect_status(a, "{count} message queued; paced at one per 1.2 seconds.", one, 1);
    assert(a->status_kind == APP_STATUS_QUEUE_NOTICE);
    assert(app_plan(a, "#test", "queued second\nqueued third", 0, &plan) && plan.count == 2 && app_send_plan(a, &plan));
    expect_status(a, "{count} messages queued; paced at one per 1.2 seconds.", two, 1);
    assert(a->chat_count == 3);
    assert(app_command(a, "/ignore add literalNick", &plan) == 1);
    assert(a->cfg.ignore_count == 1);
    const IrcDisplayField saved[] = {{"persistence", irc_tr(" /save preserves the list.")}};
    expect_status(a, "Ignore mask added.{persistence}", saved, 1);
    assert(app_command(a, "/ignore list", &plan) == 1);
    expect_status(a, "Ignore list shown in the server view.{persistence}", saved, 1);
    char limit[32]; snprintf(limit, sizeof limit, "%d", CONFIG_IGNORE_MAX);
    const IrcDisplayField count_limit[] = {{"count", "1"}, {"limit", limit}};
    expect_log(a, "Ignore masks: {count}/{limit}. Matching user PRIVMSG/NOTICE are hidden locally.", count_limit, 2);
    assert(logs_contain(a, "literalNick"));
    assert(app_command(a, "/ignore clear", &plan) == 1 && a->cfg.ignore_count == 0);
    expect_status(a, "Ignore list cleared.{persistence}", saved, 1);
    const IrcDisplayField state[] = {{"state", "off"}, {"persistence", irc_tr("/save preserves this setting.")}};
    assert(app_command(a, "/users off", &plan) == 1 && !a->cfg.user_list);
    expect_status(a, "User list {state}; {persistence}", state, 2);
    assert(app_command(a, "/set autoconnect off", &plan) == 1 && !a->cfg.autoconnect);
    expect_status(a, "Autoconnect {state}; {persistence}", state, 2);
    feed(a, ":alice!u@host PRIVMSG tester :retained private line\r\n");
    size_t query = app_buffer(a, "alice", B_QUERY, 0);
    assert(query != SIZE_MAX);
    uint64_t origin = a->buffers[query].id;
    assert(app_close_view(a, query));
    size_t before = a->chat_count, written = a->net->output_len;
    query = app_open_mention(a, origin);
    assert(query != SIZE_MAX && !strcmp(a->buffers[query].name, "alice"));
    const IrcDisplayField nick[] = {{"nick", "alice"}};
    expect_status(a, "Opened query {nick}. No message was sent.", nick, 1);
    assert(a->status_kind == APP_STATUS_PROGRAM_NOTICE && a->chat_count == before && a->net->output_len == written);
    assert(app_command(a, "/part #test", &plan) == 1);
    char cancelled[32]; snprintf(cancelled, sizeof cancelled, "%zu", before);
    const IrcDisplayField part[] = {{"channel", "#test"}, {"count", cancelled}, {"inflight", ""}};
    expect_status(a, "Leaving {channel}; history retained; {count} queued messages cancelled.{inflight}", part, 3);
    assert(!a->chat_count);
    const char *literal = "literal%n{reason}";
    const IrcDisplayField error[] = {{"error", literal}};
    app_disconnect(a, literal);
    expect_log(a, "{error}. Pending output cleared; use /connect to reconnect.", error, 1);
    assert(!strcmp(a->status, literal));
    assert(app_set_test_mode(a));
    app_disconnect(a, literal);
    expect_status(a, "OFFLINE TEST MODE stopped: {error}", error, 1);
    expect_log(a, "OFFLINE TEST MODE stopped: {error}. Pending output cleared; /connect restarts simulation.", error, 1);
    const IrcDisplayField test_state[] = {{"state", "on"}, {"persistence", irc_tr("test-session change only; /save is disabled.")}};
    assert(app_command(a, "/users on", &plan) == 1);
    expect_status(a, "User list {state}; {persistence}", test_state, 2);
    destroy(a);
    puts("Post-milestone notice and command regression: PASS");
    return 0;
}
