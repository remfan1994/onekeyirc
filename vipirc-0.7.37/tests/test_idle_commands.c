/* Backend invariants: no terminal library, public network or real credentials. */
#define _POSIX_C_SOURCE 200809L
#include "idle_commands.h"
#include <assert.h>
#include <stdio.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char output[262144];
static size_t output_used;
typedef struct Fixture {
    Client *client;
    IdleCommands commands;
    SendPlan plan;
    char directory[128], path[256];
} Fixture;

static void capture(void *context, const char *line)
{
    (void)context;
    size_t n = strlen(line);
    assert(output_used + n + 2 < sizeof output);
    memcpy(output + output_used, line, n); output_used += n;
    output[output_used++] = '\n'; output[output_used] = 0;
    for (size_t i = 0; i < n; ++i) assert((unsigned char)line[i] >= 32 && (unsigned char)line[i] != 127);
}

static void reset_output(void) { output_used = 0; output[0] = 0; }

static void init(Fixture *f, int offline)
{
    Config cfg; char error[512];
    memset(f, 0, sizeof *f);
    strcpy(f->directory, "/tmp/onekeyirc-commands-XXXXXX");
    assert(mkdtemp(f->directory));
    snprintf(f->path, sizeof f->path, "%s/config.ini", f->directory);
    config_minimal_defaults(&cfg);
    strcpy(cfg.server, "fixture.invalid"); strcpy(cfg.nick, "tester"); strcpy(cfg.username, "tester");
    strcpy(cfg.channels, "#test"); cfg.autoconnect = 0;
    f->client = calloc(1, sizeof *f->client); assert(f->client);
    assert(client_init(f->client, &cfg, f->path, "tester", offline, 0, error, sizeof error) == 1);
    assert(client_get_app(f->client, 0, error, sizeof error));
    f->commands = (IdleCommands){f->client, "onekeyirc", capture, NULL, 0, 0, NULL, NULL};
    config_wipe(&cfg, sizeof cfg); reset_output();
}

static void cleanup(Fixture *f)
{
    char usage_path[CONFIG_PATH_MAX + 16];
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        const ClientSlot *slot = &f->client->slots[i];
        if (*slot->config_path) {
            snprintf(usage_path, sizeof usage_path, "%s.commands", slot->config_path);
            (void)unlink(usage_path);
        }
    }
    client_free(f->client); free(f->client);
    (void)unlink(f->path);
    char path[320];
    snprintf(path, sizeof path, "%s.servers", f->path); (void)unlink(path);
    snprintf(path, sizeof path, "%s.servers.lock", f->path); (void)unlink(path);
    snprintf(path, sizeof path, "%s.d", f->path); (void)rmdir(path);
    assert(rmdir(f->directory) == 0);
    config_wipe(f, sizeof *f);
}

static int run(Fixture *f, const char *line)
{
    reset_output(); return idle_command(&f->commands, line, &f->plan);
}

static App *app(Fixture *f) { return f->client->slots[f->client->current].app; }

static void init_empty(Fixture *f, const char *login)
{
    Config cfg; char error[512];
    init(f, 1);
    client_free(f->client);
    config_minimal_defaults(&cfg);
    assert(client_init(f->client, &cfg, f->path, login, 1, 0, error, sizeof error) == 1);
    assert(client_get_app(f->client, 0, error, sizeof error));
    config_wipe(&cfg, sizeof cfg);
}

static size_t session_count(const Fixture *f)
{
    size_t count = 0;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) count += f->client->slots[i].used != 0;
    return count;
}

static void pump(App *a)
{
    for (unsigned i = 0; i < 32; ++i) app_network(a, 0);
}

static void metadata_and_help(void)
{
    Fixture f; init(&f, 1);
    assert(idle_command_count() >= 35 && !idle_command_spec(idle_command_count()));
    for (size_t i = 0; i < idle_command_count(); ++i) {
        const IdleCommandSpec *spec = idle_command_spec(i);
        assert(spec && *spec->name && *spec->usage && *spec->description);
        if (i) assert(strcmp(idle_command_spec(i - 1)->name, spec->name) < 0);
        assert(strcmp(spec->name, "pms") && strcmp(spec->name, "raw") && strcmp(spec->name, "mouse"));
        char command[96]; snprintf(command, sizeof command, "/help /%s", spec->name);
        assert(run(&f, command) == IDLE_COMMAND_DONE);
        assert(strstr(output, spec->usage) && strstr(output, "Examples:"));
    }
    const char *topics[] = {
        "about", "aliases", "authentication", "bouncers", "configuration", "deployment",
        "documentation", "history", "hotkeys", "keys", "limits", "logging", "messages", "reconnect",
        "security", "sessions", "startup", "tls", "troubleshooting",
        "commands", "ls", "autoconnect", "queries", "send_mode"
    };
    for (size_t i = 0; i < sizeof topics / sizeof topics[0]; ++i) {
        char command[96]; snprintf(command, sizeof command, "/help %s", topics[i]);
        assert(run(&f, command) == IDLE_COMMAND_DONE && output_used);
    }
    assert(run(&f, "/help ls") == IDLE_COMMAND_DONE && strstr(output, "Help topics"));
    assert(strstr(output, "configuration") && strstr(output, "autoauth") && !strstr(output, "Usage: /ls"));
    assert(run(&f, "/help /ls") == IDLE_COMMAND_DONE && strstr(output, "Usage: /ls"));
    assert(run(&f, "/help commands") == IDLE_COMMAND_DONE && strstr(output, "The command system"));
    assert(strstr(output, "Down selects suggestions"));
    assert(strstr(output, "Enter inserts the selected command for editing"));
    assert(strstr(output, "separate Enter after display executes it"));
    assert(strstr(output, "Up from input recalls accepted history") && strstr(output, "Tab/Shift-Tab") && strstr(output, "no shell quoting"));
    assert(run(&f, "/HELP /AUTOCONNECT") == IDLE_COMMAND_DONE);
    assert(run(&f, "/help impossible") == IDLE_COMMAND_ERROR && strstr(output, "/help commands"));
    assert(run(&f, "/help ls extra") == IDLE_COMMAND_ERROR && strstr(output, "Usage:"));
    assert(run(&f, "/manual") == IDLE_COMMAND_DONE);
    assert(strstr(output, "ThatIAm.vip") && strstr(output, "remfan1994") && strstr(output, "IALA"));
    assert(strstr(output, "Not vegetarian? www.bloodguiltcurse.net will change your mind"));
    assert(strstr(output, "Ctrl-D is inert") && strstr(output, "Escape on the log backgrounds the client"));
    cleanup(&f);
}

static void help_keeps_product_boundaries(void)
{
    Fixture f; init(&f, 1);
    const char *brands[] = {"onekeyirc", "vipirc"};
    const char *topics[] = {
        "", "ls", "commands", "about", "aliases", "authentication", "bouncers",
        "configuration", "deployment", "documentation", "history", "hotkeys", "keys", "limits",
        "logging", "messages", "reconnect", "security", "sessions", "startup", "tls",
        "troubleshooting", "all"
    };
    Config before = app(&f)->cfg;
    for (size_t brand = 0; brand < 2; ++brand) {
        f.commands.program = brands[brand];
        for (size_t i = 0; i < idle_command_count(); ++i) {
            const IdleCommandSpec *spec = idle_command_spec(i);
            char line[96]; snprintf(line, sizeof line, "/help /%s", spec->name);
            assert(run(&f, line) == IDLE_COMMAND_DONE && strstr(output, spec->usage));
            assert(!strstr(output, brands[1 - brand]));
        }
        for (size_t i = 0; i < sizeof topics / sizeof topics[0]; ++i) {
            char line[96]; snprintf(line, sizeof line, "/help %s", topics[i]);
            assert(run(&f, line) == IDLE_COMMAND_DONE && output_used);
            assert(!strstr(output, brands[1 - brand]));
        }
        assert(strstr(output, brands[brand]));
        assert(strstr(output, "--close-session NUMBER") && strstr(output, "attached and detached"));
        assert(strstr(output, "job ID, not a process ID") && strstr(output, "verified completion"));
        if (brand) assert(strstr(output, "-N --resume NUMBER") && strstr(output, "retains the worker's current interface"));
        else assert(!strstr(output, "-N --resume NUMBER") && strstr(output, "always uses idle"));
        assert(!memcmp(&before, &app(&f)->cfg, sizeof before));
        assert(!app(&f)->control_count && !app(&f)->chat_count && !app(&f)->net && !app(&f)->sim);
    }
    cleanup(&f);
}

static void strict_input_and_safe_results(void)
{
    Fixture f; init(&f, 1);
    const char *bad[] = {"/background now", "/clear now", "/channels more", "/cl more", "/ql more", "/queries more", "/sl more", "/lc", "/lq", "/ls unknown", "/panel extra", "/quickstart extra",
        "/ls servers more", "/users on", "/server 0 extra", "/server close 0 extra", "/server add host 0 nick",
        "/server add host 65536 nick", "/server add host 6697 nick extra", "/server add host not-a-port",
        "/autoconnect on extra", "/autojoin clear extra", "/autojoin add #a extra", "/autojoin add bad",
        "/autojoin #nastycode,#vipirc", "/autojoin \"#nastycode,#vipirc\"",
        "/autoauth off extra", "/autoauth sasl account plaintext SUPER-SECRET", "/set send_mode direct extra",
        "/set send_mode wrong", "/set composer_mode wrong", "/set composer_mode line extra", "/set output_format wrong", "/set output_format full extra", "/set output_format", "/save output extra", "/watch list extra", "/watch unknown text", "/ignore list extra",
        "/ignore clear extra", "/ignore add nick extra", "/join #a key extra",
        "/whois nick extra", "/nick nick extra", "/pm nick extra", "/close a b", "/save input extra",
        "/connect 0 extra", "/connect host 0", "/connect host 65536", "/connect host +0", "/connect host +65536",
        "/connect host not-a-port", "/connect host 6697 SUPER-SECRET", "/server host 6697 SUPER-SECRET",
        "/connect -plain -tls host", "/connect -plain host +6697", "/connect -nick", "/connect -nick Nick",
        "/connect -nick #bad host", "/connect -unknown host", "/connect irc://host", "/connect host/elsewhere",
        "/connect -plain", "/connect -tls", "/query nick extra", "/query", "/saveconf extra",
        "/disconnect 0 extra", "/save unknown", "/pms", "/raw QUIT", "/shell ls",
        "/help\r/quit", "/help\x1b[2J", "/help\n/quit", "/help\tcommands", "/help \xc0\x80", "ordinary text"};
    Config before = app(&f)->cfg, pending = app(&f)->config_pending;
    uint64_t token = f.client->next_token;
    unsigned pending_mask = app(&f)->config_pending_mask;
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        memset(&f.plan, 'X', sizeof f.plan);
        assert(run(&f, bad[i]) == IDLE_COMMAND_ERROR);
        assert(!f.plan.count && !f.plan.text[0]);
        assert(!memcmp(&before, &app(&f)->cfg, sizeof before));
        assert(!memcmp(&pending, &app(&f)->config_pending, sizeof pending));
        assert(app(&f)->config_pending_mask == pending_mask && !app(&f)->quit);
        assert(f.client->current == 0 && session_count(&f) == 1 && f.client->next_token == token);
        assert(!strstr(output, "SUPER-SECRET"));
    }
    char *oversized = malloc(APP_TEXT + 3u); assert(oversized);
    memset(oversized, 'a', APP_TEXT + 2u); oversized[0] = '/'; oversized[APP_TEXT + 2u] = 0;
    assert(run(&f, oversized) == IDLE_COMMAND_ERROR); free(oversized);
    assert(run(&f, "/quit Client quit") == IDLE_COMMAND_QUIT && app(&f)->quit);
    assert(run(&f, "/background") == IDLE_COMMAND_BACKGROUND);
    assert(run(&f, "/clear") == IDLE_COMMAND_CLEAR);
    cleanup(&f);
}

static void direct_connection_commands(void)
{
    Fixture f; init_empty(&f, "UnixLogin");
    App *primary = app(&f);
    assert(!primary->cfg.server[0] && !strcmp(primary->cfg.nick, "UnixLogin"));
    f.client->default_unavailable = 1;
    assert(run(&f, "/connect") == IDLE_COMMAND_ERROR && !primary->sim && !primary->net);
    assert(run(&f, "/connect first.invalid") == IDLE_COMMAND_DONE);
    assert(f.client->current == 0 && session_count(&f) == 1 && app(&f) == primary);
    assert(!strcmp(primary->cfg.server, "first.invalid") && primary->cfg.port == 6697 && primary->cfg.tls);
    assert(!strcmp(primary->cfg.nick, "UnixLogin") && !strcmp(primary->cfg.username, "UnixLogin"));
    assert(primary->sim && !primary->net && !primary->cfg.autoconnect);
    pump(primary); assert(primary->registered);
    assert(run(&f, "/query Alice") == IDLE_COMMAND_DONE);
    assert(primary->buffers[primary->current].kind == B_QUERY && !strcmp(primary->buffers[primary->current].name, "Alice"));
    uint64_t connection = primary->connection_id;
    assert(run(&f, "/connect FIRST.INVALID") == IDLE_COMMAND_DONE);
    assert(f.client->current == 0 && primary->current == 0 && session_count(&f) == 1);
    assert(primary->connection_id == connection && primary->registered);

    strcpy(primary->cfg.auth, "sasl"); strcpy(primary->cfg.auth_user, "first-account");
    strcpy(primary->cfg.password, "first-only-secret");
    strcpy(primary->cfg.server_password, "first-only-server-secret");
    strcpy(primary->cfg.ca_file, "/first-only/ca.pem");
    strcpy(primary->cfg.channels, "#first-private");
    Config original = primary->cfg;
    assert(run(&f, "/server -plain -nick OtherNick second.invalid") == IDLE_COMMAND_DONE);
    App *second = app(&f);
    assert(f.client->current == 1 && second && second != primary && session_count(&f) == 2);
    assert(!strcmp(second->cfg.server, "second.invalid") && !second->cfg.tls && second->cfg.port == 6667);
    assert(!strcmp(second->cfg.nick, "OtherNick") && !second->cfg.autoconnect);
    assert(!strcmp(second->cfg.auth, "none") && !second->cfg.auth_user[0] && !second->cfg.password[0]);
    assert(!second->cfg.password_file[0] && !second->cfg.password_gpg_file[0]);
    assert(!second->cfg.server_password[0] && !second->cfg.server_password_file[0] && !second->cfg.server_password_gpg_file[0]);
    assert(!second->cfg.channels[0] && !second->cfg.ca_file[0]);
    assert(!memcmp(&original, &primary->cfg, sizeof original) && primary->registered && primary->connection_id == connection);
    pump(second); assert(second->registered);
    uint64_t second_connection = second->connection_id;
    assert(run(&f, "/connect second.invalid") == IDLE_COMMAND_DONE);
    assert(app(&f) == second && !second->cfg.tls && second->cfg.port == 6667 && second->connection_id == second_connection);
    assert(run(&f, "/connect -tls second.invalid 6667") == IDLE_COMMAND_DONE);
    App *third = app(&f);
    assert(third != second && third->cfg.tls && third->cfg.port == 6667 && session_count(&f) == 3);
    assert(!strcmp(third->cfg.nick, "UnixLogin") && !third->cfg.password[0] && !third->cfg.server_password[0]);
    assert(second->connection_id == second_connection && second->registered);
    assert(run(&f, "/connect third.invalid +7000") == IDLE_COMMAND_DONE);
    assert(app(&f)->cfg.tls && app(&f)->cfg.port == 7000 && session_count(&f) == 4);
    assert(run(&f, "/connect 0") == IDLE_COMMAND_DONE && app(&f) == primary && primary->current == 0);
    assert(primary->connection_id == connection && !memcmp(&original, &primary->cfg, sizeof original));
    assert(run(&f, "/saveconf") == IDLE_COMMAND_ERROR && access(f.path, F_OK) != 0);
    assert(!strstr(output, "first-only-secret") && !strstr(output, "first-only-server-secret"));
    cleanup(&f);

    const char *invalid_logins[] = {"not a valid login", "123starts-with-digit", ""};
    for (size_t i = 0; i < sizeof invalid_logins / sizeof invalid_logins[0]; ++i) {
        init_empty(&f, invalid_logins[i]);
        assert(run(&f, "/connect fallback.invalid") == IDLE_COMMAND_DONE);
        assert(!strcmp(app(&f)->cfg.nick, "user"));
        cleanup(&f);
    }
    init_empty(&f, "UnixLogin");
    assert(run(&f, "/nick ChosenBeforeConnecting") == IDLE_COMMAND_DONE);
    assert(run(&f, "/connect preserved.invalid") == IDLE_COMMAND_DONE);
    assert(f.client->current == 0 && !strcmp(app(&f)->cfg.nick, "ChosenBeforeConnecting"));
    cleanup(&f);
    puts("native connect: empty primary reuse, Unix/default nick, explicit nick flag, /server hostname and /query, idempotent matching session, endpoint isolation, secure transport defaults, stable numeric compatibility and test-mode no-save passed");
}

static void pending_empty_host_preserves_session_identity(void)
{
    Fixture f; init(&f, 1);
    App *original = app(&f);
    assert(run(&f, "/connect") == IDLE_COMMAND_DONE);
    pump(original); assert(original->registered);
    assert(run(&f, "/query OldContact") == IDLE_COMMAND_DONE);
    size_t old_query = original->current;
    app_log(original, old_query, "private old-session history");
    strcpy(original->cfg.auth, "sasl");
    strcpy(original->cfg.auth_user, "old-account");
    strcpy(original->cfg.password, "old-peer-only-secret");
    strcpy(original->cfg.server_password, "old-server-only-secret");
    Config desired = original->cfg;
    desired.server[0] = 0;
    char error[512];
    assert(app_config_apply(original, &desired, 0, error, sizeof error));
    Config live = original->cfg, pending = original->config_pending;
    unsigned mask = original->config_pending_mask;
    uint64_t connection = original->connection_id;
    assert(run(&f, "/connect fixture.invalid") == IDLE_COMMAND_DONE);
    assert(app(&f) == original && original->current == 0 && original->registered);
    assert(original->connection_id == connection && session_count(&f) == 1);
    assert(!memcmp(&live, &original->cfg, sizeof live) && !memcmp(&pending, &original->config_pending, sizeof pending));
    assert(original->config_pending_mask == mask);
    assert(run(&f, "/disconnect") == IDLE_COMMAND_DONE);
    assert(!original->sim && !original->net);
    assert(run(&f, "/connect fresh.invalid") == IDLE_COMMAND_DONE);
    App *fresh = app(&f);
    assert(fresh != original && session_count(&f) == 2 && f.client->current == 1);
    assert(!strcmp(fresh->cfg.server, "fresh.invalid") && !fresh->cfg.channels[0]);
    assert(!strcmp(fresh->cfg.auth, "none") && !fresh->cfg.auth_user[0]);
    assert(!fresh->cfg.password[0] && !fresh->cfg.server_password[0]);
    assert(app_buffer(fresh, "OldContact", B_QUERY, 0) == SIZE_MAX);
    assert(original->buffers[old_query].line_count);
    assert(!memcmp(&live, &original->cfg, sizeof live) && !memcmp(&pending, &original->config_pending, sizeof pending));
    assert(original->config_pending_mask == mask);
    cleanup(&f);
    puts("native endpoint identity: pending empty hostname does not hide a live peer or turn an existing primary into a credential/history donor for a new server passed");
}

static void startup_transactions_and_redaction(void)
{
    Fixture f; init(&f, 1); App *a = app(&f);
    Config live = a->cfg, effective;
    assert(run(&f, "/autojoin add #future") == IDLE_COMMAND_DONE);
    app_config_effective(a, &effective);
    assert(!strcmp(effective.channels, "#test,#future") && !strcmp(a->cfg.channels, live.channels));
    assert(run(&f, "/autojoin add #FUTURE") == IDLE_COMMAND_DONE);
    app_config_effective(a, &effective); assert(!strcmp(effective.channels, "#test,#future"));
    Config pending = a->config_pending; unsigned mask = a->config_pending_mask;
    assert(run(&f, "/autojoin remove #missing") == IDLE_COMMAND_ERROR);
    assert(!memcmp(&pending, &a->config_pending, sizeof pending) && mask == a->config_pending_mask);
    assert(run(&f, "/autojoin remove #TEST") == IDLE_COMMAND_DONE);
    app_config_effective(a, &effective); assert(!strcmp(effective.channels, "#future"));
    assert(run(&f, "/autojoin clear") == IDLE_COMMAND_DONE);
    app_config_effective(a, &effective); assert(!effective.channels[0] && !strcmp(a->cfg.channels, "#test"));
    assert(run(&f, "/autoconnect on") == IDLE_COMMAND_DONE && a->cfg.autoconnect && !a->net && !a->sim);

    char fifo[256], command[512]; snprintf(fifo, sizeof fifo, "%s/credential fifo", f.directory);
    assert(mkfifo(fifo, 0600) == 0);
    snprintf(command, sizeof command, "/autoauth sasl account gpg %s", fifo);
    assert(run(&f, command) == IDLE_COMMAND_DONE); /* A credential read would block on this FIFO. */
    app_config_effective(a, &effective);
    assert(!strcmp(effective.auth, "sasl") && !strcmp(effective.password_gpg_file, fifo));
    assert(!strcmp(a->cfg.auth, "none") && !a->credential && !a->net && !a->sim);
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE && strstr(output, "GPG file (redacted)"));
    assert(!strstr(output, fifo) && !strstr(output, "credential fifo"));
    assert(run(&f, "/autoauth status") == IDLE_COMMAND_DONE && strstr(output, fifo));
    assert(strstr(output, "Next SASL GPG file:") && strstr(output, "No credential file was read"));
    assert(run(&f, "/autoauth off") == IDLE_COMMAND_DONE);
    app_config_effective(a, &effective);
    assert(!strcmp(effective.auth, "none") && !effective.password[0] && !effective.password_file[0] && !effective.password_gpg_file[0]);
    assert(unlink(fifo) == 0);

    strcpy(a->cfg.password, "fixture-secret-never-display");
    strcpy(a->cfg.server_password_gpg_file, "/private/credential-path-never-display.gpg");
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE);
    assert(!strstr(output, "fixture-secret-never-display") && !strstr(output, "credential-path-never-display"));
    config_wipe(a->cfg.password, sizeof a->cfg.password);
    a->cfg.server_password_gpg_file[0] = 0; a->cfg.tls = 0;
    live = a->cfg; pending = a->config_pending; mask = a->config_pending_mask;
    assert(run(&f, "/autoauth sasl account file /nonexistent") == IDLE_COMMAND_ERROR);
    assert(!memcmp(&live, &a->cfg, sizeof live) && !memcmp(&pending, &a->config_pending, sizeof pending) && mask == a->config_pending_mask);
    cleanup(&f);
}

static unsigned path_hex(char byte)
{
    assert((byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'F'));
    return (unsigned)(byte <= '9' ? byte - '0' : byte - 'A' + 10);
}

static void recover_path(const char *label, char *path, size_t capacity)
{
    size_t used = 0, label_size = strlen(label), pieces = 0;
    for (const char *line = output, *end; *line; line = end + 1) {
        end = strchr(line, '\n'); assert(end);
        if ((size_t)(end - line) <= label_size || strncmp(line, label, label_size)) continue;
        const char *data = line + label_size;
        if (!strncmp(data, ": ", 2)) data += 2;
        else if (!strncmp(data, " [bytes ", 8)) {
            data = strstr(data, "]: "); assert(data && data < end); data += 3;
        } else continue;
        assert((size_t)(end - line) < 512); /* Every fragment survives narrow notice buffers. */
        ++pieces;
        while (data < end) {
            unsigned char byte = (unsigned char)*data++;
            if (byte == '\\') {
                assert(data < end);
                if (*data == '\\') ++data;
                else {
                    assert(*data++ == 'x' && end - data >= 2);
                    byte = (unsigned char)(path_hex(data[0]) * 16 + path_hex(data[1]));
                    data += 2;
                }
            }
            assert(used + 1 < capacity);
            path[used++] = (char)byte;
        }
    }
    assert(pieces); path[used] = 0;
}

static void startup_inquiries_are_complete_and_local(void)
{
    Fixture f; init(&f, 1); App *a = app(&f);
    assert(run(&f, "/autojoin add #nastycode") == IDLE_COMMAND_DONE);
    assert(run(&f, "/autojoin add #vipirc") == IDLE_COMMAND_DONE);
    size_t selected = app_buffer(a, "kept-draft-peer", B_QUERY, 1);
    assert(selected != SIZE_MAX); a->current = selected;
    a->buffers[selected].unread = 7; a->buffers[selected].unread_mentions = 2;
    App before = *a;
    assert(run(&f, "/autojoin list") == IDLE_COMMAND_DONE);
    assert(strstr(output, "Server 0: fixture.invalid:") && strstr(output, "registered: no"));
    assert(strstr(output, "Running auto-join: #test"));
    assert(strstr(output, "Pending auto-join for next explicit /connect: #test,#nastycode,#vipirc"));
    assert(strstr(output, "automatic retries") && strstr(output, "successful IRC registration"));
    assert(!memcmp(&before, a, sizeof before) && f.client->current == 0);

    /* Maximum-length paths are inspectable without opening even a nonexistent
     * path. Include UTF-8 and backslash bytes at fragment boundaries. */
    strcpy(a->cfg.auth, "sasl");
    strcpy(a->cfg.auth_user, "fixture-account");
    strcpy(a->cfg.password_file, "/SOURCE_SASL_PATH/");
    size_t start = strlen(a->cfg.password_file);
    for (size_t i = start; i + 1 < sizeof a->cfg.password_file; ++i)
        a->cfg.password_file[i] = (char)('a' + i % 26);
    a->cfg.password_file[sizeof a->cfg.password_file - 1] = 0;
    a->cfg.password_file[383] = (char)0xc3; a->cfg.password_file[384] = (char)0xa9;
    a->cfg.password_file[700] = '\\';
    memcpy(a->cfg.server_password_gpg_file, a->cfg.password_file, sizeof a->cfg.server_password_gpg_file);
    a->cfg.server_password_gpg_file[8] = 'G';
    before = *a;
    assert(run(&f, "/autoauth status") == IDLE_COMMAND_DONE);
    char recovered[CONFIG_PATH_MAX];
    recover_path("SASL password file", recovered, sizeof recovered);
    assert(!strcmp(recovered, a->cfg.password_file));
    recover_path("Server-password GPG file", recovered, sizeof recovered);
    assert(!strcmp(recovered, a->cfg.server_password_gpg_file));
    assert(!memcmp(&before, a, sizeof before));
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE && !strstr(output, "/SOURCE_"));
    assert(run(&f, "/summary") == IDLE_COMMAND_DONE && !strstr(output, "/SOURCE_"));

    Config next; app_config_effective(a, &next);
    next.password_file[0] = next.server_password_gpg_file[0] = 0;
    strcpy(next.password, "SASL_CONTENT_NEVER_SHOWN");
    strcpy(next.server_password, "SERVER_CONTENT_NEVER_SHOWN");
    char error[512];
    assert(app_config_apply(a, &next, 0, error, sizeof error));
    before = *a;
    assert(run(&f, "/autoauth") == IDLE_COMMAND_DONE);
    assert(strstr(output, "Pending authentication for the next explicit /connect:"));
    recover_path("Current SASL password file", recovered, sizeof recovered);
    assert(!strcmp(recovered, a->cfg.password_file));
    assert(!strstr(output, "SASL_CONTENT_NEVER_SHOWN") && !strstr(output, "SERVER_CONTENT_NEVER_SHOWN"));
    assert(!memcmp(&before, a, sizeof before));
    config_wipe(&next, sizeof next); config_wipe(recovered, sizeof recovered);

    assert(run(&f, "/server add second.invalid") == IDLE_COMMAND_DONE);
    App *second = client_get_app(f.client, 1, error, sizeof error); assert(second);
    f.client->current = 1;
    assert(run(&f, "/autojoin add #other-server") == IDLE_COMMAND_DONE);
    App second_before = *second;
    assert(run(&f, "/autojoin") == IDLE_COMMAND_DONE);
    assert(strstr(output, "Server 1: second.invalid:") && strstr(output, "#other-server"));
    assert(!strstr(output, "#nastycode") && !strstr(output, "#vipirc"));
    assert(!memcmp(&second_before, second, sizeof second_before) && !memcmp(&before, a, sizeof before));
    assert(run(&f, "/autoauth status") == IDLE_COMMAND_DONE && !strstr(output, "/SOURCE_"));
    cleanup(&f);
}

static void server_views_and_global_input(void)
{
    Fixture f; init(&f, 1); App *primary = app(&f);
    assert(run(&f, "/server add second.invalid 6697 othernick") == IDLE_COMMAND_DONE);
    assert(f.client->current == 0 && f.client->slots[1].used && !f.client->slots[1].app);
    assert(!f.client->slots[1].seed.autoconnect && f.client->slots[1].seed.tls);
    assert(!strcmp(f.client->slots[1].seed.auth, "none") && !f.client->slots[1].seed.password[0]);
    assert(run(&f, "/server 1") == IDLE_COMMAND_DONE && f.client->current == 1 && app(&f));
    assert(!app(&f)->sim && !app(&f)->net);
    Config secondary = app(&f)->cfg;
    assert(run(&f, "/set send_mode review") == IDLE_COMMAND_DONE && primary->cfg.send_review);
    assert(!memcmp(&secondary, &app(&f)->cfg, sizeof secondary));
    assert(strstr(output, "/save input"));
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE && strstr(output, "send mode: review"));
    assert(run(&f, "/set send_mode direct") == IDLE_COMMAND_DONE && !primary->cfg.send_review);
    assert(run(&f, "/set output_format full") == IDLE_COMMAND_DONE && primary->cfg.output_full);
    assert(!memcmp(&secondary, &app(&f)->cfg, sizeof secondary));
    assert(strstr(output, "/save output"));
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE && strstr(output, "full"));
    assert(run(&f, "/set output_format compact") == IDLE_COMMAND_DONE && !primary->cfg.output_full);
    assert(run(&f, "/save output") == IDLE_COMMAND_ERROR && access(f.path, F_OK) != 0);
    assert(run(&f, "/save input") == IDLE_COMMAND_ERROR && access(f.path, F_OK) != 0);
    assert(run(&f, "/save all") == IDLE_COMMAND_ERROR && strstr(output, "0 profile(s); 2 failed"));
    assert(run(&f, "/servers") == IDLE_COMMAND_DONE && strstr(output, "second.invalid") && strstr(output, "0:"));
    char servers_output[4096]; assert(strlen(output) < sizeof servers_output);
    strcpy(servers_output, output);
    size_t control_count = app(&f)->control_count, chat_count = app(&f)->chat_count;
    assert(run(&f, "/sl") == IDLE_COMMAND_DONE && !strcmp(output, servers_output));
    assert(app(&f)->control_count == control_count && app(&f)->chat_count == chat_count);
    assert(run(&f, "/channels") == IDLE_COMMAND_DONE && strstr(output, "0: server"));
    char channels_output[4096]; assert(strlen(output) < sizeof channels_output);
    strcpy(channels_output, output);
    assert(run(&f, "/cl") == IDLE_COMMAND_DONE && !strcmp(output, channels_output));
    assert(app(&f)->control_count == control_count && app(&f)->chat_count == chat_count);
    assert(run(&f, "/ls channels") == IDLE_COMMAND_DONE && !strcmp(output, channels_output));
    assert(run(&f, "/help cl") == IDLE_COMMAND_DONE && strstr(output, "Usage: /cl"));
    assert(run(&f, "/help sl") == IDLE_COMMAND_DONE && strstr(output, "Usage: /sl"));
    assert(run(&f, "/help queries") == IDLE_COMMAND_DONE && strstr(output, "Usage: /queries"));
    assert(run(&f, "/pm alice") == IDLE_COMMAND_DONE);
    assert(app(&f)->buffers[app(&f)->current].kind == B_QUERY && !app(&f)->chat_count);
    assert(run(&f, "/ql") == IDLE_COMMAND_DONE && strstr(output, "alice") && !strstr(output, "0: server"));
    char queries_output[4096]; assert(strlen(output) < sizeof queries_output);
    strcpy(queries_output, output);
    assert(run(&f, "/queries") == IDLE_COMMAND_DONE && !strcmp(output, queries_output));
    assert(app(&f)->control_count == control_count && app(&f)->chat_count == chat_count);
    assert(run(&f, "/mentions") == IDLE_COMMAND_DONE && app(&f)->current == 1);
    assert(run(&f, "/server") == IDLE_COMMAND_DONE && app(&f)->current == 0);
    assert(run(&f, "/server close 0") == IDLE_COMMAND_ERROR && f.client->slots[0].used);
    assert(run(&f, "/server close 1") == IDLE_COMMAND_DONE && f.client->current == 0 && !f.client->slots[1].used);
    assert(app(&f) == primary);
    cleanup(&f);
}

static void clear_mentions_across_servers(void)
{
    Fixture f; init(&f, 1);
    assert(run(&f, "/server add second.invalid") == IDLE_COMMAND_DONE);
    assert(run(&f, "/server add lazy.invalid") == IDLE_COMMAND_DONE);
    char error[512];
    App *sessions[2] = {app(&f), client_get_app(f.client, 1, error, sizeof error)};
    size_t views[2];
    assert(sessions[1] && !f.client->slots[2].app);
    for (size_t i = 0; i < 2; ++i) {
        views[i] = app_buffer(sessions[i], "kept-peer", B_QUERY, 1);
        assert(views[i] != SIZE_MAX);
        app_log(sessions[i], views[i], "SOURCE_HISTORY_KEPT_%zu", i);
        app_log(sessions[i], 1, "MENTION_COPY_REMOVED_%zu", i);
        sessions[i]->current = views[i];
        sessions[i]->buffers[views[i]].unread = 7;
        sessions[i]->buffers[views[i]].unread_mentions = 2;
    }
    const char *invalid[] = {"/mentions clear extra", "/mentions unknown", "/mentions clear\n/quit"};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        assert(run(&f, invalid[i]) == IDLE_COMMAND_ERROR);
        assert(sessions[0]->buffers[1].line_count == 1 && sessions[1]->buffers[1].line_count == 1);
    }
    assert(run(&f, "/mentions clear") == IDLE_COMMAND_MENTIONS_CLEAR);
    assert(strstr(output, "Cleared retained mentions across all servers."));
    for (size_t i = 0; i < 2; ++i) {
        Buffer *source = &sessions[i]->buffers[views[i]];
        assert(!sessions[i]->buffers[1].line_count && !sessions[i]->buffers[1].unread);
        assert(source->line_count == 1 && strstr(source->lines[source->line_start], "SOURCE_HISTORY_KEPT_"));
        assert(source->unread == 7 && source->unread_mentions == 2);
        assert(sessions[i]->current == views[i]);
        assert(!sessions[i]->control_count && !sessions[i]->chat_count && !sessions[i]->net && !sessions[i]->sim);
    }
    assert(f.client->current == 0 && !f.client->slots[2].app);
    assert(run(&f, "/MENTIONS CLEAR") == IDLE_COMMAND_MENTIONS_CLEAR);
    cleanup(&f);
    puts("mentions clear: all live servers cleared; source history/unread counts, selections and lazy profiles preserved; invalid arguments rejected");
}

static void deferred_selection_keeps_hidden_views_unread(void)
{
    Fixture f; init(&f, 1);
    App *primary = app(&f);
    assert(run(&f, "/server add second.invalid 6697 othernick") == IDLE_COMMAND_DONE);
    assert(run(&f, "/connect 1") == IDLE_COMMAND_DONE);
    App *second = app(&f);
    pump(second);
    assert(second->registered && second->sim && !second->net);
    size_t channel = app_buffer(second, "#hidden", B_CHANNEL, 1);
    size_t query = app_buffer(second, "HiddenPeer", B_QUERY, 1);
    assert(channel != SIZE_MAX && query != SIZE_MAX);
    f.commands.defer_visibility = 1;
    static const char *const actions[] = {"/server 1", "/connect 1", "/connect second.invalid", "/connect"};
    for (size_t i = 0; i < sizeof actions / sizeof actions[0]; ++i) {
        size_t selected_view = i % 2 ? query : channel;
        app_set_visible(second, 0);
        app_switch(second, selected_view);
        second->buffers[0].unread = 2;
        second->buffers[0].unread_mentions = 1;
        second->buffers[selected_view].unread = 7;
        second->buffers[selected_view].unread_mentions = 3;
        app_set_visible(primary, 1);
        f.client->current = i == 3 ? 1 : 0;
        assert(run(&f, actions[i]) == IDLE_COMMAND_DONE);
        assert(f.client->current == 1 && second->current == selected_view);
        assert(!second->visible && primary->visible);
        assert(second->buffers[0].unread == 2 && second->buffers[0].unread_mentions == 1);
        assert(second->buffers[selected_view].unread == 7 && second->buffers[selected_view].unread_mentions == 3);
    }
    f.client->default_unavailable = 1;
    assert(run(&f, "/connect") == IDLE_COMMAND_ERROR && f.client->default_unavailable);
    assert(second->current == query && !second->visible && second->buffers[query].unread == 7);
    assert(run(&f, "/connect 1") == IDLE_COMMAND_DONE && !f.client->default_unavailable);
    assert(second->current == query && !second->visible && second->buffers[query].unread == 7);
    assert(run(&f, "/server") == IDLE_COMMAND_DONE);
    assert(second->current == query && !second->visible && second->buffers[query].unread == 7);
    /* A successful deferred UI commit reads only its actual destination,
     * leaving the hidden conversation's activity untouched. */
    app_set_visible(primary, 0);
    app_switch(second, 0);
    assert(second->buffers[0].unread == 2);
    app_set_visible(second, 1);
    assert(!second->buffers[0].unread && !second->buffers[0].unread_mentions);
    assert(second->buffers[query].unread == 7 && second->buffers[query].unread_mentions == 3);
    /* Native idle mode still commits its own visible server-message view. */
    app_set_visible(second, 0);
    app_switch(second, query);
    second->buffers[0].unread = 2;
    f.commands.defer_visibility = 0;
    assert(run(&f, "/connect 1") == IDLE_COMMAND_DONE);
    assert(second->current == 0 && second->visible && !second->buffers[0].unread);
    assert(second->buffers[query].unread == 7 && second->buffers[query].unread_mentions == 3);
    assert(!second->chat_count && !primary->chat_count);
    cleanup(&f);
}

static void setup_requests_are_side_effect_free(void)
{
    Fixture f; init(&f, 1);
    App *a = app(&f);
    Config before = a->cfg, seed = f.client->slots[0].seed;
    size_t view = a->current, server = f.client->current;
    size_t controls = a->control_count, chats = a->chat_count;
    assert(run(&f, "/quickstart") == IDLE_COMMAND_QUICKSTART);
    assert(run(&f, "/QUICKSTART") == IDLE_COMMAND_QUICKSTART);
    assert(run(&f, "/panel") == IDLE_COMMAND_PANEL);
    assert(run(&f, "/PANEL") == IDLE_COMMAND_PANEL);
    assert(!memcmp(&before, &a->cfg, sizeof before));
    assert(!memcmp(&seed, &f.client->slots[0].seed, sizeof seed));
    assert(!a->config_pending_mask && !a->net && !a->sim && !a->credential);
    assert(a->current == view && f.client->current == server);
    assert(a->control_count == controls && a->chat_count == chats);
    assert(access(f.path, F_OK) != 0);
    cleanup(&f);
}

static void send_plans_require_frontend_consent(void)
{
    Fixture f; init(&f, 1); App *a = app(&f);
    assert(run(&f, "/connect") == IDLE_COMMAND_DONE); pump(a);
    assert(a->registered && a->sim && !a->net);
    size_t channel = app_buffer(a, "#test", B_CHANNEL, 0);
    assert(channel != SIZE_MAX && a->buffers[channel].joined);
    app_switch(a, channel);
    size_t queued = a->chat_count;
    assert(run(&f, "/msg alice hello") == IDLE_COMMAND_SEND);
    assert(!strcmp(f.plan.target, "alice") && !strcmp(f.plan.text, "hello") && f.plan.count == 1 && a->chat_count == queued);
    assert(a->current == channel);
    assert(run(&f, "/me waves") == IDLE_COMMAND_SEND && f.plan.action && a->chat_count == queued);
    size_t budget = app_budget(a, "#test", 0);
    char line[2048]; assert(budget + 100 < sizeof line);
    strcpy(line, "/msg #test "); size_t prefix = strlen(line);
    memset(line + prefix, 'x', budget + 1); line[prefix + budget + 1] = 0;
    assert(run(&f, line) == IDLE_COMMAND_SEND && f.plan.count == 2 && a->chat_count == queued);
    assert(run(&f, "/set send_mode review") == IDLE_COMMAND_DONE && a->cfg.send_review);
    assert(run(&f, "/msg alice hello") == IDLE_COMMAND_SEND && a->chat_count == queued);
    assert(app_send_plan(a, &f.plan)); assert(a->chat_count == queued + 1); /* Explicit frontend consent. */
    assert(run(&f, "/cancel") == IDLE_COMMAND_DONE && a->chat_count == 0);
    assert(run(&f, "/users #test") == IDLE_COMMAND_DONE && strstr(output, "Users in #test"));
    assert(run(&f, "/watch add release day") == IDLE_COMMAND_DONE);
    assert(run(&f, "/watch list") == IDLE_COMMAND_DONE && strstr(output, "release day"));
    assert(run(&f, "/ignore add noisy!*@*") == IDLE_COMMAND_DONE);
    assert(run(&f, "/ignore list") == IDLE_COMMAND_DONE && strstr(output, "noisy!*@*"));
    assert(run(&f, "/unignore noisy!*@*") == IDLE_COMMAND_DONE && a->cfg.ignore_count == 0);
    cleanup(&f);
}

static void history_policy(void)
{
    const char *safe[] = {"/help ls", "/panel", "/quickstart", "/autoconnect on", "/autojoin add #a", "/server 1", "/connect irc.example.org", "/server -tls -nick ChosenNick irc.example.org +6697", "/query alice", "/saveconf", "/set send_mode review", "/join #a", "/msg alice hello", "/watch add release day"};
    const char *unsafe[] = {"/autoauth status", "/autoauth sasl account file /secret", "/connect irc.example.org 6697 SUPER-SECRET", "/server irc.example.org 6697 SUPER-SECRET", "/join #a key", "/JOIN #a key", "/msg NickServ IDENTIFY secret", "/msg ns@server identify secret", "/msg alice LOGIN secret", "/msg alice :AUTH secret", "/msg alice SET PASSWORD secret", "/msg q hello", "/unknown password", "/help\n/quit", "/help \x1b[2J"};
    for (size_t i = 0; i < sizeof safe / sizeof safe[0]; ++i) assert(idle_command_recordable(safe[i]));
    for (size_t i = 0; i < sizeof unsafe / sizeof unsafe[0]; ++i) assert(!idle_command_recordable(unsafe[i]));
}

static void persistent_primary_and_failure(void)
{
    Fixture f; init(&f, 0); App *a = app(&f);
    assert(run(&f, "/set send_mode review") == IDLE_COMMAND_DONE);
    assert(run(&f, "/set output_format full") == IDLE_COMMAND_DONE);
    assert(run(&f, "/autoconnect on") == IDLE_COMMAND_DONE);
    assert(run(&f, "/autojoin add #future") == IDLE_COMMAND_DONE);
    assert(run(&f, "/save input") == IDLE_COMMAND_DONE);
    Config loaded; config_minimal_defaults(&loaded); char error[512];
    assert(config_load(&loaded, f.path, error, sizeof error) == 1);
    assert(loaded.send_review && loaded.output_full && loaded.autoconnect && !strcmp(loaded.channels, "#test,#future"));
    struct stat st; assert(stat(f.path, &st) == 0 && (st.st_mode & 0777) == 0600);
    assert(!strcmp(a->cfg.channels, "#test"));
    assert(run(&f, "/set output_format compact") == IDLE_COMMAND_DONE);
    assert(run(&f, "/save output") == IDLE_COMMAND_DONE);
    config_minimal_defaults(&loaded);
    assert(config_load(&loaded, f.path, error, sizeof error) == 1 && !loaded.output_full && loaded.send_review);
    assert(unlink(f.path) == 0 && mkdir(f.path, 0700) == 0);
    Config before = a->cfg, pending = a->config_pending;
    unsigned mask = a->config_pending_mask;
    assert(run(&f, "/save") == IDLE_COMMAND_ERROR && strstr(output, "save did not complete"));
    assert(!memcmp(&before, &a->cfg, sizeof before) && !memcmp(&pending, &a->config_pending, sizeof pending) && mask == a->config_pending_mask);
    assert(rmdir(f.path) == 0);
    cleanup(&f);
}

static void persistent_saveconf_and_unreadable_profile(void)
{
    Fixture f; init(&f, 0);
    char error[512], selected_path[CONFIG_PATH_MAX], broken_path[CONFIG_PATH_MAX];
    App *primary = app(&f);
    strcpy(primary->cfg.auth, "sasl");
    strcpy(primary->cfg.auth_user, "primary-account");
    strcpy(primary->cfg.password, "primary-only-secret");
    assert(run(&f, "/server add saved.invalid 6697 SelectedNick") == IDLE_COMMAND_DONE);
    /* Frontend visibility policy cannot change which selected profile
     * /saveconf persists as the next launch's connection default. */
    f.commands.defer_visibility = 1;
    assert(run(&f, "/server 1") == IDLE_COMMAND_DONE);
    assert(run(&f, "/autojoin add #saved") == IDLE_COMMAND_DONE);
    assert(run(&f, "/set send_mode review") == IDLE_COMMAND_DONE);
    assert(run(&f, "/set output_format full") == IDLE_COMMAND_DONE);
    assert(run(&f, "/saveconf") == IDLE_COMMAND_DONE);
    assert(!strstr(output, "primary-only-secret") && !strstr(output, "primary-account"));
    assert(!primary->net && !primary->sim && !app(&f)->net && !app(&f)->sim);
    Config loaded;
    config_minimal_defaults(&loaded);
    assert(config_load(&loaded, f.path, error, sizeof error) == 1);
    assert(!strcmp(loaded.server, "fixture.invalid") && !strcmp(loaded.nick, "tester"));
    assert(!strcmp(loaded.password, "primary-only-secret") && loaded.send_review && loaded.output_full);
    assert(!loaded.autoconnect && !strcmp(loaded.channels, "#test"));
    ConfigProfiles profiles;
    assert(config_profiles_load(&profiles, f.path, error, sizeof error) == 1);
    assert(profiles.count == 1 && !strcmp(profiles.default_id, f.client->slots[1].profile_id));
    assert(config_profile_path(selected_path, sizeof selected_path, f.path, profiles.default_id, error, sizeof error) == 1);
    Config saved;
    config_minimal_defaults(&saved);
    assert(config_load(&saved, selected_path, error, sizeof error) == 1);
    assert(!strcmp(saved.server, "saved.invalid") && !strcmp(saved.nick, "SelectedNick"));
    assert(!strcmp(saved.channels, "#saved") && !saved.autoconnect && !saved.password[0] && !saved.server_password[0]);

    /* One unreadable saved profile must not undo the healthy writes, change
     * the remembered default, or overwrite that unreadable profile. */
    Config broken;
    client_new_config(f.client, &broken);
    strcpy(broken.server, "unreadable.invalid");
    size_t index;
    assert(client_add(f.client, &broken, &index, error, sizeof error) == 1 && index == 2);
    assert(client_save(f.client, index, &broken, error, sizeof error) == 1);
    assert(config_profile_path(broken_path, sizeof broken_path, f.path, f.client->slots[index].profile_id, error, sizeof error) == 1);
    assert(!chmod(broken_path, 0666));
    client_free(f.client);
    assert(client_init(f.client, &loaded, f.path, "tester", 0, 0, error, sizeof error) == 1);
    assert(f.client->current == 1 && !f.client->default_unavailable && f.client->slots[2].load_failed);
    assert(run(&f, "/set output_format compact") == IDLE_COMMAND_DONE);
    assert(run(&f, "/saveconf") == IDLE_COMMAND_ERROR);
    assert(strstr(output, "profile could not be loaded") && strstr(output, "save incomplete") && strstr(output, "remembered"));
    assert(!strstr(output, "primary-only-secret"));
    struct stat st;
    assert(stat(broken_path, &st) == 0 && (st.st_mode & 0777) == 0666);
    config_minimal_defaults(&loaded);
    assert(config_load(&loaded, f.path, error, sizeof error) == 1 && !loaded.output_full && loaded.send_review);
    assert(!strcmp(loaded.password, "primary-only-secret"));
    assert(config_profiles_load(&profiles, f.path, error, sizeof error) == 1 && profiles.count == 2);
    assert(!strcmp(profiles.default_id, f.client->slots[1].profile_id));
    assert(!unlink(selected_path) && !unlink(broken_path));
    config_wipe(&loaded, sizeof loaded); config_wipe(&saved, sizeof saved); config_wipe(&broken, sizeof broken);
    cleanup(&f);
    puts("native saveconf: application preferences, independent credentials, applied startup settings, stable selected default and honest partial failure without replacing unreadable profiles passed");
}

static void native_server_numbers(void)
{
    assert(idle_command_recordable("/server 8") && idle_command_recordable("/connect 8"));
    assert(!idle_command_recordable("/server 9") && !idle_command_recordable("/connect 9"));
    Fixture f; init(&f, 1);
    f.commands.server_base = 1;
    char error[512];
    for (size_t i = 1; i < CLIENT_MAX_SERVERS; ++i) {
        Config cfg; client_new_config(f.client, &cfg);
        snprintf(cfg.server, sizeof cfg.server, "native%zu.invalid", i);
        size_t index = SIZE_MAX;
        assert(client_add(f.client, &cfg, &index, error, sizeof error) == 1 && index == i);
        config_wipe(&cfg, sizeof cfg);
    }
    assert(run(&f, "/servers") == IDLE_COMMAND_DONE);
    assert(strstr(output, "  1: fixture.invalid:") && strstr(output, "  8: native7.invalid:"));
    assert(!strstr(output, "  0:"));
    const char *invalid[] = {"/server 0", "/connect 0", "/disconnect 0", "/server close 0",
                             "/server 9", "/connect 9", "/disconnect 9", "/server 999999999999999999999999"};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        assert(run(&f, invalid[i]) == IDLE_COMMAND_ERROR);
        assert(f.client->current == 0 && session_count(&f) == CLIENT_MAX_SERVERS);
        assert(!app(&f)->net && !app(&f)->sim && strstr(output, "Servers start at 1"));
    }
    assert(run(&f, "/server 8") == IDLE_COMMAND_DONE && f.client->current == 7);
    assert(strstr(output, "Selected server 8:"));
    assert(run(&f, "/channels") == IDLE_COMMAND_DONE && strstr(output, "Channels on server 8:"));
    assert(strstr(output, "  0: server")); /* Channel zero keeps its meaning. */
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE && strstr(output, "Application preferences:"));
    assert(strstr(output, "Effective settings for server 8") && !strstr(output, "profile 0"));
    assert(run(&f, "/server 1") == IDLE_COMMAND_DONE && f.client->current == 0);
    assert(run(&f, "/server connect 2") == IDLE_COMMAND_DONE && f.client->current == 0);
    assert(f.client->slots[1].app->sim && !f.client->slots[0].app->sim);
    assert(run(&f, "/disconnect 2") == IDLE_COMMAND_DONE && strstr(output, "Server 2:"));
    assert(!f.client->slots[1].app->sim && f.client->current == 0);
    assert(run(&f, "/server close 1") == IDLE_COMMAND_ERROR && f.client->slots[0].used);
    assert(run(&f, "/server close 2") == IDLE_COMMAND_DONE && !f.client->slots[1].used);
    assert(f.client->slots[7].used && f.client->current == 0);
    assert(run(&f, "/server 2") == IDLE_COMMAND_ERROR); /* A hole is not renumbered. */
    assert(run(&f, "/save") == IDLE_COMMAND_ERROR && strstr(output, "Server 1 save did not complete:"));
    f.commands.server_base = 0;
    assert(run(&f, "/server 0") == IDLE_COMMAND_DONE && f.client->current == 0);
    assert(run(&f, "/server 7") == IDLE_COMMAND_DONE && f.client->current == 7);
    assert(strstr(output, "Selected server 7:"));
    cleanup(&f);
    puts("server numbers: native 1..8 and panel-zero reservation, stable holes, channel-zero retention, selection/connect/disconnect/close/save routing and unchanged curses numbering passed");
}

static void aliases_queries_and_autocommands(void)
{
    Fixture f; init(&f, 1);
    assert(run(&f, "/help configure") == IDLE_COMMAND_DONE && strstr(output, "Usage: /panel"));
    assert(run(&f, "/help aliases") == IDLE_COMMAND_DONE && strstr(output, "/privmsg"));
    assert(run(&f, "/preferences") == IDLE_COMMAND_PANEL);
    assert(run(&f, "/con fixture.invalid") == IDLE_COMMAND_ERROR);
    assert(run(&f, "/autocommands") == IDLE_COMMAND_DONE && strstr(output, "delay 15 seconds"));
    assert(run(&f, "/autocommand add /msg NickServ IDENTIFY TEST-SECRET") == IDLE_COMMAND_DONE);
    Config state; app_config_effective(app(&f), &state);
    assert(state.autocommands.count == 1 && !strstr(output, "TEST-SECRET"));
    assert(!app(&f)->cfg.autocommands.count); /* Applied to the next connection. */
    assert(run(&f, "/autocommands add /j #private JOIN-SECRET") == IDLE_COMMAND_DONE);
    assert(run(&f, "/autocommands list") == IDLE_COMMAND_DONE && strstr(output, "1: /msg NickServ [text hidden]"));
    assert(!strstr(output, "TEST-SECRET") && !strstr(output, "JOIN-SECRET"));
    assert(run(&f, "/autocommands delay 20") == IDLE_COMMAND_DONE);
    assert(run(&f, "/autocommands interval 3") == IDLE_COMMAND_DONE);
    Config before; app_config_effective(app(&f), &before);
    const char *bad[] = {"/autocommands remove 0", "/autocommands remove 3", "/autocommands delay 301",
        "/autocommands interval 1", "/autocommands interval 61", "/autocommands on more",
        "/autocommands add /raw PASS TEST-SECRET", "/autocommands add /connect elsewhere",
        "/autocommands add /msg no-text", "/autocommands remove 999999999999999999999999999"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        assert(run(&f, bad[i]) == IDLE_COMMAND_ERROR);
        app_config_effective(app(&f), &state);
        assert(!memcmp(&before, &state, sizeof before));
        assert(!strstr(output, "TEST-SECRET"));
    }
    assert(run(&f, "/autocommands remove 1") == IDLE_COMMAND_DONE);
    app_config_effective(app(&f), &state);
    assert(state.autocommands.count == 1 && strstr(state.autocommands.lines[0], "#private"));
    assert(run(&f, "/autocommands clear") == IDLE_COMMAND_DONE);
    app_config_effective(app(&f), &state); assert(!state.autocommands.count);
    assert(run(&f, "/set queue_notices off") == IDLE_COMMAND_DONE && !app(&f)->cfg.queue_notices);
    assert(run(&f, "/set queue_notices wrong") == IDLE_COMMAND_ERROR);
    assert(run(&f, "/server add other.invalid") == IDLE_COMMAND_DONE);
    assert(!f.client->slots[1].seed.queue_notices);
    assert(run(&f, "/set queue_notices on") == IDLE_COMMAND_DONE && app(&f)->cfg.queue_notices);
    assert(f.client->slots[1].seed.queue_notices);
    assert(run(&f, "/query Alice") == IDLE_COMMAND_DONE);
    assert(run(&f, "/query Bob") == IDLE_COMMAND_DONE);
    assert(run(&f, "/querylist") == IDLE_COMMAND_DONE && strstr(output, "1: Alice") && strstr(output, "2: Bob"));
    assert(run(&f, "/query 1") == IDLE_COMMAND_DONE && !strcmp(app(&f)->buffers[app(&f)->current].name, "Alice"));
    assert(run(&f, "/pm 2") == IDLE_COMMAND_DONE && !strcmp(app(&f)->buffers[app(&f)->current].name, "Bob"));
    size_t count = app(&f)->buffer_count;
    assert(run(&f, "/query 0") == IDLE_COMMAND_ERROR && app(&f)->buffer_count == count);
    assert(run(&f, "/query 3") == IDLE_COMMAND_ERROR && app(&f)->buffer_count == count);
    assert(run(&f, "/query 99999999999999999999999999") == IDLE_COMMAND_ERROR && app(&f)->buffer_count == count);
    assert(run(&f, "/dcc") == IDLE_COMMAND_DCC);
    assert(run(&f, "/transfers list") == IDLE_COMMAND_DCC);
    assert(run(&f, "/exit") == IDLE_COMMAND_QUIT);
    assert(!idle_command_recordable("/j #private JOIN-SECRET"));
    assert(!idle_command_recordable("/message NickServ IDENTIFY TEST-SECRET"));
    assert(!idle_command_recordable("/privmsg Bot login TEST-SECRET"));
    assert(!idle_command_recordable("/autocommand add /msg bot TEST-SECRET"));
    assert(idle_command_recordable("/message Alice ordinary text"));
    config_wipe(&before, sizeof before);
    config_wipe(&state, sizeof state);
    cleanup(&f);
}

static void summary_without_selection(void)
{
    Fixture f; init(&f, 1); App *a = app(&f);
    assert(run(&f, "/server add second.invalid") == IDLE_COMMAND_DONE);
    assert(!f.client->slots[1].app);
    a->buffers[0].unread = 12; a->buffers[0].unread_mentions = 3;
    a->buffers[1].unread = a->buffers[1].unread_mentions = 999;
    a->visible = 0;
    App *before = malloc(sizeof *before); assert(before); memcpy(before, a, sizeof *a);
    f.client->current = 1;
    assert(run(&f, "/summary") == IDLE_COMMAND_DONE);
    assert(strstr(output, "Session summary") && strstr(output, "second.invalid"));
    assert(strstr(output, "Unread: 12 | mentions: 3"));
    assert(f.client->current == 1 && !f.client->slots[1].app && !memcmp(before, a, sizeof *a));
    assert(run(&f, "/summary extra") == IDLE_COMMAND_ERROR && strstr(output, "Usage: /summary"));
    f.client->current = SIZE_MAX;
    assert(run(&f, "/summary") == IDLE_COMMAND_DONE && !f.client->slots[1].app);
    assert(run(&f, "/help summary") == IDLE_COMMAND_DONE && strstr(output, "Usage: /summary"));
    f.client->current = 0;
    free(before); cleanup(&f);
}

static void suggestion_modes_and_network_list(void)
{
    Fixture f; init(&f, 0); App *primary = app(&f);
    assert(primary->cfg.command_suggestions == CONFIG_SUGGEST_COMMON);
    assert(run(&f, "/server add second.invalid") == IDLE_COMMAND_DONE);
    assert(run(&f, "/set command_suggestions contextual") == IDLE_COMMAND_DONE);
    assert(primary->cfg.command_suggestions == CONFIG_SUGGEST_CONTEXTUAL);
    assert(f.client->slots[1].seed.command_suggestions == CONFIG_SUGGEST_CONTEXTUAL);
    assert(run(&f, "/server 1") == IDLE_COMMAND_DONE);
    assert(app(&f)->cfg.command_suggestions == CONFIG_SUGGEST_CONTEXTUAL);
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE && strstr(output, "Command suggestions: contextual") && strstr(output, "Composer mode: line"));
    assert(run(&f, "/set composer_mode fullscreen") == IDLE_COMMAND_DONE);
    assert(primary->cfg.composer_fullscreen && f.client->slots[1].seed.composer_fullscreen);
    assert(run(&f, "/server 1") == IDLE_COMMAND_DONE && app(&f)->cfg.composer_fullscreen);
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE && strstr(output, "Composer mode: fullscreen"));
    assert(run(&f, "/set composer_mode line") == IDLE_COMMAND_DONE);
    assert(!primary->cfg.composer_fullscreen && !app(&f)->cfg.composer_fullscreen);
    assert(run(&f, "/set background_away off") == IDLE_COMMAND_DONE);
    assert(!primary->cfg.background_away && !app(&f)->cfg.background_away);
    assert(run(&f, "/set part_message Client part") == IDLE_COMMAND_DONE);
    assert(run(&f, "/set quit_message Client quit") == IDLE_COMMAND_DONE);
    assert(run(&f, "/set background_away_message Detached safely") == IDLE_COMMAND_DONE);
    assert(run(&f, "/set autoaway_minutes 7") == IDLE_COMMAND_DONE);
    assert(run(&f, "/set autoaway_message Idle seven") == IDLE_COMMAND_DONE);
    assert(!strcmp(primary->cfg.part_message, "Client part") &&
           !strcmp(app(&f)->cfg.quit_message, "Client quit") &&
           !strcmp(primary->cfg.background_away_message, "Detached safely") &&
           primary->cfg.autoaway_minutes == 7 &&
           !strcmp(app(&f)->cfg.autoaway_message, "Idle seven"));
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE && strstr(output, "background-away off") &&
           strstr(output, "auto-away 7 minutes") && strstr(output, "PART default: Client part") &&
           strstr(output, "QUIT default: Client quit"));
    const char *names[] = {"alphabetical", "common", "history", "frequent", "contextual"};
    for (int i = CONFIG_SUGGEST_ALPHABETICAL; i <= CONFIG_SUGGEST_CONTEXTUAL; ++i) {
        char command[128]; (void)snprintf(command, sizeof command, "/set command_suggestions %s", names[i]);
        assert(run(&f, command) == IDLE_COMMAND_DONE);
        assert(primary->cfg.command_suggestions == i && app(&f)->cfg.command_suggestions == i);
    }
    assert(run(&f, "/set command_suggestions") == IDLE_COMMAND_ERROR);
    assert(run(&f, "/set command_suggestions random") == IDLE_COMMAND_ERROR);
    assert(run(&f, "/set command_suggestions common extra") == IDLE_COMMAND_ERROR);
    assert(primary->cfg.command_suggestions == CONFIG_SUGGEST_CONTEXTUAL &&
           app(&f)->cfg.command_suggestions == CONFIG_SUGGEST_CONTEXTUAL);
    assert(run(&f, "/save input") == IDLE_COMMAND_DONE);
    Config saved; char error[512]; config_defaults(&saved);
    assert(config_load(&saved, f.path, error, sizeof error) == 1);
    assert(saved.command_suggestions == CONFIG_SUGGEST_CONTEXTUAL && !saved.composer_fullscreen);
    assert(!saved.background_away && saved.autoaway_minutes == 7 &&
           !strcmp(saved.part_message, "Client part") && !strcmp(saved.quit_message, "Client quit") &&
           !strcmp(saved.background_away_message, "Detached safely") && !strcmp(saved.autoaway_message, "Idle seven"));
    config_wipe(&saved, sizeof saved);
    assert(run(&f, "/help command_suggestions") == IDLE_COMMAND_DONE && strstr(output, "Contextual"));
    assert(run(&f, "/help composer_mode") == IDLE_COMMAND_DONE && strstr(output, "composer_mode"));
    assert(run(&f, "/help list") == IDLE_COMMAND_DONE && strstr(output, "Usage: /list [MASK]"));
    size_t before = app(&f)->control_count;
    assert(run(&f, "/list *linux* extra") == IDLE_COMMAND_ERROR && strstr(app(&f)->status, "Usage: /list"));
    assert(run(&f, "/list") == IDLE_COMMAND_ERROR && app(&f)->control_count == before);
    cleanup(&f);

    init(&f, 1);
    assert(run(&f, "/connect") == IDLE_COMMAND_DONE); pump(app(&f));
    assert(app(&f)->registered);
    before = app(&f)->control_count;
    size_t before_view = app(&f)->current;
    assert(run(&f, "/list *linux* --min-users 5 --max-users 500 --name lin --topic help --sort users --desc --limit 20") == IDLE_COMMAND_DONE);
    assert(app(&f)->current == before_view && app(&f)->control_count == before + 1);
    assert(app(&f)->list.active && app(&f)->list.min_set && app(&f)->list.min_users == 5);
    assert(app(&f)->list.max_set && app(&f)->list.max_users == 500);
    assert(app(&f)->list.display_limit == 20 && app(&f)->list.sort == APP_LIST_SORT_USERS && app(&f)->list.descending);
    assert(run(&f, "/list show") == IDLE_COMMAND_DONE && strstr(output, "Network channel directory") && strstr(output, "still receiving"));
    assert(run(&f, "/list show --sort channel --asc --limit 7") == IDLE_COMMAND_DONE);
    assert(app(&f)->list.sort == APP_LIST_SORT_CHANNEL && !app(&f)->list.descending && app(&f)->list.display_limit == 7);
    assert(run(&f, "/list show --min-users 2") == IDLE_COMMAND_ERROR);
    assert(run(&f, "/ls") == IDLE_COMMAND_DONE && strstr(output, "Channels on server"));
    cleanup(&f);
}

static void default_interface_preference(void)
{
    Fixture f; init(&f, 0);
    App *primary = app(&f);
    assert(primary->cfg.default_interface == CONFIG_INTERFACE_IDLE);
    /* vipirc -i keeps the full configuration namespace and can choose either
     * frontend for a future launch. The fixture starts with saved idle. */
    primary->cfg.minimal = f.client->slots[0].seed.minimal = 0;
    f.commands.program = "vipirc";
    assert(run(&f, "/server add second.invalid") == IDLE_COMMAND_DONE);
    assert(f.client->slots[1].seed.default_interface == CONFIG_INTERFACE_IDLE);
    assert(run(&f, "/server 1") == IDLE_COMMAND_DONE);
    App *secondary = app(&f);
    assert(secondary != primary && !secondary->net && !secondary->sim);
    size_t selected = f.client->current;
    assert(run(&f, "/set default_interface ncurses") == IDLE_COMMAND_DONE);
    assert(strstr(output, "next fresh launch") && strstr(output, "/save input"));
    assert(primary->cfg.default_interface == CONFIG_INTERFACE_NCURSES &&
           secondary->cfg.default_interface == CONFIG_INTERFACE_NCURSES);
    assert(f.client->slots[0].seed.default_interface == CONFIG_INTERFACE_NCURSES &&
           f.client->slots[1].seed.default_interface == CONFIG_INTERFACE_NCURSES);
    assert(f.client->current == selected && !secondary->net && !secondary->sim);
    assert(!secondary->chat_count && !secondary->control_count && access(f.path, F_OK) != 0);
    const char *bad[] = {"/set default_interface", "/set default_interface full",
                         "/set default_interface on", "/set default_interface idle extra"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        assert(run(&f, bad[i]) == IDLE_COMMAND_ERROR);
        assert(primary->cfg.default_interface == CONFIG_INTERFACE_NCURSES &&
               secondary->cfg.default_interface == CONFIG_INTERFACE_NCURSES);
    }
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE && strstr(output, "Default interface: ncurses (next fresh launch"));
    assert(run(&f, "/help default_interface") == IDLE_COMMAND_DONE && strstr(output, "Explicit -i/--idle or -N/--ncurses"));
    assert(run(&f, "/save input") == IDLE_COMMAND_DONE);
    Config saved; char error[512]; config_defaults(&saved);
    assert(config_load(&saved, f.path, error, sizeof error) == 1);
    assert(saved.default_interface == CONFIG_INTERFACE_NCURSES && !strcmp(saved.server, primary->cfg.server));
    assert(!secondary->config_saved && !*secondary->config_path && f.client->current == selected);
    assert(run(&f, "/set default_interface idle") == IDLE_COMMAND_DONE);
    assert(primary->cfg.default_interface == CONFIG_INTERFACE_IDLE && secondary->cfg.default_interface == CONFIG_INTERFACE_IDLE);
    assert(config_load(&saved, f.path, error, sizeof error) == 1 && saved.default_interface == CONFIG_INTERFACE_NCURSES);
    config_wipe(&saved, sizeof saved);
    cleanup(&f);

    init(&f, 0);
    assert(run(&f, "/set default_interface ncurses") == IDLE_COMMAND_ERROR &&
           strstr(output, "onekeyirc always uses the idle interface"));
    assert(app(&f)->cfg.default_interface == CONFIG_INTERFACE_IDLE && access(f.path, F_OK) != 0);
    /* A known value loaded from disk survives a standalone read/save even
     * though the fixed-mode UI never offers to launch ncurses. */
    app(&f)->cfg.default_interface = f.client->slots[0].seed.default_interface = CONFIG_INTERFACE_NCURSES;
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE && strstr(output, "idle (fixed in onekeyirc; saved preference: ncurses)"));
    assert(run(&f, "/save input") == IDLE_COMMAND_DONE);
    config_minimal_defaults(&saved);
    assert(config_load(&saved, f.path, error, sizeof error) == 1 && saved.default_interface == CONFIG_INTERFACE_NCURSES);
    assert(run(&f, "/set default_interface idle") == IDLE_COMMAND_DONE && app(&f)->cfg.default_interface == CONFIG_INTERFACE_IDLE);
    config_wipe(&saved, sizeof saved);
    cleanup(&f);

    init(&f, 1);
    app(&f)->cfg.minimal = f.client->slots[0].seed.minimal = 0;
    assert(run(&f, "/connect") == IDLE_COMMAND_DONE); pump(app(&f));
    App *running = app(&f);
    Sim *transport = running->sim;
    uint64_t connection_id = running->connection_id;
    size_t view = running->current, queued = running->chat_count;
    assert(transport && running->registered);
    assert(run(&f, "/set default_interface ncurses") == IDLE_COMMAND_DONE);
    assert(running->cfg.default_interface == CONFIG_INTERFACE_NCURSES);
    assert(running->sim == transport && running->connection_id == connection_id && running->registered);
    assert(running->current == view && running->chat_count == queued);
    assert(run(&f, "/save input") == IDLE_COMMAND_ERROR && access(f.path, F_OK) != 0);
    cleanup(&f);
    puts("default interface commands: strict choices, next-launch-only behavior, primary-owned save, live/lazy propagation and offline isolation passed");
}

/* A UTF-8 localized help heading has one underline cell per rendered cell,
 * not one underline per encoded byte. Keep the callback display-only. */
static const char *accent_help_heading(const char *english)
{
    return strcmp(english, "Help topics (use /help TOPIC)") == 0 ? "Caf\303\251" : english;
}

static void localized_heading_cells(void)
{
    const char *old_locale = setlocale(LC_CTYPE, NULL);
    char saved_locale[128];
    if (old_locale) (void)snprintf(saved_locale, sizeof saved_locale, "%s", old_locale);
    else *saved_locale = 0;
    if (!setlocale(LC_CTYPE, "C.UTF-8") && !setlocale(LC_CTYPE, "en_US.UTF-8")) return;
    Fixture f;
    init(&f, 1);
    f.commands.translate = accent_help_heading;
    assert(run(&f, "/help ls") == IDLE_COMMAND_DONE);
    assert(strstr(output, "Caf\303\251\n----\n") != NULL);
    cleanup(&f);
    if (*saved_locale) (void)setlocale(LC_CTYPE, saved_locale);
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--manual")) { idle_manual(stdout, "onekeyirc"); return ferror(stdout) ? 1 : 0; }
    assert(argc == 1);
    metadata_and_help(); localized_heading_cells(); help_keeps_product_boundaries(); strict_input_and_safe_results(); startup_transactions_and_redaction();
    startup_inquiries_are_complete_and_local();
    direct_connection_commands(); pending_empty_host_preserves_session_identity();
    server_views_and_global_input(); deferred_selection_keeps_hidden_views_unread(); setup_requests_are_side_effect_free(); send_plans_require_frontend_consent(); history_policy(); persistent_primary_and_failure();
    persistent_saveconf_and_unreadable_profile();
    native_server_numbers();
    clear_mentions_across_servers();
    aliases_queries_and_autocommands();
    summary_without_selection(); suggestion_modes_and_network_list();
    default_interface_preference();
    puts("idle commands: complete topic/catalog help, strict grammar, transactional startup settings, secret redaction/no reads, stable server/query/channel actions, global primary send mode, explicit unqueued plans, history policy and private persistence passed");
    return 0;
}
