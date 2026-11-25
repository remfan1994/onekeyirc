/* Local return and newer presence intent under ordinary control-queue pressure.
 * The shared fixture supplies an in-memory transport and a fake clock. */
#define main legacy_app_test_main
#include "test_app.c"
#undef main

static size_t full_queue_notices;

static void observe_full_queue(void *context, const App *a, const char *text)
{
    (void)context;
    (void)a;
    if (strstr(text, "Outgoing queue is full")) ++full_queue_notices;
}

static void fill_control_queue(App *a)
{
    for (size_t i = 0; i < APP_QUEUE; ++i)
        assert(app_outgoing(a, 0, "PING :queued-control-%zu", i));
}

static void drain_control_queue(App *a)
{
    for (size_t i = 0; i < 3 && (a->inflight || a->control_count); ++i)
        app_network(a, 0);
    assert(!a->inflight && !a->control_count);
}

static App *failed_return(int background, int confirmed)
{
    App *a = fresh(0);
    register_plain(a);
    if (background) {
        a->cfg.background_away = 1;
        strcpy(a->cfg.background_away_message, "Detached");
        app_set_backgrounded(a, 1);
    } else {
        a->cfg.autoaway_minutes = 1;
        strcpy(a->cfg.autoaway_message, "Idle");
        a->last_local_activity = clock_ms - 60000;
    }
    app_network(a, 0);
    if (confirmed) feed(a, ":server 306 tester :Away\r\n");
    assert(a->automatic_away == (background ? APP_AUTO_AWAY_BACKGROUND : APP_AUTO_AWAY_IDLE));
    output_clear(a);
    full_queue_notices = 0;
    a->status_event = observe_full_queue;
    fill_control_queue(a);
    if (background) app_set_backgrounded(a, 0);
    else app_user_activity(a);
    assert(full_queue_notices == 1 && a->control_count == APP_QUEUE);
    return a;
}

static void bounded_clear_retry(void)
{
    for (int background = 0; background <= 1; ++background) {
        App *a = failed_return(background, 1);
        a->net->block = 1;
        for (size_t i = 0; i < 100; ++i) app_network(a, 0);
        assert(full_queue_notices == 1 && app_timeout(a) == 1000);
        clock_ms += 1000;
        app_network(a, 0);
        assert(full_queue_notices == 2);
        for (size_t i = 0; i < 100; ++i) app_network(a, 0);
        assert(full_queue_notices == 2 && app_timeout(a) == 1000);
        a->net->block = 0;
        drain_control_queue(a);
        output_clear(a);
        clock_ms += 999;
        app_network(a, 0);
        assert(!a->net->output_len && app_timeout(a) == 1);
        ++clock_ms;
        assert(app_timeout(a) == 0);
        app_network(a, 0);
        assert(!strcmp(a->net->output, "AWAY\r\n"));
        assert(a->automatic_away == APP_AUTO_AWAY_NONE && !a->automatic_away_retry_at);
        feed(a, ":server 305 tester :Back\r\n");
        clock_ms += 5000;
        app_network(a, 0);
        assert(!strcmp(a->net->output, "AWAY\r\n"));
        assert(!a->away);
        destroy(a);
    }
}

static void newer_manual_presence(void)
{
    static const char *const commands[] = {"/away Manual replacement", "/back"};
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
        App *a = failed_return(1, 1);
        SendPlan plan;
        drain_control_queue(a);
        output_clear(a);
        assert(app_command(a, commands[i], &plan) == 1);
        app_network(a, 0);
        feed(a, i == 0 ? ":server 306 tester :Manual away\r\n" : ":server 305 tester :Back\r\n");
        clock_ms += 5000;
        app_network(a, 0);
        assert(!strcmp(a->net->output, i == 0 ? "AWAY :Manual replacement\r\n" : "AWAY\r\n"));
        assert(!a->automatic_away_retry_at && a->automatic_away == APP_AUTO_AWAY_NONE);
        destroy(a);
    }
}

static void renewed_background_presence(void)
{
    App *a = failed_return(1, 1);
    app_set_backgrounded(a, 1);
    assert(a->control_count == APP_QUEUE);
    drain_control_queue(a);
    output_clear(a);
    clock_ms += 5000;
    app_network(a, 0);
    assert(!a->net->output_len && a->automatic_away == APP_AUTO_AWAY_BACKGROUND);
    assert(!a->automatic_away_retry_at);
    app_set_backgrounded(a, 0);
    app_network(a, 0);
    assert(!strcmp(a->net->output, "AWAY\r\n"));
    destroy(a);
}

static void server_presence_confirmation(void)
{
    for (int away = 0; away <= 1; ++away) {
        App *a = failed_return(1, 0);
        a->net->block = 1;
        feed(a, away ? ":server 306 tester :Old away confirmed\r\n" : ":server 305 tester :Already back\r\n");
        a->net->block = 0;
        drain_control_queue(a);
        output_clear(a);
        clock_ms += 1000;
        app_network(a, 0);
        assert(!strcmp(a->net->output, away ? "AWAY\r\n" : ""));
        assert(a->automatic_away == APP_AUTO_AWAY_NONE && !a->automatic_away_retry_at);
        destroy(a);
    }
}

/* Registration starts the script's one-second delay. Local return occurs half
 * a second later, so a queued script can supersede a not-yet-due clear retry. */
static App *script_over_clear(const char *command)
{
    App *a = fresh_automatic(command, NULL, 1);
    register_plain(a);
    a->cfg.background_away = 1;
    strcpy(a->cfg.background_away_message, "Detached");
    app_set_backgrounded(a, 1);
    app_network(a, 0);
    feed(a, ":server 306 tester :Away\r\n");
    output_clear(a);
    clock_ms += 500;
    fill_control_queue(a);
    app_set_backgrounded(a, 0);
    drain_control_queue(a);
    output_clear(a);
    assert(app_outgoing(a, 0, "PING :hold-control"));
    a->net->block = 1;
    app_network(a, 0);
    clock_ms += 500;
    app_network(a, 0);
    assert(a->autocommand_waiting && a->inflight && a->control_count == 1);
    return a;
}

static void newer_script_presence(void)
{
    static const char *const commands[] = {"/away Script replacement", "/back"};
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
        App *a = script_over_clear(commands[i]);
        a->net->block = 0;
        app_network(a, 0);
        feed(a, i == 0 ? ":server 306 tester :Script away\r\n" : ":server 305 tester :Back\r\n");
        clock_ms += 5000;
        app_network(a, 0);
        assert(!strcmp(a->net->output, i == 0 ?
            "PING :hold-control\r\nAWAY :Script replacement\r\n" : "PING :hold-control\r\nAWAY\r\n"));
        assert(!a->automatic_away_retry_at && a->automatic_away == APP_AUTO_AWAY_NONE);
        destroy(a);
    }
}

static void cancelled_script_restores_clear(void)
{
    static const char *const commands[] = {"/away Script replacement", "/back"};
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
        App *a = script_over_clear(commands[i]);
        assert(app_autocommands_cancel(a) == 1);
        a->net->block = 0;
        app_network(a, 0);
        assert(!strcmp(a->net->output, "PING :hold-control\r\n"));
        assert(a->automatic_away == APP_AUTO_AWAY_BACKGROUND && app_timeout(a) == 500);
        clock_ms += 499;
        app_network(a, 0);
        assert(app_timeout(a) == 1);
        assert(!strcmp(a->net->output, "PING :hold-control\r\n"));
        ++clock_ms;
        app_network(a, 0);
        assert(!strcmp(a->net->output, "PING :hold-control\r\nAWAY\r\n"));
        assert(a->automatic_away == APP_AUTO_AWAY_NONE && !a->automatic_away_retry_at);
        destroy(a);
    }
}

static void rejected_script_restores_clear(void)
{
    App *a = script_over_clear("/away Scheduled absence message longer than the newly advertised limit for this connection");
    feed(a, ":server 005 tester LINELEN=64 :supported\r\n");
    a->net->block = 0;
    app_network(a, 0);
    assert(!strcmp(a->net->output, "PING :hold-control\r\n"));
    assert(!a->autocommand_active && app_timeout(a) == 500);
    clock_ms += 500;
    app_network(a, 0);
    assert(!strcmp(a->net->output, "PING :hold-control\r\nAWAY\r\n"));
    destroy(a);
}

static void background_after_script_cancel(void)
{
    App *a = script_over_clear("/away Script replacement");
    app_set_backgrounded(a, 1);
    assert(app_autocommands_cancel(a) == 1);
    a->net->block = 0;
    app_network(a, 0);
    clock_ms += 5000;
    app_network(a, 0);
    assert(!strcmp(a->net->output, "PING :hold-control\r\n"));
    assert(a->automatic_away == APP_AUTO_AWAY_BACKGROUND && !a->automatic_away_retry_at);
    destroy(a);
}

static void old_confirmation_during_script(void)
{
    static const char *const commands[] = {"/away Script replacement", "/back"};
    for (int away = 1; away >= 0; --away) {
        for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
            App *a = script_over_clear(commands[i]);
            feed(a, away ? ":server 306 tester :Earlier away confirmed\r\n" :
                           ":server 305 tester :Earlier clear confirmed\r\n");
            assert(a->manual_away_pending == (i == 0));
            assert(app_autocommands_cancel(a) == 1);
            a->net->block = 0;
            app_network(a, 0);
            assert(!strcmp(a->net->output, "PING :hold-control\r\n"));
            assert(!a->manual_away_pending);
            if (away) assert(a->automatic_away == APP_AUTO_AWAY_BACKGROUND && app_timeout(a) == 500);
            else assert(a->automatic_away == APP_AUTO_AWAY_NONE && !a->automatic_away_retry_at);
            clock_ms += 500;
            app_network(a, 0);
            assert(!strcmp(a->net->output, away ? "PING :hold-control\r\nAWAY\r\n" :
                                                       "PING :hold-control\r\n"));
            destroy(a);
        }
    }
}

static void manual_confirmation_during_script(void)
{
    static const char *const commands[] = {"/away Script replacement", "/back"};
    static const char *const earlier[] = {"/away Earlier manual", "/back"};
    for (size_t prior = 0; prior < sizeof earlier / sizeof earlier[0]; ++prior) {
        for (int away = 0; away <= 1; ++away) {
            for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
                App *a = fresh_automatic(commands[i], NULL, 1);
                SendPlan plan;
                register_plain(a);
                assert(app_command(a, earlier[prior], &plan) == 1);
                app_network(a, 0);
                output_clear(a);
                assert(app_outgoing(a, 0, "PING :hold-control"));
                a->net->block = 1;
                app_network(a, 0);
                clock_ms += 1000;
                app_network(a, 0);
                feed(a, away ? ":server 306 tester :Earlier away confirmed\r\n" :
                               ":server 305 tester :Earlier clear confirmed\r\n");
                assert(a->manual_away_pending == (i == 0));
                assert(app_autocommands_cancel(a) == 1);
                assert(a->away == away && a->manual_away_pending == (!away && prior == 0));
                assert(a->automatic_away == APP_AUTO_AWAY_NONE && !a->automatic_away_retry_at);
                a->net->block = 0;
                app_network(a, 0);
                clock_ms += 5000;
                app_network(a, 0);
                assert(!strcmp(a->net->output, "PING :hold-control\r\n"));
                destroy(a);
            }
        }
    }
}

static void foreground_while_script_pending(void)
{
    static const char *const commands[] = {
        "/away Script replacement", "/back",
        "/away Scheduled absence message longer than the newly advertised limit for this connection"
    };
    for (int outcome = 0; outcome < 4; ++outcome) {
        for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
            App *a = fresh_automatic(commands[i], NULL, 2);
            register_plain(a);
            a->cfg.background_away = 1;
            strcpy(a->cfg.background_away_message, "Detached");
            app_set_backgrounded(a, 1);
            app_network(a, 0);
            feed(a, ":server 306 tester :Away\r\n");
            output_clear(a);
            assert(app_outgoing(a, 0, "PING :hold-control"));
            a->net->block = 1;
            app_network(a, 0);
            clock_ms += 2000;
            app_network(a, 0);
            assert(a->autocommand_waiting && a->control_count == 1);
            app_set_backgrounded(a, 0);
            assert(a->manual_away_pending == (i != 1));
            if (outcome == 1) feed(a, ":server 305 tester :Already back\r\n");
            if (outcome == 2) feed(a, ":server 306 tester :Earlier away confirmed\r\n");
            if (outcome == 3) app_set_backgrounded(a, 1);
            if (i == 2) feed(a, ":server 005 tester LINELEN=64 :supported\r\n");
            else assert(app_autocommands_cancel(a) == 1);
            a->net->block = 0;
            app_network(a, 0);
            int cleared = outcome == 0 || outcome == 2;
            assert(!strcmp(a->net->output, cleared ? "PING :hold-control\r\nAWAY\r\n" :
                                                    "PING :hold-control\r\n"));
            assert(!a->manual_away_pending && !a->automatic_away_retry_at);
            assert(a->automatic_away == (outcome == 3 ? APP_AUTO_AWAY_BACKGROUND : APP_AUTO_AWAY_NONE));
            destroy(a);
        }
    }
}

static void activity_preserves_prior_manual_away(void)
{
    App *a = fresh_automatic("/away Script replacement", NULL, 1);
    SendPlan plan;
    register_plain(a);
    assert(app_command(a, "/away Earlier manual", &plan) == 1);
    app_network(a, 0);
    feed(a, ":server 306 tester :Manual away\r\n");
    output_clear(a);
    assert(app_outgoing(a, 0, "PING :hold-control"));
    a->net->block = 1;
    app_network(a, 0);
    clock_ms += 1000;
    app_network(a, 0);
    app_user_activity(a);
    assert(app_command(a, "/cancel", &plan) == 1);
    a->net->block = 0;
    app_network(a, 0);
    assert(!strcmp(a->net->output, "PING :hold-control\r\n"));
    assert(a->away && a->automatic_away == APP_AUTO_AWAY_NONE && !a->automatic_away_retry_at);
    destroy(a);
}

static void stale_back_confirmation_after_away(void)
{
    for (int script = 0; script <= 1; ++script) {
        for (int stage = 0; stage < 3; ++stage) {
            App *a = script ? fresh_automatic("/away Script replacement", NULL, 1) : fresh(0);
            SendPlan plan;
            register_plain(a);
            a->cfg.background_away = 1;
            strcpy(a->cfg.background_away_message, "Detached");
            assert(app_command(a, "/back", &plan) == 1);
            app_network(a, 0);
            output_clear(a);
            if (stage == 1) a->net->block = 1;
            if (stage == 2) a->net->write_limit = 3;
            if (script) clock_ms += 1000;
            else assert(app_command(a, "/away Manual replacement", &plan) == 1);
            app_network(a, 0);
            assert(a->manual_away_pending);
            assert(stage ? a->inflight != NULL : a->inflight == NULL);
            feed(a, ":server 305 tester :Earlier BACK confirmed\r\n");
            assert(!a->away && a->manual_away_pending);
            app_set_backgrounded(a, 1);
            app_network(a, 0);
            a->net->block = 0;
            a->net->write_limit = 0;
            app_network(a, 0);
            const char *expected = script ? "AWAY :Script replacement\r\n" : "AWAY :Manual replacement\r\n";
            assert(!strcmp(a->net->output, expected));
            feed(a, ":server 306 tester :Later AWAY confirmed\r\n");
            assert(a->away && !a->manual_away_pending && a->automatic_away == APP_AUTO_AWAY_NONE);
            clock_ms += 1000;
            app_network(a, 0);
            assert(!strcmp(a->net->output, expected));
            app_set_backgrounded(a, 0);
            assert(app_command(a, "/back", &plan) == 1 && !a->manual_away_pending);
            app_network(a, 0);
            feed(a, ":server 305 tester :Latest BACK confirmed\r\n");
            assert(!a->away && !a->manual_away_pending);
            destroy(a);
        }
    }
}

static void disabled_policy_preserves_clear(void)
{
    App *a = failed_return(1, 1);
    Config desired;
    char error[512];
    app_config_effective(a, &desired);
    desired.background_away = 0;
    desired.autoaway_minutes = 0;
    assert(app_config_apply(a, &desired, 0, error, sizeof error));
    drain_control_queue(a);
    output_clear(a);
    clock_ms += 1000;
    app_network(a, 0);
    assert(!strcmp(a->net->output, "AWAY\r\n"));
    assert(a->automatic_away == APP_AUTO_AWAY_NONE && !a->automatic_away_retry_at);
    destroy(a);
}

static void reconnect_discards_clear(void)
{
    App *a = failed_return(1, 1);
    app_disconnect(a, "Reconnect fixture");
    assert(!a->automatic_away_retry_at && a->automatic_away == APP_AUTO_AWAY_NONE);
    assert(app_connect(a));
    app_network(a, 0);
    register_plain(a);
    output_clear(a);
    clock_ms += 5000;
    app_network(a, 0);
    assert(!a->net->output_len && !a->automatic_away_retry_at);
    destroy(a);
}

int main(int argc, char **argv)
{
    static const struct { const char *name; void (*run)(void); } cases[] = {
        {"bounded-retry", bounded_clear_retry}, {"manual-priority", newer_manual_presence},
        {"renewed-background", renewed_background_presence}, {"server-confirmation", server_presence_confirmation},
        {"script-priority", newer_script_presence}, {"cancel-restore", cancelled_script_restores_clear},
        {"reject-restore", rejected_script_restores_clear}, {"disabled-policy", disabled_policy_preserves_clear},
        {"background-after-script", background_after_script_cancel},
        {"old-confirmation-during-script", old_confirmation_during_script},
        {"manual-confirmation-during-script", manual_confirmation_during_script},
        {"foreground-during-script", foreground_while_script_pending},
        {"activity-preserves-manual", activity_preserves_prior_manual_away},
        {"stale-back-confirmation", stale_back_confirmation_after_away},
        {"reconnect", reconnect_discards_clear}
    };
    int matched = 0;
    assert(argc == 1 || argc == 2);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
        if (argc == 1 || !strcmp(argv[1], cases[i].name)) {
            cases[i].run();
            printf("presence recovery: %s passed\n", cases[i].name);
            matched = 1;
        }
    assert(matched);
    return 0;
}
