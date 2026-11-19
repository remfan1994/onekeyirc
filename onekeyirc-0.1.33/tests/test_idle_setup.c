/* Guided setup uses the real profile/transaction code; no public network. */
#define _POSIX_C_SOURCE 200809L
#include "idle_setup.h"
#include "credentials.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct Fixture {
    Client *client;
    char directory[128], path[256];
} Fixture;

static char executable[CONFIG_PATH_MAX];
static const char creation_key[] = "0123456789ABCDEF0123456789ABCDEF01234567";
static const char auth_secret[] = "PANEL_AUTH_SECRET_KEPT_HIDDEN";
static const char server_secret[] = "PANEL_SERVER_SECRET_KEPT_HIDDEN";

static int64_t milliseconds(void)
{
    struct timespec value; assert(!clock_gettime(CLOCK_MONOTONIC, &value));
    return (int64_t)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}

static void put(int fd, const char *text)
{
    size_t remaining = strlen(text);
    while (remaining) {
        ssize_t count = write(fd, text, remaining);
        if (count < 0 && errno == EINTR) continue;
        assert(count > 0); text += count; remaining -= (size_t)count;
    }
}

static int fake_gpg(int argc, char **argv)
{
    assert(argc > 1 && !strcmp(argv[argc - 1], "--encrypt"));
    for (int i = 0; i < argc; ++i) {
        assert(!strstr(argv[i], auth_secret) && !strstr(argv[i], server_secret));
    }
    char input[CONFIG_SECRET_MAX + 1];
    size_t used = 0;
    for (;;) {
        ssize_t count = read(0, input + used, sizeof input - used);
        if (count < 0 && errno == EINTR) continue;
        assert(count >= 0);
        if (!count) break;
        used += (size_t)count; assert(used < sizeof input);
    }
    input[used] = 0;
    const char *marker = getenv("IRC_TEST_GPG_MARKER");
    if (marker) {
        int fd = open(marker, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW, 0600);
        assert(fd >= 0); put(fd, "x"); assert(!close(fd));
    }
    const char *mode = getenv("IRC_TEST_GPG_MODE");
    if (mode && !strcmp(mode, "hang")) for (;;) pause();
    if (mode && !strcmp(mode, "failure")) return 2;
    const char *gate = getenv("IRC_TEST_GPG_GATE");
    if (gate && !strcmp(input, server_secret)) {
        for (unsigned i = 0; access(gate, F_OK); ++i) { assert(i < 1000); (void)poll(NULL, 0, 5); }
    }
    config_wipe(input, sizeof input);
    put(3, "[GNUPG:] BEGIN_ENCRYPTION 2 9\n");
    put(1, "ENCRYPTED SETUP FIXTURE");
    put(3, "[GNUPG:] END_ENCRYPTION\n");
    return 0;
}

static void fixture_write(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY | O_TRUNC | O_CREAT | O_NOFOLLOW, 0600);
    assert(fd >= 0); put(fd, value); assert(!close(fd));
}

static void reap_creation(void)
{
    int64_t until = milliseconds() + 2000;
    do {
        credential_reap();
        if (!credential_cleanup_pending()) return;
        (void)poll(NULL, 0, 5);
    } while (milliseconds() < until);
    assert(!"GPG helper was not reaped");
}

static void remove_tree(const char *path)
{
    DIR *directory = opendir(path); assert(directory);
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[CONFIG_PATH_MAX]; struct stat st;
        int n = snprintf(child, sizeof child, "%s/%s", path, entry->d_name);
        assert(n > 0 && (size_t)n < sizeof child && lstat(child, &st) == 0);
        if (S_ISDIR(st.st_mode)) remove_tree(child);
        else assert(unlink(child) == 0);
    }
    assert(closedir(directory) == 0 && rmdir(path) == 0);
}

static void init(Fixture *f, int offline)
{
    memset(f, 0, sizeof *f);
    strcpy(f->directory, "/tmp/irc-setup-test-XXXXXX"); assert(mkdtemp(f->directory));
    int n = snprintf(f->path, sizeof f->path, "%s/config.ini", f->directory);
    assert(n > 0 && (size_t)n < sizeof f->path);
    Config cfg; config_minimal_defaults(&cfg);
    strcpy(cfg.server, "fixture.invalid"); strcpy(cfg.nick, "tester");
    strcpy(cfg.username, "tester"); cfg.autoconnect = 0;
    f->client = calloc(1, sizeof *f->client); assert(f->client);
    char error[512];
    assert(client_init(f->client, &cfg, f->path, "tester", offline, 0, error, sizeof error) == 1);
    assert(client_get_app(f->client, 0, error, sizeof error));
    config_wipe(&cfg, sizeof cfg);
}

static App *app(Fixture *f) { return f->client->slots[f->client->current].app; }

static void cleanup(Fixture *f)
{
    client_free(f->client); free(f->client); remove_tree(f->directory);
    config_wipe(f, sizeof *f);
}

static IdleSetup *begin(Fixture *f, int quick)
{
    char error[512];
    IdleSetup *setup = idle_setup_new(f->client, quick, error, sizeof error);
    if (!setup) fprintf(stderr, "setup start failed: %s\n", error);
    assert(setup); return setup;
}

static IdleSetupView view(IdleSetup *setup)
{
    IdleSetupView result; memset(&result, 0xa5, sizeof result);
    idle_setup_view(setup, &result); return result;
}

static void question(IdleSetup *setup, const char *part)
{
    IdleSetupView current = view(setup);
    if (!strstr(current.question, part))
        fprintf(stderr, "expected question containing %s; got %s\n", part, current.question);
    assert(strstr(current.question, part) && !current.final);
}

static void answer(IdleSetup *setup, const char *value, int expected)
{
    char error[512];
    int result = idle_setup_answer(setup, value, error, sizeof error);
    if (result != expected) {
        IdleSetupView current = view(setup);
        fprintf(stderr, "setup question %s: answer result %d (expected %d): %s\n",
                current.question, result, expected, error);
    }
    assert(result == expected);
}

static void quick_fields(IdleSetup *setup)
{
    question(setup, "Server"); answer(setup, "next.invalid", 1);
    question(setup, "TLS"); answer(setup, "off", 1);
    question(setup, "Port"); answer(setup, "6667", 1);
    question(setup, "Nick"); answer(setup, "NextNick", 1);
    assert(view(setup).final);
}

static void defaults_to_review(IdleSetup *setup)
{
    for (unsigned count = 0; count < 64; ++count) {
        if (view(setup).final) return;
        answer(setup, "", 1);
    }
    assert(!"setup did not reach its review within the bounded field count");
}

static void cancel_and_validation(void)
{
    Fixture f; init(&f, 0);
    App *a = app(&f); Config before = a->cfg, seed = f.client->slots[0].seed;
    const char *values[] = {"next.invalid", "off", "6667", "NextNick"};
    for (unsigned round = 0; round < 32; ++round) {
        for (size_t count = 0; count <= 4; ++count) {
            IdleSetup *setup = begin(&f, 1);
            for (size_t i = 0; i < count; ++i) answer(setup, values[i], 1);
            idle_setup_free(setup);
            assert(!memcmp(&before, &a->cfg, sizeof before));
            assert(!memcmp(&seed, &f.client->slots[0].seed, sizeof seed));
            assert(!a->config_pending_mask && !a->net && !a->sim && !a->credential);
            assert(access(f.path, F_OK) != 0);
        }
    }
    IdleSetup *setup = begin(&f, 1);
    question(setup, "Server"); answer(setup, "bad host", 0); answer(setup, "host\nJOIN #else", 0);
    answer(setup, "next.invalid", 1);
    question(setup, "TLS"); answer(setup, "perhaps", 0); answer(setup, "off", 1);
    question(setup, "Port");
    assert(!strcmp(view(setup).current, "6667"));
    answer(setup, "0", 0); answer(setup, "65536", 0); answer(setup, "6667x", 0); answer(setup, "6667", 1);
    question(setup, "Nick"); answer(setup, "bad nick", 0); answer(setup, "#bad", 0); answer(setup, "NextNick", 1);
    assert(view(setup).final && !strcmp(view(setup).current, "3"));
    char message[512];
    assert(idle_setup_commit(setup, message, sizeof message) == 0);
    assert(!a->net && !a->sim && !a->config_pending_mask);
    answer(setup, "5", 0);
    assert(idle_setup_back(setup)); question(setup, "Nick");
    assert(!strcmp(view(setup).current, "NextNick"));
    answer(setup, "", 1); answer(setup, "4", 2);
    assert(!memcmp(&before, &a->cfg, sizeof before) && !a->config_pending_mask);
    assert(idle_setup_commit(setup, message, sizeof message) == 1);
    Config effective; app_config_effective(a, &effective);
    assert(!strcmp(effective.server, "next.invalid") && !effective.tls && effective.port == 6667);
    assert(!strcmp(effective.nick, "NextNick") && !a->net && !a->sim && !a->credential);
    assert(f.client->current == 0 && access(f.path, F_OK) != 0);
    idle_setup_free(setup); cleanup(&f);
}

static void save_is_private_and_transactional(void)
{
    Fixture f; init(&f, 0); App *a = app(&f); Config before = a->cfg;
    IdleSetup *setup = begin(&f, 1); quick_fields(setup); answer(setup, "2", 2);
    assert(mkdir(f.path, 0700) == 0);
    char message[512]; assert(idle_setup_commit(setup, message, sizeof message) == 0 && *message);
    assert(!memcmp(&before, &a->cfg, sizeof before) && !a->config_pending_mask && !a->net && !a->sim);
    assert(view(setup).final);
    assert(rmdir(f.path) == 0);
    assert(idle_setup_commit(setup, message, sizeof message) == 0);
    answer(setup, "2", 2);
    assert(idle_setup_commit(setup, message, sizeof message) == 1);
    Config loaded; config_minimal_defaults(&loaded);
    assert(config_load(&loaded, f.path, message, sizeof message) == 1);
    assert(!strcmp(loaded.server, "next.invalid") && !strcmp(loaded.nick, "NextNick"));
    assert(!loaded.tls && loaded.port == 6667 && !a->net && !a->sim && !a->credential);
    struct stat st; assert(stat(f.path, &st) == 0 && (st.st_mode & 0777) == 0600);
    idle_setup_free(setup); cleanup(&f);
}

static void simulated_connect_and_offline_save_guard(void)
{
    Fixture f; init(&f, 1); App *a = app(&f); Config before = a->cfg;
    IdleSetup *setup = begin(&f, 1); quick_fields(setup); answer(setup, "1", 2);
    char message[512];
    assert(idle_setup_commit(setup, message, sizeof message) == 0);
    assert(!memcmp(&before, &a->cfg, sizeof before) && !a->net && !a->sim && access(f.path, F_OK) != 0);
    answer(setup, "3", 2);
    assert(idle_setup_commit(setup, message, sizeof message) == 1);
    assert(!a->net && a->sim && access(f.path, F_OK) != 0);
    for (unsigned i = 0; i < 32 && !a->registered; ++i) app_network(a, 0);
    assert(a->registered && !strcmp(a->nick, "NextNick"));
    idle_setup_free(setup); cleanup(&f);
}

static void panel_defaults_and_stale_profile(void)
{
    Fixture f; init(&f, 0); App *primary = app(&f); Config before = primary->cfg;
    IdleSetup *setup = begin(&f, 0);
    question(setup, "Choose a panel");
    assert(!strcmp(view(setup).current, "0") && strstr(view(setup).summary, "0: Application preferences\n1: fixture.invalid\n"));
    defaults_to_review(setup);
    assert(!strcmp(view(setup).current, "1"));
    answer(setup, "3", 0); answer(setup, "4", 0); answer(setup, "2", 2);
    char message[512]; assert(idle_setup_commit(setup, message, sizeof message) == 1);
    Config effective; app_config_effective(primary, &effective);
    assert(!memcmp(&before, &effective, sizeof before));
    idle_setup_free(setup);

    Config secondary; client_new_config(f.client, &secondary);
    strcpy(secondary.server, "secondary.invalid"); strcpy(secondary.nick, "Secondary");
    size_t index;
    assert(client_add(f.client, &secondary, &index, message, sizeof message) == 1 && index == 1);
    f.client->current = index;
    assert(client_get_app(f.client, index, message, sizeof message));
    setup = begin(&f, 1);
    assert(!strcmp(view(setup).title, "Quickstart | Server 2"));
    quick_fields(setup); answer(setup, "4", 2);
    uint64_t token = f.client->slots[index].token;
    assert(client_close(f.client, index, message, sizeof message) == 1);
    strcpy(secondary.nick, "Replacement");
    assert(client_add(f.client, &secondary, &index, message, sizeof message) == 1 && index == 1);
    assert(f.client->slots[index].token != token);
    assert(idle_setup_commit(setup, message, sizeof message) == 0);
    assert(!memcmp(&secondary, &f.client->slots[index].seed, sizeof secondary));
    assert(!f.client->slots[index].app && f.client->current == 0);
    app_config_effective(primary, &effective);
    assert(!memcmp(&before, &effective, sizeof before));
    idle_setup_free(setup); cleanup(&f);
}

static void panel_profile_and_primary_preferences(void)
{
    Fixture f; init(&f, 0); App *primary = app(&f); Config primary_before = primary->cfg;
    Config secondary; client_new_config(f.client, &secondary);
    strcpy(secondary.server, "secondary.invalid"); strcpy(secondary.nick, "Secondary");
    size_t index; char message[512];
    assert(client_add(f.client, &secondary, &index, message, sizeof message) == 1 && index == 1);
    f.client->current = 1;
    IdleSetup *setup = begin(&f, 0); question(setup, "Choose a panel");
    assert(!strcmp(view(setup).current, "0"));
    answer(setup, "9", 0); answer(setup, "3", 0); answer(setup, "2", 1);
    assert(!strcmp(view(setup).title, "Settings panel | Server 2"));
    unsigned count, watch_seen = 0, ignore_seen = 0;
    for (count = 0; count < 64 && !view(setup).final; ++count) {
        IdleSetupView current = view(setup);
        const char *value = "";
        assert(strcmp(current.question, "History lines") && strcmp(current.question, "Send mode") &&
               strcmp(current.question, "Composer mode") && strcmp(current.question, "Output format") && strcmp(current.question, "Queue notices") &&
               strcmp(current.question, "Program messages") && strcmp(current.question, "Persistent logging") &&
               strcmp(current.question, "Single-session mode") && strcmp(current.question, "Command suggestions") &&
               strcmp(current.question, "Default interface"));
        assert(!strstr(current.detail, "Enter keeps") && !strstr(current.detail, "Up returns") &&
               !strstr(current.detail, "Esc cancels"));
        if (!strncmp(current.question, "Mention text", 12) && !watch_seen++) value = "release day";
        if (!strncmp(current.question, "Ignored sender", 14) && !ignore_seen++) value = "Noisy!*@*";
        answer(setup, value, 1);
    }
    assert(count < 64 && watch_seen == 2 && ignore_seen == 2 && !f.client->slots[1].app && f.client->current == 1);
    assert(!strcmp(view(setup).current, "2"));
    answer(setup, "4", 2); assert(idle_setup_commit(setup, message, sizeof message) == 1);
    App *other = f.client->slots[1].app;
    assert(other && other->cfg.history_lines == secondary.history_lines);
    assert(other->cfg.word_count == 1 && !strcmp(other->cfg.words[0], "release day"));
    assert(other->cfg.ignore_count == 1 && !strcmp(other->cfg.ignores[0], "Noisy!*@*"));
    assert(!memcmp(&primary_before, &primary->cfg, sizeof primary_before) && f.client->current == 1);
    size_t selected = app_buffer(other, "#selected", B_CHANNEL, 1);
    assert(selected != SIZE_MAX); app_switch(other, selected);
    other->buffers[selected].unread = 7; other->buffers[selected].unread_mentions = 3;
    Config secondary_before = f.client->slots[1].app->cfg;
    idle_setup_free(setup); setup = begin(&f, 0);
    answer(setup, "", 1); question(setup, "History lines");
    assert(!strcmp(view(setup).title, "Settings panel | Application preferences"));
    assert(idle_setup_back(setup)); question(setup, "Choose a panel");
    assert(!strcmp(view(setup).current, "0")); answer(setup, "", 1);
    answer(setup, "200", 1); question(setup, "Send mode");
    answer(setup, "review", 1); question(setup, "Composer mode");
    answer(setup, "fullscreen", 1); question(setup, "Output format");
    answer(setup, "full", 1); question(setup, "Queue notices");
    answer(setup, "no", 1); question(setup, "Program messages");
    answer(setup, "no", 1); question(setup, "Persistent logging");
    answer(setup, "yes", 1); question(setup, "Single-session mode");
    answer(setup, "yes", 1); question(setup, "Command suggestions");
    answer(setup, "", 1); assert(view(setup).final);
    assert(strstr(view(setup).summary, "Default interface: idle (fixed in onekeyirc)"));
    assert(!strstr(view(setup).summary, "Server:") && !strstr(view(setup).summary, "Nickname:"));
    assert(!strcmp(view(setup).detail, "1 Save | 2 Apply without saving"));
    assert(!primary->cfg.send_review && !primary->cfg.output_full && !primary->cfg.word_count && !primary->cfg.ignore_count);
    answer(setup, "2", 2); assert(idle_setup_commit(setup, message, sizeof message) == 1);
    assert(primary->cfg.history_lines == 200 && primary->cfg.send_review && primary->cfg.composer_fullscreen && primary->cfg.output_full &&
           !primary->cfg.queue_notices && !primary->cfg.program_messages && primary->cfg.persistent_logging && primary->cfg.single_session);
    assert(!primary->cfg.word_count && !primary->cfg.ignore_count);
    assert(primary->cfg.default_interface == CONFIG_INTERFACE_IDLE);
    secondary_before.queue_notices = 0;
    secondary_before.program_messages = 0;
    secondary_before.persistent_logging = 1;
    secondary_before.single_session = 1;
    secondary_before.composer_fullscreen = 1;
    assert(!memcmp(&secondary_before, &f.client->slots[1].app->cfg, sizeof secondary_before));
    assert(!f.client->slots[0].seed.queue_notices && !f.client->slots[1].seed.queue_notices);
    assert(!f.client->slots[0].seed.program_messages && !f.client->slots[1].seed.program_messages);
    assert(f.client->slots[0].seed.persistent_logging && f.client->slots[1].seed.persistent_logging);
    assert(f.client->slots[0].seed.single_session && f.client->slots[1].seed.single_session);
    assert(f.client->slots[0].seed.composer_fullscreen && f.client->slots[1].seed.composer_fullscreen);
    assert(f.client->slots[0].seed.default_interface == CONFIG_INTERFACE_IDLE &&
           f.client->slots[1].seed.default_interface == CONFIG_INTERFACE_IDLE);
    assert(f.client->current == 1 && other->current == selected && other->buffers[selected].unread == 7 &&
           other->buffers[selected].unread_mentions == 3 && !other->net && !other->sim && !other->credential);
    assert(access(f.path, F_OK) != 0);
    idle_setup_free(setup); cleanup(&f);
}

static void panel_numbers_keep_stable_slots(void)
{
    Fixture f; init(&f, 0);
    char message[512], label[64]; Config cfg;
    for (size_t i = 1; i < CLIENT_MAX_SERVERS; ++i) {
        client_new_config(f.client, &cfg);
        snprintf(cfg.server, sizeof cfg.server, "server-%zu.invalid", i + 1u);
        size_t index;
        assert(client_add(f.client, &cfg, &index, message, sizeof message) == 1 && index == i);
    }
    f.client->current = CLIENT_MAX_SERVERS - 1u;
    IdleSetup *setup = begin(&f, 0);
    assert(!strcmp(view(setup).current, "0"));
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        snprintf(label, sizeof label, "%zu: ", i + 1u);
        assert(strstr(view(setup).summary, label));
    }
    answer(setup, "8", 1);
    assert(!strcmp(view(setup).title, "Settings panel | Server 8") &&
           !strcmp(view(setup).current, "server-8.invalid"));
    assert(idle_setup_back(setup)); question(setup, "Choose a panel");
    assert(!strcmp(view(setup).current, "8"));
    assert(client_close(f.client, 3, message, sizeof message) == 1);
    assert(!strstr(view(setup).summary, "4: ") && strstr(view(setup).summary, "5: server-5.invalid"));
    answer(setup, "4", 0); answer(setup, "5", 1);
    assert(!strcmp(view(setup).title, "Settings panel | Server 5") &&
           !strcmp(view(setup).current, "server-5.invalid"));
    assert(f.client->current == CLIENT_MAX_SERVERS - 1u);
    for (size_t i = 1; i < CLIENT_MAX_SERVERS; ++i) assert(!f.client->slots[i].app);
    idle_setup_free(setup); cleanup(&f);
}

static void application_preferences_without_connection_fields(void)
{
    Fixture f; init(&f, 0); ClientSlot *primary = &f.client->slots[0];
    app_free(primary->app); free(primary->app); primary->app = NULL;
    config_wipe(primary->seed.server, sizeof primary->seed.server);
    config_wipe(primary->seed.nick, sizeof primary->seed.nick);
    config_wipe(primary->seed.username, sizeof primary->seed.username);
    Config original = primary->seed;
    const char *values[] = {"", "250", "review", "fullscreen", "full", "no", "no", "yes", "yes", "contextual"};
    for (size_t count = 0; count <= sizeof values / sizeof values[0]; ++count) {
        IdleSetup *cancelled = begin(&f, 0);
        for (size_t i = 0; i < count; ++i) answer(cancelled, values[i], 1);
        idle_setup_free(cancelled);
        assert(!primary->app && !memcmp(&original, &primary->seed, sizeof original));
        assert(f.client->current == 0 && !primary->saved && access(f.path, F_OK) != 0);
    }
    IdleSetup *setup = begin(&f, 0); answer(setup, "", 1);
    question(setup, "History lines"); answer(setup, "49", 0); answer(setup, "5001", 0); answer(setup, "250", 1);
    question(setup, "Send mode"); answer(setup, "unknown", 0); answer(setup, "review", 1);
    question(setup, "Composer mode"); answer(setup, "unknown", 0); answer(setup, "fullscreen", 1);
    question(setup, "Output format"); answer(setup, "unknown", 0); answer(setup, "full", 1);
    question(setup, "Queue notices"); answer(setup, "unknown", 0); answer(setup, "no", 1);
    question(setup, "Program messages"); answer(setup, "unknown", 0); answer(setup, "no", 1);
    question(setup, "Persistent logging"); answer(setup, "unknown", 0); answer(setup, "yes", 1);
    question(setup, "Single-session mode"); answer(setup, "unknown", 0); answer(setup, "yes", 1);
    question(setup, "Command suggestions"); answer(setup, "unknown", 0); answer(setup, "contextual", 1);
    assert(view(setup).final && !strcmp(view(setup).current, "1"));
    answer(setup, "", 2);
    char message[512]; assert(idle_setup_commit(setup, message, sizeof message) == 1);
    assert(!strcmp(message, "Application preferences saved."));
    Config wanted = original, loaded;
    wanted.history_lines = 250; wanted.send_review = wanted.composer_fullscreen = wanted.output_full = 1;
    wanted.queue_notices = 0;
    wanted.program_messages = 0;
    wanted.persistent_logging = 1;
    wanted.single_session = 1;
    wanted.command_suggestions = CONFIG_SUGGEST_CONTEXTUAL;
    wanted.default_interface = CONFIG_INTERFACE_IDLE;
    assert(!primary->app && !memcmp(&wanted, &primary->seed, sizeof wanted) && f.client->current == 0);
    config_minimal_defaults(&loaded);
    assert(config_load(&loaded, f.path, message, sizeof message) == 1);
    assert(!memcmp(&wanted, &loaded, sizeof wanted));
    struct stat st; assert(stat(f.path, &st) == 0 && (st.st_mode & 0777) == 0600);
    idle_setup_free(setup);
    setup = begin(&f, 0); answer(setup, "0", 1); answer(setup, "300", 1);
    answer(setup, "", 1); answer(setup, "", 1); answer(setup, "", 1); answer(setup, "", 1); answer(setup, "", 1);
    answer(setup, "", 1); answer(setup, "", 1); answer(setup, "", 1); answer(setup, "2", 2);
    assert(idle_setup_commit(setup, message, sizeof message) == 1 && !primary->app);
    assert(primary->seed.history_lines == 300 && f.client->current == 0);
    config_minimal_defaults(&loaded); assert(config_load(&loaded, f.path, message, sizeof message) == 1);
    assert(!memcmp(&wanted, &loaded, sizeof wanted)); /* Apply leaves the saved file alone. */
    idle_setup_free(setup); cleanup(&f);
}

static void application_save_failure_preserves_history_and_credentials(void)
{
    Fixture f; init(&f, 0); App *a = app(&f);
    char fifo[320], message[512];
    snprintf(fifo, sizeof fifo, "%s/unopened-password.gpg", f.directory);
    assert(mkfifo(fifo, 0600) == 0);
    strcpy(a->cfg.auth, "sasl"); strcpy(a->cfg.auth_user, "existing-account");
    strcpy(a->cfg.password_gpg_file, fifo); strcpy(a->cfg.server_password_file, fifo);
    size_t selected = app_buffer(a, "#selected", B_CHANNEL, 1);
    assert(selected != SIZE_MAX); app_switch(a, selected);
    for (size_t i = 0; i < 80; ++i) app_log(a, selected, "retained-%zu", i);
    Buffer *b = &a->buffers[selected];
    char **lines = b->lines; size_t line_count = b->line_count, line_cap = b->line_cap;
    Config before = a->cfg, pending = a->config_pending, seed = f.client->slots[0].seed;
    unsigned pending_mask = a->config_pending_mask;
    IdleSetup *setup = begin(&f, 0); answer(setup, "0", 1); answer(setup, "50", 1);
    answer(setup, "review", 1); answer(setup, "fullscreen", 1); answer(setup, "full", 1);
    answer(setup, "", 1); answer(setup, "", 1); answer(setup, "", 1); answer(setup, "", 1); answer(setup, "", 1); answer(setup, "1", 2);
    assert(mkdir(f.path, 0700) == 0);
    alarm(5);
    assert(idle_setup_commit(setup, message, sizeof message) == 0 && *message);
    alarm(0);
    assert(!memcmp(&before, &a->cfg, sizeof before) && !memcmp(&pending, &a->config_pending, sizeof pending));
    assert(!memcmp(&seed, &f.client->slots[0].seed, sizeof seed) && a->config_pending_mask == pending_mask);
    assert(b->lines == lines && b->line_count == line_count && b->line_cap == line_cap);
    assert(a->current == selected && f.client->current == 0 && !a->net && !a->sim && !a->credential);
    assert(rmdir(f.path) == 0);
    assert(idle_setup_commit(setup, message, sizeof message) == 0); /* A failure needs a new decision. */
    answer(setup, "1", 2);
    alarm(5); assert(idle_setup_commit(setup, message, sizeof message) == 1); alarm(0);
    Config wanted = before, loaded, effective;
    wanted.history_lines = 50; wanted.send_review = wanted.composer_fullscreen = wanted.output_full = 1;
    app_config_effective(a, &effective); assert(!memcmp(&wanted, &effective, sizeof wanted));
    config_minimal_defaults(&loaded); assert(config_load(&loaded, f.path, message, sizeof message) == 1);
    assert(!memcmp(&wanted, &loaded, sizeof wanted));
    assert(b->line_cap == 50 && b->line_count == 50 && strstr(b->lines[b->line_start], "retained-30"));
    assert(a->current == selected && !a->net && !a->sim && !a->credential && !a->chat_count && !a->control_count);
    struct stat st; assert(stat(f.path, &st) == 0 && (st.st_mode & 0777) == 0600);
    assert(lstat(fifo, &st) == 0 && S_ISFIFO(st.st_mode));
    idle_setup_free(setup); cleanup(&f);
}

static void application_apply_preserves_live_server_edits(void)
{
    Fixture f; init(&f, 1); App *a = app(&f); char message[512], fifo[320];
    assert(client_connect(f.client, 0, message, sizeof message) == 1);
    for (unsigned i = 0; i < 32 && !a->registered; ++i) app_network(a, 0);
    assert(a->registered && a->sim && !a->net);
    size_t selected = app_buffer(a, "#selected", B_CHANNEL, 1);
    assert(selected != SIZE_MAX); app_switch(a, selected);
    Sim *connection = a->sim; uint64_t connection_id = a->connection_id;
    IdleSetup *setup = begin(&f, 0); answer(setup, "", 1); answer(setup, "250", 1);
    answer(setup, "", 1); answer(setup, "", 1); answer(setup, "", 1); answer(setup, "", 1); answer(setup, "", 1);
    answer(setup, "", 1); answer(setup, "", 1); answer(setup, "", 1);
    assert(view(setup).final);
    snprintf(fifo, sizeof fifo, "%s/late-password.gpg", f.directory); assert(mkfifo(fifo, 0600) == 0);
    Config later;
    app_config_effective(a, &later);
    strcpy(later.server, "late.invalid"); later.port = 7000;
    strcpy(later.nick, "LaterNick"); strcpy(later.username, "later-user"); strcpy(later.realname, "Later real name");
    strcpy(later.channels, "#later"); later.autoconnect = 1;
    strcpy(later.auth, "sasl"); strcpy(later.auth_user, "later-account"); strcpy(later.password_gpg_file, fifo);
    strcpy(later.server_password, "later-server-password"); strcpy(later.ca_file, "/later/ca.pem");
    strcpy(later.words[0], "later word"); later.word_count = 1;
    strcpy(later.ignores[0], "Later!*@*"); later.ignore_count = 1;
    later.send_review = later.output_full = 1;
    later.default_interface = CONFIG_INTERFACE_NCURSES;
    assert(app_config_apply(a, &later, 0, message, sizeof message) == 1);
    assert(strstr(view(setup).summary, "Send mode: review") && strstr(view(setup).summary, "Composer mode: line") && strstr(view(setup).summary, "Output format: full"));
    assert(strstr(view(setup).summary, "Default interface: idle (fixed in onekeyirc)"));
    size_t queued_chat = a->chat_count, queued_control = a->control_count;
    OutLine *chat = a->chat_head, *control = a->control_head;
    answer(setup, "1", 2); /* Offline Save refuses before any preference or history mutation. */
    assert(idle_setup_commit(setup, message, sizeof message) == 0 && strstr(message, "OFFLINE TEST MODE"));
    Config effective; app_config_effective(a, &effective); assert(!memcmp(&later, &effective, sizeof later));
    answer(setup, "2", 2);
    alarm(5); assert(idle_setup_commit(setup, message, sizeof message) == 1); alarm(0);
    later.history_lines = 250;
    app_config_effective(a, &effective); assert(!memcmp(&later, &effective, sizeof later));
    assert(a->registered && a->sim == connection && a->connection_id == connection_id);
    assert(!strcmp(a->cfg.server, "fixture.invalid") && !strcmp(a->nick, "tester"));
    assert(f.client->current == 0 && a->current == selected && !a->net && !a->credential);
    assert(a->chat_count == queued_chat && a->control_count == queued_control && a->chat_head == chat && a->control_head == control);
    assert(access(f.path, F_OK) != 0);
    idle_setup_free(setup); cleanup(&f);
}

static void credential_staging_and_redaction(void)
{
    for (int plain = 0; plain < 2; ++plain) {
        Fixture f; init(&f, 0); App *a = app(&f);
        char fifo[320], message[512];
        snprintf(fifo, sizeof fifo, "%s/never-read.gpg", f.directory);
        assert(mkfifo(fifo, 0600) == 0);
        const char *secret = "fixture-password-kept-hidden";
        IdleSetup *setup = begin(&f, 0); answer(setup, "1", 1);
        unsigned count, secret_seen = 0, source_seen = 0;
        for (count = 0; count < 64 && !view(setup).final; ++count) {
            IdleSetupView current = view(setup); const char *value = "";
            assert(!strstr(current.current, secret) && !strstr(current.summary, secret) && !strstr(current.detail, secret));
            if (!strcmp(current.question, "Auto-authentication")) value = "sasl";
            else if (!strcmp(current.question, "Authentication account")) value = "fixture-account";
            else if (!strcmp(current.question, "Authentication password source")) {
                answer(setup, "none", 0); value = plain ? "plain" : "gpg"; ++source_seen;
            } else if (!strcmp(current.question, "Authentication GPG file")) {
                assert(!plain); value = "existing";
            } else if (!strcmp(current.question, "Authentication password")) {
                assert(plain && current.secret); value = secret; ++secret_seen;
            } else if (!strcmp(current.question, "Authentication password file")) {
                assert(!plain && !current.secret); value = fifo; ++secret_seen;
            }
            answer(setup, value, 1);
        }
        assert(count < 64 && source_seen == 1 && secret_seen == 1);
        assert(!strcmp(a->cfg.auth, "none") && !a->credential && !a->net && !a->sim);
        IdleSetupView final = view(setup);
        assert(!strstr(final.summary, secret) && !strstr(final.current, secret));
        answer(setup, "2", 2);
        alarm(5); /* A premature read of this owned FIFO must fail boundedly. */
        assert(idle_setup_commit(setup, message, sizeof message) == 1);
        alarm(0);
        Config loaded; config_minimal_defaults(&loaded);
        assert(config_load(&loaded, f.path, message, sizeof message) == 1);
        assert(!strcmp(loaded.auth, "sasl") && !strcmp(loaded.auth_user, "fixture-account"));
        assert(plain ? !strcmp(loaded.password, secret) && !*loaded.password_gpg_file :
                       !strcmp(loaded.password_gpg_file, fifo) && !*loaded.password);
        assert(!a->credential && !a->net && !a->sim && !a->chat_count && !a->control_count);
        idle_setup_free(setup); cleanup(&f);
    }
}

static void transport_defaults_and_live_identity(void)
{
    Fixture f; init(&f, 0); App *a = app(&f); char message[512];
    a->cfg.port = 7000;
    strcpy(a->cfg.channels, "#old"); a->cfg.autoconnect = 1;
    strcpy(a->cfg.auth, "sasl"); strcpy(a->cfg.auth_user, "old-account");
    strcpy(a->cfg.password, "old-password"); strcpy(a->cfg.server_password, "old-server-password");
    strcpy(a->cfg.ca_file, "/old/ca.pem");
    IdleSetup *setup = begin(&f, 1);
    answer(setup, "next.invalid", 1); answer(setup, "off", 1);
    assert(!strcmp(view(setup).current, "7000"));
    answer(setup, "", 1); answer(setup, "", 1);
    /* A server NICK event updates an unedited identity while the form is open. */
    strcpy(a->cfg.nick, "ServerAssigned"); strcpy(a->nick, "ServerAssigned");
    IdleSetupView review = view(setup);
    assert(strstr(review.summary, "Nickname: ServerAssigned"));
    answer(setup, "4", 2);
    assert(idle_setup_commit(setup, message, sizeof message) == 1);
    Config effective; app_config_effective(a, &effective);
    assert(!strcmp(effective.nick, "ServerAssigned"));
    assert(effective.port == 7000 && !effective.tls && !strcmp(effective.server, "next.invalid"));
    assert(!effective.channels[0] && !effective.autoconnect && !strcmp(effective.auth, "none"));
    assert(!effective.auth_user[0] && !effective.password[0] && !effective.server_password[0] && !effective.ca_file[0]);
    assert(!a->net && !a->sim && !a->credential && access(f.path, F_OK) != 0);
    idle_setup_free(setup); cleanup(&f);
}

static void endpoint_reset_clears_late_old_server_settings(void)
{
    Fixture f; init(&f, 0); App *a = app(&f);
    IdleSetup *setup = begin(&f, 1); quick_fields(setup);
    /* Another configuration update can arrive while this private draft is
     * open. Old-host secrets must never migrate to the newly chosen host. */
    strcpy(a->cfg.auth, "sasl"); strcpy(a->cfg.auth_user, "late-old-account");
    strcpy(a->cfg.password, "late-old-password");
    strcpy(a->cfg.server_password, "late-old-server-password");
    strcpy(a->cfg.channels, "#late-old"); a->cfg.autoconnect = 1;
    strcpy(a->cfg.ca_file, "/late/old-ca.pem");
    a->cfg.autocommands.count = 1;
    strcpy(a->cfg.autocommands.lines[0], "/msg NickServ old-server-private-token");
    answer(setup, "4", 2);
    char message[512]; assert(idle_setup_commit(setup, message, sizeof message) == 1);
    Config effective; app_config_effective(a, &effective);
    assert(!strcmp(effective.server, "next.invalid") && !effective.tls);
    assert(!strcmp(effective.auth, "none") && !effective.auth_user[0]);
    assert(!effective.password[0] && !effective.password_file[0] && !effective.password_gpg_file[0]);
    assert(!effective.server_password[0] && !effective.server_password_file[0] && !effective.server_password_gpg_file[0]);
    assert(!effective.channels[0] && !effective.autoconnect && !effective.ca_file[0]);
    assert(!effective.autocommands.count && !effective.autocommands.lines[0][0]);
    assert(!a->credential && !a->net && !a->sim && access(f.path, F_OK) != 0);
    idle_setup_free(setup); cleanup(&f);
}

static void fake_creation_environment(Fixture *f, char marker[CONFIG_PATH_MAX])
{
    char program[CONFIG_PATH_MAX];
    assert(snprintf(program, sizeof program, "%s/gpg", f->directory) > 0);
    assert(snprintf(marker, CONFIG_PATH_MAX, "%s/gpg-started", f->directory) > 0);
    assert(!symlink(executable, program));
    assert(!setenv("PATH", f->directory, 1));
    assert(!setenv("IRC_TEST_GPG_MARKER", marker, 1));
    assert(!setenv("IRC_TEST_GPG_MODE", "okay", 1));
    assert(!unsetenv("IRC_TEST_GPG_GATE"));
}

/* Reach the destination before supplying a fingerprint or secret. Both
 * credential kinds use this same public setup flow in all three interfaces. */
static void creation_path_question(IdleSetup *setup, int server_password)
{
    answer(setup, "1", 1);
    for (unsigned i = 0; i < 64; ++i) {
        IdleSetupView current = view(setup);
        const char *value = "";
        assert(!current.final && !current.secret);
        if (!strcmp(current.question, server_password ? "Server password file" : "Authentication password file"))
            return;
        if (!strcmp(current.question, "Auto-authentication")) value = server_password ? "none" : "sasl";
        else if (!strcmp(current.question, "Authentication account")) value = "account";
        else if (!strcmp(current.question, server_password ? "Server password source" : "Authentication password source")) value = "gpg";
        else if (!strcmp(current.question, server_password ? "Server GPG file" : "Authentication GPG file")) value = "create";
        answer(setup, value, 1);
    }
    assert(!"credential destination question was not reached");
}

static void creation_remaining_questions(IdleSetup *setup)
{
    for (unsigned i = 0; i < 64 && !view(setup).final; ++i) {
        IdleSetupView current = view(setup);
        const char *value = "";
        if (strstr(current.question, "GPG key fingerprint")) value = creation_key;
        else if (!strcmp(current.question, "Authentication password to encrypt") ||
                 !strcmp(current.question, "Confirm authentication password")) value = auth_secret;
        else if (!strcmp(current.question, "Server password to encrypt") ||
                 !strcmp(current.question, "Confirm server password")) value = server_secret;
        answer(setup, value, 1);
    }
    assert(view(setup).final);
}

static void creation_fields(IdleSetup *setup, const char *auth_path, const char *server_path)
{
    answer(setup, "1", 1);
    for (unsigned i = 0; i < 64 && !view(setup).final; ++i) {
        IdleSetupView current = view(setup);
        const char *value = "";
        assert(!strstr(current.current, auth_secret) && !strstr(current.current, server_secret));
        assert(!strstr(current.detail, auth_secret) && !strstr(current.summary, server_secret));
        if (!strcmp(current.question, "Auto-authentication")) value = "sasl";
        else if (!strcmp(current.question, "Authentication account")) value = "account";
        else if (!strcmp(current.question, "Authentication password source")) value = "gpg";
        else if (!strcmp(current.question, "Server password source") && server_path) value = "gpg";
        else if (!strcmp(current.question, "Authentication GPG file") || !strcmp(current.question, "Server GPG file")) {
            assert(!strcmp(current.current, "create")); value = "create";
        } else if (!strcmp(current.question, "Authentication password file")) value = auth_path;
        else if (!strcmp(current.question, "Server password file")) value = server_path;
        else if (strstr(current.question, "GPG key fingerprint")) {
            assert(!current.secret);
            answer(setup, "0123456789ABCDEF", 0); answer(setup, "--recipient invalid", 0);
            value = creation_key;
        } else if (!strcmp(current.question, "Authentication password to encrypt")) { assert(current.secret); value = auth_secret; }
        else if (!strcmp(current.question, "Server password to encrypt")) { assert(current.secret); value = server_secret; }
        else if (!strcmp(current.question, "Confirm authentication password")) {
            assert(current.secret); answer(setup, "wrong-password", 0); value = auth_secret;
        } else if (!strcmp(current.question, "Confirm server password")) { assert(current.secret); value = server_secret; }
        answer(setup, value, 1);
    }
    assert(view(setup).final && !idle_setup_pending(setup));
    assert(!strstr(view(setup).summary, auth_secret) && !strstr(view(setup).summary, server_secret));
    assert(strstr(view(setup).summary, creation_key));
}

static int creation_finish(IdleSetup *setup, char *message, size_t cap)
{
    int64_t end = milliseconds() + 11000;
    for (;;) {
        int result = idle_setup_poll(setup, milliseconds(), message, cap);
        if (result) return result;
        assert(idle_setup_pending(setup) && milliseconds() < end);
        (void)poll(NULL, 0, 5);
    }
}

static void encrypted_panel_cancel_and_offline(void)
{
    for (int mode = 0; mode < 3; ++mode) {
        Fixture f; init(&f, 0); App *a = app(&f); Config original = a->cfg;
        char marker[CONFIG_PATH_MAX], destination[CONFIG_PATH_MAX], message[512];
        fake_creation_environment(&f, marker);
        assert(snprintf(destination, sizeof destination, "%s/auth.gpg", f.directory) > 0);
        IdleSetup *setup = begin(&f, 0); creation_fields(setup, destination, NULL);
        assert(access(marker, F_OK) < 0 && access(destination, F_OK) < 0 && access(f.path, F_OK) < 0);
        if (mode) {
            answer(setup, "4", 2);
            assert(!setenv("IRC_TEST_GPG_MODE", mode == 1 ? "hang" : "failure", 1));
            assert(idle_setup_commit(setup, message, sizeof message) == 2 && idle_setup_pending(setup));
            assert(!memcmp(&original, &a->cfg, sizeof original) && !a->config_pending_mask);
            assert(!idle_setup_answer(setup, "2", message, sizeof message));
            if (mode == 1) {
                int64_t began = milliseconds();
                assert(idle_setup_back(setup) && !idle_setup_pending(setup));
                assert(milliseconds() - began < 1000);
            } else assert(creation_finish(setup, message, sizeof message) == -1);
        }
        idle_setup_free(setup); reap_creation();
        assert(!memcmp(&original, &a->cfg, sizeof original) && !a->config_pending_mask);
        assert(access(destination, F_OK) < 0 && access(f.path, F_OK) < 0 && !a->net && !a->credential);
        cleanup(&f);
    }
}

static void encrypted_path_preflight_before_secret(void)
{
    for (int server_password = 0; server_password < 2; ++server_password) {
        for (int kind = 0; kind < 4; ++kind) {
            Fixture f; init(&f, 0); App *a = app(&f); Config before = a->cfg;
            char marker[CONFIG_PATH_MAX], parent[CONFIG_PATH_MAX], destination[CONFIG_PATH_MAX], message[512];
            fake_creation_environment(&f, marker);
            int n = snprintf(parent, sizeof parent, "%s/custom", f.directory);
            assert(n > 0 && (size_t)n < sizeof parent);
            n = snprintf(destination, sizeof destination, "%s/credential.gpg", parent);
            assert(n > 0 && (size_t)n < sizeof destination);
            if (kind == 1 || kind == 2) assert(!mkdir(parent, 0700));
            if (kind == 1) assert(!chmod(parent, 0755));
            if (kind == 2) fixture_write(destination, "EXISTING CIPHERTEXT");
            if (kind == 3) assert(!symlink(f.directory, parent));
            IdleSetup *setup = begin(&f, 0);
            creation_path_question(setup, server_password);
            assert(idle_setup_answer(setup, destination, message, sizeof message) == 0);
            assert(strstr(message, f.directory));
            question(setup, server_password ? "Server password file" : "Authentication password file");
            assert(!view(setup).secret && access(marker, F_OK) < 0 && access(f.path, F_OK) < 0);
            assert(!memcmp(&before, &a->cfg, sizeof before) && !a->config_pending_mask);
            struct stat st;
            if (!kind) assert(lstat(parent, &st) < 0 && errno == ENOENT);
            else if (kind == 1) assert(!stat(parent, &st) && (st.st_mode & 0777) == 0755);
            else if (kind == 2) assert(!stat(destination, &st) && st.st_size == 19);
            else assert(!lstat(parent, &st) && S_ISLNK(st.st_mode));
            assert(snprintf(destination, sizeof destination, "%s/corrected.gpg", f.directory) > 0);
            answer(setup, destination, 1); question(setup, "GPG key fingerprint");
            assert(access(destination, F_OK) < 0 && access(marker, F_OK) < 0);
            idle_setup_free(setup); cleanup(&f);
        }
    }
}

static void encrypted_offline_path_guard(void)
{
    for (int server_password = 0; server_password < 2; ++server_password) {
        Fixture f; init(&f, 1); App *a = app(&f); Config before = a->cfg;
        char marker[CONFIG_PATH_MAX], destination[CONFIG_PATH_MAX], directory[CONFIG_PATH_MAX], message[512];
        fake_creation_environment(&f, marker);
        assert(snprintf(directory, sizeof directory, "%s/credentials", f.directory) > 0);
        assert(snprintf(destination, sizeof destination, "%s/unopened.gpg", f.directory) > 0);
        assert(!mkfifo(destination, 0600));
        IdleSetup *setup = begin(&f, 0);
        creation_path_question(setup, server_password);
        assert(!idle_setup_answer(setup, "", message, sizeof message) && strstr(message, "--test-mode"));
        assert(!idle_setup_answer(setup, destination, message, sizeof message) && strstr(message, "--test-mode"));
        assert(!view(setup).secret && !idle_setup_pending(setup));
        assert(access(marker, F_OK) < 0 && access(directory, F_OK) < 0 && access(f.path, F_OK) < 0);
        struct stat st; assert(!lstat(destination, &st) && S_ISFIFO(st.st_mode));
        assert(!memcmp(&before, &a->cfg, sizeof before) && !a->config_pending_mask);
        idle_setup_free(setup); cleanup(&f);
    }
}

static void encrypted_default_and_absolute_locations(void)
{
    for (int kind = 0; kind < 5; ++kind) {
        int server_password = kind == 1;
        Fixture f; init(&f, 0); App *a = app(&f); Config before = a->cfg;
        char marker[CONFIG_PATH_MAX], directory[CONFIG_PATH_MAX], expected[CONFIG_PATH_MAX], message[768];
        fake_creation_environment(&f, marker);
        assert(snprintf(directory, sizeof directory, "%s/credentials", f.directory) > 0);
        if (kind == 2) {
            assert(!mkdir(directory, 0700));
            assert(snprintf(expected, sizeof expected, "%s/account-password.gpg", directory) > 0);
            fixture_write(expected, "KEEP-EXISTING");
        }
        if (kind == 3) assert(snprintf(expected, sizeof expected, "%s/relative.gpg", f.directory) > 0);
        else assert(snprintf(expected, sizeof expected, "%s/%s-password%s.gpg", directory,
                             server_password ? "server" : "account", kind == 2 ? "-2" : "") > 0);
        int previous = -1;
        if (kind == 3) { previous = open(".", O_RDONLY | O_DIRECTORY); assert(previous >= 0 && !chdir(f.directory)); }
        IdleSetup *setup = begin(&f, 0);
        creation_path_question(setup, server_password);
        if (kind != 3) assert(!strcmp(view(setup).current, expected));
        if (kind < 2 || kind == 4) assert(access(directory, F_OK) < 0); /* Merely showing a default is read-only. */
        assert(access(marker, F_OK) < 0 && access(expected, F_OK) < 0);
        answer(setup, kind == 3 ? "relative.gpg" : "", 1);
        question(setup, "GPG key fingerprint");
        assert(access(marker, F_OK) < 0 && access(expected, F_OK) < 0 && access(f.path, F_OK) < 0);
        if (kind != 3) {
            struct stat st; assert(!stat(directory, &st) && st.st_uid == getuid() && (st.st_mode & 0777) == 0700);
        }
        if (kind == 4) {
            idle_setup_free(setup);
            assert(access(expected, F_OK) < 0 && access(marker, F_OK) < 0 && access(f.path, F_OK) < 0);
            assert(!memcmp(&before, &a->cfg, sizeof before) && !a->config_pending_mask);
            cleanup(&f); continue;
        }
        creation_remaining_questions(setup);
        assert(strstr(view(setup).summary, expected));
        assert(access(expected, F_OK) < 0);
        answer(setup, "4", 2);
        assert(idle_setup_commit(setup, message, sizeof message) == 2);
        assert(creation_finish(setup, message, sizeof message) == 1);
        assert(strstr(message, expected));
        Config effective; app_config_effective(a, &effective);
        assert(!strcmp(server_password ? effective.server_password_gpg_file : effective.password_gpg_file, expected));
        struct stat st; assert(!stat(expected, &st) && (st.st_mode & 0777) == 0600);
        config_wipe(&effective, sizeof effective);
        idle_setup_free(setup); reap_creation();
        if (kind == 3) assert(!fchdir(previous) && !close(previous));
        cleanup(&f);
    }
}

static void encrypted_config_destination_conflicts(void)
{
    for (int kind = 0; kind < 10; ++kind) {
        Fixture f; init(&f, 0); App *a = app(&f); Config before = a->cfg;
        char marker[CONFIG_PATH_MAX], destination[CONFIG_PATH_MAX], sub[CONFIG_PATH_MAX], message[512];
        fake_creation_environment(&f, marker);
        strcpy(destination, f.path);
        int previous = open(".", O_RDONLY | O_DIRECTORY); assert(previous >= 0 && !chdir(f.directory));
        assert(snprintf(sub, sizeof sub, "%s/sub", f.directory) > 0);
        assert(!mkdir(sub, 0700));
        if (kind == 1 || kind == 2 || kind == 3) {
            strcpy(f.client->primary_path, kind == 1 ? "config.ini" : kind == 2 ? "sub/../config.ini" : "./config.ini");
            f.client->slots[0].config_path[0] = 0;
        } else if (kind == 4 || kind == 9) {
            f.client->slots[1].used = 1; f.client->slots[1].token = 2;
            f.client->slots[1].seed = before;
            strcpy(f.client->slots[1].config_path, "sub/../other.ini");
            assert(snprintf(destination, sizeof destination, "%s/other.ini%s", f.directory,
                            kind == 9 ? ".commands" : "") > 0);
        } else if (kind >= 5) {
            static const char *const suffixes[] = {".commands", ".servers", ".servers.lock", ".d"};
            assert(snprintf(destination, sizeof destination, "%s%s", f.path, suffixes[kind - 5]) > 0);
        }
        IdleSetup *setup = begin(&f, 0); creation_path_question(setup, 0);
        assert(!idle_setup_answer(setup, destination, message, sizeof message));
        assert(strstr(message, "configuration file"));
        question(setup, "Authentication password file");
        assert(access(marker, F_OK) < 0 && access(destination, F_OK) < 0 && access(f.path, F_OK) < 0);
        assert(!memcmp(&before, &a->cfg, sizeof before) && !a->config_pending_mask);
        idle_setup_free(setup);
        assert(!fchdir(previous) && !close(previous)); cleanup(&f);
    }

    Fixture f; init(&f, 0); App *a = app(&f); Config before = a->cfg;
    char marker[CONFIG_PATH_MAX], first[CONFIG_PATH_MAX], alias[CONFIG_PATH_MAX], message[512];
    fake_creation_environment(&f, marker);
    assert(snprintf(first, sizeof first, "%s/shared.gpg", f.directory) > 0);
    assert(snprintf(alias, sizeof alias, "%s/./shared.gpg", f.directory) > 0);
    IdleSetup *setup = begin(&f, 0); creation_path_question(setup, 0); answer(setup, first, 1);
    for (unsigned i = 0; i < 24; ++i) {
        IdleSetupView current = view(setup);
        const char *value = "";
        if (!strcmp(current.question, "Server password file")) break;
        if (strstr(current.question, "GPG key fingerprint")) value = creation_key;
        else if (current.secret) value = auth_secret;
        else if (!strcmp(current.question, "Server password source")) value = "gpg";
        else if (!strcmp(current.question, "Server GPG file")) value = "create";
        answer(setup, value, 1);
    }
    question(setup, "Server password file");
    assert(!idle_setup_answer(setup, alias, message, sizeof message) && strstr(message, "different new files"));
    assert(access(first, F_OK) < 0 && access(marker, F_OK) < 0 && access(f.path, F_OK) < 0);
    assert(!memcmp(&before, &a->cfg, sizeof before) && !a->config_pending_mask);
    idle_setup_free(setup); cleanup(&f);
}

static void encrypted_published_path_revisit(void)
{
    for (int tampered = 0; tampered < 3; ++tampered) {
        Fixture f; init(&f, 0); App *a = app(&f); Config before = a->cfg;
        char marker[CONFIG_PATH_MAX], destination[CONFIG_PATH_MAX], message[768];
        fake_creation_environment(&f, marker);
        assert(snprintf(destination, sizeof destination, "%s/auth.gpg", f.directory) > 0);
        assert(!mkdir(f.path, 0700)); /* Explicit Save fails after ciphertext publication. */
        IdleSetup *setup = begin(&f, 0); creation_fields(setup, destination, NULL); answer(setup, "2", 2);
        assert(idle_setup_commit(setup, message, sizeof message) == 2);
        assert(creation_finish(setup, message, sizeof message) == -1);
        struct stat first, starts; assert(!stat(destination, &first) && !stat(marker, &starts) && starts.st_size == 1);
        assert(!memcmp(&before, &a->cfg, sizeof before) && !a->config_pending_mask);
        int held = -1;
        if (tampered == 1) fixture_write(destination, "CHANGED CIPHERTEXT");
        if (tampered == 2) {
            held = open(destination, O_RDONLY); assert(held >= 0 && !unlink(destination));
            fixture_write(destination, "ENCRYPTED SETUP FIXTURE");
            struct stat replaced; assert(!stat(destination, &replaced) && replaced.st_ino != first.st_ino);
        }
        for (unsigned i = 0; i < 64 && strcmp(view(setup).question, "Authentication password file"); ++i)
            assert(idle_setup_back(setup));
        question(setup, "Authentication password file");
        assert(!strstr(view(setup).summary, "not created"));
        int accepted = idle_setup_answer(setup, "", message, sizeof message);
        if (tampered) {
            assert(!accepted && strstr(message, "changed or replaced"));
            question(setup, "Authentication password file");
        } else {
            assert(accepted == 1); question(setup, "GPG key fingerprint");
            creation_remaining_questions(setup);
            assert(!rmdir(f.path)); answer(setup, "2", 2);
            assert(idle_setup_commit(setup, message, sizeof message) == 1);
            assert(!stat(marker, &starts) && starts.st_size == 1); /* Revisit did not re-encrypt. */
        }
        idle_setup_free(setup); reap_creation();
        assert(!access(destination, F_OK)); /* Successful or externally changed files survive cancellation. */
        if (held >= 0) assert(!close(held));
        cleanup(&f);
    }
}

static void encrypted_panel_save_retry_and_apply(void)
{
    for (int action = 2; action <= 4; action += 2) {
        Fixture f; init(&f, 0); App *a = app(&f); Config before = a->cfg;
        char marker[CONFIG_PATH_MAX], first[CONFIG_PATH_MAX], second[CONFIG_PATH_MAX], message[512];
        fake_creation_environment(&f, marker);
        assert(snprintf(first, sizeof first, "%s/auth.gpg", f.directory) > 0);
        assert(snprintf(second, sizeof second, "%s/server.gpg", f.directory) > 0);
        IdleSetup *setup = begin(&f, 0); creation_fields(setup, first, second);
        /* An unedited live automatic command must survive unrelated GPG edits. */
        a->cfg.autocommands.count = 1; strcpy(a->cfg.autocommands.lines[0], "/msg ChanServ op #room tester");
        if (action == 2) assert(!mkdir(f.path, 0700));
        answer(setup, action == 2 ? "2" : "4", 2);
        assert(idle_setup_commit(setup, message, sizeof message) == 2);
        int result = creation_finish(setup, message, sizeof message);
        struct stat one, two, starts;
        assert(!stat(first, &one) && !stat(second, &two) && !stat(marker, &starts) && starts.st_size == 2);
        assert((one.st_mode & 0777) == 0600 && (two.st_mode & 0777) == 0600);
        assert(strstr(message, "Encrypted credentials created"));
        if (action == 2) {
            assert(result == -1 && strstr(message, "retained") && !a->config_pending_mask);
            IdleSetupView review = view(setup);
            assert(strstr(review.summary, first) && strstr(review.summary, second));
            assert(strstr(review.summary, "Authentication encrypted file (created)") &&
                   strstr(review.summary, "Server encrypted file (created)"));
            assert(!strcmp(a->cfg.auth, before.auth) && !*a->cfg.password_gpg_file);
            assert(!rmdir(f.path));
            answer(setup, "2", 2);
            assert(idle_setup_commit(setup, message, sizeof message) == 1 && !idle_setup_pending(setup));
            assert(!stat(marker, &starts) && starts.st_size == 2); /* No re-encryption on retry. */
        } else assert(result == 1 && access(f.path, F_OK) < 0);
        Config effective; app_config_effective(a, &effective);
        assert(!strcmp(effective.password_gpg_file, first) && !strcmp(effective.server_password_gpg_file, second));
        assert(!effective.password[0] && !effective.server_password[0]);
        assert(effective.autocommands.count == 1 && !strcmp(effective.autocommands.lines[0], a->cfg.autocommands.lines[0]));
        idle_setup_free(setup); reap_creation();
        assert(!access(first, F_OK) && !access(second, F_OK) && !a->credential && !a->net);
        cleanup(&f);
    }
}

static void encrypted_group_publish_rollback(void)
{
    for (int edited = 0; edited < 2; ++edited) {
        Fixture f; init(&f, 0); App *a = app(&f); Config before = a->cfg;
        char marker[CONFIG_PATH_MAX], first[CONFIG_PATH_MAX], second[CONFIG_PATH_MAX], gate[CONFIG_PATH_MAX], message[512];
        fake_creation_environment(&f, marker);
        assert(snprintf(first, sizeof first, "%s/auth.gpg", f.directory) > 0);
        assert(snprintf(second, sizeof second, "%s/server.gpg", f.directory) > 0);
        assert(snprintf(gate, sizeof gate, "%s/release-server", f.directory) > 0);
        assert(!setenv("IRC_TEST_GPG_GATE", gate, 1));
        IdleSetup *setup = begin(&f, 0); creation_fields(setup, first, second); answer(setup, "2", 2);
        assert(idle_setup_commit(setup, message, sizeof message) == 2);
        int64_t until = milliseconds() + 4000;
        for (;;) {
            assert(!idle_setup_poll(setup, milliseconds(), message, sizeof message));
            struct stat st;
            if (!stat(marker, &st) && st.st_size == 2) break;
            assert(milliseconds() < until); (void)poll(NULL, 0, 5);
        }
        assert(access(first, F_OK) < 0 && access(second, F_OK) < 0);
        fixture_write(second, "KEEP-EXISTING"); fixture_write(gate, "release");
        assert(creation_finish(setup, message, sizeof message) == -1 && !idle_setup_pending(setup));
        assert(!access(first, F_OK)); /* First publication retained for correction/retry. */
        assert(strstr(message, "Encrypted credential created"));
        IdleSetupView review = view(setup);
        assert(strstr(review.summary, first) && strstr(review.summary, second));
        assert(strstr(review.summary, "Authentication encrypted file (created)") &&
               strstr(review.summary, "Server encrypted file (not created"));
        if (edited) fixture_write(first, "KEEP-EDITED");
        assert(!memcmp(&before, &a->cfg, sizeof before) && !a->config_pending_mask && access(f.path, F_OK) < 0);
        idle_setup_free(setup); reap_creation();
        struct stat st;
        if (edited) assert(!stat(first, &st) && st.st_size == 11);
        else assert(access(first, F_OK) < 0);
        assert(!stat(second, &st) && st.st_size == 13);
        cleanup(&f);
    }
}

static int fail_after_config_replacement(void *context, const Config *config, char *error, size_t cap)
{
    const char *path = context;
    assert(config_save(config, path, error, cap) == 1);
    (void)snprintf(error, cap, "Fixture: configuration replaced; directory synchronization failed.");
    return -1;
}

static void encrypted_save_failure_cancel_and_replaced_config(void)
{
    for (int replaced = 0; replaced < 2; ++replaced) {
        Fixture f; init(&f, 0); App *a = app(&f); Config before = a->cfg;
        char marker[CONFIG_PATH_MAX], destination[CONFIG_PATH_MAX], message[512];
        fake_creation_environment(&f, marker);
        assert(snprintf(destination, sizeof destination, "%s/auth.gpg", f.directory) > 0);
        if (replaced) { a->save_config = fail_after_config_replacement; a->save_context = f.path; }
        else assert(!mkdir(f.path, 0700));
        IdleSetup *setup = begin(&f, 0); creation_fields(setup, destination, NULL); answer(setup, "2", 2);
        assert(idle_setup_commit(setup, message, sizeof message) == 2);
        assert(creation_finish(setup, message, sizeof message) == -1);
        assert(!memcmp(&before, &a->cfg, sizeof before) && !a->config_pending_mask);
        if (replaced) assert(strstr(message, "saved configuration may reference"));
        assert(strstr(message, "Encrypted credential created"));
        assert(strstr(view(setup).summary, destination));
        assert(strstr(view(setup).summary, "Authentication encrypted file (created)"));
        idle_setup_free(setup); reap_creation();
        if (replaced) {
            Config saved; config_minimal_defaults(&saved);
            assert(!access(destination, F_OK) && config_load(&saved, f.path, message, sizeof message) == 1);
            assert(!strcmp(saved.password_gpg_file, destination));
            config_wipe(&saved, sizeof saved);
        } else assert(access(destination, F_OK) < 0);
        cleanup(&f);
    }
}

static void default_interface_application_preference(void)
{
    Fixture f; init(&f, 0);
    App *primary = app(&f);
    primary->cfg.minimal = f.client->slots[0].seed.minimal = 0;
    primary->cfg.default_interface = f.client->slots[0].seed.default_interface = CONFIG_INTERFACE_NCURSES;
    Config extra; client_new_config(f.client, &extra);
    strcpy(extra.server, "second.invalid");
    char message[512]; size_t index;
    assert(client_add(f.client, &extra, &index, message, sizeof message) == 1);
    App *secondary = client_get_app(f.client, index, message, sizeof message);
    assert(secondary); f.client->current = index;
    for (int save = 0; save < 2; ++save) {
        IdleSetup *setup = begin(&f, 0); answer(setup, "0", 1);
        unsigned steps;
        for (steps = 0; steps < 32 && strcmp(view(setup).question, "Default interface"); ++steps) {
            assert(!view(setup).final);
            answer(setup, "", 1);
        }
        assert(steps < 32 && !strcmp(view(setup).current, "ncurses"));
        assert(strstr(view(setup).detail, "Next fresh launch") && strstr(view(setup).detail, "Explicit -i or -N"));
        answer(setup, "unknown", 0); answer(setup, "idle extra", 0); answer(setup, "idle", 1);
        assert(view(setup).final && strstr(view(setup).summary, "Default interface: idle (next fresh launch)"));
        assert(primary->cfg.default_interface == CONFIG_INTERFACE_NCURSES &&
               secondary->cfg.default_interface == CONFIG_INTERFACE_NCURSES && access(f.path, F_OK) != 0);
        if (save) {
            answer(setup, "1", 2);
            assert(idle_setup_commit(setup, message, sizeof message) == 1);
            Config saved; config_defaults(&saved);
            assert(config_load(&saved, f.path, message, sizeof message) == 1 && saved.default_interface == CONFIG_INTERFACE_IDLE);
            assert(!saved.minimal && primary->cfg.default_interface == CONFIG_INTERFACE_IDLE &&
                   secondary->cfg.default_interface == CONFIG_INTERFACE_IDLE);
            assert(!secondary->config_saved && !*secondary->config_path);
            config_wipe(&saved, sizeof saved);
        }
        assert(f.client->current == index && !primary->net && !secondary->net && !primary->sim && !secondary->sim);
        assert(!primary->chat_count && !secondary->chat_count && !primary->control_count && !secondary->control_count);
        idle_setup_free(setup);
    }
    config_wipe(&extra, sizeof extra);
    cleanup(&f);

    /* Standalone exposes its fixed interface honestly and preserves a known
     * saved value when editing unrelated application preferences. */
    init(&f, 0); primary = app(&f);
    primary->cfg.default_interface = f.client->slots[0].seed.default_interface = CONFIG_INTERFACE_NCURSES;
    IdleSetup *setup = begin(&f, 0); answer(setup, "0", 1);
    unsigned steps;
    for (steps = 0; steps < 32 && !view(setup).final; ++steps) {
        assert(strcmp(view(setup).question, "Default interface"));
        answer(setup, "", 1);
    }
    assert(steps < 32 && strstr(view(setup).summary, "Default interface: idle (fixed in onekeyirc)"));
    answer(setup, "1", 2); assert(idle_setup_commit(setup, message, sizeof message) == 1);
    Config saved; config_minimal_defaults(&saved);
    assert(config_load(&saved, f.path, message, sizeof message) == 1 && saved.default_interface == CONFIG_INTERFACE_NCURSES);
    config_wipe(&saved, sizeof saved);
    idle_setup_free(setup); cleanup(&f);
    puts("default interface panel: full namespace selection, cancel/save transaction, primary-only persistence, global propagation and fixed standalone behavior passed");
}

int main(int argc, char **argv)
{
    if (!strcmp(argv[0], "gpg")) return fake_gpg(argc, argv);
    assert(realpath(argv[0], executable));
    char *original_path = getenv("PATH") ? strdup(getenv("PATH")) : NULL;
    cancel_and_validation(); save_is_private_and_transactional();
    simulated_connect_and_offline_save_guard(); panel_defaults_and_stale_profile();
    panel_profile_and_primary_preferences(); panel_numbers_keep_stable_slots();
    application_preferences_without_connection_fields();
    default_interface_application_preference();
    application_save_failure_preserves_history_and_credentials(); application_apply_preserves_live_server_edits();
    credential_staging_and_redaction(); transport_defaults_and_live_identity();
    endpoint_reset_clears_late_old_server_settings();
    encrypted_panel_cancel_and_offline(); encrypted_path_preflight_before_secret();
    encrypted_offline_path_guard(); encrypted_default_and_absolute_locations();
    encrypted_config_destination_conflicts(); encrypted_published_path_revisit();
    encrypted_panel_save_retry_and_apply();
    encrypted_group_publish_rollback(); encrypted_save_failure_cancel_and_replaced_config();
    if (original_path) { assert(!setenv("PATH", original_path, 1)); free(original_path); }
    else assert(!unsetenv("PATH"));
    assert(!unsetenv("IRC_TEST_GPG_MARKER") && !unsetenv("IRC_TEST_GPG_MODE") && !unsetenv("IRC_TEST_GPG_GATE"));
    puts("idle setup: bounded cancelled drafts, four-field Quickstart, app panel 0 and stable one-based servers, global queue notices, private Save/history rollback, simulator no-write, preserved active chat/connection/late commands, masked existing and newly encrypted credentials, early path validation, absolute/private defaults, config/sidecar collision refusal, truthful partial creation, Finish-only helper, confirmation, pending cancellation, two-file publication conflict rollback, unchanged-inode cleanup, safe published-file revisit, config-save retry without re-encryption, and post-replacement-save-failure credential retention passed");
    return 0;
}
