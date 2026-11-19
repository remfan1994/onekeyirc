/* Real application/parser logic with the established in-memory transport.
 * No public network, bouncer account, keyring or external service is used. */
#define main legacy_app_test_main
#include "test_app.c"
#undef main
#include <errno.h>

static unsigned bouncer_cases, dcc_seen;

static App *bouncer_fresh(int type, int gpg)
{
    Config c;
    memset(&c, 0, sizeof c);
    strcpy(c.server, "localhost"); c.port = 6697; c.tls = 1;
    strcpy(c.nick, "tester"); strcpy(c.username, "tester");
    strcpy(c.realname, "Bouncer regression"); strcpy(c.channels, "#configured");
    strcpy(c.auth, type == CONFIG_BOUNCER_SOJU ? "sasl" : "none");
    c.history_lines = 50; c.program_messages = 1;
    c.bouncer_type = type;
    strcpy(c.bouncer_user, "account"); strcpy(c.bouncer_network, "ircnow");
    strcpy(c.bouncer_client, "desktop");
    char *source = type == CONFIG_BOUNCER_SOJU ?
        (gpg ? c.password_gpg_file : c.password_file) :
        (gpg ? c.server_password_gpg_file : c.server_password_file);
    strcpy(source, "/fake/bouncer-password");
    App *a = calloc(1, sizeof *a);
    assert(a && app_init(a, &c, "/fake/config.ini"));
    assert(app_connect(a)); app_network(a, 0);
    assert(a->net);
    return a;
}

static void bouncer_register(App *a)
{
    if (a->cfg.bouncer_type == CONFIG_BOUNCER_SOJU) {
        feed(a, ":server CAP tester LS :sasl=PLAIN multi-prefix server-time batch\r\n");
        feed(a, ":server CAP tester ACK :sasl multi-prefix server-time batch\r\nAUTHENTICATE +\r\n");
        assert(a->auth_sent && !a->registered);
        feed(a, ":server 903 tester :Authenticated\r\n");
    } else {
        feed(a, ":server CAP tester LS :multi-prefix server-time batch znc.in/self-message\r\n");
        feed(a, ":server CAP tester ACK :multi-prefix server-time batch znc.in/self-message\r\n");
    }
    assert(a->cap_done && a->cap_batch && a->cap_time);
    feed(a, ":server 001 tester :Welcome\r\n");
    assert(a->registered && !strstr(a->net->output, "JOIN "));
    output_clear(a);
}

static int record_dcc(void *context, const char *nick, const char *body)
{
    (void)context; assert(!strcmp(nick, "alice"));
    assert(!strncmp(body, "DCC SEND ", 9)); ++dcc_seen;
    return 1;
}

static void tag_and_time(void)
{
    char value[80];
    assert(!irc_tag_value(NULL, "time", value, sizeof value) && !*value);
    assert(irc_tag_value("x=one\\:two\\sthree\\\\four\\rfive\\n;empty;last=abc\\q\\", "x", value, sizeof value) == 1);
    assert(!strcmp(value, "one;two three\\four\rfive\n"));
    assert(irc_tag_value("last=abc\\q\\", "last", value, sizeof value) == 1 && !strcmp(value, "abcq"));
    assert(irc_tag_value("x=1;x=final", "x", value, sizeof value) == 1 && !strcmp(value, "final"));
    assert(irc_tag_value("x=1;x", "x", value, sizeof value) == 1 && !*value);
    assert(!irc_tag_value("+time=wrong;Time=wrong", "time", value, sizeof value));
    assert(irc_tag_value("x=long", "x", value, 4) == -1 && !*value);
    assert(irc_server_time("time=2012-06-30T23:59:60.419Z", value, sizeof value));
    assert(!strcmp(value, "2012-06-30 23:59:60 UTC"));
    assert(irc_server_time("time=2000-02-29T01:02:03Z", value, sizeof value));
    assert(irc_server_time("time=2024-02-29T01:02:03.123456789Z", value, sizeof value));
    static const char *const bad[] = {
        "time=1900-02-29T01:02:03.000Z", "time=2025-02-29T01:02:03.000Z",
        "time=2024-04-31T01:02:03.000Z", "time=2024-13-01T01:02:03.000Z",
        "time=2024-01-00T01:02:03.000Z", "time=2024-01-01T24:02:03.000Z",
        "time=2024-01-01T01:60:03.000Z", "time=2024-01-01T01:02:61.000Z",
        "time=2024-01-01T01:02:03.Z", "time=2024-01-01T01:02:03.000+00:00",
        "time=2024-01-01T01:02:03.000Z\\n", "time=bad;time=also-bad",
        "+time=2024-01-01T01:02:03.000Z"
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i)
        assert(!irc_server_time(bad[i], value, sizeof value) && !*value);
    assert(!irc_server_time("time=2024-01-01T01:02:03.000Z", value, 23) && !*value);
    ++bouncer_cases;
}

static void credentials_and_capabilities(void)
{
    App *a = bouncer_fresh(CONFIG_BOUNCER_ZNC, 0);
    const char *registration = "PASS :account@desktop/ircnow:secret\r\nCAP LS 302\r\n";
    assert(!strncmp(a->net->output, registration, strlen(registration)));
    bouncer_register(a);
    assert(a->cap_self && !a->auth_started);
    feed(a, ":server CAP tester DEL :server-time batch znc.in/self-message\r\n");
    assert(!a->cap_time && !a->cap_batch && !a->cap_self);
    feed(a, ":server CAP tester NEW :server-time\r\n");
    assert(!a->cap_time && !a->net->output_len);
    destroy(a);

    a = bouncer_fresh(CONFIG_BOUNCER_SOJU, 0);
    assert(!strstr(a->net->output, "PASS")); output_clear(a);
    feed(a, ":server CAP tester LS * :multi-prefix server-time batch\r\n");
    assert(!strstr(a->net->output, "CAP REQ"));
    feed(a, ":server CAP tester LS :sasl=EXTERNAL,PLAIN\r\n");
    assert(strstr(a->net->output, "CAP REQ :sasl multi-prefix server-time batch\r\n"));
    assert(!a->cap_time && !a->cap_batch && !a->cap_sasl);
    feed(a, ":server CAP tester ACK :server-time batch\r\n");
    assert(a->cap_time && a->cap_batch && !a->auth_started && !a->cap_done);
    feed(a, ":server CAP tester ACK :sasl\r\nAUTHENTICATE +\r\n");
    assert(strstr(a->net->output, "AUTHENTICATE AGFjY291bnQvaXJjbm93QGRlc2t0b3AAc2VjcmV0\r\n"));
    feed(a, ":server 903 tester :Authenticated\r\n");
    assert(a->authenticated && !a->cap_done);
    feed(a, ":server CAP tester ACK :multi-prefix\r\n");
    assert(a->cap_done && !a->cap_pending);
    assert(!strstr(a->net->output, "echo-message") && !strstr(a->net->output, "chathistory") &&
           !strstr(a->net->output, "znc.in/playback"));
    destroy(a); ++bouncer_cases;
}

static void capability_rejection(void)
{
    App *a = bouncer_fresh(CONFIG_BOUNCER_ZNC, 0);
    feed(a, ":server CAP tester LS :server-time batch\r\n:server CAP tester NAK :server-time batch\r\n");
    assert(a->net && a->cap_done && !a->cap_time && !a->cap_batch); destroy(a);
    a = bouncer_fresh(CONFIG_BOUNCER_SOJU, 0);
    feed(a, ":server CAP tester LS :sasl batch\r\n:server CAP tester NAK :batch\r\n");
    assert(a->net && !a->cap_done && !a->cap_batch && !a->auth_started);
    feed(a, ":server CAP tester ACK :sasl\r\nAUTHENTICATE +\r\n:server 903 tester :Authenticated\r\n");
    assert(a->net && a->cap_done && a->authenticated); destroy(a);
    a = bouncer_fresh(CONFIG_BOUNCER_SOJU, 0);
    feed(a, ":server CAP tester LS :sasl batch\r\n:server CAP tester NAK :sasl batch\r\n");
    assert(!a->net && !a->auth_sent); destroy(a);
    a = bouncer_fresh(CONFIG_BOUNCER_SOJU, 0);
    feed(a, ":server CAP tester LS :sasl=EXTERNAL batch\r\n");
    assert(!a->net && !a->auth_sent); destroy(a);
    a = bouncer_fresh(CONFIG_BOUNCER_ZNC, 0);
    feed(a, ":server CAP tester LS :batch\r\n:server CAP tester ACK :server-time\r\n");
    assert(!a->cap_time && !a->cap_done);
    feed(a, ":server CAP tester ACK :batch\r\n");
    assert(a->cap_batch && a->cap_done); destroy(a);
    ++bouncer_cases;
}

static void credential_boundaries(void)
{
    size_t identity = strlen("account@desktop/ircnow:");
    size_t maximum = 503u - identity;
    memset(test_password, 'x', maximum); test_password[maximum] = 0;
    App *a = bouncer_fresh(CONFIG_BOUNCER_ZNC, 0);
    assert(strstr(a->net->output, "PASS :")); destroy(a);
    memset(test_password, 'x', maximum + 1); test_password[maximum + 1] = 0;
    Config c; memset(&c, 0, sizeof c);
    strcpy(c.server, "localhost"); strcpy(c.nick, "tester"); strcpy(c.username, "tester");
    strcpy(c.auth, "none"); c.tls = 1; c.port = 6697; c.history_lines = 50;
    c.bouncer_type = CONFIG_BOUNCER_ZNC;
    strcpy(c.bouncer_user, "account"); strcpy(c.bouncer_client, "desktop"); strcpy(c.bouncer_network, "ircnow");
    strcpy(c.server_password_file, "/fake/password");
    a = calloc(1, sizeof *a); assert(a && app_init(a, &c, ""));
    assert(app_connect(a)); app_network(a, 0);
    assert(!a->net && strstr(a->status, "Registration setup failed")); destroy(a);
    strcpy(test_password, "secret");

    credential_result = 0;
    a = bouncer_fresh(CONFIG_BOUNCER_ZNC, 1);
    assert(a->credential && !a->net->output_len && !a->started);
    const char *welcome = ":server 001 tester :Premature welcome\r\n";
    memcpy(a->net->input, welcome, strlen(welcome)); a->net->input_len = strlen(welcome);
    app_network(a, POLLIN);
    assert(a->credential && !a->registered && !a->net->output_len);
    credential_result = 1; app_network(a, POLLIN);
    assert(!a->credential && a->started && strstr(a->net->output, "PASS :account@desktop/ircnow:secret\r\n"));
    assert(!strstr(a->net->output, "JOIN "));
    credential_result = 0; destroy(a); ++bouncer_cases;
}

static void playback_and_membership(void)
{
    App *a = bouncer_fresh(CONFIG_BOUNCER_ZNC, 0); bouncer_register(a);
    feed(a, ":tester!self@host JOIN #restored\r\n:server 353 tester = #restored :tester alice\r\n:server 366 tester #restored :End\r\n");
    size_t room = app_buffer(a, "#restored", B_CHANNEL, 0);
    assert(room != SIZE_MAX && a->buffers[room].joined && !strstr(a->net->output, "PART"));
    assert(a->buffers[room].user_count == 2);
    a->dcc_offer = record_dcc; dcc_seen = 0;
    app_switch(a, 0); output_clear(a);
    size_t mentions = a->buffers[1].line_count;
    feed(a, ":server BATCH +old znc.in/playback #restored\r\n");
    feed(a, "@batch=old;time=2012-06-30T23:59:60.419Z :alice!u@h PRIVMSG #restored :hello tester\r\n");
    assert(strstr(last_line(&a->buffers[room]), "[2012-06-30 23:59:60 UTC] [history] [#restored] <alice> hello tester"));
    assert(!a->buffers[room].unread && a->buffers[1].line_count == mentions);
    feed(a, "@batch=old :alice!u@h PRIVMSG tester :\001DCC SEND old 2130706433 5555 5\001\r\n");
    assert(!dcc_seen);
    feed(a, "@batch=old :tester!old@old JOIN #ghost\r\n@batch=old :tester!old@old NICK :obsolete\r\n@batch=old :alice!u@h PART #restored\r\n@batch=old :server 306 tester :Old away\r\n@batch=old PING :old-token\r\n");
    assert(!strcmp(a->nick, "tester") && !strcmp(a->own_user, "self"));
    assert(!a->away && a->buffers[room].user_count == 2 && !a->net->output_len);
    assert(app_buffer(a, "#ghost", B_CHANNEL, 0) == SIZE_MAX);
    feed(a, "@batch=old :server BATCH +child example/unknown\r\n@batch=child :tester!u@h PRIVMSG Alice :old own private\r\n");
    size_t alice = app_buffer(a, "Alice", B_QUERY, 0);
    assert(alice != SIZE_MAX && strstr(last_line(&a->buffers[alice]), "[history]"));
    assert(!a->buffers[alice].last_own_retained && !a->buffers[alice].unread);
    feed(a, ":server BATCH +live example/unknown\r\n@batch=live :alice!u@h PRIVMSG #restored :live tester\r\n");
    assert(a->buffers[room].unread == 1 && a->buffers[1].line_count == mentions + 1);
    assert(!strstr(last_line(&a->buffers[room]), "[history]"));
    feed(a, "@batch=old :server BATCH -child\r\n:server BATCH -old\r\n:server BATCH -live\r\n");
    feed(a, "@time=2024-01-01T01:02:03.000Z :tester!u@h PRIVMSG Bob :other device\r\n");
    size_t bob = app_buffer(a, "Bob", B_QUERY, 0);
    assert(bob != SIZE_MAX && !a->buffers[bob].last_own_retained);
    assert(strstr(last_line(&a->buffers[bob]), "[2024-01-01 01:02:03 UTC]"));
    feed(a, ":alice!u@h PRIVMSG tester :\001DCC SEND live 2130706433 5555 5\001\r\n");
    assert(dcc_seen == 1);
    feed(a, "@batch=missing :alice!u@h PRIVMSG tester :\001DCC SEND unknown 2130706433 5555 5\001\r\n");
    assert(dcc_seen == 1 && a->net);
    feed(a, ":server BATCH +join netjoin old new\r\n@batch=join :bob!u@h JOIN #restored\r\n:server BATCH -join\r\n");
    assert(a->buffers[room].user_count == 3);
    app_disconnect(a, "test reset");
    for (size_t i = 0; i < APP_BATCHES; ++i) assert(!a->batches[i].reference[0]);
    assert(!a->cap_batch && !a->cap_time && !a->cap_self && !a->cap_pending);
    destroy(a); ++bouncer_cases;
}

static void batch_limits(void)
{
    static const char *const invalid[] = {
        ":server BATCH +bad_id chathistory\r\n",
        ":server BATCH +a chathistory\r\n:server BATCH +a chathistory\r\n",
        ":server BATCH -missing\r\n",
        "@batch=missing :server BATCH +a chathistory\r\n",
        ":server BATCH +a chathistory\r\n@batch=a :server BATCH +b other\r\n:server BATCH -a\r\n",
        ":server BATCH +a chathistory\r\n@batch=a :server BATCH +b other\r\n:server BATCH -b\r\n"
    };
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        App *a = bouncer_fresh(CONFIG_BOUNCER_ZNC, 0); bouncer_register(a);
        feed(a, invalid[i]); assert(!a->net); destroy(a);
    }
    App *a = bouncer_fresh(CONFIG_BOUNCER_ZNC, 0); bouncer_register(a);
    char line[256];
    for (unsigned i = 0; i < APP_BATCHES; ++i) {
        snprintf(line, sizeof line, ":server BATCH +b%u chathistory\r\n", i); feed(a, line);
    }
    assert(a->net);
    feed(a, ":server BATCH +overflow chathistory\r\n"); assert(!a->net); destroy(a);
    a = bouncer_fresh(CONFIG_BOUNCER_ZNC, 0); bouncer_register(a);
    for (unsigned i = 0; i < 100; ++i) feed(a, ":server BATCH +a chathistory\r\n:server BATCH -a\r\n");
    assert(a->net); destroy(a); ++bouncer_cases;
}

static void control_commands(void)
{
    App *a = bouncer_fresh(CONFIG_BOUNCER_ZNC, 0); bouncer_register(a);
    SendPlan p; size_t lines = a->buffers[0].line_count, views = a->buffer_count;
    assert(!irc_valid_target("*status"));
    assert(app_command(a, "/znc", &p) == 1); app_network(a, 0);
    assert(!strcmp(a->net->output, "PRIVMSG *status :help\r\n"));
    assert(a->buffers[0].line_count == lines && a->buffer_count == views);
    output_clear(a); clock_ms += 1200;
    assert(app_command(a, "/msg *sasl Set account CONTROL_CANARY", &p) == 1); app_network(a, 0);
    assert(!strcmp(a->net->output, "PRIVMSG *sasl :Set account CONTROL_CANARY\r\n"));
    assert(a->buffers[0].line_count == lines && !strstr(a->status, "CONTROL_CANARY"));
    assert(!app_command(a, "/soju help", &p));
    assert(!app_command(a, "/znc one\nsecond", &p));
    char too_long[600]; strcpy(too_long, "/znc "); memset(too_long + 5, 'x', 590); too_long[595] = 0;
    assert(!app_command(a, too_long, &p) && !a->chat_count);
    assert(app_command(a, "/znc help", &p));
    assert(app_command(a, "/cancel", &p) && !a->chat_count);
    a->cfg.tls = 0; assert(!app_command(a, "/znc help", &p)); a->cfg.tls = 1;
    char directory[] = "/tmp/vipirc-bouncer-log-XXXXXX";
    assert(mkdtemp(directory));
    snprintf(a->config_path, sizeof a->config_path, "%s/config.ini", directory);
    char logfile[CONFIG_PATH_MAX + 5]; snprintf(logfile, sizeof logfile, "%s.log", a->config_path);
    a->cfg.persistent_logging = 1;
    feed(a, ":*status!znc@znc.in PRIVMSG tester :SERVICE_CANARY\r\n");
    assert(strstr(last_line(&a->buffers[0]), "*status: SERVICE_CANARY"));
    assert(access(logfile, F_OK) < 0 && errno == ENOENT);
    size_t service_lines = a->buffers[0].line_count;
    feed(a, ":tester!u@h PRIVMSG *sasl :Set account OWN_SERVICE_CANARY\r\n");
    feed(a, ":server BATCH +controls znc.in/playback\r\n@batch=controls :tester!u@h PRIVMSG *sasl :Set account HISTORY_SERVICE_CANARY\r\n:server BATCH -controls\r\n");
    assert(a->buffers[0].line_count == service_lines && access(logfile, F_OK) < 0 && errno == ENOENT);
    a->cfg.persistent_logging = 0; destroy(a); assert(rmdir(directory) == 0);

    a = bouncer_fresh(CONFIG_BOUNCER_SOJU, 0);
    assert(!app_command(a, "/soju help", &p)); bouncer_register(a);
    assert(app_command(a, "/soju", &p)); app_network(a, 0);
    assert(!strcmp(a->net->output, "PRIVMSG BouncerServ :help\r\n"));
    output_clear(a); clock_ms += 1200;
    assert(app_command(a, "/msg BouncerServ network status", &p)); app_network(a, 0);
    assert(!strcmp(a->net->output, "PRIVMSG BouncerServ :network status\r\n"));
    output_clear(a); clock_ms += 1200;
    size_t before = a->buffers[0].line_count;
    assert(app_plan(a, "BouncerServ", "password PLAN_CANARY", 0, &p));
    assert(p.count == 1 && !app_plan_history_safe(&p));
    assert(app_send_plan(a, &p)); app_network(a, 0);
    assert(!strcmp(a->net->output, "PRIVMSG BouncerServ :password PLAN_CANARY\r\n"));
    assert(a->buffers[0].line_count == before);
    assert(!app_plan(a, "BouncerServ", "one\nsecond", 0, &p));
    assert(!app_plan(a, "BouncerServ", "action", 1, &p));
    assert(!app_plan(a, "BouncerServ", too_long, 0, &p));
    size_t query_count = a->buffer_count;
    feed(a, ":tester!u@h PRIVMSG BouncerServ :network create -pass OWN_SERVICE_CANARY\r\n");
    feed(a, ":server BATCH +controls chathistory\r\n@batch=controls :tester!u@h PRIVMSG BouncerServ :network create -pass HISTORY_SERVICE_CANARY\r\n:server BATCH -controls\r\n");
    assert(a->buffer_count == query_count && a->buffers[0].line_count == before);
    destroy(a); ++bouncer_cases;
}

static void away_and_staging(void)
{
    App *a = bouncer_fresh(CONFIG_BOUNCER_ZNC, 0); bouncer_register(a);
    a->cfg.background_away = 1; a->cfg.autoaway_minutes = 1;
    strcpy(a->cfg.background_away_message, "Detached"); strcpy(a->cfg.autoaway_message, "Idle");
    a->last_local_activity = clock_ms - 60000;
    app_network(a, 0); assert(!a->net->output_len && app_timeout(a) == 1000);
    app_set_backgrounded(a, 1); app_network(a, 0);
    assert(!a->net->output_len && app_timeout(a) == 1000);
    SendPlan p; assert(app_command(a, "/away Manual", &p)); app_network(a, 0);
    assert(!strcmp(a->net->output, "AWAY :Manual\r\n"));
    feed(a, ":server 306 tester :Away\r\n");
    output_clear(a); app_set_backgrounded(a, 0); app_network(a, 0);
    assert(!a->net->output_len && a->away);
    assert(app_command(a, "/back", &p)); app_network(a, 0);
    feed(a, ":server 305 tester :Back\r\n"); output_clear(a);
    a->cfg.bouncer_away = 1; app_set_backgrounded(a, 1); app_network(a, 0);
    assert(!strcmp(a->net->output, "AWAY :Detached\r\n"));
    feed(a, ":server 306 tester :Away\r\n"); output_clear(a);
    a->cfg.bouncer_away = 0; app_set_backgrounded(a, 0); app_network(a, 0);
    assert(!strcmp(a->net->output, "AWAY\r\n"));
    Config desired, effective; char error[256];
    a->cfg.autocommands.count = 1; strcpy(a->cfg.autocommands.lines[0], "/msg operator old-network-command");
    app_config_effective(a, &desired); strcpy(desired.bouncer_network, "newnet");
    strcpy(desired.bouncer_client, "phone"); desired.bouncer_away = 1;
    assert(app_config_apply(a, &desired, 0, error, sizeof error));
    assert(!strcmp(a->cfg.bouncer_network, "ircnow"));
    app_config_effective(a, &effective);
    assert(!strcmp(effective.bouncer_network, "newnet") && !strcmp(effective.bouncer_client, "phone"));
    assert(effective.bouncer_away && !effective.autocommands.count);
    destroy(a); ++bouncer_cases;
}

int main(void)
{
    tag_and_time(); credentials_and_capabilities(); capability_rejection();
    credential_boundaries(); playback_and_membership(); batch_limits();
    control_commands(); away_and_staging();
    printf("bouncer core: %u groups passed (tags/time, credentials/CAP, rejection, secret bounds/GPG, playback/state, batch limits, private controls, away/staging)\n", bouncer_cases);
    return 0;
}
