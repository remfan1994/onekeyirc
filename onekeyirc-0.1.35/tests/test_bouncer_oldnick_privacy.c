/* A history sender may retain an earlier nickname. Control-message privacy
 * belongs to the destination, not to a match against the current nickname.
 * Uses only fake credentials and a private temporary canary log. */
#define main legacy_app_test_main
#include "test_app.c"
#undef main
#include <errno.h>

static unsigned privacy_checks, privacy_failures;
static const char privacy_canary[] = "FAKE_OLD_NICK_CONTROL_CANARY_8421";

static App *privacy_profile(int type)
{
    Config c;
    memset(&c, 0, sizeof c);
    strcpy(c.server, "localhost"); c.port = 6697; c.tls = 1;
    strcpy(c.nick, "tester"); strcpy(c.username, "tester");
    strcpy(c.realname, "Old nickname privacy regression");
    strcpy(c.auth, type == CONFIG_BOUNCER_SOJU ? "sasl" : "none");
    c.history_lines = 50; c.program_messages = 1;
    c.bouncer_type = type;
    strcpy(c.bouncer_user, "account"); strcpy(c.bouncer_network, "ircnow");
    strcpy(c.bouncer_client, "desktop");
    strcpy(type == CONFIG_BOUNCER_SOJU ? c.password_file : c.server_password_file,
           "/fake/bouncer-password");
    App *a = calloc(1, sizeof *a);
    assert(a && app_init(a, &c, "/fake/config.ini"));
    assert(app_connect(a)); app_network(a, 0);
    if (type == CONFIG_BOUNCER_SOJU) {
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

static int retained(const App *a)
{
    if (strstr(a->status, privacy_canary)) return 1;
    for (size_t i = 0; i < a->buffer_count; ++i) {
        const Buffer *b = &a->buffers[i];
        for (size_t j = 0; j < b->line_count; ++j)
            if (strstr(b->lines[(b->line_start + j) % b->line_cap], privacy_canary)) return 1;
    }
    return 0;
}

static int persisted(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { assert(errno == ENOENT); return 0; }
    char data[8192]; ssize_t n = read(fd, data, sizeof data - 1);
    assert(n >= 0 && n < (ssize_t)sizeof data - 1);
    data[n] = 0; assert(close(fd) == 0);
    return strstr(data, privacy_canary) != NULL;
}

static void privacy_case(int type, unsigned variation)
{
    App *a = privacy_profile(type);
    char directory[] = "/tmp/vipirc-oldnick-privacy-XXXXXX";
    char logfile[CONFIG_PATH_MAX + 5], wire[1024];
    assert(mkdtemp(directory));
    snprintf(a->config_path, sizeof a->config_path, "%s/config.ini", directory);
    snprintf(logfile, sizeof logfile, "%s.log", a->config_path);
    a->cfg.persistent_logging = 1;
    const char *service = type == CONFIG_BOUNCER_ZNC ? "*sasl" : "BouncerServ";
    int expected_memory = 0, expected_log = 0;
    if (variation <= 2) {
        feed(a, ":server BATCH +controls chathistory control\r\n");
        const char *nick = variation == 0 ? "newtester" : variation == 1 ? "tester" : "previoussession";
        snprintf(wire, sizeof wire, "@batch=controls :%s!user@host PRIVMSG %s :Set account %s\r\n",
                 nick, service, privacy_canary);
        feed(a, wire);
        feed(a, ":server BATCH -controls\r\n");
    } else if (variation == 3) {
        snprintf(wire, sizeof wire, "@batch=unknown :tester!user@host NOTICE %s :Set account %s\r\n", service, privacy_canary);
        feed(a, wire);
    } else if (variation == 4) {
        snprintf(wire, sizeof wire, ":%s!service@host NOTICE newtester :%s\r\n", service, privacy_canary);
        feed(a, wire); expected_memory = 1;
    } else if (variation == 5) {
        snprintf(wire, sizeof wire, ":alice!user@host PRIVMSG newtester :%s\r\n", privacy_canary);
        feed(a, wire); expected_memory = expected_log = 1;
    } else {
        feed(a, ":server BATCH +peer chathistory alice\r\n");
        snprintf(wire, sizeof wire, "@batch=peer :alice!user@host PRIVMSG tester :%s\r\n", privacy_canary);
        feed(a, wire); feed(a, ":server BATCH -peer\r\n");
        expected_memory = expected_log = 1;
    }
    int memory = retained(a), disk = persisted(logfile);
    ++privacy_checks;
    if (memory != expected_memory || disk != expected_log) {
        fprintf(stderr, "FAIL mode=%s case=%u expected_memory=%d actual_memory=%d expected_log=%d actual_log=%d\n",
                type == CONFIG_BOUNCER_ZNC ? "znc" : "soju", variation,
                expected_memory, memory, expected_log, disk);
        ++privacy_failures;
    }
    a->cfg.persistent_logging = 0;
    destroy(a);
    if (unlink(logfile) < 0) assert(errno == ENOENT);
    assert(rmdir(directory) == 0);
}

int main(void)
{
    strcpy(test_password, "FAKE_OLD_NICK_PASSWORD_6318");
    for (int mode = CONFIG_BOUNCER_ZNC; mode <= CONFIG_BOUNCER_SOJU; ++mode)
        for (unsigned variation = 0; variation < 7; ++variation) privacy_case(mode, variation);
    printf("Bouncer old-nickname privacy regression: %u cases, %u failures\n", privacy_checks, privacy_failures);
    return privacy_failures ? 1 : 0;
}
