/* Bouncer setup transactions use real client/config code without networking. */
#define _POSIX_C_SOURCE 200809L
#include "idle_setup.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct Fixture {
    Client *client;
    char directory[128], path[256];
} Fixture;

static const char secret[] = "BOUNCER_SETUP_SECRET_NOT_DISPLAYED";

static void remove_tree(const char *path)
{
    DIR *directory = opendir(path); assert(directory);
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[CONFIG_PATH_MAX]; struct stat st;
        int n = snprintf(child, sizeof child, "%s/%s", path, entry->d_name);
        assert(n > 0 && (size_t)n < sizeof child && !lstat(child, &st));
        if (S_ISDIR(st.st_mode)) remove_tree(child);
        else assert(!unlink(child));
    }
    assert(!closedir(directory) && !rmdir(path));
}

static void init(Fixture *f, int type)
{
    memset(f, 0, sizeof *f);
    strcpy(f->directory, "/tmp/irc-bouncer-setup-XXXXXX"); assert(mkdtemp(f->directory));
    int n = snprintf(f->path, sizeof f->path, "%s/config.ini", f->directory);
    assert(n > 0 && (size_t)n < sizeof f->path);
    Config cfg; config_minimal_defaults(&cfg);
    strcpy(cfg.server, "bouncer.invalid"); cfg.port = 31337;
    strcpy(cfg.nick, "tester"); strcpy(cfg.username, "tester");
    strcpy(cfg.channels, "#saved-direct-channel");
    cfg.background_away = 1; cfg.autoaway_minutes = 17; cfg.autoconnect = 0;
    cfg.bouncer_type = type;
    if (type != CONFIG_BOUNCER_NONE) {
        strcpy(cfg.bouncer_user, "old-account"); strcpy(cfg.bouncer_network, "IRCNow");
        strcpy(cfg.bouncer_client, "old-terminal");
        strcpy(type == CONFIG_BOUNCER_SOJU ? cfg.password : cfg.server_password, "old-secret");
        strcpy(cfg.auth, type == CONFIG_BOUNCER_SOJU ? "sasl" : "none");
    }
    f->client = calloc(1, sizeof *f->client); assert(f->client);
    char error[512];
    assert(client_init(f->client, &cfg, f->path, "tester", 0, 0, error, sizeof error) == 1);
    assert(client_get_app(f->client, 0, error, sizeof error));
    config_wipe(&cfg, sizeof cfg);
}

static App *app(Fixture *f) { return f->client->slots[0].app; }

static void cleanup(Fixture *f)
{
    client_free(f->client); free(f->client); remove_tree(f->directory);
    config_wipe(f, sizeof *f);
}

static IdleSetupView view(IdleSetup *setup)
{
    IdleSetupView v; idle_setup_view(setup, &v);
    assert(!strstr(v.current, secret) && !strstr(v.detail, secret) && !strstr(v.summary, secret));
    return v;
}

static void answer(IdleSetup *setup, const char *value, int expected)
{
    char error[512]; int result = idle_setup_answer(setup, value, error, sizeof error);
    if (result != expected) {
        IdleSetupView v = view(setup);
        fprintf(stderr, "%s: result %d, expected %d (%s)\n", v.question, result, expected, error);
    }
    assert(result == expected);
}

static void to(IdleSetup *setup, const char *question)
{
    for (unsigned i = 0; i < 80; ++i) {
        IdleSetupView v = view(setup);
        if (!strcmp(v.question, question)) return;
        assert(!v.final); answer(setup, "", 1);
    }
    assert(!"question was not reached");
}

static IdleSetup *begin(Fixture *f, int application)
{
    char error[512]; IdleSetup *s = idle_setup_new(f->client, 0, error, sizeof error);
    assert(s); answer(s, application ? "0" : "1", 1); return s;
}

static void finish_fields(IdleSetup *s)
{
    for (unsigned i = 0; i < 80; ++i) {
        if (view(s).final) return;
        answer(s, "", 1);
    }
    assert(!"Finish was not reached");
}

static void credentials(IdleSetup *s, const char *source, const char *value)
{
    to(s, "Bouncer password source"); answer(s, "none", 0); answer(s, source, 1);
    if (!strcmp(source, "gpg")) { to(s, "Bouncer GPG file"); answer(s, "existing", 1); }
    assert(!strcmp(view(s).question, !strcmp(source, "plain") ? "Bouncer password" : "Bouncer password file"));
    assert(view(s).secret == !strcmp(source, "plain")); answer(s, value, 1);
}

static void configure(IdleSetup *s, int type, const char *source, const char *value)
{
    to(s, "Connection type"); answer(s, "unknown", 0);
    answer(s, type == CONFIG_BOUNCER_ZNC ? "znc" : "soju", 1);
    assert(!strcmp(view(s).question, "Bouncer username"));
    answer(s, "", 0); answer(s, "account/network", 0); answer(s, "new-account", 1);
    assert(!strcmp(view(s).question, "Bouncer network"));
    answer(s, "", 0); answer(s, "network@client", 0); answer(s, "IRCNow", 1);
    assert(!strcmp(view(s).question, "Bouncer client name"));
    answer(s, "client:password", 0); answer(s, "terminal", 1);
    assert(!strcmp(view(s).question, "Apply automatic away") && !strcmp(view(s).current, "no"));
    answer(s, "", 1);
    credentials(s, source, value); finish_fields(s);
    assert(strstr(view(s).summary, "Channels: restored by the bouncer"));
}

static void sources_and_cancel(void)
{
    const char *sources[] = {"plain", "file", "gpg"};
    for (int type = CONFIG_BOUNCER_ZNC; type <= CONFIG_BOUNCER_SOJU; ++type)
        for (size_t source = 0; source < sizeof sources / sizeof sources[0]; ++source) {
            Fixture f; init(&f, CONFIG_BOUNCER_NONE); App *a = app(&f);
            a->cfg.autocommands.count = 1;
            strcpy(a->cfg.autocommands.lines[0], "/msg NickServ old-startup-secret");
            Config before = a->cfg, seed = f.client->slots[0].seed;
            char path[320], error[512]; snprintf(path, sizeof path, "%s/unread-secret", f.directory);
            /* An unopened FIFO proves setup does not read the source. */
            assert(!mkfifo(path, 0600));
            IdleSetup *s = begin(&f, 0);
            configure(s, type, sources[source], source ? path : secret);
            assert(!memcmp(&a->cfg, &before, sizeof before) && !memcmp(&f.client->slots[0].seed, &seed, sizeof seed));
            idle_setup_free(s);
            assert(!memcmp(&a->cfg, &before, sizeof before) && !a->net && !a->credential && !a->sim);
            s = begin(&f, 0); configure(s, type, sources[source], source ? path : secret);
            answer(s, "4", 2); assert(idle_setup_commit(s, error, sizeof error) == 1);
            Config current; app_config_effective(a, &current);
            assert(current.bouncer_type == type && current.bouncer_away == 0 && current.tls == 1);
            assert(current.background_away == 1 && current.autoaway_minutes == 17 && current.port == 31337);
            assert(!strcmp(current.bouncer_user, "new-account") && !strcmp(current.bouncer_network, "IRCNow"));
            assert(!strcmp(current.bouncer_client, "terminal") && !current.auth_user[0]);
            assert(!strcmp(current.auth, type == CONFIG_BOUNCER_SOJU ? "sasl" : "none"));
            assert(!current.autocommands.count && !current.autocommands.lines[0][0]);
            assert(!a->net && !a->credential && !a->sim && access(f.path, F_OK));
            Config unchanged = current;
            IdleSetup *quick = idle_setup_new(f.client, 1, error, sizeof error);
            assert(!quick && strstr(error, "/panel"));
            app_config_effective(a, &current); assert(!memcmp(&current, &unchanged, sizeof current));
            idle_setup_free(s); cleanup(&f);
        }
}

static void atomic_identity_merge(void)
{
    for (int type = CONFIG_BOUNCER_ZNC; type <= CONFIG_BOUNCER_SOJU; ++type) {
        Fixture f; init(&f, type); App *a = app(&f); char error[512];
        a->cfg.autocommands.count = 1;
        strcpy(a->cfg.autocommands.lines[0], "/msg NickServ old-startup-secret");
        IdleSetup *s = begin(&f, 0); to(s, "Bouncer username"); answer(s, "draft-account", 1);
        to(s, "Bouncer password source"); assert(!strcmp(view(s).current, "none"));
        credentials(s, "plain", secret); finish_fields(s);
        Config concurrent = a->cfg;
        strcpy(concurrent.server, "different-provider.invalid"); concurrent.port = 7443;
        strcpy(concurrent.bouncer_user, "concurrent-account"); strcpy(concurrent.nick, "LaterNick");
        strcpy(type == CONFIG_BOUNCER_SOJU ? concurrent.password : concurrent.server_password, "concurrent-secret");
        strcpy(concurrent.autocommands.lines[0], "/msg NickServ concurrent-startup-secret");
        assert(app_config_apply(a, &concurrent, 0, error, sizeof error));
        answer(s, "4", 2); assert(idle_setup_commit(s, error, sizeof error) == 1);
        Config final; app_config_effective(a, &final);
        assert(!strcmp(final.server, "bouncer.invalid") && final.port == 31337);
        assert(!strcmp(final.bouncer_user, "draft-account") && !strcmp(final.nick, "LaterNick"));
        assert(!strcmp(type == CONFIG_BOUNCER_SOJU ? final.password : final.server_password, secret));
        assert(!final.autocommands.count && !final.autocommands.lines[0][0]);
        idle_setup_free(s); cleanup(&f);
    }
}

static void application_preserves_connection(void)
{
    Fixture f; init(&f, CONFIG_BOUNCER_SOJU); App *a = app(&f); char error[512];
    IdleSetup *s = begin(&f, 1); answer(s, "250", 1); finish_fields(s);
    Config concurrent = a->cfg; strcpy(concurrent.server, "later.invalid");
    strcpy(concurrent.bouncer_user, "later-account"); strcpy(concurrent.password, secret);
    concurrent.autocommands.count = 1;
    strcpy(concurrent.autocommands.lines[0], "/msg ChanServ op #room tester");
    assert(app_config_apply(a, &concurrent, 0, error, sizeof error));
    answer(s, "2", 2); assert(idle_setup_commit(s, error, sizeof error) == 1);
    Config final; app_config_effective(a, &final);
    concurrent.history_lines = 250;
    assert(!memcmp(&final, &concurrent, sizeof final));
    idle_setup_free(s); cleanup(&f);
}

static void direct_secret_does_not_merge_into_concurrent_bouncer(void)
{
    Fixture f; init(&f, CONFIG_BOUNCER_NONE); App *a = app(&f); char error[512];
    Config direct = a->cfg;
    strcpy(direct.auth, "sasl"); strcpy(direct.auth_user, "direct-account");
    strcpy(direct.password, "direct-old-secret");
    direct.autocommands.count = 1;
    strcpy(direct.autocommands.lines[0], "/msg NickServ direct-startup-secret");
    assert(app_config_apply(a, &direct, 0, error, sizeof error));
    IdleSetup *s = begin(&f, 0); to(s, "Authentication password"); answer(s, secret, 1);
    finish_fields(s);
    Config concurrent = direct;
    concurrent.bouncer_type = CONFIG_BOUNCER_SOJU;
    strcpy(concurrent.server, "different-bouncer.invalid"); concurrent.port = 7443;
    strcpy(concurrent.bouncer_user, "other-account"); strcpy(concurrent.bouncer_network, "other-network");
    concurrent.auth_user[0] = 0; strcpy(concurrent.password, "other-secret");
    strcpy(concurrent.autocommands.lines[0], "/msg NickServ other-startup-secret");
    assert(app_config_apply(a, &concurrent, 0, error, sizeof error));
    answer(s, "4", 2); assert(idle_setup_commit(s, error, sizeof error) == 1);
    Config final; app_config_effective(a, &final);
    assert(final.bouncer_type == CONFIG_BOUNCER_NONE && !strcmp(final.server, direct.server));
    assert(final.port == direct.port && !strcmp(final.auth_user, "direct-account"));
    assert(!strcmp(final.password, secret));
    assert(final.autocommands.count == 1 && !strcmp(final.autocommands.lines[0], direct.autocommands.lines[0]));
    idle_setup_free(s); cleanup(&f);
}

static void password_edit_preserves_late_startup_commands(void)
{
    Fixture f; init(&f, CONFIG_BOUNCER_SOJU); App *a = app(&f); char error[512];
    IdleSetup *s = begin(&f, 0); to(s, "Bouncer password"); answer(s, secret, 1);
    finish_fields(s);
    Config concurrent = a->cfg; concurrent.autocommands.count = 1;
    strcpy(concurrent.autocommands.lines[0], "/msg ChanServ op #room tester");
    assert(app_config_apply(a, &concurrent, 0, error, sizeof error));
    answer(s, "4", 2); assert(idle_setup_commit(s, error, sizeof error) == 1);
    Config final; app_config_effective(a, &final);
    concurrent.password[0] = 0; strcpy(concurrent.password, secret);
    assert(!memcmp(&final, &concurrent, sizeof final));
    idle_setup_free(s); cleanup(&f);
}

static void ca_retry_and_failed_save(void)
{
    Fixture f; init(&f, CONFIG_BOUNCER_ZNC); App *a = app(&f); char error[512];
    a->cfg.autocommands.count = 1;
    strcpy(a->cfg.autocommands.lines[0], "/msg NickServ old-startup-secret");
    IdleSetup *s = begin(&f, 0); to(s, "CA certificate file");
    answer(s, "/custom/authority.pem", 1);
    assert(!strcmp(view(s).question, "Bouncer password source") && !strcmp(view(s).current, "none"));
    credentials(s, "plain", secret); finish_fields(s);
    Config before = a->cfg, seed = f.client->slots[0].seed;
    assert(!mkdir(f.path, 0700)); answer(s, "2", 2);
    assert(idle_setup_commit(s, error, sizeof error) == 0);
    assert(!memcmp(&a->cfg, &before, sizeof before) && !memcmp(&f.client->slots[0].seed, &seed, sizeof seed));
    assert(!rmdir(f.path));
    answer(s, "4", 2); assert(idle_setup_commit(s, error, sizeof error) == 1);
    Config final; app_config_effective(a, &final);
    assert(!strcmp(final.ca_file, "/custom/authority.pem") && !strcmp(final.server_password, secret));
    assert(!final.autocommands.count && !final.autocommands.lines[0][0]);
    idle_setup_free(s); cleanup(&f);
}

static void transport_and_type_changes(void)
{
    Fixture f; init(&f, CONFIG_BOUNCER_ZNC); char error[512];
    IdleSetup *s = begin(&f, 0); to(s, "Use TLS"); answer(s, "off", 0); answer(s, "", 1);
    to(s, "Connection type"); answer(s, "direct", 1);
    assert(!strcmp(view(s).question, "IRC username")); finish_fields(s);
    answer(s, "4", 2); assert(idle_setup_commit(s, error, sizeof error) == 1);
    Config final; app_config_effective(app(&f), &final);
    assert(final.bouncer_type == CONFIG_BOUNCER_NONE && !final.bouncer_user[0] && !final.bouncer_network[0]);
    assert(!final.bouncer_client[0] && !final.bouncer_away && !final.server_password[0] && !strcmp(final.auth, "none"));
    assert(final.port == 31337 && final.tls);
    idle_setup_free(s); cleanup(&f);
}

int main(void)
{
    alarm(30);
    sources_and_cancel(); atomic_identity_merge(); application_preserves_connection();
    direct_secret_does_not_merge_into_concurrent_bouncer();
    password_edit_preserves_late_startup_commands();
    ca_retry_and_failed_save(); transport_and_type_changes();
    puts("bouncer setup: six source/mode cases, cancellation, Quickstart refusal, atomic connection merge, concurrent mode-change isolation, startup credential isolation, unrelated startup edits, application isolation, CA retry, failed save and TLS/type changes passed");
    return 0;
}
