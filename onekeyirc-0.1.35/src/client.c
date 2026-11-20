/* Workspace ownership and explicit profile persistence.
 *
 * Read client_init/client_get_app first for eager profile loading and lazy App
 * construction. client_add/close preserve stable slot positions; the save
 * functions below separate profile bytes, catalog discovery and default choice.
 * No connection/profile is saved implicitly. See client.h for borrowed state.
 */
#include "client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failure(ClientSlot *slot, char *error, size_t cap, const char *message)
{
    char copy[512];
    (void)snprintf(copy, sizeof(copy), "%s", message);
    if (slot) (void)snprintf(slot->error, sizeof(slot->error), "%s", copy);
    if (error && cap) (void)snprintf(error, cap, "%s", copy);
    return -1;
}

static ClientSlot *get_slot(Client *client, size_t index)
{
    return client && index < CLIENT_MAX_SERVERS && client->slots[index].used ?
           &client->slots[index] : NULL;
}

static void login_defaults(const Client *client, Config *config)
{
    const char *login = client->login;
    int username_ok = strlen(login) <= 32 && *login;
    for (size_t i = 0; login[i]; ++i) {
        unsigned char ch = (unsigned char)login[i];
        if (ch <= 32 || ch >= 127 || ch == ':' || ch == ',' || ch == '@' || ch == '!')
            username_ok = 0;
    }
    if (!config->nick[0])
        (void)snprintf(config->nick, sizeof(config->nick), "%s", irc_valid_nick(login) ? login : "user");
    if (!config->username[0])
        (void)snprintf(config->username, sizeof(config->username), "%s",
                       username_ok ? login : "user");
}

/* Reusing a fixed slot creates a fresh incarnation. A deferred action
 * must compare token, not index, before acting on this slot again. */
static void prepare_slot(Client *client, size_t index)
{
    ClientSlot *slot = &client->slots[index];
    memset(slot, 0, sizeof(*slot));
    slot->owner = client;
    slot->index = index;
    slot->used = 1;
    slot->token = client->next_token++;
}

/* A loaded profile contributes connection settings. Application-wide
 * preferences come from primary, including when primary has no App yet. */
static int load_profile(Client *client, ClientSlot *slot)
{
    Config config;
    char detail[512];
    config_defaults(&config);
    if (client->slots[0].seed.minimal) config_minimal_defaults(&config);
    int result = config_load(&config, slot->config_path, detail, sizeof(detail));
    if (result <= 0 || !config.server[0]) {
        slot->load_failed = 1;
        int failed = failure(slot, NULL, 0, result < 0 ? detail :
                       !result ? "Saved server profile is missing." : "Saved server profile needs a server.");
        config_wipe(&config, sizeof config);
        return failed;
    }
    if (!client->test_mode)
        (void)config_command_usage_load(&config, slot->config_path, detail, sizeof detail);
    login_defaults(client, &config);
    if (config_validate(&config, detail, sizeof(detail)) < 0) {
        slot->load_failed = 1;
        config_wipe(&config, sizeof config);
        return failure(slot, NULL, 0, detail);
    }
    if (slot->index) {
        const Config *interface = client->slots[0].app ? &client->slots[0].app->cfg : &client->slots[0].seed;
        config.mouse = interface->mouse;
        config.user_list = interface->user_list;
        config.hints = interface->hints;
        config.shortcut_labels = interface->shortcut_labels;
        config.output_full = interface->output_full;
        config.queue_notices = interface->queue_notices;
        config.program_messages = interface->program_messages;
        config.persistent_logging = interface->persistent_logging;
        config.single_session = interface->single_session;
        config.background_away = interface->background_away;
        config.autoaway_minutes = interface->autoaway_minutes;
        memcpy(config.part_message, interface->part_message, sizeof config.part_message);
        memcpy(config.quit_message, interface->quit_message, sizeof config.quit_message);
        memcpy(config.background_away_message, interface->background_away_message, sizeof config.background_away_message);
        memcpy(config.autoaway_message, interface->autoaway_message, sizeof config.autoaway_message);
        config.default_interface = interface->default_interface;
        config.command_suggestions = interface->command_suggestions;
        config.composer_fullscreen = interface->composer_fullscreen;
        config.force_terminal_signals = interface->force_terminal_signals;
        memcpy(config.hotkeys, interface->hotkeys, sizeof config.hotkeys);
        config.hotkey_count = interface->hotkey_count;
    }
    slot->seed = config;
    config_wipe(&config, sizeof config);
    slot->load_failed = 0;
    slot->error[0] = 0;
    return 1;
}

int client_init(Client *client, const Config *primary, const char *path,
                const char *login, int test_mode, int primary_saved,
                char *error, size_t error_cap)
{
    ConfigProfiles profiles;
    char detail[512];
    if (!client || !primary || !path || !*path || strlen(path) >= CONFIG_PATH_MAX)
        return failure(NULL, error, error_cap, "Invalid client or primary configuration path.");
    if (config_validate(primary, error, error_cap) < 0) return -1;
    memset(client, 0, sizeof(*client));
    if (error && error_cap) error[0] = 0;
    client->test_mode = test_mode != 0;
    client->next_token = 1;
    (void)snprintf(client->primary_path, sizeof(client->primary_path), "%s", path);
    (void)snprintf(client->login, sizeof(client->login), "%s",
                   login && *login && strlen(login) < sizeof(client->login) ? login : "user");
    prepare_slot(client, 0);
    client->slots[0].seed = *primary;
    if (!client->test_mode && primary_saved)
        (void)config_command_usage_load(&client->slots[0].seed, path, detail, sizeof detail);
    login_defaults(client, &client->slots[0].seed);
    client->slots[0].saved = primary_saved != 0;
    (void)snprintf(client->slots[0].config_path, CONFIG_PATH_MAX, "%s", path);
    if (config_profiles_load(&profiles, path, detail, sizeof(detail)) < 0) {
        (void)snprintf(client->catalog_error, sizeof(client->catalog_error), "%s", detail);
        client->default_unavailable = 1;
        return 1;
    }
    for (size_t i = 0; i < profiles.count; ++i) {
        ClientSlot *slot = &client->slots[i + 1];
        prepare_slot(client, i + 1);
        slot->saved = 1;
        (void)snprintf(slot->profile_id, sizeof(slot->profile_id), "%s", profiles.ids[i]);
        config_defaults(&slot->seed);
        if (primary->minimal) config_minimal_defaults(&slot->seed);
        if (config_profile_path(slot->config_path, sizeof(slot->config_path), path,
                                slot->profile_id, slot->error, sizeof(slot->error)) < 0) {
            slot->load_failed = 1;
            continue;
        }
        (void)load_profile(client, slot);
    }
    if (profiles.default_id[0]) {
        for (size_t i = 1; i < CLIENT_MAX_SERVERS; ++i) {
            ClientSlot *slot = &client->slots[i];
            if (!slot->used || strcmp(slot->profile_id, profiles.default_id)) continue;
            if (slot->load_failed) {
                client->default_unavailable = 1;
                (void)snprintf(client->catalog_error, sizeof(client->catalog_error),
                               "Saved default server is unavailable: %.450s", slot->error);
            } else client->current = i;
            break;
        }
    }
    return 1;
}

void client_free(Client *client)
{
    if (!client) return;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        if (client->slots[i].app) {
            app_free(client->slots[i].app);
            free(client->slots[i].app);
        }
    }
    config_wipe(client, sizeof(*client));
}

static int save_callback(void *context, const Config *config, char *error, size_t cap)
{
    ClientSlot *slot = context;
    return client_save(slot->owner, slot->index, config, error, cap);
}

void client_set_reconnect(Client *client, int enabled)
{
    client->reconnect_enabled = !!enabled && !client->test_mode;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (client->slots[i].app)
            app_set_reconnect(client->slots[i].app, client->reconnect_enabled);
}

void client_user_activity(Client *client)
{
    if (!client) return;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (client->slots[i].app) app_user_activity(client->slots[i].app);
}

void client_set_backgrounded(Client *client, int backgrounded)
{
    if (!client) return;
    client->backgrounded = backgrounded != 0;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (client->slots[i].app) app_set_backgrounded(client->slots[i].app, client->backgrounded);
}

int client_request_quit(Client *client, const char *reason)
{
    if (!client) return 0;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (client->slots[i].app && !app_request_quit(client->slots[i].app, reason)) return 0;
    return 1;
}

/* The caller borrows the lazily created App; the slot
 * remains its owner. A failed profile load can be retried, but callers
 * must check the result before accessing any live connection state. */
App *client_get_app(Client *client, size_t index, char *error, size_t error_cap)
{
    ClientSlot *slot = get_slot(client, index);
    if (!slot) {
        (void)failure(NULL, error, error_cap, "No such server session.");
        return NULL;
    }
    if (slot->app) return slot->app;
    if (slot->load_failed && load_profile(client, slot) < 0) {
        (void)failure(slot, error, error_cap, slot->error);
        return NULL;
    }
    App *app = calloc(1, sizeof(*app));
    if (!app || !app_init(app, &slot->seed, slot->config_path)) {
        free(app);
        (void)failure(slot, error, error_cap, "Cannot allocate this server session.");
        return NULL;
    }
    if (client->test_mode && !app_set_test_mode(app)) {
        app_free(app);
        free(app);
        (void)failure(slot, error, error_cap, "Cannot initialize this offline simulation.");
        return NULL;
    }
    app->config_saved = slot->saved;
    app_set_reconnect(app, client->reconnect_enabled);
    app_set_backgrounded(app, client->backgrounded);
    app->save_config = save_callback;
    app->save_context = slot;
    app_set_visible(app, 0);
    slot->app = app;
    slot->error[0] = 0;
    return app;
}

void client_new_config(const Client *client, Config *config)
{
    const Config *primary = client->slots[0].app ? &client->slots[0].app->cfg : &client->slots[0].seed;
    config_defaults(config);
    if (primary->minimal) config_minimal_defaults(config);
    login_defaults(client, config);
    config->user_list = primary->user_list;
    config->mouse = primary->mouse;
    config->hints = primary->hints;
    config->shortcut_labels = primary->shortcut_labels;
    config->focus_style = primary->focus_style;
    config->history_lines = primary->history_lines;
    config->send_review = primary->send_review;
    config->output_full = primary->output_full;
    config->queue_notices = primary->queue_notices;
    config->program_messages = primary->program_messages;
    config->persistent_logging = primary->persistent_logging;
    config->single_session = primary->single_session;
    config->background_away = primary->background_away;
    config->autoaway_minutes = primary->autoaway_minutes;
    memcpy(config->part_message, primary->part_message, sizeof config->part_message);
    memcpy(config->quit_message, primary->quit_message, sizeof config->quit_message);
    memcpy(config->background_away_message, primary->background_away_message, sizeof config->background_away_message);
    memcpy(config->autoaway_message, primary->autoaway_message, sizeof config->autoaway_message);
    config->default_interface = primary->default_interface;
    config->command_suggestions = primary->command_suggestions;
    config->composer_fullscreen = primary->composer_fullscreen;
    config->force_terminal_signals = primary->force_terminal_signals;
    memcpy(config->hotkeys, primary->hotkeys, sizeof config->hotkeys);
    config->hotkey_count = primary->hotkey_count;
}

/* New server slots inherit primary interface preferences
 * separately from their own endpoint settings. Never use the array index
 * as a long-lived identity: a closed slot can be reused with a new token. */
int client_add(Client *client, const Config *config, size_t *index, char *error, size_t error_cap)
{
    Config candidate = *config;
    size_t free_slot;
    int result = -1;
    login_defaults(client, &candidate);
    const Config *interface = client->slots[0].app ? &client->slots[0].app->cfg : &client->slots[0].seed;
    candidate.mouse = interface->mouse;
    candidate.user_list = interface->user_list;
    candidate.hints = interface->hints;
    candidate.shortcut_labels = interface->shortcut_labels;
    candidate.output_full = interface->output_full;
    candidate.queue_notices = interface->queue_notices;
    candidate.program_messages = interface->program_messages;
    candidate.persistent_logging = interface->persistent_logging;
    candidate.single_session = interface->single_session;
    candidate.background_away = interface->background_away;
    candidate.autoaway_minutes = interface->autoaway_minutes;
    memcpy(candidate.part_message, interface->part_message, sizeof candidate.part_message);
    memcpy(candidate.quit_message, interface->quit_message, sizeof candidate.quit_message);
    memcpy(candidate.background_away_message, interface->background_away_message, sizeof candidate.background_away_message);
    memcpy(candidate.autoaway_message, interface->autoaway_message, sizeof candidate.autoaway_message);
    candidate.default_interface = interface->default_interface;
    candidate.command_suggestions = interface->command_suggestions;
    candidate.composer_fullscreen = interface->composer_fullscreen;
    candidate.force_terminal_signals = interface->force_terminal_signals;
    memcpy(candidate.hotkeys, interface->hotkeys, sizeof candidate.hotkeys);
    candidate.hotkey_count = interface->hotkey_count;
    if (config_validate(&candidate, error, error_cap) < 0) goto done;
    if (!candidate.server[0]) {
        failure(NULL, error, error_cap, "Enter a server before adding it.");
        goto done;
    }
    for (free_slot = 1; free_slot < CLIENT_MAX_SERVERS; ++free_slot)
        if (!client->slots[free_slot].used) break;
    if (free_slot == CLIENT_MAX_SERVERS) {
        failure(NULL, error, error_cap, "Server limit reached (8); close a secondary session to make room.");
        goto done;
    }
    if (!client->next_token || client->next_token == UINT64_MAX) {
        failure(NULL, error, error_cap, "Server identity counter exhausted; restart the application.");
        goto done;
    }
    prepare_slot(client, free_slot);
    client->slots[free_slot].seed = candidate;
    if (index) *index = free_slot;
    result = 1;
done:
    config_wipe(&candidate, sizeof candidate);
    return result;
}

int client_connect(Client *client, size_t index, char *error, size_t error_cap)
{
    App *app = client_get_app(client, index, error, error_cap);
    if (!app) return -1;
    ClientSlot *slot = &client->slots[index];
    if (!app_connect(app)) return failure(slot, error, error_cap, app->status);
    slot->error[0] = 0;
    return 1;
}

void client_disconnect(Client *client, size_t index)
{
    ClientSlot *slot = get_slot(client, index);
    if (slot && slot->app) app_disconnect(slot->app, "Disconnected by user");
}

/* Remove saved discovery before retiring the runtime slot. If that
 * operation reports an error, keep the live slot available for user recovery. */
int client_close(Client *client, size_t index, char *error, size_t error_cap)
{
    ClientSlot *slot = get_slot(client, index);
    char detail[512];
    int committed;
    if (!slot) return failure(NULL, error, error_cap, "No such server session.");
    if (!index) return failure(slot, error, error_cap, "The primary server stays available; disconnect it instead.");
    if (slot->saved && !client->test_mode &&
        config_profile_remove(client->primary_path, slot->profile_id, &committed, detail, sizeof(detail)) < 0)
        return failure(slot, error, error_cap, detail);
    if (slot->app) {
        app_free(slot->app);
        free(slot->app);
    }
    config_wipe(slot, sizeof(*slot));
    if (client->current == index) client->current = 0;
    return 1;
}

/* Saving a slot is an explicit persistence boundary. The
 * primary config has a different storage path from secondary profiles;
 * failure after publication may still leave new bytes on disk. */
int client_save(Client *client, size_t index, const Config *config, char *error, size_t error_cap)
{
    ClientSlot *slot = get_slot(client, index);
    char detail[512], path_error[512];
    int result, committed = 0;
    if (!slot) return failure(NULL, error, error_cap, "No such server session.");
    if (client->test_mode)
        return failure(slot, error, error_cap, "OFFLINE TEST MODE: saving server profiles is disabled.");
    if (!index) result = config_save(config, client->primary_path, detail, sizeof(detail));
    else {
        result = config_profile_save(config, client->primary_path, slot->profile_id, &committed,
                                     detail, sizeof(detail));
        if (slot->profile_id[0])
            (void)config_profile_path(slot->config_path, sizeof(slot->config_path), client->primary_path,
                                      slot->profile_id, path_error, sizeof(path_error));
        if (committed) slot->saved = 1;
    }
    if (result < 0) return failure(slot, error, error_cap, detail);
    slot->saved = 1;
    slot->seed = *config;
    slot->error[0] = 0;
    if (slot->app) {
        slot->app->config_saved = 1;
        (void)snprintf(slot->app->config_path, sizeof(slot->app->config_path), "%s", slot->config_path);
    }
    if (config_command_usage_save(config, slot->config_path, detail, sizeof detail) < 0) {
        /* The settings transaction has committed. Keep that success so the
         * caller applies the same settings in memory; report counter-only
         * persistence trouble on this server's diagnostic row. */
        (void)snprintf(slot->error, sizeof slot->error, "Settings saved; command usage could not be saved: %.420s", detail);
        if (error && error_cap) (void)snprintf(error, error_cap, "%s", slot->error);
    }
    return 1;
}

int client_save_default(Client *client, size_t index, char *error, size_t error_cap)
{
    ClientSlot *slot = get_slot(client, index);
    Config desired;
    char detail[512], message[640];
    int committed = 0;
    if (!slot) return failure(NULL, error, error_cap, "No such server session.");
    if (client->test_mode)
        return failure(slot, error, error_cap, "OFFLINE TEST MODE: saving configuration is disabled.");
    if (slot->load_failed)
        return failure(slot, error, error_cap, "Cannot save a server profile that could not be loaded.");
    if (slot->app) app_config_effective(slot->app, &desired);
    else desired = slot->seed;
    if (!desired.server[0]) {
        config_wipe(&desired, sizeof desired);
        return failure(slot, error, error_cap, "Choose a server with /connect HOST before /saveconf.");
    }
    int saved = client_save(client, index, &desired, detail, sizeof detail);
    config_wipe(&desired, sizeof desired);
    if (saved < 0) return failure(slot, error, error_cap, detail);
    if (config_profile_set_default(client->primary_path, index ? slot->profile_id : "",
                                    &committed, detail, sizeof detail) < 0) {
        (void)snprintf(message, sizeof message, committed ?
                       "Server settings and default selection were written, but durability confirmation failed: %s" :
                       "Server settings were saved, but the default selection was not changed: %s", detail);
        return failure(slot, error, error_cap, message);
    }
    client->default_unavailable = 0;
    client->catalog_error[0] = 0;
    /* Keep any counter-only save warning from client_save(). The settings
     * and default have committed successfully in either case. */
    if (error && error_cap) (void)snprintf(error, error_cap, "%s", slot->error);
    return 1;
}

/* Merge the fields owned by this preferences form into the primary
 * effective config. Copying the whole candidate would overwrite connection
 * edits staged elsewhere. Optional persistence precedes live propagation. */
int client_interface_apply(Client *client, const Config *candidate, int save,
                           char *error, size_t error_cap)
{
    if (!client || !candidate || !client->slots[0].used)
        return failure(NULL, error, error_cap, "No application configuration is available.");
    Config desired;
    ClientSlot *primary = &client->slots[0];
    if (primary->app) app_config_effective(primary->app, &desired);
    else desired = primary->seed;
    desired.mouse = candidate->mouse;
    desired.user_list = candidate->user_list;
    desired.hints = candidate->hints;
    desired.shortcut_labels = candidate->shortcut_labels;
    desired.queue_notices = candidate->queue_notices;
    desired.program_messages = candidate->program_messages;
    desired.persistent_logging = candidate->persistent_logging;
    desired.single_session = candidate->single_session;
    desired.background_away = candidate->background_away;
    desired.autoaway_minutes = candidate->autoaway_minutes;
    memcpy(desired.part_message, candidate->part_message, sizeof desired.part_message);
    memcpy(desired.quit_message, candidate->quit_message, sizeof desired.quit_message);
    memcpy(desired.background_away_message, candidate->background_away_message, sizeof desired.background_away_message);
    memcpy(desired.autoaway_message, candidate->autoaway_message, sizeof desired.autoaway_message);
    desired.default_interface = candidate->default_interface;
    desired.command_suggestions = candidate->command_suggestions;
    desired.composer_fullscreen = candidate->composer_fullscreen;
    desired.force_terminal_signals = candidate->force_terminal_signals;
    memcpy(desired.hotkeys, candidate->hotkeys, sizeof desired.hotkeys);
    desired.hotkey_count = candidate->hotkey_count;
    if (config_validate(&desired, error, error_cap) < 0 ||
        (save && client_save(client, 0, &desired, error, error_cap) < 0)) {
        config_wipe(&desired, sizeof desired);
        return -1;
    }
    /* These bounded scalar preferences cannot fail after the save. In
     * particular, this path never resizes or discards message histories. */
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        ClientSlot *slot = &client->slots[i];
        if (!slot->used) continue;
        slot->seed.mouse = desired.mouse;
        slot->seed.user_list = desired.user_list;
        slot->seed.hints = desired.hints;
        slot->seed.shortcut_labels = desired.shortcut_labels;
        slot->seed.queue_notices = desired.queue_notices;
        slot->seed.program_messages = desired.program_messages;
        slot->seed.persistent_logging = desired.persistent_logging;
        slot->seed.single_session = desired.single_session;
        slot->seed.background_away = desired.background_away;
        slot->seed.autoaway_minutes = desired.autoaway_minutes;
        memcpy(slot->seed.part_message, desired.part_message, sizeof slot->seed.part_message);
        memcpy(slot->seed.quit_message, desired.quit_message, sizeof slot->seed.quit_message);
        memcpy(slot->seed.background_away_message, desired.background_away_message, sizeof slot->seed.background_away_message);
        memcpy(slot->seed.autoaway_message, desired.autoaway_message, sizeof slot->seed.autoaway_message);
        slot->seed.default_interface = desired.default_interface;
        slot->seed.command_suggestions = desired.command_suggestions;
        slot->seed.composer_fullscreen = desired.composer_fullscreen;
        slot->seed.force_terminal_signals = desired.force_terminal_signals;
        memcpy(slot->seed.hotkeys, desired.hotkeys, sizeof slot->seed.hotkeys);
        slot->seed.hotkey_count = desired.hotkey_count;
        if (slot->app) {
            slot->app->cfg.mouse = desired.mouse;
            slot->app->cfg.user_list = desired.user_list;
            slot->app->cfg.hints = desired.hints;
            slot->app->cfg.shortcut_labels = desired.shortcut_labels;
            slot->app->cfg.queue_notices = desired.queue_notices;
            slot->app->cfg.program_messages = desired.program_messages;
            if (slot->app->cfg.persistent_logging != desired.persistent_logging)
                slot->app->persistent_log_failed = 0;
            slot->app->cfg.persistent_logging = desired.persistent_logging;
            slot->app->cfg.single_session = desired.single_session;
            slot->app->cfg.background_away = desired.background_away;
            slot->app->cfg.autoaway_minutes = desired.autoaway_minutes;
            memcpy(slot->app->cfg.part_message, desired.part_message, sizeof slot->app->cfg.part_message);
            memcpy(slot->app->cfg.quit_message, desired.quit_message, sizeof slot->app->cfg.quit_message);
            memcpy(slot->app->cfg.background_away_message, desired.background_away_message, sizeof slot->app->cfg.background_away_message);
            memcpy(slot->app->cfg.autoaway_message, desired.autoaway_message, sizeof slot->app->cfg.autoaway_message);
            slot->app->cfg.default_interface = desired.default_interface;
            slot->app->cfg.command_suggestions = desired.command_suggestions;
            slot->app->cfg.composer_fullscreen = desired.composer_fullscreen;
            slot->app->cfg.force_terminal_signals = desired.force_terminal_signals;
            memcpy(slot->app->cfg.hotkeys, desired.hotkeys, sizeof slot->app->cfg.hotkeys);
            slot->app->cfg.hotkey_count = desired.hotkey_count;
            slot->app->dirty = 1;
        }
    }
    config_wipe(&desired, sizeof desired);
    if (error && error_cap) error[0] = 0;
    return 1;
}

/* Startup attempts eligible connections independently.
 * One failed server does not suppress the remaining auto-connect slots;
 * the caller receives a failure result and the first diagnostic. */
int client_startup(Client *client, int primary_connect, int no_connect, char *error, size_t error_cap)
{
    char detail[512];
    int result = 1;
    if (error && error_cap) error[0] = 0;
    if (no_connect) return 1;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        ClientSlot *slot = &client->slots[i];
        if (!slot->used || slot->load_failed ||
            !(client->test_mode || (i ? slot->seed.autoconnect : primary_connect))) continue;
        if (client_connect(client, i, detail, sizeof(detail)) < 0) {
            if (result > 0 && error && error_cap) (void)snprintf(error, error_cap, "%s", detail);
            result = -1;
        }
    }
    return result;
}
