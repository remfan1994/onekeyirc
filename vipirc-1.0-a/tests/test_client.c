#define _POSIX_C_SOURCE 200809L
#include "client.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* The actual profile/catalog code is exercised. Minimal App doubles make
 * any session allocation or connect explicit and open no network sockets. */
static int allocations, releases, connections, simulated_connections, disconnections;
int app_init(App *a, const Config *c, const char *path)
{
    memset(a, 0, sizeof(*a));
    a->cfg = *c;
    (void)snprintf(a->nick, sizeof(a->nick), "%s", c->nick);
    (void)snprintf(a->config_path, sizeof(a->config_path), "%s", path);
    ++allocations;
    return 1;
}
void app_free(App *a) { ++releases; memset(a, 0, sizeof(*a)); }
int app_set_test_mode(App *a) { a->test_mode = 1; return 1; }
void app_set_visible(App *a, int visible) { a->visible = visible; }
void app_set_reconnect(App *a, int enabled) { a->reconnect_enabled = !!enabled && !a->test_mode; }
void app_user_activity(App *a) { ++a->last_local_activity; }
void app_set_backgrounded(App *a, int backgrounded) { a->backgrounded = !!backgrounded; }
int app_request_quit(App *a, const char *reason)
{
    (void)reason;
    a->quit = 1;
    return 1;
}
void app_config_effective(const App *a, Config *out) { *out = a->cfg; }
int app_connect(App *a)
{
    ++connections;
    if (a->test_mode) ++simulated_connections;
    a->registered = 1;
    return 1;
}
void app_disconnect(App *a, const char *reason)
{
    (void)reason;
    ++disconnections;
    a->registered = 0;
}

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
    assert(!chmod(path, 0600));
}

static size_t read_text(const char *path, char *text, size_t cap)
{
    FILE *file = fopen(path, "r");
    assert(file);
    size_t n = fread(text, 1, cap - 1, file);
    assert(!ferror(file) && feof(file) && !fclose(file));
    text[n] = 0;
    return n;
}

static void remove_tree(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    char child[8192];
    assert(dir);
    while ((entry = readdir(dir)) != NULL) {
        struct stat st;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        int n = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        assert(n > 0 && (size_t)n < sizeof(child) && !lstat(child, &st));
        if (S_ISDIR(st.st_mode)) remove_tree(child);
        else assert(!unlink(child));
    }
    assert(!closedir(dir) && !rmdir(path));
}

static size_t used_slots(const Client *client)
{
    size_t count = 0;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) count += client->slots[i].used != 0;
    return count;
}

static void repeated_lifecycle(void)
{
    enum { ROUNDS = 512 };
    char directory[] = "/tmp/vipirc-client-cycles-XXXXXX", primary[CONFIG_PATH_MAX];
    char error[512], path[CONFIG_PATH_MAX];
    Config config, candidate;
    ConfigProfiles profiles;
    Client *client = calloc(1, sizeof(*client)), *restart = calloc(1, sizeof(*restart));
    struct timespec began, ended;
    assert(client && restart && mkdtemp(directory));
    assert(!clock_gettime(CLOCK_MONOTONIC, &began));
    assert(snprintf(primary, sizeof(primary), "%s/config.ini", directory) > 0);
    config_defaults(&config);
    strcpy(config.server, "primary.invalid"); strcpy(config.nick, "Primary");
    assert(!config.use_signals);
    assert(client_init(client, &config, primary, "tester", 0, 0, error, sizeof(error)) == 1);
    App *primary_app = client_get_app(client, 0, error, sizeof(error));
    assert(primary_app);
    uint64_t previous = client->slots[0].token;
    unsigned persisted = 0, reloads = 0, full_sets = 0;
    for (unsigned round = 0; round < ROUNDS; ++round) {
        size_t index;
        client_new_config(client, &candidate);
        strcpy(candidate.server, "secondary.invalid");
        assert(snprintf(candidate.nick, sizeof(candidate.nick), "Session%u", round) > 0);
        assert(client_add(client, &candidate, &index, error, sizeof(error)) == 1 && index == 1);
        assert(client->slots[index].token > previous && client->slots[index].token);
        App *app = client_get_app(client, index, error, sizeof(error));
        assert(app && app != primary_app && !strcmp(app->cfg.nick, candidate.nick));
        assert(client_connect(client, index, error, sizeof(error)) == 1);
        client_disconnect(client, index);
        assert(!app->registered && client->slots[0].app == primary_app);
        if (!(round % 8)) {
            Config future = candidate;
            strcpy(future.server, "next-startup.invalid");
            assert(client_save(client, index, &future, error, sizeof(error)) == 1);
            assert(!strcmp(app->cfg.server, "secondary.invalid") && client->slots[index].saved);
            strcpy(path, client->slots[index].config_path);
            ++persisted;
            if (!(round % 32)) {
                assert(client_init(restart, &config, primary, "tester", 0, 0, error, sizeof(error)) == 1);
                assert(used_slots(restart) == 2 && !restart->slots[1].app &&
                       !strcmp(restart->slots[1].seed.server, "next-startup.invalid"));
                App *restored = client_get_app(restart, 1, error, sizeof(error));
                assert(restored && !strcmp(restored->cfg.nick, candidate.nick));
                client_free(restart); ++reloads;
            }
            for (size_t i = 2; i < CLIENT_MAX_SERVERS; ++i) {
                size_t added;
                assert(client_add(client, &candidate, &added, error, sizeof(error)) == 1 && added == i);
                assert(client_get_app(client, added, error, sizeof(error)));
            }
            assert(used_slots(client) == CLIENT_MAX_SERVERS);
            size_t unchanged = 999;
            assert(client_add(client, &candidate, &unchanged, error, sizeof(error)) == -1 && unchanged == 999);
            for (size_t i = CLIENT_MAX_SERVERS; --i > 1;)
                assert(client_close(client, i, error, sizeof(error)) == 1);
            ++full_sets;
        }
        previous = client->next_token - 1;
        client->current = index;
        assert(client_close(client, index, error, sizeof(error)) == 1 && client->current == 0);
        assert(!client->slots[index].used && !client->slots[index].token && !client->slots[index].app);
        assert(!client_get_app(client, index, error, sizeof(error)) && used_slots(client) == 1);
        if (!(round % 8)) {
            struct stat st;
            assert(!stat(path, &st) && (st.st_mode & 0777) == 0600);
            assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == 1 && !profiles.count);
        }
    }
    client->next_token = UINT64_MAX;
    size_t unchanged = 123;
    assert(client_add(client, &candidate, &unchanged, error, sizeof(error)) == -1 && unchanged == 123 && used_slots(client) == 1);
    client->next_token = 0;
    assert(client_add(client, &candidate, &unchanged, error, sizeof(error)) == -1 && unchanged == 123 && used_slots(client) == 1);
    client_free(client); free(client); free(restart);
    assert(allocations == releases);
    remove_tree(directory);
    assert(!clock_gettime(CLOCK_MONOTONIC, &ended));
    long long elapsed = (long long)(ended.tv_sec - began.tv_sec) * 1000 +
                        (ended.tv_nsec - began.tv_nsec) / 1000000;
    printf("client lifecycle: %d reused-slot connections, %u full eight-slot sets, %u saved/closed profiles, %u reloads, elapsed_ms=%lld; all Apps released\n",
           ROUNDS, full_sets, persisted, reloads, elapsed);
}

static void application_preferences(void)
{
    char directory[] = "/tmp/vipirc-interface-XXXXXX", primary[CONFIG_PATH_MAX];
    char first_path[CONFIG_PATH_MAX], error[512], before[4096], after[4096];
    Client *client = calloc(1, sizeof *client), *restart = calloc(1, sizeof *restart);
    Config config, first, second, desired, loaded;
    size_t one, two;
    assert(client && restart && mkdtemp(directory));
    assert(snprintf(primary, sizeof primary, "%s/config.ini", directory) > 0);
    config_defaults(&config);
    strcpy(config.server, "primary.invalid"); strcpy(config.nick, "Primary");
    config.autoconnect = 1;
    assert(client_init(client, &config, primary, "tester", 0, 0, error, sizeof error) == 1);
    client_new_config(client, &first);
    strcpy(first.server, "second.invalid"); strcpy(first.channels, "#second");
    strcpy(first.nick, "Second"); first.autoconnect = 1;
    first.use_signals = 1; /* A secondary cannot override the primary. */
    first.composer_fullscreen = 1;
    assert(client_add(client, &first, &one, error, sizeof error) == 1);
    assert(!client->slots[one].seed.use_signals);
    assert(!client->slots[one].seed.composer_fullscreen);
    first.use_signals = 0;
    first.composer_fullscreen = 0;
    second = first;
    strcpy(second.server, "third.invalid"); strcpy(second.channels, "#third");
    strcpy(second.nick, "Third");
    assert(client_add(client, &second, &two, error, sizeof error) == 1);
    assert(client_save(client, one, &first, error, sizeof error) == 1);
    assert(client_save(client, two, &second, error, sizeof error) == 1);
    strcpy(first_path, client->slots[one].config_path);
    read_text(first_path, before, sizeof before);
    App *active = client_get_app(client, one, error, sizeof error);
    assert(active);
    client->current = one;
    desired = config; desired.mouse = 0; desired.user_list = 0; desired.hints = desired.shortcut_labels = 0;
    desired.default_interface = CONFIG_INTERFACE_IDLE;
    desired.use_signals = 1;
    desired.composer_fullscreen = 1;
    assert(client_interface_apply(client, &desired, 1, error, sizeof error) == 1);
    assert(client->current == one && client->slots[one].app == active && !client->slots[two].app);
    for (size_t i = 0; i <= two; ++i)
        assert(!client->slots[i].seed.mouse && !client->slots[i].seed.user_list && !client->slots[i].seed.hints && !client->slots[i].seed.shortcut_labels);
    for (size_t i = 0; i <= two; ++i) {
        assert(client->slots[i].seed.default_interface == CONFIG_INTERFACE_IDLE);
        assert(client->slots[i].seed.use_signals);
        assert(client->slots[i].seed.composer_fullscreen);
    }
    assert(active->cfg.default_interface == CONFIG_INTERFACE_IDLE && active->cfg.use_signals);
    assert(active->cfg.composer_fullscreen);
    Config fresh;
    client_new_config(client, &fresh);
    assert(fresh.default_interface == CONFIG_INTERFACE_IDLE && fresh.use_signals);
    assert(fresh.composer_fullscreen);
    strcpy(fresh.server, "fresh.invalid");
    fresh.composer_fullscreen = 0;
    size_t fresh_index;
    assert(client_add(client, &fresh, &fresh_index, error, sizeof error) == 1);
    assert(client->slots[fresh_index].seed.composer_fullscreen);
    assert(client_close(client, fresh_index, error, sizeof error) == 1);
    assert(!active->cfg.mouse && !active->cfg.user_list && !active->cfg.hints && !active->cfg.shortcut_labels);
    assert(!strcmp(active->cfg.server, "second.invalid") && !strcmp(active->cfg.channels, "#second"));
    read_text(first_path, after, sizeof after);
    assert(!strcmp(before, after)); /* Save updates only the application owner. */
    config_defaults(&loaded);
    assert(config_load(&loaded, primary, error, sizeof error) == 1);
    assert(!loaded.mouse && !loaded.user_list && !loaded.hints && !loaded.shortcut_labels && !strcmp(loaded.server, "primary.invalid"));
    assert(loaded.default_interface == CONFIG_INTERFACE_IDLE && loaded.use_signals);
    assert(loaded.composer_fullscreen);
    assert(client_init(restart, &loaded, primary, "tester", 0, 1, error, sizeof error) == 1);
    assert(used_slots(restart) == 3);
    for (size_t i = 0; i <= two; ++i)
        assert(!restart->slots[i].seed.mouse && !restart->slots[i].seed.user_list && !restart->slots[i].seed.hints && !restart->slots[i].seed.shortcut_labels);
    for (size_t i = 0; i <= two; ++i) {
        assert(restart->slots[i].seed.default_interface == CONFIG_INTERFACE_IDLE);
        assert(restart->slots[i].seed.use_signals);
        assert(restart->slots[i].seed.composer_fullscreen);
    }
    int previous_connections = connections;
    assert(client_startup(restart, 1, 0, error, sizeof error) == 1);
    assert(connections == previous_connections + 3);
    for (size_t i = 0; i <= two; ++i) {
        assert(restart->slots[i].app->registered);
        assert(restart->slots[i].app->cfg.default_interface == CONFIG_INTERFACE_IDLE);
        assert(restart->slots[i].app->cfg.use_signals);
        assert(restart->slots[i].app->cfg.composer_fullscreen);
    }
    assert(!strcmp(restart->slots[1].app->cfg.channels, "#second") &&
           !strcmp(restart->slots[2].app->cfg.channels, "#third"));
    client_free(restart);

    /* A failed private save must leave every current and future slot alone. */
    assert(!unlink(primary) && !symlink("/dev/null", primary));
    desired.mouse = desired.user_list = desired.hints = desired.shortcut_labels = 1;
    desired.default_interface = CONFIG_INTERFACE_NCURSES;
    desired.use_signals = 0;
    desired.composer_fullscreen = 0;
    assert(client_interface_apply(client, &desired, 1, error, sizeof error) < 0);
    assert(!active->cfg.mouse && !active->cfg.user_list && !active->cfg.hints && !active->cfg.shortcut_labels && client->current == one);
    assert(!client->slots[two].seed.mouse && !client->slots[two].seed.user_list && !client->slots[two].seed.hints && !client->slots[two].seed.shortcut_labels);
    assert(active->cfg.default_interface == CONFIG_INTERFACE_IDLE &&
           client->slots[two].seed.default_interface == CONFIG_INTERFACE_IDLE);
    assert(active->cfg.use_signals && client->slots[two].seed.use_signals);
    assert(active->cfg.composer_fullscreen && client->slots[two].seed.composer_fullscreen);
    assert(!unlink(primary));
    client->test_mode = 1;
    assert(client_interface_apply(client, &desired, 1, error, sizeof error) < 0);
    assert(!active->cfg.mouse && access(primary, F_OK) < 0);
    assert(active->cfg.default_interface == CONFIG_INTERFACE_IDLE && active->cfg.use_signals);
    assert(client_interface_apply(client, &desired, 0, error, sizeof error) == 1);
    assert(active->cfg.mouse && active->cfg.hints && active->cfg.shortcut_labels && client->slots[two].seed.mouse && client->slots[two].seed.hints && client->slots[two].seed.shortcut_labels && client->current == one);
    assert(active->cfg.default_interface == CONFIG_INTERFACE_NCURSES &&
           client->slots[two].seed.default_interface == CONFIG_INTERFACE_NCURSES);
    assert(!active->cfg.use_signals && !client->slots[two].seed.use_signals);
    assert(!active->cfg.composer_fullscreen && !client->slots[two].seed.composer_fullscreen);
    client_free(client); free(client); free(restart);
    remove_tree(directory);
    assert(allocations == releases);
    puts("application preferences: all live/lazy profiles updated, primary-only persistence, independent startup of three saved servers, failed/offline save atomicity and current-session preservation passed");
}

static void saved_default_selection(void)
{
    char directory[] = "/tmp/vipirc-client-default-XXXXXX";
    char primary[CONFIG_PATH_MAX], catalog[CONFIG_PATH_MAX], first_path[CONFIG_PATH_MAX];
    char first_id[CONFIG_PROFILE_ID_SIZE], second_id[CONFIG_PROFILE_ID_SIZE];
    char before[32768], after[32768], text[512], error[512];
    Config config, first_config, fresh, loaded;
    ConfigProfiles profiles;
    Client *client = calloc(1, sizeof *client), *restart = calloc(1, sizeof *restart);
    size_t first, second;
    assert(client && restart && mkdtemp(directory));
    assert(snprintf(primary, sizeof primary, "%s/config.ini", directory) > 0);
    assert(snprintf(catalog, sizeof catalog, "%s/config.ini.servers", directory) > 0);
    config_defaults(&config);
    strcpy(config.server, "primary.invalid"); strcpy(config.nick, "PrimaryNick");
    strcpy(config.auth, "sasl"); strcpy(config.auth_user, "primary-account");
    strcpy(config.password, "primary-secret-must-stay-primary");
    config.send_review = config.output_full = 1;
    assert(config_save(&config, primary, error, sizeof error) == 1);
    size_t primary_size = read_text(primary, before, sizeof before);
    assert(client_init(client, &config, primary, "login_user", 0, 1, error, sizeof error) == 1);
    client_new_config(client, &fresh);
    strcpy(fresh.server, "remembered.invalid"); strcpy(fresh.nick, "RememberedNick");
    strcpy(fresh.channels, "#remembered");
    first_config = fresh;
    assert(client_add(client, &fresh, &first, error, sizeof error) == 1);
    client->current = first;
    int old_connections = connections;
    assert(client_save_default(client, first, error, sizeof error) == 1);
    assert(client->current == first && connections == old_connections && !client->default_unavailable);
    assert(read_text(primary, after, sizeof after) == primary_size && !strcmp(before, after));
    assert(config_profiles_load(&profiles, primary, error, sizeof error) == 1);
    assert(!strcmp(profiles.default_id, client->slots[first].profile_id));
    strcpy(first_path, client->slots[first].config_path); strcpy(first_id, profiles.default_id);
    strcpy(fresh.server, "unrelated.invalid"); strcpy(fresh.nick, "UnrelatedNick");
    assert(client_add(client, &fresh, &second, error, sizeof error) == 1);
    assert(client_save(client, second, &fresh, error, sizeof error) == 1);
    strcpy(second_id, client->slots[second].profile_id);
    assert(config_profiles_load(&profiles, primary, error, sizeof error) == 1 && !strcmp(profiles.default_id, first_id));

    /* Slot numbering is derived from catalog order; remembered selection is not. */
    assert(snprintf(text, sizeof text, "[servers]\nprofile=%s\nprofile=%s\ndefault=%s\n", second_id, first_id, first_id) > 0);
    write_text(catalog, text);
    assert(client_init(restart, &config, primary, "login_user", 0, 1, error, sizeof error) == 1);
    assert(restart->current == 2 && !restart->default_unavailable && !restart->catalog_error[0]);
    assert(!strcmp(restart->slots[restart->current].seed.server, "remembered.invalid"));
    assert(!strcmp(restart->slots[0].seed.password, "primary-secret-must-stay-primary"));
    assert(!restart->slots[restart->current].seed.password[0]);
    assert(restart->slots[0].seed.send_review && restart->slots[0].seed.output_full);
    assert(client_startup(restart, 0, 0, error, sizeof error) == 1 && connections == old_connections);
    assert(client_connect(restart, restart->current, error, sizeof error) == 1 && connections == old_connections + 1);
    assert(!strcmp(restart->slots[2].app->cfg.server, "remembered.invalid") && !restart->slots[0].app);
    client_free(restart);

    assert(!unlink(first_path));
    assert(client_init(restart, &config, primary, "login_user", 0, 1, error, sizeof error) == 1);
    assert(restart->current == 0 && restart->default_unavailable && restart->catalog_error[0]);
    assert(restart->slots[2].load_failed && !client_get_app(restart, 2, error, sizeof error));
    assert(client_get_app(restart, 0, error, sizeof error));
    client_free(restart);
    assert(config_save(&first_config, first_path, error, sizeof error) == 1);
    write_text(catalog, "[servers]\ndefault=missing.ini\nprofile=other.ini\n");
    assert(client_init(restart, &config, primary, "login_user", 0, 1, error, sizeof error) == 1);
    assert(restart->current == 0 && restart->default_unavailable && restart->catalog_error[0]);
    assert(client_get_app(restart, 0, error, sizeof error));
    client_free(restart);
    write_text(catalog, text);

    size_t catalog_size = read_text(catalog, before, sizeof before);
    assert(client_init(restart, &config, primary, "not a valid login", 1, 1, error, sizeof error) == 1);
    client_new_config(restart, &fresh);
    assert(!strcmp(fresh.nick, "user") && !strcmp(fresh.username, "user"));
    assert(client_save_default(restart, restart->current, error, sizeof error) == -1 && strstr(error, "OFFLINE"));
    assert(read_text(catalog, after, sizeof after) == catalog_size && !strcmp(before, after));
    client_free(restart);

    /* Primary data can be saved even when the later default update fails.
     * The caller must receive that partial outcome and retain the old default. */
    App *primary_app = client_get_app(client, 0, error, sizeof error);
    assert(primary_app);
    strcpy(primary_app->cfg.nick, "SavedBeforeDefaultFailure");
    assert(!chmod(catalog, 0666));
    assert(client_save_default(client, 0, error, sizeof error) == -1);
    assert(strstr(error, "settings were saved") && strstr(error, "default selection was not changed"));
    config_defaults(&loaded);
    assert(config_load(&loaded, primary, error, sizeof error) == 1 && !strcmp(loaded.nick, "SavedBeforeDefaultFailure"));
    assert(!chmod(catalog, 0600));
    assert(config_profiles_load(&profiles, primary, error, sizeof error) == 1 && !strcmp(profiles.default_id, first_id));
    assert(client_save_default(client, 0, error, sizeof error) == 1);
    assert(config_profiles_load(&profiles, primary, error, sizeof error) == 1 && !profiles.default_id[0]);
    assert(client_save_default(client, first, error, sizeof error) == 1);
    client->current = first;
    assert(client_close(client, first, error, sizeof error) == 1 && client->current == 0);
    assert(config_profiles_load(&profiles, primary, error, sizeof error) == 1 && !profiles.default_id[0]);
    assert(!access(first_path, F_OK));
    client_free(client); free(client); free(restart);
    assert(allocations == releases);
    remove_tree(directory);
    puts("client default: stable selected-server restart, preserved primary preferences/credentials, no implicit connect, missing/corrupt default diagnostics, offline refusal, honest partial-save failure and explicit close reset passed");
}

static void automatic_profile_isolation(void)
{
    char directory[] = "/tmp/vipirc-automatic-profiles-XXXXXX", primary[CONFIG_PATH_MAX];
    char error[512], profile[CONFIG_PATH_MAX], before[4096], after[4096];
    Client *client = calloc(1, sizeof *client), *restart = calloc(1, sizeof *restart);
    Config config, next, desired, loaded;
    size_t one, two;
    assert(client && restart && mkdtemp(directory));
    assert(snprintf(primary, sizeof primary, "%s/config.ini", directory) > 0);
    config_defaults(&config);
    strcpy(config.server, "primary.invalid"); strcpy(config.nick, "Primary");
    config.queue_notices = 0; config.autocommands.count = 1;
    strcpy(config.autocommands.lines[0], "/msg NickServ IDENTIFY primary-private-value");
    assert(config_save(&config, primary, error, sizeof error) == 1);
    assert(client_init(client, &config, primary, "tester", 0, 0, error, sizeof error) == 1);
    client_new_config(client, &next);
    assert(!next.queue_notices && !next.autocommands.count && !next.autocommands.lines[0][0]);
    strcpy(next.server, "second.invalid"); next.autocommands.count = 1;
    strcpy(next.autocommands.lines[0], "/msg NickServ IDENTIFY second-private-value");
    next.queue_notices = 1;
    assert(client_add(client, &next, &one, error, sizeof error) == 1);
    assert(!client->slots[one].seed.queue_notices);
    App *active = client_get_app(client, one, error, sizeof error);
    assert(active && !active->cfg.queue_notices);
    assert(client_save(client, one, &active->cfg, error, sizeof error) == 1);
    strcpy(profile, client->slots[one].config_path);
    read_text(profile, before, sizeof before);
    client_new_config(client, &next);
    strcpy(next.server, "third.invalid");
    assert(client_add(client, &next, &two, error, sizeof error) == 1 && !client->slots[two].app);
    desired = config; desired.queue_notices = 1;
    assert(client_interface_apply(client, &desired, 1, error, sizeof error) == 1);
    assert(active->cfg.queue_notices && client->slots[two].seed.queue_notices);
    assert(!strcmp(active->cfg.autocommands.lines[0], "/msg NickServ IDENTIFY second-private-value"));
    assert(!client->slots[two].seed.autocommands.count);
    read_text(profile, after, sizeof after);
    assert(!strcmp(before, after));
    config_defaults(&loaded);
    assert(config_load(&loaded, primary, error, sizeof error) == 1 && loaded.queue_notices);
    assert(!strcmp(loaded.autocommands.lines[0], config.autocommands.lines[0]));
    assert(client_init(restart, &loaded, primary, "tester", 0, 0, error, sizeof error) == 1);
    assert(restart->slots[one].seed.queue_notices && restart->slots[one].seed.autocommands.count == 1);
    assert(!strcmp(restart->slots[one].seed.autocommands.lines[0], active->cfg.autocommands.lines[0]));
    client_new_config(restart, &next);
    assert(next.queue_notices && !next.autocommands.count && !next.autocommands.lines[0][0]);
    client_free(client); client_free(restart); free(client); free(restart);
    config_wipe(&config, sizeof config); config_wipe(&loaded, sizeof loaded);
    config_wipe(&next, sizeof next); config_wipe(&desired, sizeof desired);
    remove_tree(directory);
    assert(allocations == releases);
    puts("automatic profiles: independent private lists, clean new-server defaults and global primary-owned queue notice persistence passed");
}


static void bouncer_profile_isolation(void)
{
    char directory[] = "/tmp/vipirc-bouncer-profiles-XXXXXX";
    char path[CONFIG_PATH_MAX], error[512];
    Client *client = calloc(1, sizeof *client), *restart = calloc(1, sizeof *restart);
    Config primary, fresh, soju, loaded, preferences;
    size_t index;
    assert(client && restart && mkdtemp(directory));
    assert(snprintf(path, sizeof path, "%s/config.ini", directory) > 0);
    config_defaults(&primary);
    strcpy(primary.server, "znc.invalid"); strcpy(primary.nick, "Tester");
    primary.bouncer_type = CONFIG_BOUNCER_ZNC;
    primary.bouncer_away = 1;
    strcpy(primary.bouncer_user, "account");
    strcpy(primary.bouncer_network, "ircnow");
    strcpy(primary.bouncer_client, "desktop");
    strcpy(primary.server_password, "test-only-znc-password");
    assert(config_save(&primary, path, error, sizeof error) == 1);
    assert(client_init(client, &primary, path, "tester", 0, 1, error, sizeof error) == 1);
    client_new_config(client, &fresh);
    assert(fresh.bouncer_type == CONFIG_BOUNCER_NONE && !fresh.bouncer_away);
    assert(!fresh.bouncer_user[0] && !fresh.bouncer_network[0] && !fresh.bouncer_client[0]);
    assert(!fresh.password[0] && !fresh.server_password[0] && !fresh.auth_user[0]);
    assert(!fresh.password_file[0] && !fresh.password_gpg_file[0] &&
           !fresh.server_password_file[0] && !fresh.server_password_gpg_file[0]);
    soju = fresh;
    strcpy(soju.server, "soju.invalid");
    soju.bouncer_type = CONFIG_BOUNCER_SOJU;
    strcpy(soju.bouncer_user, "other");
    strcpy(soju.bouncer_network, "secondnet");
    strcpy(soju.bouncer_client, "phone");
    strcpy(soju.auth, "sasl");
    strcpy(soju.password, "test-only-soju-password");
    assert(client_add(client, &soju, &index, error, sizeof error) == 1);
    App *app = client_get_app(client, index, error, sizeof error);
    assert(app && app->cfg.bouncer_type == CONFIG_BOUNCER_SOJU && !app->cfg.bouncer_away);
    assert(!strcmp(app->cfg.bouncer_user, "other") && !strcmp(app->cfg.bouncer_network, "secondnet"));
    assert(!strcmp(app->cfg.bouncer_client, "phone") && !app->cfg.server_password[0]);
    assert(client_save(client, index, &app->cfg, error, sizeof error) == 1);
    preferences = primary;
    preferences.composer_fullscreen = 1;
    assert(client_interface_apply(client, &preferences, 1, error, sizeof error) == 1);
    assert(app->cfg.bouncer_type == CONFIG_BOUNCER_SOJU && !app->cfg.bouncer_away);
    assert(!strcmp(app->cfg.bouncer_user, "other") && !strcmp(app->cfg.bouncer_network, "secondnet"));
    assert(!strcmp(app->cfg.bouncer_client, "phone") && !app->cfg.server_password[0]);
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof error) == 1);
    assert(client_init(restart, &loaded, path, "tester", 0, 1, error, sizeof error) == 1);
    const Config *restored = &restart->slots[index].seed;
    assert(restored->composer_fullscreen && restored->bouncer_type == CONFIG_BOUNCER_SOJU && !restored->bouncer_away);
    assert(!strcmp(restored->bouncer_user, "other") && !strcmp(restored->bouncer_network, "secondnet"));
    assert(!strcmp(restored->bouncer_client, "phone") && !restored->server_password[0]);
    assert(!strcmp(restored->password, "test-only-soju-password"));
    assert(restart->slots[0].seed.bouncer_type == CONFIG_BOUNCER_ZNC &&
           restart->slots[0].seed.bouncer_away &&
           !strcmp(restart->slots[0].seed.bouncer_user, "account"));
    client_free(client); client_free(restart); free(client); free(restart);
    remove_tree(directory);
    assert(allocations == releases);
    puts("bouncer profiles: new profiles inherit no bouncer identity or credentials; add/save/reload and application preferences preserve independent account, network, client and automatic-away policy");
}

static void command_usage_saved_primary(void)
{
    char directory[] = "/tmp/vipirc-usage-primary-XXXXXX";
    char primary[CONFIG_PATH_MAX], sidecar[CONFIG_PATH_MAX + 16], error[512];
    Config cfg, orphan, loaded;
    Client *client = calloc(1, sizeof *client), *restart = calloc(1, sizeof *restart);
    assert(client && restart && mkdtemp(directory));
    snprintf(primary, sizeof primary, "%s/config.ini", directory);
    snprintf(sidecar, sizeof sidecar, "%s.commands", primary);
    config_defaults(&cfg);
    strcpy(cfg.server, "primary.invalid"); strcpy(cfg.nick, "tester");
    /* A sidecar alone must not turn the intended default path into a saved
     * configuration, or import counts into an unsaved ordinary launch. */
    orphan = cfg; orphan.command_usage_count = 1;
    strcpy(orphan.command_usage[0].name, "summary"); orphan.command_usage[0].count = 99;
    assert(config_command_usage_save(&orphan, primary, error, sizeof error) == 1);
    assert(client_init(client, &cfg, primary, "tester", 0, 0, error, sizeof error) == 1);
    App *app = client_get_app(client, 0, error, sizeof error);
    assert(app && *app->config_path && !app->config_saved && !client->slots[0].saved);
    assert(app->cfg.command_usage_count == 0 && !unlink(sidecar));
    app->cfg.command_usage_count = 1;
    strcpy(app->cfg.command_usage[0].name, "summary"); app->cfg.command_usage[0].count = 3;
    assert(access(primary, F_OK) < 0 && errno == ENOENT);
    assert(access(sidecar, F_OK) < 0 && errno == ENOENT);
    assert(client_save(client, 0, &app->cfg, error, sizeof error) == 1);
    assert(app->config_saved && client->slots[0].saved);
    config_defaults(&loaded);
    assert(config_load(&loaded, primary, error, sizeof error) == 1);
    assert(!loaded.command_usage_count);
    assert(client_init(restart, &loaded, primary, "tester", 0, 1, error, sizeof error) == 1);
    App *saved = client_get_app(restart, 0, error, sizeof error);
    assert(saved && saved->config_saved && saved->cfg.command_usage_count == 1);
    assert(!strcmp(saved->cfg.command_usage[0].name, "summary") && saved->cfg.command_usage[0].count == 3);
    client_free(client); client_free(restart); free(client); free(restart); remove_tree(directory);
    puts("personal command counts: unsaved primary ignores orphan sidecar; first successful save persists retained counts and enables saved-profile writes");
}

static void command_usage_profile_lifecycle(void)
{
    char directory[] = "/tmp/vipirc-usage-profile-XXXXXX";
    char primary[CONFIG_PATH_MAX], separate[CONFIG_PATH_MAX], path[CONFIG_PATH_MAX], error[512];
    Config cfg, added, loaded;
    Client *client = calloc(1, sizeof *client), *restart = calloc(1, sizeof *restart);
    assert(client && restart && mkdtemp(directory));
    snprintf(primary, sizeof primary, "%s/config.ini", directory);
    snprintf(separate, sizeof separate, "%s/other.ini", directory);
    config_defaults(&cfg);
    strcpy(cfg.server, "primary.invalid"); strcpy(cfg.nick, "tester");
    assert(client_init(client, &cfg, primary, "tester", 0, 0, error, sizeof error) == 1);
    client_new_config(client, &added); strcpy(added.server, "secondary.invalid");
    size_t slot;
    assert(client_add(client, &added, &slot, error, sizeof error) == 1);
    App *app = client_get_app(client, slot, error, sizeof error);
    assert(app && !*app->config_path && !app->config_saved);
    app->cfg.command_usage_count = 1;
    strcpy(app->cfg.command_usage[0].name, "summary"); app->cfg.command_usage[0].count = 7;
    assert(client_save(client, slot, &app->cfg, error, sizeof error) == 1);
    assert(*app->config_path && app->config_saved && client->slots[slot].saved);
    snprintf(path, sizeof path, "%s", app->config_path);
    assert(client_init(restart, &cfg, primary, "tester", 0, 0, error, sizeof error) == 1);
    assert(restart->slots[1].seed.command_usage_count == 1 && restart->slots[1].seed.command_usage[0].count == 7);
    App *saved = client_get_app(restart, 1, error, sizeof error);
    assert(saved && saved->config_saved && saved->cfg.command_usage[0].count == 7);
    client_free(restart);
    app->cfg.command_usage[0].count = 0;
    assert(config_command_usage_save(&app->cfg, app->config_path, error, sizeof error) == 1);
    assert(client_init(restart, &cfg, primary, "tester", 0, 0, error, sizeof error) == 1);
    assert(restart->slots[1].seed.command_usage[0].count == 0);
    client_free(restart);
    loaded = app->cfg; loaded.command_usage[0].count = 19;
    assert(config_command_usage_save(&loaded, separate, error, sizeof error) == 1);
    config_defaults(&loaded);
    assert(config_command_usage_load(&loaded, path, error, sizeof error) == 1 && loaded.command_usage[0].count == 0);
    assert(config_command_usage_load(&loaded, separate, error, sizeof error) == 1 && loaded.command_usage[0].count == 19);
    /* Counter write failure must not roll back an already saved preference. */
    char sidecar[CONFIG_PATH_MAX + 16];
    snprintf(sidecar, sizeof sidecar, "%s.commands", path);
    assert(!unlink(sidecar) && !mkdir(sidecar, 0700));
    app->cfg.command_suggestions = CONFIG_SUGGEST_CONTEXTUAL;
    assert(client_save(client, slot, &app->cfg, error, sizeof error) == 1);
    assert(strstr(client->slots[slot].error, "Settings saved; command usage"));
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof error) == 1 && loaded.command_suggestions == CONFIG_SUGGEST_CONTEXTUAL);
    assert(client_save_default(client, slot, error, sizeof error) == 1);
    assert(strstr(client->slots[slot].error, "Settings saved; command usage"));
    assert(strstr(error, "Settings saved; command usage"));
    client_free(client); free(client); free(restart); remove_tree(directory);
    puts("personal command counts: unsaved-to-saved profile migration, restart, deletion, separate configuration isolation and counter-only save warning passed");
}

static void runtime_reconnect_preference(void)
{
    char directory[] = "/tmp/onekey-reconnect-client-XXXXXX";
    char path[CONFIG_PATH_MAX], error[512];
    Config cfg;
    Client *client = calloc(1, sizeof *client);
    assert(client && mkdtemp(directory));
    assert(snprintf(path, sizeof path, "%s/config.ini", directory) > 0);
    config_defaults(&cfg);
    strcpy(cfg.server, "fixture.invalid");
    strcpy(cfg.nick, "tester");
    assert(client_init(client, &cfg, path, "tester", 0, 0, error, sizeof error) == 1);
    int count = connections;
    client_set_reconnect(client, 1);
    assert(client->reconnect_enabled && !client->slots[0].app && connections == count);
    App *first = client_get_app(client, 0, error, sizeof error);
    assert(first && first->reconnect_enabled && connections == count);
    size_t second;
    assert(client_add(client, &cfg, &second, error, sizeof error) == 1);
    App *next = client_get_app(client, second, error, sizeof error);
    assert(next && next->reconnect_enabled && connections == count);
    client_set_reconnect(client, 0);
    assert(!first->reconnect_enabled && !next->reconnect_enabled && connections == count);
    client_free(client);
    assert(client_init(client, &cfg, path, "tester", 1, 0, error, sizeof error) == 1);
    client_set_reconnect(client, 1);
    assert(!client->reconnect_enabled);
    first = client_get_app(client, 0, error, sizeof error);
    assert(first && !first->reconnect_enabled && connections == count);
    client_free(client);
    free(client);
    remove_tree(directory);
    assert(allocations == releases);
    puts("reconnect preference: live/lazy profiles inherit opt-in without implicit connects or offline network access");
}

int main(void)
{
    char directory[] = "/tmp/vipirc-client-XXXXXX";
    char primary[CONFIG_PATH_MAX], catalog[CONFIG_PATH_MAX], profile_dir[CONFIG_PATH_MAX];
    char secret[CONFIG_PATH_MAX], first_path[CONFIG_PATH_MAX], second_path[CONFIG_PATH_MAX];
    char before[4096], after[4096], error[512];
    Config config, fresh, future, loaded;
    ConfigProfiles profiles;
    Client *client = calloc(1, sizeof(*client));
    Client *restart = calloc(1, sizeof(*restart));
    Client *offline = calloc(1, sizeof(*offline));
    size_t first, second, extra;
    struct stat st;
    assert(client && restart && offline && mkdtemp(directory));
    assert(snprintf(primary, sizeof(primary), "%s/config.ini", directory) > 0);
    assert(snprintf(catalog, sizeof(catalog), "%s/config.ini.servers", directory) > 0);
    assert(snprintf(profile_dir, sizeof(profile_dir), "%s/config.ini.d", directory) > 0);
    assert(snprintf(secret, sizeof(secret), "%s/credential-must-not-be-read", directory) > 0);
    assert(!mkfifo(secret, 0600));
    config_defaults(&config);
    strcpy(config.server, "primary.invalid");
    strcpy(config.nick, "Primary");
    strcpy(config.username, "primary_user");
    strcpy(config.channels, "#private-primary");
    strcpy(config.auth, "sasl");
    strcpy(config.auth_user, "primary-account");
    strcpy(config.password_file, secret);
    strcpy(config.server_password_file, secret);
    strcpy(config.ca_file, "/primary-only/ca.pem");
    config.word_count = config.ignore_count = 1;
    strcpy(config.words[0], "primary watch");
    strcpy(config.ignores[0], "PrimaryIgnore");
    config.focus_style = CONFIG_FOCUS_BRACKETS;
    config.mouse = 0;
    assert(client_init(client, &config, primary, "login_tester", 0, 0, error, sizeof(error)) == 1);
    assert(used_slots(client) == 1 && client->slots[0].token && !client->slots[0].app);
    assert(!client->slots[0].saved && !allocations && !connections);
    assert(access(catalog, F_OK) < 0 && errno == ENOENT);
    assert(access(profile_dir, F_OK) < 0 && errno == ENOENT);
    client_new_config(client, &fresh);
    assert(!fresh.server[0] && !strcmp(fresh.nick, "login_tester") && !strcmp(fresh.username, "login_tester"));
    assert(fresh.tls && fresh.port == 6697 && !fresh.autoconnect && !strcmp(fresh.auth, "none"));
    assert(!fresh.channels[0] && !fresh.auth_user[0] && !fresh.password_file[0] &&
           !fresh.server_password_file[0] && !fresh.ca_file[0] && !fresh.word_count && !fresh.ignore_count);
    assert(fresh.focus_style == CONFIG_FOCUS_BRACKETS && !fresh.mouse);
    assert(client_add(client, &fresh, &first, error, sizeof(error)) == -1 && used_slots(client) == 1);
    strcpy(fresh.server, "second.invalid");
    strcpy(fresh.nick, "Second");
    fresh.autoconnect = 1;
    assert(client_add(client, &fresh, &first, error, sizeof(error)) == 1 && first == 1);
    fresh.autoconnect = 0;
    strcpy(fresh.nick, "Third");
    assert(client_add(client, &fresh, &second, error, sizeof(error)) == 1 && second == 2);
    assert(!allocations && !connections && !client->slots[first].saved && !client->slots[first].config_path[0]);
    assert(client_startup(client, 1, 1, error, sizeof(error)) == 1 && !allocations);
    assert(client_startup(client, 0, 0, error, sizeof(error)) == 1);
    assert(allocations == 1 && connections == 1 && client->slots[first].app->registered);
    assert(!client->slots[0].app && !client->slots[second].app);
    App *a = client_get_app(client, first, error, sizeof(error));
    App *b = client_get_app(client, second, error, sizeof(error));
    assert(a && b && a != b && allocations == 2 && !b->registered);
    assert(client_get_app(client, first, error, sizeof(error)) == a && allocations == 2);
    assert(!strcmp(a->cfg.nick, "Second") && !strcmp(b->cfg.nick, "Third"));
    strcpy(a->status, "only second session");
    assert(!b->status[0]);
    client_disconnect(client, first);
    assert(!a->registered && disconnections == 1);
    assert(client_connect(client, second, error, sizeof(error)) == 1 && b->registered && !a->registered);
    assert(access(catalog, F_OK) < 0 && errno == ENOENT);

    future = a->cfg;
    strcpy(future.server, "future-second.invalid");
    assert(a->save_config && a->save_config(a->save_context, &future, error, sizeof(error)) == 1);
    assert(client->slots[first].saved && *a->config_path && !strcmp(a->cfg.server, "second.invalid"));
    assert(!strcmp(client->slots[first].seed.server, "future-second.invalid"));
    strcpy(first_path, a->config_path);
    assert(!stat(first_path, &st) && (st.st_mode & 0777) == 0600);
    assert(!stat(catalog, &st) && (st.st_mode & 0777) == 0600);
    assert(!stat(profile_dir, &st) && (st.st_mode & 0777) == 0700);
    assert(access(primary, F_OK) < 0 && errno == ENOENT);
    assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == 1 && profiles.count == 1);
    config_defaults(&loaded);
    assert(config_load(&loaded, first_path, error, sizeof(error)) == 1);
    assert(!strcmp(loaded.server, future.server) && !strcmp(loaded.nick, future.nick) &&
           !strcmp(loaded.username, future.username) && !strcmp(loaded.auth, future.auth) &&
           loaded.port == future.port && loaded.tls == future.tls &&
           loaded.focus_style == CONFIG_FOCUS_REVERSE);
    assert(client_save(client, second, &b->cfg, error, sizeof(error)) == 1);
    strcpy(second_path, b->config_path);
    assert(strcmp(first_path, second_path));
    assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == 1 && profiles.count == 2);
    size_t before_size = read_text(catalog, before, sizeof(before));
    App *main_app = client_get_app(client, 0, error, sizeof(error));
    assert(main_app && main_app->save_config(main_app->save_context, &config, error, sizeof(error)) == 1);
    assert(client->slots[0].saved && read_text(catalog, after, sizeof(after)) == before_size && !strcmp(before, after));

    /* Saved metadata is loaded, but no App or connection is allocated. */
    int count = allocations;
    assert(client_init(restart, &config, primary, "login_tester", 0, 1, error, sizeof(error)) == 1);
    assert(used_slots(restart) == 3 && allocations == count);
    assert(!restart->slots[1].app && !strcmp(restart->slots[1].seed.server, "future-second.invalid"));
    client_free(restart);
    write_text(first_path, "[connection]\nunknown=value\n");
    assert(client_init(restart, &config, primary, "login_tester", 0, 1, error, sizeof(error)) == 1);
    assert(restart->slots[1].load_failed && restart->slots[1].error[0] && restart->slots[2].used);
    assert(!client_get_app(restart, 1, error, sizeof(error)) && allocations == count);
    assert(config_save(&future, first_path, error, sizeof(error)) == 1);
    assert(client_get_app(restart, 1, error, sizeof(error)) && !restart->slots[1].load_failed);
    client_free(restart);

    /* A failed removal keeps the live session, token, draft owner and
     * current selection available. A successful removal keeps the file. */
    uint64_t old_token = client->slots[first].token;
    client->current = first;
    write_text(catalog, "[servers]\nprofile=../unsafe.ini\n");
    assert(client_close(client, first, error, sizeof(error)) == -1);
    assert(client->slots[first].used && client->slots[first].app == a &&
           client->slots[first].token == old_token && client->current == first);
    assert(client_init(restart, &config, primary, "login_tester", 0, 1, error, sizeof(error)) == 1);
    assert(restart->catalog_error[0] && used_slots(restart) == 1);
    assert(client_get_app(restart, 0, error, sizeof(error)));
    client_free(restart);
    write_text(catalog, before);
    assert(client_close(client, first, error, sizeof(error)) == 1);
    assert(!client->slots[first].used && client->current == 0 && !access(first_path, F_OK));
    assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == 1 && profiles.count == 1);
    assert(client_add(client, &fresh, &extra, error, sizeof(error)) == 1 && extra == first);
    assert(client->slots[extra].token > old_token && !client->slots[extra].app);
    while (used_slots(client) < CLIENT_MAX_SERVERS)
        assert(client_add(client, &fresh, &extra, error, sizeof(error)) == 1);
    extra = 999;
    assert(client_add(client, &fresh, &extra, error, sizeof(error)) == -1 && extra == 999);
    assert(client_close(client, 0, error, sizeof(error)) == -1 && client->slots[0].app == main_app);

    before_size = read_text(catalog, before, sizeof(before));
    count = connections;
    assert(client_init(offline, &config, primary, "not a valid login", 1, 1, error, sizeof(error)) == 1);
    client_new_config(offline, &fresh);
    assert(!strcmp(fresh.nick, "user") && !strcmp(fresh.username, "user"));
    assert(client_startup(offline, 0, 1, error, sizeof(error)) == 1 && connections == count);
    assert(!offline->slots[0].app && !offline->slots[1].app);
    assert(client_startup(offline, 0, 0, error, sizeof(error)) == 1);
    assert(connections == count + 2 && simulated_connections == 2);
    assert(offline->slots[0].app->test_mode && offline->slots[1].app->test_mode);
    assert(client_save(offline, 0, &config, error, sizeof(error)) == -1);
    assert(client_close(offline, 1, error, sizeof(error)) == 1);
    strcpy(fresh.server, "offline-new.invalid");
    assert(client_add(offline, &fresh, &extra, error, sizeof(error)) == 1);
    assert(client_connect(offline, extra, error, sizeof(error)) == 1 && simulated_connections == 3);
    assert(client_save(offline, extra, &fresh, error, sizeof(error)) == -1 && !offline->slots[extra].profile_id[0]);
    assert(read_text(catalog, after, sizeof(after)) == before_size && !strcmp(before, after));
    assert(!access(first_path, F_OK) && !access(second_path, F_OK));
    client_free(offline);
    client_free(client);
    assert(allocations == releases);
    free(client); free(restart); free(offline);
    remove_tree(directory);
    command_usage_saved_primary();
    command_usage_profile_lifecycle();
    repeated_lifecycle();
    application_preferences();
    automatic_profile_isolation();
    bouncer_profile_isolation();
    saved_default_selection();
    runtime_reconnect_preference();
    puts("client: lazy isolated sessions, safe defaults, per-profile save, catalog reload/errors, close transaction, stable tokens, cap and offline no-write checks passed");
    return 0;
}
