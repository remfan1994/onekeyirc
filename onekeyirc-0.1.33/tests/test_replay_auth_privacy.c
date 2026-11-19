/* Replayed authentication must remain private when its source nickname is
 * no longer ours. Exercise real App dispatch/logging with test_app's bounded
 * in-memory transport and private temporary canary logs; no live accounts. */
#define main legacy_app_test_main
#include "test_app.c"
#undef main
#include <errno.h>

static const char replay_canary[] = "FAKE_REPLAY_AUTH_CANARY_4829";
static unsigned replay_checks, replay_failures;

static App *replay_profile(int type)
{
    Config c;
    memset(&c, 0, sizeof c);
    strcpy(c.server, "localhost"); c.port = 6697; c.tls = 1;
    strcpy(c.nick, "tester"); strcpy(c.username, "tester");
    strcpy(c.realname, "Replay authentication privacy regression");
    strcpy(c.auth, type == CONFIG_BOUNCER_ZNC ? "none" : "sasl");
    strcpy(c.auth_user, "account");
    c.history_lines = 50; c.program_messages = 1;
    c.bouncer_type = type;
    if (type) {
        strcpy(c.bouncer_user, "account"); strcpy(c.bouncer_network, "ircnow");
        strcpy(c.bouncer_client, "desktop");
    }
    strcpy(type == CONFIG_BOUNCER_ZNC ? c.server_password_file : c.password_file,
           "/fake/replay-password");
    App *a = calloc(1, sizeof *a);
    assert(a && app_init(a, &c, "/fake/config.ini"));
    assert(app_connect(a)); app_network(a, 0);
    if (type != CONFIG_BOUNCER_ZNC) {
        feed(a, ":server CAP tester LS :sasl=PLAIN batch\r\n"
                ":server CAP tester ACK :sasl batch\r\nAUTHENTICATE +\r\n"
                ":server 903 tester :Authenticated\r\n");
    } else {
        feed(a, ":server CAP tester LS :batch\r\n"
                ":server CAP tester ACK :batch\r\n");
    }
    feed(a, ":server 001 tester :Welcome\r\n:tester!user@host NICK :newtester\r\n");
    assert(a->net && a->registered && a->cap_batch && !strcmp(a->nick, "newtester"));
    output_clear(a);
    return a;
}

static int replay_visible(const App *a)
{
    int visible = 0;
    for (size_t i = 0; i < a->buffer_count; ++i) {
        const Buffer *b = &a->buffers[i];
        assert(!b->last_own_log_valid && !b->last_own_retained);
        for (size_t j = 0; j < b->line_count; ++j)
            if (strstr(b->lines[(b->line_start + j) % b->line_cap], replay_canary)) visible = 1;
    }
    return visible;
}

static int replay_persisted(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    char data[8192];
    size_t count = fread(data, 1, sizeof data - 1, file);
    assert(!ferror(file) && count < sizeof data - 1 && fclose(file) == 0);
    data[count] = 0;
    assert(strstr(data, "ordinary-after-private-control"));
    return strstr(data, replay_canary) != NULL;
}

static void replay_case(int type, unsigned source, unsigned framing, unsigned variation)
{
    static const char *const sources[] = {
        "newtester!user@host", "tester!user@host", "anotherclient!user@host",
        "long-server-name-that-is-deliberately-not-an-irc-nickname.example.invalid"
    };
    static const struct { const char *command, *target, *format; int persisted; } cases[] = {
        {"PRIVMSG", "NickServ", "IDENTIFY %s", 0},
        {"NOTICE", "nS", "fragment-without-a-verb %s", 0},
        {"PRIVMSG", "CustomAuthBot", "IDENTIFY %s", 0},
        {"NOTICE", "CustomAuthBot", ": :sAsEt account PASSWORD %s", 0},
        {"PRIVMSG", "NickServ", "\001PING %s\001", 0},
        {"NOTICE", "NickServ", "\001ACTION token %s\001", 0},
        {"PRIVMSG", "NickServ", "HELP %s", 0},
        {"PRIVMSG", "Alice", "ordinary private text %s", 1},
        {"NOTICE", "CustomAuthBot", "IDENTIFYING a problem %s", 1},
        {"PRIVMSG", "#test", "ordinary channel text %s", 1},
        {"PRIVMSG", "NickServ@services.example", "fragment-without-a-verb %s", 0},
        {"NOTICE", "CustomAuthBot@services.example", "LOGIN account %s", 0},
        {"PRIVMSG", "malformed/target", "IDENTIFY %s", 0},
        {"NOTICE", "malformed/target", "ordinary fallback text %s", 1}
    };
    App *a = replay_profile(type);
    char directory[] = "/tmp/vipirc-replay-auth-XXXXXX";
    char logfile[CONFIG_PATH_MAX + 5], wire[1024], body[512];
    assert(mkdtemp(directory));
    snprintf(a->config_path, sizeof a->config_path, "%s/config.ini", directory);
    snprintf(logfile, sizeof logfile, "%s.log", a->config_path);
    a->cfg.persistent_logging = 1;
    if (framing == 1) feed(a, ":server BATCH +auth chathistory NickServ\r\n");
    const char *marker = strstr(cases[variation].format, "%s");
    assert(marker && !strchr(marker + 2, '%'));
    int n = snprintf(body, sizeof body, "%.*s%s%s",
                     (int)(marker - cases[variation].format), cases[variation].format,
                     replay_canary, marker + 2);
    assert(n > 0 && (size_t)n < sizeof body);
    n = snprintf(wire, sizeof wire, "%s:%s %s %s :%s\r\n",
                 framing == 1 ? "@batch=auth " : framing == 2 ? "@batch=unknown " : "",
                 sources[source], cases[variation].command, cases[variation].target, body);
    assert(n > 0 && (size_t)n < sizeof wire);
    feed(a, wire);
    if (framing == 1) feed(a, ":server BATCH -auth\r\n");
    assert(a->net && a->registered && !a->persistent_log_suppressed && !a->message_history);
    int visible = replay_visible(a);
    /* A private classification applies to this message only. */
    feed(a, ":alice!user@host PRIVMSG newtester :ordinary-after-private-control\r\n");
    int persisted = replay_persisted(logfile);
    ++replay_checks;
    if (!visible || persisted != cases[variation].persisted) {
        fprintf(stderr, "FAIL profile=%d source=%u framing=%u case=%u visible=%d persisted=%d expected=%d\n",
                type, source, framing, variation, visible, persisted, cases[variation].persisted);
        ++replay_failures;
    }
    a->cfg.persistent_logging = 0;
    destroy(a);
    assert(unlink(logfile) == 0 && rmdir(directory) == 0);
}

int main(void)
{
    strcpy(test_password, "FAKE_REPLAY_PASSWORD_9314");
    for (int type = CONFIG_BOUNCER_NONE; type <= CONFIG_BOUNCER_SOJU; ++type)
        for (unsigned source = 0; source < 4; ++source)
            for (unsigned framing = 0; framing < 3; ++framing)
                for (unsigned variation = 0; variation < 14; ++variation)
                    replay_case(type, source, framing, variation);
    printf("Replay authentication privacy: %u cases, %u failures\n", replay_checks, replay_failures);
    return replay_failures ? 1 : 0;
}
