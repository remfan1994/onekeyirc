#define main legacy_app_test_main
#include "test_app.c"
#undef main

/* A directory request belongs to one connection and must remain refreshable
 * when that connection disappears or the server rejects LIST. All transport
 * and time inputs here are deterministic; no public network is contacted. */
static App *partial_directory(int send)
{
    App *a = fresh(0);
    SendPlan plan;
    register_plain(a);
    output_clear(a);
    assert(app_command(a, "/list *room --min-users 3 --name room --topic partial --sort channel --asc --limit 7", &plan) == 6);
    if (send) {
        app_network(a, 0);
        assert(!strcmp(a->net->output, "LIST *room\r\n"));
        feed(a, ":server 322 tester #room 8 :partial directory\r\n");
        assert(a->list.count == 1 && a->list.received == 1);
    }
    assert(a->list.active && !a->list.complete);
    return a;
}

static void interrupted_cache(const App *a, uint64_t revision, size_t count)
{
    assert(!a->list.active && !a->list.complete);
    assert(a->list.revision > revision && a->list.count == count);
    assert(a->list.received == count && a->list.matched == count);
    if (count) {
        assert(!strcmp(a->list.entries[0].channel, "#room"));
        assert(a->list.entries[0].users == 8);
        assert(!strcmp(a->list.entries[0].topic, "partial directory"));
    }
    assert(!strcmp(a->list.server_mask, "*room"));
    assert(!strcmp(a->list.name_filter, "room"));
    assert(!strcmp(a->list.topic_filter, "partial"));
    assert(a->list.min_set && a->list.min_users == 3);
    assert(a->list.sort == APP_LIST_SORT_CHANNEL && !a->list.descending);
    assert(a->list.display_limit == 7);
}

static void loss_releases_request(void)
{
    for (unsigned loss = 0; loss < 6; ++loss) {
        App *a = partial_directory(loss != 5);
        SendPlan plan;
        uint64_t revision = a->list.revision;
        if (loss == 0) {
            a->net->eof = 1;
            app_network(a, POLLIN);
        } else if (loss == 1 || loss == 5) {
            assert(app_command(a, "/disconnect", &plan));
        } else if (loss == 2) {
            feed(a, "ERROR :Server restarting\r\n");
        } else {
            app_set_reconnect(a, 1);
            a->net->failed = 1;
            a->net->retryable = loss == 3;
            app_network(a, POLLIN);
        }
        assert(!a->net && !a->control_count && !a->inflight);
        interrupted_cache(a, revision, loss == 5 ? 0u : 1u);
        assert(app_command(a, "/list show", &plan) == 6);
        if (loss == 3) {
            assert(a->reconnect_at > clock_ms);
            clock_ms = a->reconnect_at;
            app_network(a, 0);
            assert(a->net);
        } else {
            assert(!a->reconnect_at);
            assert(app_connect(a));
        }
        register_plain(a);
        output_clear(a);
        assert(app_list_repeat(a));
        assert(a->list.active && !a->list.complete && !a->list.count);
        app_network(a, 0);
        assert(!strcmp(a->net->output, "LIST *room\r\n"));
        feed(a, ":server 322 tester #newroom 9 :partial fresh directory\r\n");
        feed(a, ":server 323 tester :End of LIST\r\n");
        assert(!a->list.active && a->list.complete && a->list.count == 1);
        assert(!strcmp(a->list.entries[0].channel, "#newroom"));
        assert(app_command(a, "/list clear", &plan));
        assert(!a->list.count && !a->list.active && !a->list.complete);
        destroy(a);
    }

    App *a = partial_directory(1);
    SendPlan plan;
    app_disconnect(a, "manual disconnect");
    assert(app_command(a, "/list clear", &plan));
    assert(!a->list.count && !a->list.active);
    destroy(a);
}

static void rejection_releases_request(void)
{
    static const unsigned codes[] = {263u, 421u, 461u};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; ++i) {
        for (unsigned lower = 0; lower < 2; ++lower) {
            App *a = partial_directory(1);
            SendPlan plan;
            char line[128];
            uint64_t revision = a->list.revision;
            snprintf(line, sizeof line, ":server %03u tester %s :Request rejected\r\n",
                     codes[i], lower ? "list" : "LIST");
            feed(a, line);
            interrupted_cache(a, revision, 1);
            assert(a->registered && a->net);
            assert(strstr(a->status, "Channel list request failed"));
            assert(buffer_contains(&a->buffers[0], "Request rejected"));
            output_clear(a);
            assert(app_list_repeat(a));
            app_network(a, 0);
            assert(!strcmp(a->net->output, "LIST *room\r\n"));
            assert(a->list.active && !a->list.count);
            feed(a, line);
            assert(app_command(a, "/list clear", &plan));
            assert(!a->list.count && !a->list.active && !a->list.complete);
            destroy(a);
        }
    }
}

static void unrelated_errors_do_not_interrupt(void)
{
    static const char *const replies[] = {
        ":server 263 tester WHO :Please try again later\r\n",
        ":server 421 tester LISTEN :Unknown command\r\n",
        ":server 461 tester JOIN :Not enough parameters\r\n",
        ":server 263 tester LIST\r\n",
        ":server 421 tester LIST\r\n",
        ":server 461 tester LIST\r\n",
        "@batch=unknown :server 263 tester LIST :Historical reply\r\n",
        "@batch=unknown :server 421 tester LIST :Historical reply\r\n",
        "@batch=unknown :server 461 tester LIST :Historical reply\r\n"
    };
    App *a = partial_directory(1);
    uint64_t revision = a->list.revision;
    for (size_t i = 0; i < sizeof replies / sizeof replies[0]; ++i) {
        feed(a, replies[i]);
        assert(a->list.active && !a->list.complete && a->list.count == 1);
        assert(a->list.revision == revision);
    }
    feed(a, ":server 323 tester :End of LIST\r\n");
    assert(a->list.complete && !a->list.active && a->list.count == 1);
    destroy(a);
}

static void completed_directory_survives_disconnect_and_errors(void)
{
    App *a = partial_directory(1);
    feed(a, ":server 323 tester :End of LIST\r\n");
    ChannelList saved = a->list;
    feed(a, ":server 421 tester LIST :No request is active\r\n");
    assert(!memcmp(&a->list, &saved, sizeof saved));
    app_disconnect(a, "manual disconnect");
    assert(!memcmp(&a->list, &saved, sizeof saved));
    assert(app_connect(a));
    register_plain(a);
    assert(!memcmp(&a->list, &saved, sizeof saved));
    destroy(a);
}

int main(void)
{
    loss_releases_request();
    rejection_releases_request();
    unrelated_errors_do_not_interrupt();
    completed_directory_survives_disconnect_and_errors();
    puts("LIST recovery: EOF, manual/queued disconnect, server restart, transient/fatal failure, reconnect, qualified rejection, partial/completed cache preservation and unrelated/history controls passed");
    return 0;
}
