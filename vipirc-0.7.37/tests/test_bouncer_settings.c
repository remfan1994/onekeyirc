/* Exercise the settings state machine through public key APIs, without
 * initializing a terminal or making a network connection. */
#define _POSIX_C_SOURCE 200809L
#include "settings.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char secret[] = "BOUNCER_SETTINGS_SECRET_NOT_DISPLAYED";

typedef struct Fixture {
    App *app;
    Settings *settings;
    char directory[128], path[256];
} Fixture;

static void init(Fixture *f, int type)
{
    memset(f, 0, sizeof *f);
    strcpy(f->directory, "/tmp/irc-bouncer-settings-XXXXXX"); assert(mkdtemp(f->directory));
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
    f->app = calloc(1, sizeof *f->app); assert(f->app);
    assert(app_init(f->app, &cfg, f->path));
    f->settings = settings_create(); assert(f->settings);
    config_wipe(&cfg, sizeof cfg);
}

static void cleanup(Fixture *f)
{
    assert(!f->app->net && !f->app->credential && !f->app->sim);
    settings_destroy(f->settings); app_free(f->app); free(f->app);
    assert(access(f->path, F_OK)); assert(!rmdir(f->directory));
    config_wipe(f, sizeof *f);
}

static void key(Fixture *f, wint_t value, int special)
{
    settings_key(f->settings, f->app, value, special);
}

static void help(Fixture *f, char *item, size_t capacity)
{
    char screen[80], detail[1024];
    assert(settings_context_help(f->settings, screen, sizeof screen, item, capacity, detail, sizeof detail));
    assert(!strstr(screen, secret) && !strstr(item, secret) && !strstr(detail, secret));
}

static void select_field(Fixture *f, const char *name)
{
    key(f, 'g', 0);
    for (unsigned i = 0; i < 100; ++i) {
        char item[160]; help(f, item, sizeof item);
        if (!strcmp(item, name)) return;
        key(f, KEY_DOWN, 1);
    }
    fprintf(stderr, "Missing settings field: %s\n", name);
    assert(!"settings field unavailable");
}

static void choose(Fixture *f, const char *name, int value)
{
    select_field(f, name); key(f, '\r', 0);
    assert(settings_mouse_state(f->settings) & 4u);
    for (int i = 0; i < 5; ++i) key(f, KEY_UP, 1);
    for (int i = 0; i < value; ++i) key(f, KEY_DOWN, 1);
    key(f, '\r', 0); assert(!(settings_mouse_state(f->settings) & 4u));
}

static void text_field(Fixture *f, const char *name, const char *text, int valid)
{
    select_field(f, name); key(f, '\r', 0);
    assert(settings_mouse_state(f->settings) & 2u);
    if (!*text) key(f, KEY_BACKSPACE, 1);
    else for (const unsigned char *p = (const unsigned char *)text; *p; ++p) key(f, *p, 0);
    key(f, '\r', 0);
    assert(!!(settings_mouse_state(f->settings) & 2u) == !valid);
}

static const char *password_field(int type)
{
    return type == CONFIG_BOUNCER_ZNC ? "Plaintext server password" : "Plaintext password";
}

static void action(Fixture *f, unsigned index)
{
    for (unsigned i = 0; i < index; ++i) key(f, 'l', 0);
    key(f, '\r', 0);
}

static void new_modes_and_required_fields(void)
{
    for (int type = CONFIG_BOUNCER_ZNC; type <= CONFIG_BOUNCER_SOJU; ++type) {
        Fixture f; init(&f, CONFIG_BOUNCER_NONE);
        Config input = f.app->cfg; input.tls = 0;
        strcpy(input.auth, "sasl"); strcpy(input.auth_user, "old-auth-user");
        strcpy(input.password, "old-auth-password");
        strcpy(input.server_password, "old-server-password");
        input.autocommands.count = 1;
        strcpy(input.autocommands.lines[0], "/msg NickServ old-startup-secret");
        settings_open_new(f.settings, &input);
        choose(&f, "Connection type", type);
        assert(settings_mouse_state(f.settings) & 16u);
        /* Incomplete drafts remain editable, but Add must reject them. */
        action(&f, 1); assert(settings_active(f.settings)); key(&f, 'h', 0);
        text_field(&f, "Bouncer username", "new-account", 1);
        text_field(&f, "Bouncer network", "IRCNow", 1);
        text_field(&f, "Bouncer client name", "terminal", 1);
        select_field(&f, "Transport"); key(&f, '\r', 0);
        select_field(&f, "Auto-join channels"); key(&f, '\r', 0);
        assert(!(settings_mouse_state(f.settings) & 2u));
        select_field(&f, password_field(type == CONFIG_BOUNCER_ZNC ? CONFIG_BOUNCER_SOJU : CONFIG_BOUNCER_ZNC));
        key(&f, '\r', 0); assert(!(settings_mouse_state(f.settings) & 2u));
        text_field(&f, password_field(type), secret, 1);
        action(&f, 1); assert(!settings_active(f.settings));
        Config added; assert(settings_take_new(f.settings, &added) == 1);
        assert(added.bouncer_type == type && added.tls == 1 && added.port == 31337);
        assert(!added.bouncer_away && added.background_away && added.autoaway_minutes == 17);
        assert(!strcmp(added.bouncer_user, "new-account") && !strcmp(added.bouncer_network, "IRCNow"));
        assert(!strcmp(added.bouncer_client, "terminal") && !added.auth_user[0]);
        assert(!strcmp(added.auth, type == CONFIG_BOUNCER_SOJU ? "sasl" : "none"));
        assert(!strcmp(type == CONFIG_BOUNCER_ZNC ? added.server_password : added.password, secret));
        assert(!*(type == CONFIG_BOUNCER_ZNC ? added.password : added.server_password));
        assert(!added.autocommands.count && !added.autocommands.lines[0][0]);
        config_wipe(&added, sizeof added); config_wipe(&input, sizeof input);
        cleanup(&f);
    }
}

static void cancellation_and_invalid_identity(void)
{
    Fixture f; init(&f, CONFIG_BOUNCER_ZNC); Config before = f.app->cfg;
    settings_open(f.settings, f.app);
    text_field(&f, "Bouncer username", "injected/network", 0);
    key(&f, 27, 0); assert(!settings_active(f.settings));
    assert(!memcmp(&f.app->cfg, &before, sizeof before));
    settings_open(f.settings, f.app);
    choose(&f, "Connection type", CONFIG_BOUNCER_SOJU);
    text_field(&f, "Bouncer username", "draft-account", 1);
    key(&f, 27, 0); assert(!memcmp(&f.app->cfg, &before, sizeof before));
    cleanup(&f);
}

static void connection_merge_and_reset(void)
{
    for (int type = CONFIG_BOUNCER_ZNC; type <= CONFIG_BOUNCER_SOJU; ++type) {
        Fixture f; init(&f, type); char error[512];
        f.app->cfg.autocommands.count = 1;
        strcpy(f.app->cfg.autocommands.lines[0], "/msg NickServ old-startup-secret");
        settings_open(f.settings, f.app);
        text_field(&f, "Bouncer username", "draft-account", 1);
        text_field(&f, password_field(type), secret, 1);
        Config concurrent = f.app->cfg;
        strcpy(concurrent.server, "different-provider.invalid"); concurrent.port = 7443;
        strcpy(concurrent.bouncer_user, "concurrent-account"); strcpy(concurrent.nick, "LaterNick");
        strcpy(type == CONFIG_BOUNCER_SOJU ? concurrent.password : concurrent.server_password, "concurrent-secret");
        strcpy(concurrent.autocommands.lines[0], "/msg NickServ concurrent-startup-secret");
        assert(app_config_apply(f.app, &concurrent, 0, error, sizeof error));
        action(&f, 1);
        Config final; app_config_effective(f.app, &final);
        assert(!strcmp(final.server, "bouncer.invalid") && final.port == 31337);
        assert(!strcmp(final.bouncer_user, "draft-account") && !strcmp(final.nick, "LaterNick"));
        assert(!strcmp(type == CONFIG_BOUNCER_SOJU ? final.password : final.server_password, secret));
        assert(!final.autocommands.count && !final.autocommands.lines[0][0]);
        /* Port changes invalidate the password even without an account edit. */
        text_field(&f, "Port", "7444", 1);
        action(&f, 1); Config failed; app_config_effective(f.app, &failed);
        assert(!memcmp(&final, &failed, sizeof final)); key(&f, 'h', 0);
        text_field(&f, password_field(type), secret, 1); action(&f, 1);
        app_config_effective(f.app, &final); assert(final.port == 7444);
        cleanup(&f);
    }
}

static void application_edit_and_failed_save(void)
{
    Fixture f; init(&f, CONFIG_BOUNCER_SOJU); char error[512];
    settings_open(f.settings, f.app); text_field(&f, "History lines", "250", 1);
    Config concurrent = f.app->cfg;
    strcpy(concurrent.server, "concurrent.invalid"); strcpy(concurrent.bouncer_user, "later-account");
    strcpy(concurrent.password, secret);
    concurrent.autocommands.count = 1;
    strcpy(concurrent.autocommands.lines[0], "/msg ChanServ op #room tester");
    assert(app_config_apply(f.app, &concurrent, 0, error, sizeof error));
    action(&f, 1); Config final; app_config_effective(f.app, &final);
    concurrent.history_lines = 250; assert(!memcmp(&final, &concurrent, sizeof final));
    text_field(&f, "Bouncer client name", "changed-terminal", 1);
    text_field(&f, password_field(CONFIG_BOUNCER_SOJU), "changed-secret", 1);
    assert(!mkdir(f.path, 0700)); action(&f, 2);
    Config failed; app_config_effective(f.app, &failed); assert(!memcmp(&final, &failed, sizeof final));
    assert(!rmdir(f.path)); key(&f, 'h', 0); key(&f, '\r', 0);
    app_config_effective(f.app, &final);
    assert(!strcmp(final.bouncer_client, "changed-terminal") && !strcmp(final.password, "changed-secret"));
    assert(!final.autocommands.count && !final.autocommands.lines[0][0]);
    cleanup(&f);
}

static void direct_secret_does_not_merge_into_concurrent_bouncer(void)
{
    Fixture f; init(&f, CONFIG_BOUNCER_NONE); char error[512];
    Config direct = f.app->cfg;
    strcpy(direct.auth, "sasl"); strcpy(direct.auth_user, "direct-account");
    strcpy(direct.password, "direct-old-secret");
    direct.autocommands.count = 1;
    strcpy(direct.autocommands.lines[0], "/msg NickServ direct-startup-secret");
    assert(app_config_apply(f.app, &direct, 0, error, sizeof error));
    settings_open(f.settings, f.app); text_field(&f, "Plaintext password", secret, 1);
    Config concurrent = direct;
    concurrent.bouncer_type = CONFIG_BOUNCER_SOJU;
    strcpy(concurrent.server, "different-bouncer.invalid"); concurrent.port = 7443;
    strcpy(concurrent.bouncer_user, "other-account"); strcpy(concurrent.bouncer_network, "other-network");
    concurrent.auth_user[0] = 0; strcpy(concurrent.password, "other-secret");
    strcpy(concurrent.autocommands.lines[0], "/msg NickServ other-startup-secret");
    assert(app_config_apply(f.app, &concurrent, 0, error, sizeof error));
    action(&f, 1); Config final; app_config_effective(f.app, &final);
    assert(final.bouncer_type == CONFIG_BOUNCER_NONE && !strcmp(final.server, direct.server));
    assert(final.port == direct.port && !strcmp(final.auth_user, "direct-account"));
    assert(!strcmp(final.password, secret));
    assert(final.autocommands.count == 1 && !strcmp(final.autocommands.lines[0], direct.autocommands.lines[0]));
    cleanup(&f);
}

static void password_edit_preserves_late_startup_commands(void)
{
    Fixture f; init(&f, CONFIG_BOUNCER_SOJU); char error[512];
    settings_open(f.settings, f.app); text_field(&f, "Plaintext password", secret, 1);
    Config concurrent = f.app->cfg; concurrent.autocommands.count = 1;
    strcpy(concurrent.autocommands.lines[0], "/msg ChanServ op #room tester");
    assert(app_config_apply(f.app, &concurrent, 0, error, sizeof error));
    action(&f, 1); Config final; app_config_effective(f.app, &final);
    config_wipe(concurrent.password, sizeof concurrent.password); strcpy(concurrent.password, secret);
    assert(!memcmp(&final, &concurrent, sizeof final)); cleanup(&f);
}

int main(void)
{
    alarm(30);
    new_modes_and_required_fields(); cancellation_and_invalid_identity();
    connection_merge_and_reset(); application_edit_and_failed_save();
    direct_secret_does_not_merge_into_concurrent_bouncer();
    password_edit_preserves_late_startup_commands();
    puts("bouncer settings state: new ZNC/soju, required identity/password, source isolation, TLS, cancel, atomic merge, concurrent mode-change isolation, startup credential isolation, unrelated startup edits, port credential reset, unrelated merge and failed Save passed");
    return 0;
}
