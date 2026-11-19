/* Real config/client/app/command integration, without connecting to a server.
 * Temporary private profiles are the only external state this test changes. */
#define _POSIX_C_SOURCE 200809L
#include "idle_commands.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct Fixture {
    Client *client;
    IdleCommands commands;
    SendPlan plan;
    char directory[128], path[256];
    char output[32768];
    size_t output_used;
} Fixture;

static void capture(void *context, const char *line)
{
    Fixture *f = context;
    size_t n = strlen(line);
    assert(n + 2u < sizeof f->output - f->output_used);
    for (size_t i = 0; i < n; ++i)
        assert((unsigned char)line[i] >= 32 && (unsigned char)line[i] < 127);
    memcpy(f->output + f->output_used, line, n);
    f->output_used += n;
    f->output[f->output_used++] = '\n';
    f->output[f->output_used] = 0;
}

static int run(Fixture *f, const char *line)
{
    f->output_used = 0; f->output[0] = 0;
    return idle_command(&f->commands, line, &f->plan);
}

static void remove_tree(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    assert(dir);
    while ((entry = readdir(dir)) != NULL) {
        char child[CONFIG_PATH_MAX];
        struct stat st;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        int n = snprintf(child, sizeof child, "%s/%s", path, entry->d_name);
        assert(n > 0 && (size_t)n < sizeof child && !lstat(child, &st));
        if (S_ISDIR(st.st_mode)) remove_tree(child);
        else assert(!unlink(child));
    }
    assert(!closedir(dir) && !rmdir(path));
}

static void init(Fixture *f, int minimal, int offline)
{
    Config cfg;
    char error[512];
    memset(f, 0, sizeof *f);
    strcpy(f->directory, "/tmp/irc-hotkey-preferences-XXXXXX");
    assert(mkdtemp(f->directory));
    assert(snprintf(f->path, sizeof f->path, "%s/config.ini", f->directory) > 0);
    if (minimal) config_minimal_defaults(&cfg); else config_defaults(&cfg);
    strcpy(cfg.server, "primary.invalid");
    strcpy(cfg.nick, "Primary"); strcpy(cfg.username, "tester");
    cfg.autoconnect = 0;
    f->client = calloc(1, sizeof *f->client);
    assert(f->client);
    assert(client_init(f->client, &cfg, f->path, "tester", offline, 0, error, sizeof error) == 1);
    assert(client_get_app(f->client, 0, error, sizeof error));
    f->commands = (IdleCommands){f->client, minimal ? "onekeyirc" : "vipirc", capture, f, 0, 0, NULL};
    config_wipe(&cfg, sizeof cfg);
}

static void cleanup(Fixture *f)
{
    client_free(f->client); free(f->client);
    remove_tree(f->directory);
    config_wipe(f, sizeof *f);
}

static App *primary(Fixture *f) { return f->client->slots[0].app; }

static void verify_bindings(const Client *client, unsigned key, int action, size_t count)
{
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        const ClientSlot *s = &client->slots[i];
        if (!s->used) continue;
        assert(s->seed.hotkey_count == count && config_hotkey_lookup(&s->seed, key) == action);
        if (s->app) {
            assert(s->app->cfg.hotkey_count == count && config_hotkey_lookup(&s->app->cfg, key) == action);
            assert(!s->app->net && !s->app->sim && !s->app->registered);
            assert(!s->app->control_count && !s->app->chat_count && !s->app->quit);
        }
    }
}

static void read_text(const char *path, char *text, size_t cap)
{
    FILE *file = fopen(path, "r");
    assert(file);
    size_t n = fread(text, 1, cap - 1u, file);
    assert(!ferror(file) && feof(file) && !fclose(file));
    text[n] = 0;
}

static void command_validation(void)
{
    static const char *const bad[] = {
        "/bind", "/bind F1", "/bind F1 summary extra", "/bind F13 quit",
        "/bind a quit", "/bind Ctrl-Alt-a quit", "/bind F1 send", "/bind F1 confirm",
        "/bind F1 /quit", "/bind F1 summary;quit", "/bind F1 summary\n/quit",
        "/bind F1 summary\r/quit", "/bind F1 summary\033[2J", "/unbind",
        "/unbind F13", "/unbind F1 extra", "/unbind all extra",
        "/bindings unknown", "/bindings actions extra", "/bindings keys extra"
    };
    Fixture f;
    init(&f, 1, 0);
    Config before = primary(&f)->cfg;
    assert(run(&f, "/bindings") == IDLE_COMMAND_DONE && f.output_used);
    assert(run(&f, "/bindings actions") == IDLE_COMMAND_DONE);
    for (size_t i = 0; i < config_hotkey_action_count(); ++i)
        assert(strstr(f.output, config_hotkey_action_at(i)->name));
    assert(run(&f, "/bindings keys") == IDLE_COMMAND_DONE && f.output_used);
    assert(!memcmp(&before, &primary(&f)->cfg, sizeof before));
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        assert(run(&f, bad[i]) == IDLE_COMMAND_ERROR);
        assert(!memcmp(&before, &primary(&f)->cfg, sizeof before));
        assert(!primary(&f)->quit && !primary(&f)->control_count && !primary(&f)->chat_count);
    }
    assert(run(&f, "/bind F7 summary") == IDLE_COMMAND_DONE);
    verify_bindings(f.client, CONFIG_KEY_F1 + 6u, CONFIG_ACTION_SUMMARY, 1);
    assert(run(&f, "/bind F7 manual") == IDLE_COMMAND_DONE);
    verify_bindings(f.client, CONFIG_KEY_F1 + 6u, CONFIG_ACTION_MANUAL, 1);
    assert(run(&f, "/bind Ctrl-I command") == IDLE_COMMAND_DONE);
    assert(run(&f, "/bind Tab help") == IDLE_COMMAND_DONE);
    verify_bindings(f.client, CONFIG_KEY_TAB, CONFIG_ACTION_HELP, 2);
    assert(run(&f, "/bindings") == IDLE_COMMAND_DONE && strstr(f.output, "F7") && strstr(f.output, "manual"));
    assert(strstr(f.output, "Tab") && strstr(f.output, "help"));
    assert(run(&f, "/unbind Ctrl-I") == IDLE_COMMAND_DONE);
    verify_bindings(f.client, CONFIG_KEY_TAB, CONFIG_ACTION_NONE, 1);
    assert(run(&f, "/unbind all") == IDLE_COMMAND_DONE);
    verify_bindings(f.client, CONFIG_KEY_F1 + 6u, CONFIG_ACTION_NONE, 0);
    assert(access(f.path, F_OK) < 0);
    assert(!primary(&f)->cfg.force_terminal_signals);
    cleanup(&f);
    puts("hotkey commands: read-only catalogs, strict arguments, injection rejection, replace/remove aliases and no implicit save passed");
}

static void primary_owned_persistence(int minimal)
{
    Fixture f;
    Config candidate, pending, effective, loaded;
    char error[512], secondary_path[CONFIG_PATH_MAX];
    char before_file[16384], after_file[16384];
    size_t secondary, lazy;
    init(&f, minimal, 0);
    Client *client = f.client;
    App *p = primary(&f);
    client_new_config(client, &candidate);
    strcpy(candidate.server, "secondary.invalid"); strcpy(candidate.nick, "Secondary");
    assert(config_hotkey_set(&candidate, CONFIG_KEY_F1, CONFIG_ACTION_QUIT, error, sizeof error) == 1);
    assert(client_add(client, &candidate, &secondary, error, sizeof error) == 1);
    assert(!client->slots[secondary].seed.hotkey_count);
    /* A profile may contain old preferences. Its owner is still the primary. */
    assert(client_save(client, secondary, &candidate, error, sizeof error) == 1);
    strcpy(secondary_path, client->slots[secondary].config_path);
    read_text(secondary_path, before_file, sizeof before_file);
    /* Restore the in-memory seed through the real application preference path. */
    assert(client_interface_apply(client, &p->cfg, 0, error, sizeof error) == 1);
    client_new_config(client, &candidate);
    strcpy(candidate.server, "lazy.invalid"); strcpy(candidate.nick, "Lazy");
    assert(client_add(client, &candidate, &lazy, error, sizeof error) == 1);
    assert(client_save(client, lazy, &candidate, error, sizeof error) == 1);
    App *active = client_get_app(client, secondary, error, sizeof error);
    assert(active && !client->slots[lazy].app);
    client->current = secondary;
    app_config_effective(p, &pending);
    strcpy(pending.server, "future-primary.invalid");
    strcpy(pending.nick, "FuturePrimary");
    assert(app_config_apply(p, &pending, 0, error, sizeof error));
    assert(p->config_pending_mask && !strcmp(p->cfg.server, "primary.invalid"));
    unsigned pending_mask = p->config_pending_mask;
    size_t current_view = active->current;
    assert(run(&f, "/bind F7 summary") == IDLE_COMMAND_DONE);
    verify_bindings(client, CONFIG_KEY_F1 + 6u, CONFIG_ACTION_SUMMARY, 1);
    assert(client->current == secondary && active->current == current_view && !client->slots[lazy].app);
    assert(p->config_pending_mask == pending_mask);
    app_config_effective(p, &effective);
    assert(!strcmp(effective.server, "future-primary.invalid") && !strcmp(effective.nick, "FuturePrimary"));
    assert(config_hotkey_lookup(&effective, CONFIG_KEY_F1 + 6u) == CONFIG_ACTION_SUMMARY);
    assert(access(f.path, F_OK) < 0);
    /* An unrelated staged setting update must retain freshly edited keys. */
    effective.autoconnect = !effective.autoconnect;
    assert(app_config_apply(p, &effective, 0, error, sizeof error));
    assert(config_hotkey_lookup(&p->cfg, CONFIG_KEY_F1 + 6u) == CONFIG_ACTION_SUMMARY);
    client_new_config(client, &candidate);
    assert(config_hotkey_lookup(&candidate, CONFIG_KEY_F1 + 6u) == CONFIG_ACTION_SUMMARY);
    strcpy(candidate.server, "new.invalid");
    candidate.hotkey_count = 0;
    size_t fresh;
    assert(client_add(client, &candidate, &fresh, error, sizeof error) == 1);
    assert(config_hotkey_lookup(&client->slots[fresh].seed, CONFIG_KEY_F1 + 6u) == CONFIG_ACTION_SUMMARY);
    assert(client_close(client, fresh, error, sizeof error) == 1);
    assert(run(&f, "/save input") == IDLE_COMMAND_DONE);
    if (minimal) config_minimal_defaults(&loaded); else config_defaults(&loaded);
    assert(config_load(&loaded, f.path, error, sizeof error) == 1);
    assert(!strcmp(loaded.server, "future-primary.invalid") && !strcmp(loaded.nick, "FuturePrimary"));
    assert(config_hotkey_lookup(&loaded, CONFIG_KEY_F1 + 6u) == CONFIG_ACTION_SUMMARY);
    assert(!loaded.force_terminal_signals && !strcmp(active->cfg.server, "secondary.invalid"));
    read_text(secondary_path, after_file, sizeof after_file);
    assert(!strcmp(before_file, after_file));
    Client *restart = calloc(1, sizeof *restart);
    assert(restart && client_init(restart, &loaded, f.path, "tester", 0, 1, error, sizeof error) == 1);
    verify_bindings(restart, CONFIG_KEY_F1 + 6u, CONFIG_ACTION_SUMMARY, 1);
    assert(config_hotkey_lookup(&restart->slots[secondary].seed, CONFIG_KEY_F1) == CONFIG_ACTION_NONE);
    App *restored = client_get_app(restart, secondary, error, sizeof error);
    assert(restored && config_hotkey_lookup(&restored->cfg, CONFIG_KEY_F1 + 6u) == CONFIG_ACTION_SUMMARY);
    assert(config_hotkey_lookup(&restored->cfg, CONFIG_KEY_F1) == CONFIG_ACTION_NONE);
    client_free(restart); free(restart);
    /* Save-and-apply failures do not partially change the table or any slot. */
    assert(!unlink(f.path) && !symlink("/dev/null", f.path));
    app_config_effective(p, &candidate);
    candidate.hotkey_count = 0;
    memset(candidate.hotkeys, 0, sizeof candidate.hotkeys);
    assert(client_interface_apply(client, &candidate, 1, error, sizeof error) == -1);
    verify_bindings(client, CONFIG_KEY_F1 + 6u, CONFIG_ACTION_SUMMARY, 1);
    assert(p->config_pending_mask == pending_mask);
    assert(!unlink(f.path));
    assert(run(&f, "/unbind all") == IDLE_COMMAND_DONE);
    verify_bindings(client, CONFIG_KEY_F1 + 6u, CONFIG_ACTION_NONE, 0);
    assert(run(&f, "/save input") == IDLE_COMMAND_DONE);
    assert(config_load(&loaded, f.path, error, sizeof error) == 1 && !loaded.hotkey_count);
    config_wipe(&candidate, sizeof candidate); config_wipe(&pending, sizeof pending);
    config_wipe(&effective, sizeof effective); config_wipe(&loaded, sizeof loaded);
    cleanup(&f);
    puts(minimal ? "minimal hotkey preferences: primary-only save, live/lazy/new/restarted inheritance, stale profile isolation and pending settings retention passed" :
                   "full hotkey preferences: primary-only save, live/lazy/new/restarted inheritance, stale profile isolation and pending settings retention passed");
}

static void offline_save_boundary(void)
{
    Fixture f;
    init(&f, 1, 1);
    assert(run(&f, "/bind F7 summary") == IDLE_COMMAND_DONE);
    assert(run(&f, "/save input") == IDLE_COMMAND_ERROR);
    assert(access(f.path, F_OK) < 0);
    verify_bindings(f.client, CONFIG_KEY_F1 + 6u, CONFIG_ACTION_SUMMARY, 1);
    char error[512];
    Config clear = primary(&f)->cfg;
    clear.hotkey_count = 0; memset(clear.hotkeys, 0, sizeof clear.hotkeys);
    assert(client_interface_apply(f.client, &clear, 1, error, sizeof error) == -1);
    verify_bindings(f.client, CONFIG_KEY_F1 + 6u, CONFIG_ACTION_SUMMARY, 1);
    assert(client_interface_apply(f.client, &clear, 0, error, sizeof error) == 1);
    verify_bindings(f.client, CONFIG_KEY_F1 + 6u, CONFIG_ACTION_NONE, 0);
    config_wipe(&clear, sizeof clear);
    cleanup(&f);
    puts("hotkey offline boundary: explicit saves refused without disk or network activity, in-memory customization retained passed");
}

int main(void)
{
    command_validation();
    primary_owned_persistence(0);
    primary_owned_persistence(1);
    offline_save_boundary();
    return 0;
}
