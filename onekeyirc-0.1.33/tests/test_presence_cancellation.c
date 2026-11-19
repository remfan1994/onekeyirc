/* Ordinary automatic-command cancellation through the real application.
 * The shared fixture supplies only an in-memory transport and fake clock. */
#define main legacy_app_test_main
#include "test_app.c"
#undef main

static void hold_control(App *a)
{
    assert(app_outgoing(a, 0, "PING :hold-control"));
    a->net->block = 1;
    app_network(a, 0);
    assert(a->inflight && a->net->retry && !a->net->output_len);
}

static void cancel_script_away(void)
{
    App *a = fresh_automatic("/away Scheduled absence", "/whois later", 1);
    SendPlan plan;
    register_plain(a);
    output_clear(a);
    hold_control(a);
    clock_ms += 1000;
    app_network(a, 0);
    assert(a->autocommand_waiting && a->control_count == 1);
    assert(a->manual_away_pending && !a->away);
    assert(app_command(a, "/cancel", &plan) == 1);
    assert(!a->autocommand_active && !a->control_count && a->inflight);
    if (a->manual_away_pending) {
        puts("FAIL: cancelling an unsent automatic AWAY leaves manual_away_pending set");
        fflush(stdout);
    }
    assert(!a->manual_away_pending);
    a->cfg.background_away = 1;
    strcpy(a->cfg.background_away_message, "Detached");
    app_set_backgrounded(a, 1);
    a->net->block = 0;
    app_network(a, 0);
    assert(!strcmp(a->net->output, "PING :hold-control\r\nAWAY :Detached\r\n"));
    assert(a->automatic_away == APP_AUTO_AWAY_BACKGROUND);
    destroy(a);
}

static void cancel_script_back(void)
{
    App *a = fresh_automatic("/back", "/whois later", 1);
    SendPlan plan;
    register_plain(a);
    output_clear(a);
    hold_control(a);
    assert(app_command(a, "/away Earlier manual", &plan) == 1);
    clock_ms += 1000;
    app_network(a, 0);
    assert(a->autocommand_waiting && a->control_count == 2);
    assert(!a->manual_away_pending);
    assert(app_autocommands_cancel(a) == 2);
    if (!a->manual_away_pending) {
        puts("FAIL: cancelling an unsent script BACK loses the earlier manual AWAY intent");
        fflush(stdout);
    }
    assert(a->manual_away_pending);
    a->cfg.background_away = 1;
    strcpy(a->cfg.background_away_message, "Detached");
    app_set_backgrounded(a, 1);
    a->net->block = 0;
    app_network(a, 0);
    assert(!strcmp(a->net->output, "PING :hold-control\r\nAWAY :Earlier manual\r\n"));
    feed(a, ":server 306 tester :Away\r\n");
    assert(a->away && !a->manual_away_pending && a->automatic_away == APP_AUTO_AWAY_NONE);
    destroy(a);
}

static void cancel_restores_idle_away(void)
{
    App *a = fresh_automatic("/away Scheduled absence", NULL, 2);
    SendPlan plan;
    register_plain(a);
    a->cfg.autoaway_minutes = 1;
    strcpy(a->cfg.autoaway_message, "Idle");
    a->last_local_activity = clock_ms - 60000;
    app_network(a, 0);
    feed(a, ":server 306 tester :Away\r\n");
    assert(a->away && a->automatic_away == APP_AUTO_AWAY_IDLE);
    output_clear(a);
    hold_control(a);
    clock_ms += 2000;
    app_network(a, 0);
    assert(a->manual_away_pending && a->automatic_away == APP_AUTO_AWAY_NONE);
    app_user_activity(a);
    assert(app_command(a, "/cancel", &plan) == 1);
    assert(!a->manual_away_pending && a->automatic_away == APP_AUTO_AWAY_IDLE);
    a->net->block = 0;
    app_network(a, 0);
    assert(!strcmp(a->net->output, "PING :hold-control\r\nAWAY\r\n"));
    assert(a->automatic_away == APP_AUTO_AWAY_NONE);
    destroy(a);
}

static void newer_manual_intent(void)
{
    static const char *const newer[] = {"/away Newer manual", "/back"};
    for (size_t i = 0; i < sizeof newer / sizeof newer[0]; ++i) {
        App *a = fresh_automatic("/back", NULL, 1);
        SendPlan plan;
        register_plain(a);
        output_clear(a);
        hold_control(a);
        assert(app_command(a, "/away Earlier manual", &plan) == 1);
        clock_ms += 1000;
        app_network(a, 0);
        assert(a->autocommand_waiting && !a->manual_away_pending);
        assert(app_command(a, newer[i], &plan) == 1);
        assert(app_autocommands_cancel(a) == 1);
        assert(a->manual_away_pending == (i == 0));
        assert(a->automatic_away == APP_AUTO_AWAY_NONE);
        a->net->block = 0;
        app_network(a, 0);
        assert(!strcmp(a->net->output, i == 0 ?
            "PING :hold-control\r\nAWAY :Earlier manual\r\nAWAY :Newer manual\r\n" :
            "PING :hold-control\r\nAWAY :Earlier manual\r\nAWAY\r\n"));
        destroy(a);
    }
}

static void newer_server_confirmation(void)
{
    App *a = fresh_automatic("/back", NULL, 1);
    SendPlan plan;
    register_plain(a);
    assert(app_command(a, "/away Earlier manual", &plan) == 1);
    app_network(a, 0);
    assert(a->manual_away_pending);
    output_clear(a);
    hold_control(a);
    clock_ms += 1000;
    app_network(a, 0);
    assert(a->autocommand_waiting && !a->manual_away_pending);
    feed(a, ":server 306 tester :Earlier manual confirmed\r\n");
    assert(a->away && !a->manual_away_pending);
    assert(app_autocommands_cancel(a) == 1);
    assert(a->away && !a->manual_away_pending);
    a->net->block = 0;
    app_network(a, 0);
    assert(!strcmp(a->net->output, "PING :hold-control\r\n"));
    destroy(a);
}

static void inflight_script_presence(void)
{
    for (int partial = 0; partial <= 1; ++partial) {
        App *a = fresh_automatic("/away Scheduled absence", "/whois later", 1);
        register_plain(a);
        output_clear(a);
        if (partial) a->net->write_limit = 3;
        else a->net->block = 1;
        clock_ms += 1000;
        app_network(a, 0);
        assert(a->inflight && a->autocommand_waiting && a->manual_away_pending);
        assert(a->net->output_len == (partial ? 3u : 0u));
        assert(app_autocommands_cancel(a) == 1);
        assert(a->inflight && a->manual_away_pending);
        a->net->block = 0;
        a->net->write_limit = 0;
        app_network(a, 0);
        assert(!a->inflight && !a->autocommand_active && a->manual_away_pending);
        assert(!strcmp(a->net->output, "AWAY :Scheduled absence\r\n"));
        feed(a, ":server 306 tester :Away\r\n");
        assert(a->away && !a->manual_away_pending);
        clock_ms += 5000;
        app_network(a, 0);
        assert(!strcmp(a->net->output, "AWAY :Scheduled absence\r\n"));
        destroy(a);
    }
}

static void reject_unsent_script_presence(void)
{
    App *a = fresh_automatic("/away Scheduled absence message longer than the newly advertised limit for this connection", NULL, 1);
    register_plain(a);
    output_clear(a);
    hold_control(a);
    clock_ms += 1000;
    app_network(a, 0);
    assert(a->manual_away_pending && a->control_count == 1);
    feed(a, ":server 005 tester LINELEN=64 :supported\r\n");
    assert(a->control_count == 1 && a->manual_away_pending);
    a->net->block = 0;
    app_network(a, 0);
    assert(!a->autocommand_active && !a->manual_away_pending);
    assert(!strcmp(a->net->output, "PING :hold-control\r\n"));
    a->cfg.background_away = 1;
    strcpy(a->cfg.background_away_message, "Detached");
    app_set_backgrounded(a, 1);
    app_network(a, 0);
    assert(!strcmp(a->net->output, "PING :hold-control\r\nAWAY :Detached\r\n"));
    destroy(a);
}

static void disconnect_clears_presence(void)
{
    App *a = fresh_automatic("/away Scheduled absence", NULL, 1);
    register_plain(a);
    output_clear(a);
    hold_control(a);
    clock_ms += 1000;
    app_network(a, 0);
    assert(a->manual_away_pending && a->control_count == 1);
    app_disconnect(a, "Reconnect fixture");
    assert(!a->manual_away_pending && a->automatic_away == APP_AUTO_AWAY_NONE);
    assert(!app_autocommands_cancel(a));
    assert(!a->manual_away_pending && !a->control_head && !a->inflight);
    assert(app_connect(a));
    app_network(a, 0);
    register_plain(a);
    output_clear(a);
    assert(app_autocommands_cancel(a) == 1 && !a->autocommand_active);
    a->cfg.background_away = 1;
    strcpy(a->cfg.background_away_message, "Detached");
    app_set_backgrounded(a, 1);
    app_network(a, 0);
    assert(!strcmp(a->net->output, "AWAY :Detached\r\n"));
    destroy(a);
}

int main(int argc, char **argv)
{
    static const struct { const char *name; void (*run)(void); } cases[] = {
        {"queued-away", cancel_script_away}, {"queued-back", cancel_script_back},
        {"restore-idle", cancel_restores_idle_away}, {"newer-manual", newer_manual_intent},
        {"server-confirmation", newer_server_confirmation}, {"inflight", inflight_script_presence},
        {"reject-unsent", reject_unsent_script_presence}, {"reconnect", disconnect_clears_presence}
    };
    int matched = 0;
    assert(argc == 1 || argc == 2);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
        if (argc == 1 || !strcmp(argv[1], cases[i].name)) {
            cases[i].run();
            printf("presence cancellation: %s passed\n", cases[i].name);
            matched = 1;
        }
    assert(matched);
    return 0;
}
