/* Actual application scheduling with deterministic transport/time. */
#define main legacy_app_test_main
#include "test_app.c"
#undef main

static size_t failure_notices;

static void count_failure(void *context, const App *a, const char *text)
{
    (void)context; (void)a;
    if (strstr(text, "protocol byte limit") || strstr(text, "queue is full"))
        ++failure_notices;
}

static App *presence_ready(void)
{
    App *a = fresh(0);
    register_plain(a);
    feed(a, ":server 005 tester LINELEN=64 :supported\r\n");
    assert(a->wire_limit == 64);
    output_clear(a);
    memset(a->cfg.background_away_message, 'A', 100);
    a->cfg.background_away_message[100] = 0;
    memset(a->cfg.autoaway_message, 'A', 100);
    a->cfg.autoaway_message[100] = 0;
    a->status_event = count_failure;
    failure_notices = 0;
    return a;
}

static void start_due_presence(App *a, int background)
{
    if (background) {
        a->cfg.background_away = 1;
        app_set_backgrounded(a, 1);
    } else {
        a->cfg.autoaway_minutes = 1;
        a->last_local_activity = clock_ms - 60000;
        app_network(a, 0);
    }
    assert(failure_notices == 1 && a->automatic_away == APP_AUTO_AWAY_NONE);
    assert(!a->control_count && !a->chat_count && !a->inflight && !a->net->output_len);
}

static void failed_presence_waits_and_recovers(int background)
{
    App *a = presence_ready();
    start_due_presence(a, background);
    for (size_t i = 0; i < 1000; ++i) {
        assert(app_timeout(a) == 1000);
        app_network(a, 0);
    }
    assert(failure_notices == 1 && !a->net->output_len);
    clock_ms += 999;
    assert(app_timeout(a) == 1);
    app_network(a, 0);
    assert(failure_notices == 1);
    ++clock_ms;
    assert(app_timeout(a) == 0);
    app_network(a, 0);
    assert(failure_notices == 2 && app_timeout(a) == 1000);

    /* A retry wait must not block protocol traffic, and a later advertised
     * limit can make the same configured message sendable without a restart. */
    feed(a, "PING :presence-retry-wait\r\n");
    assert(strstr(a->net->output, "PONG :presence-retry-wait\r\n"));
    output_clear(a);
    feed(a, ":server 005 tester LINELEN=512 :supported\r\n");
    assert(!a->net->output_len && app_timeout(a) == 1000);
    clock_ms += 999;
    app_network(a, 0);
    assert(a->automatic_away == APP_AUTO_AWAY_NONE && !a->net->output_len);
    ++clock_ms;
    app_network(a, 0);
    assert(a->automatic_away == (background ? APP_AUTO_AWAY_BACKGROUND : APP_AUTO_AWAY_IDLE));
    char expected[IRC_BODY_MAX + 3];
    snprintf(expected, sizeof expected, "AWAY :%s\r\n", a->cfg.autoaway_message);
    assert(!strcmp(a->net->output, expected) && failure_notices == 2);
    output_clear(a);
    for (size_t i = 0; i < 10; ++i) { clock_ms += 1000; app_network(a, 0); }
    assert(!a->net->output_len);
    destroy(a);
}

static void activity_and_reconnect_reset(void)
{
    App *a = presence_ready();
    start_due_presence(a, 0);
    app_user_activity(a);
    clock_ms += 1000;
    app_network(a, 0);
    assert(failure_notices == 1 && a->automatic_away == APP_AUTO_AWAY_NONE);
    clock_ms += 59000;
    app_network(a, 0);
    assert(failure_notices == 2 && app_timeout(a) == 1000);
    destroy(a);

    a = presence_ready();
    start_due_presence(a, 1);
    app_disconnect(a, "presence retry reset");
    /* No time passes: the new connection must not inherit the old wait. */
    assert(app_connect(a));
    app_network(a, 0);
    register_plain(a);
    assert(a->wire_limit == 512 && a->automatic_away == APP_AUTO_AWAY_BACKGROUND);
    assert(strstr(a->net->output, "AWAY :AAAA"));
    destroy(a);
}

static void manual_away_and_queue_pressure(void)
{
    App *a = presence_ready();
    SendPlan plan;
    start_due_presence(a, 1);
    assert(app_command(a, "/away manual", &plan) == 1);
    app_network(a, 0);
    feed(a, ":server 306 tester :away\r\n");
    output_clear(a);
    clock_ms += 2000;
    app_network(a, 0);
    assert(a->away && a->automatic_away == APP_AUTO_AWAY_NONE);
    assert(failure_notices == 1 && !a->net->output_len);
    destroy(a);

    a = presence_ready();
    strcpy(a->cfg.background_away_message, "short");
    for (size_t i = 0; i < APP_QUEUE; ++i) assert(app_outgoing(a, 0, "WHOIS peer"));
    a->cfg.background_away = 1;
    app_set_backgrounded(a, 1);
    assert(failure_notices == 1 && a->automatic_away == APP_AUTO_AWAY_NONE);
    for (size_t i = 0; i < 8; ++i) app_network(a, 0);
    assert(!a->control_count && !a->inflight && !strstr(a->net->output, "AWAY"));
    assert(app_timeout(a) == 1000 && failure_notices == 1);
    clock_ms += 1000;
    app_network(a, 0);
    assert(a->automatic_away == APP_AUTO_AWAY_BACKGROUND);
    assert(strstr(a->net->output, "AWAY :short\r\n"));
    destroy(a);
}

int main(void)
{
    failed_presence_waits_and_recovers(0);
    failed_presence_waits_and_recovers(1);
    activity_and_reconnect_reset();
    manual_away_and_queue_pressure();
    puts("presence security: failed automatic-away attempts wait, retry at the deadline, serve PING, recover after limit/queue changes, preserve manual away, and reset on activity/reconnect");
    return 0;
}
