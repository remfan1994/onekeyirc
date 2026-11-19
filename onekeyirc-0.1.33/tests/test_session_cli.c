/* Exercise the real CLI parser and startup routing with in-process frontend
 * and session-command boundaries. No AF_UNIX socket or curses screen is used. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "config.h"
#include "client.h"
#include "background.h"
#include "dcc.h"
#include "idle.h"
#include "ui.h"

static int loaded_interface, load_calls, terminal_available;
static int welcome_frontend, run_frontend, observed_interface, run_background;
static int resume_calls, resume_frontend, sessions_calls, close_calls, detach_calls;
static int close_result, detach_result;
static int teardown_order;
static char selected_job[64], observed_nick[64];
static char config_path[CONFIG_PATH_MAX];

static int fixture_config_load(Config *, const char *, char *, size_t);
static int fixture_isatty(int);
static int fixture_idle_welcome(const char *, int);
static int fixture_idle_run_session(Client *, const char *, Background *, DccManager *,
                                    int, int, int, int *);
#ifndef ONEKEY_ONLY
static int fixture_ui_welcome(int);
static int fixture_ui_run_session(Client *, Background *, DccManager *,
                                  int, int, int, int *);
#endif
static int fixture_background_resume(const char *, int);
static int fixture_background_sessions(void);
static int fixture_background_close_session(const char *);
static int fixture_background_detach_session(const char *);
static void fixture_dcc_free(DccManager *);
static void fixture_client_free(Client *);
static void fixture_background_finish(Background *, int);

#define main session_client_main
#define config_load fixture_config_load
#define isatty fixture_isatty
#define idle_welcome fixture_idle_welcome
#define idle_run_session fixture_idle_run_session
#ifndef ONEKEY_ONLY
#define ui_welcome fixture_ui_welcome
#define ui_run_session fixture_ui_run_session
#endif
#define background_resume fixture_background_resume
#define background_sessions fixture_background_sessions
#define background_close_session fixture_background_close_session
#define background_detach_session fixture_background_detach_session
#define dcc_free fixture_dcc_free
#define client_free fixture_client_free
#define background_finish fixture_background_finish
#include "../src/main.c"
#undef main
#undef config_load
#undef isatty
#undef idle_welcome
#undef idle_run_session
#ifndef ONEKEY_ONLY
#undef ui_welcome
#undef ui_run_session
#endif
#undef background_resume
#undef background_sessions
#undef background_close_session
#undef background_detach_session
#undef dcc_free
#undef client_free
#undef background_finish

#ifndef ONEKEY_ONLY
/* Main's informational manual branch is not the subject of this fixture. */
const char *const vipirc_manual[] = {"CLI routing fixture", NULL};
#endif

static int fixture_config_load(Config *cfg, const char *path, char *error, size_t cap)
{
    (void)error; (void)cap;
    assert(!strcmp(path, config_path));
    ++load_calls;
    cfg->default_interface = loaded_interface;
    snprintf(cfg->nick, sizeof cfg->nick, "FixtureNick");
    return 1;
}

static int fixture_isatty(int fd)
{
    assert(fd == STDIN_FILENO || fd == STDOUT_FILENO);
    return terminal_available;
}

static int fixture_idle_welcome(const char *program, int force_terminal_signals)
{
    assert(force_terminal_signals == 0 || force_terminal_signals == 1);
    assert(program && *program);
    welcome_frontend = BACKGROUND_FRONTEND_IDLE;
    return 1;
}

#ifndef ONEKEY_ONLY
static int fixture_ui_welcome(int force_terminal_signals)
{
    assert(force_terminal_signals == 0 || force_terminal_signals == 1);
    welcome_frontend = BACKGROUND_FRONTEND_FULL;
    return 1;
}
#endif

static int observe_run(Client *client, int background, int *next, int frontend)
{
    assert(client && client->test_mode && next);
    run_frontend = frontend;
    run_background = background;
    const ClientSlot *primary = &client->slots[0];
    const Config *cfg = primary->app ? &primary->app->cfg : &primary->seed;
    observed_interface = cfg->default_interface;
    snprintf(observed_nick, sizeof observed_nick, "%s", cfg->nick);
    *next = BACKGROUND_FRONTEND_CURRENT;
    return 0;
}

static int fixture_idle_run_session(Client *client, const char *program,
                                    Background *background, DccManager *dcc,
                                    int start, int connect, int no_connect, int *next)
{
    (void)program; (void)background; (void)dcc; (void)connect;
    assert(no_connect);
    return observe_run(client, start, next, BACKGROUND_FRONTEND_IDLE);
}

#ifndef ONEKEY_ONLY
static int fixture_ui_run_session(Client *client, Background *background, DccManager *dcc,
                                  int start, int connect, int no_connect, int *next)
{
    (void)background; (void)dcc; (void)connect;
    assert(no_connect);
    return observe_run(client, start, next, BACKGROUND_FRONTEND_FULL);
}
#endif

static int fixture_background_resume(const char *job, int frontend)
{
    ++resume_calls;
    resume_frontend = frontend;
    snprintf(selected_job, sizeof selected_job, "%s", job ? job : "");
    return 0;
}

static int fixture_background_sessions(void)
{
    ++sessions_calls;
    return 0;
}

static int fixture_background_close_session(const char *job)
{
    assert(job);
    ++close_calls;
    snprintf(selected_job, sizeof selected_job, "%s", job);
    return close_result;
}

static int fixture_background_detach_session(const char *job)
{
    assert(job);
    ++detach_calls;
    snprintf(selected_job, sizeof selected_job, "%s", job);
    return detach_result;
}

static void fixture_dcc_free(DccManager *dcc)
{
    assert(teardown_order == 0);
    dcc_free(dcc);
    teardown_order = 1;
}

static void fixture_client_free(Client *client)
{
    assert(teardown_order == 1);
    client_free(client);
    teardown_order = 2;
}

static void fixture_background_finish(Background *background, int result)
{
    assert(teardown_order == 2);
    background_finish(background, result);
    teardown_order = 3;
}

static void reset_run(void)
{
#if defined(__GLIBC__)
    optind = 0;
#else
    optind = 1;
#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__APPLE__)
    optreset = 1;
#endif
#endif
    load_calls = welcome_frontend = run_frontend = run_background = 0;
    resume_calls = sessions_calls = close_calls = detach_calls = 0;
    teardown_order = 0;
    resume_frontend = observed_interface = -1;
    selected_job[0] = observed_nick[0] = 0;
}

static int run_options(const char *const *options, int startup)
{
    char *args[32];
    int argc = 0;
    args[argc++] = "session-client";
    if (startup) {
        args[argc++] = "--test-mode";
        args[argc++] = "--no-connect";
        args[argc++] = "--config";
        args[argc++] = config_path;
    }
    for (size_t i = 0; options[i]; ++i) {
        assert(argc + 1 < (int)(sizeof args / sizeof args[0]));
        args[argc++] = (char *)options[i];
    }
    args[argc] = NULL;
    reset_run();
    int result = session_client_main(argc, args);
    if (run_frontend) assert(teardown_order == 3);
    return result;
}

static void launch_cases(void)
{
    static const char *const none[] = {NULL};
    static const char *const idle[] = {"-i", NULL};
    static const char *const long_idle[] = {"--idle", NULL};
    static const char *const nick[] = {"-n", "NicknameStillWorks", NULL};
    static const char *const background[] = {"--background", NULL};
    terminal_available = 1;
    for (loaded_interface = CONFIG_INTERFACE_NCURSES;
         loaded_interface <= CONFIG_INTERFACE_IDLE; ++loaded_interface) {
        assert(run_options(none, 1) == 0 && load_calls == 1);
#ifdef ONEKEY_ONLY
        int expected = BACKGROUND_FRONTEND_IDLE;
#else
        int expected = loaded_interface == CONFIG_INTERFACE_IDLE ?
                       BACKGROUND_FRONTEND_IDLE : BACKGROUND_FRONTEND_FULL;
#endif
        assert(welcome_frontend == expected && run_frontend == expected);
        assert(observed_interface == loaded_interface && !run_background);
        assert(run_options(idle, 1) == 0 && run_frontend == BACKGROUND_FRONTEND_IDLE);
        assert(observed_interface == loaded_interface);
        assert(run_options(long_idle, 1) == 0 && run_frontend == BACKGROUND_FRONTEND_IDLE);
        assert(observed_interface == loaded_interface);
        assert(run_options(nick, 1) == 0 && run_frontend == expected);
        assert(!strcmp(observed_nick, "NicknameStillWorks"));
        assert(observed_interface == loaded_interface);
        terminal_available = 0;
        assert(run_options(background, 1) == 0 && run_frontend == expected);
        assert(!welcome_frontend && run_background && observed_interface == loaded_interface);
        terminal_available = 1;
#ifndef ONEKEY_ONLY
        static const char *const full[] = {"-N", NULL};
        static const char *const long_full[] = {"--ncurses", NULL};
        static const char *const last_idle[] = {"-N", "-i", NULL};
        static const char *const last_full[] = {"-i", "-N", NULL};
        assert(run_options(full, 1) == 0 && run_frontend == BACKGROUND_FRONTEND_FULL);
        assert(observed_interface == loaded_interface);
        assert(run_options(long_full, 1) == 0 && run_frontend == BACKGROUND_FRONTEND_FULL);
        assert(observed_interface == loaded_interface);
        assert(run_options(last_idle, 1) == 0 && run_frontend == BACKGROUND_FRONTEND_IDLE);
        assert(run_options(last_full, 1) == 0 && run_frontend == BACKGROUND_FRONTEND_FULL);
        assert(observed_interface == loaded_interface);
#endif
    }
#ifdef ONEKEY_ONLY
    static const char *const full[] = {"-N", NULL};
    static const char *const long_full[] = {"--ncurses", NULL};
    assert(run_options(full, 1) == 2 && !load_calls && !run_frontend);
    assert(run_options(long_full, 1) == 2 && !load_calls && !run_frontend);
#endif
}

static void management_cases(void)
{
    terminal_available = 0;
    close_result = detach_result = 0;
    static const char *const detach_one[] = {"--detach-session", "1", NULL};
    static const char *const detach_equal[] = {"--detach-session=7", NULL};
    static const char *const close_one[] = {"--close-session", "1", NULL};
    static const char *const close_equal[] = {"--close-session=7", NULL};
    static const char *const list[] = {"--sessions", NULL};
    static const char *const resume[] = {"--resume", "3", NULL};
    static const char *const resume_idle[] = {"-i", "--resume", NULL};
    assert(run_options(detach_one, 0) == 0 && detach_calls == 1);
    assert(!strcmp(selected_job, "1") && !load_calls && !welcome_frontend);
    assert(run_options(detach_equal, 0) == 0 && detach_calls == 1);
    assert(!strcmp(selected_job, "7") && !load_calls);
    detach_result = 1;
    assert(run_options(detach_one, 0) == 1 && detach_calls == 1 && !load_calls);
    detach_result = 2;
    assert(run_options(detach_one, 0) == 2 && detach_calls == 1 && !load_calls);
    detach_result = 0;
    assert(run_options(close_one, 0) == 0 && close_calls == 1);
    assert(!strcmp(selected_job, "1") && !load_calls && !welcome_frontend);
    assert(run_options(close_equal, 0) == 0 && close_calls == 1);
    assert(!strcmp(selected_job, "7") && !load_calls);
    close_result = 1;
    assert(run_options(close_one, 0) == 1 && close_calls == 1 && !load_calls);
    close_result = 2;
    assert(run_options(close_one, 0) == 2 && close_calls == 1 && !load_calls);
    close_result = 0;
    assert(run_options(list, 0) == 0 && sessions_calls == 1 && !load_calls);
    assert(run_options(resume, 0) == 0 && resume_calls == 1 && !load_calls);
    assert(!strcmp(selected_job, "3"));
#ifdef ONEKEY_ONLY
    assert(resume_frontend == BACKGROUND_FRONTEND_IDLE);
#else
    assert(resume_frontend == BACKGROUND_FRONTEND_CURRENT);
    static const char *const resume_full[] = {"--ncurses", "--resume=2", NULL};
    assert(run_options(resume_full, 0) == 0 && resume_calls == 1 && !load_calls);
    assert(resume_frontend == BACKGROUND_FRONTEND_FULL && !strcmp(selected_job, "2"));
#endif
    assert(run_options(resume_idle, 0) == 0 && resume_calls == 1 && !load_calls);
    assert(resume_frontend == BACKGROUND_FRONTEND_IDLE);
    const char *const invalid[][6] = {
        {"--detach-session", NULL},
        {"--detach-session", "1", "--detach-session", "2", NULL},
        {"--detach-session", "1", "--resume", NULL},
        {"--detach-session", "1", "--sessions", NULL},
        {"--detach-session", "1", "--close-session", "2", NULL},
        {"--detach-session", "1", "--config", "/unused", NULL},
        {"--close-session", NULL},
        {"--close-session", "1", "--close-session", "2", NULL},
        {"--close-session", "1", "--resume", NULL},
        {"--close-session", "1", "--sessions", NULL},
        {"--close-session", "1", "--config", "/unused", NULL},
        {"--close-session", "1", "--server", "example.invalid", NULL},
        {"--close-session", "1", "--background", NULL},
        {"--close-session", "1", "--test-mode", NULL}
    };
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        assert(run_options(invalid[i], 0) == 2);
        assert(!load_calls && !close_calls && !detach_calls && !resume_calls && !sessions_calls);
    }
    static const char *const version[] = {"--close-session", "1", "--version", NULL};
    assert(run_options(version, 0) == 0 && !close_calls && !load_calls);
}

static void bouncer_destination_cases(void)
{
    int failures = 0;
    for (int type = CONFIG_BOUNCER_ZNC; type <= CONFIG_BOUNCER_SOJU; ++type) {
        Config initial, changed, expected;
        config_defaults(&initial);
        strcpy(initial.server, "bnc.example.invalid");
        strcpy(initial.nick, "FixtureNick");
        initial.bouncer_type = type;
        initial.bouncer_away = 1;
        strcpy(initial.bouncer_user, "account");
        strcpy(initial.bouncer_network, "ircnow");
        strcpy(initial.bouncer_client, "ssh");
        strcpy(initial.auth, type == CONFIG_BOUNCER_ZNC ? "none" : "sasl");
        strcpy(type == CONFIG_BOUNCER_ZNC ? initial.server_password_file : initial.password_file,
               "/no-credential-file-is-read-by-this-test");
        strcpy(initial.channels, "#saved-channel");
        strcpy(initial.ca_file, "/no-CA-file-is-read-by-this-test");
        initial.autocommands.count = 1;
        strcpy(initial.autocommands.lines[0], "/msg NickServ IDENTIFY fake-test-canary");
        char error[256];
        assert(config_validate(&initial, error, sizeof error) == 1);

        ConnectionOptions options = { .tls = -1, .server_set = 1 };
        strcpy(options.server, "direct.example.invalid");
        changed = initial;
        connection_options_apply(&changed, &options);
        int clear = changed.bouncer_type == CONFIG_BOUNCER_NONE && !changed.bouncer_away &&
            !*changed.bouncer_user && !*changed.bouncer_network && !*changed.bouncer_client &&
            !strcmp(changed.auth, "none") && !*changed.auth_user && !*changed.password &&
            !*changed.server_password && !*changed.password_file && !*changed.server_password_file &&
            !*changed.password_gpg_file && !*changed.server_password_gpg_file &&
            !changed.autocommands.count && !*changed.autocommands.lines[0] &&
            !*changed.channels && !*changed.ca_file &&
            !strcmp(changed.server, options.server) && !strcmp(changed.nick, initial.nick) &&
            changed.tls == initial.tls && changed.port == initial.port &&
            config_validate(&changed, error, sizeof error) == 1;
        if (!clear) { fprintf(stderr, "bouncer type %d retained stale different-host state\n", type); ++failures; }

        changed = initial;
        options.ca_set = options.channels_set = options.nick_set = 1;
        strcpy(options.ca_file, "/new-explicit-CA");
        strcpy(options.channels, "#new-explicit-channel");
        strcpy(options.nick, "NewNick");
        options.tls = 0;
        connection_options_apply(&changed, &options);
        int explicit_valid = changed.bouncer_type == CONFIG_BOUNCER_NONE &&
            !strcmp(changed.ca_file, options.ca_file) && !strcmp(changed.channels, options.channels) &&
            !strcmp(changed.nick, options.nick) && !changed.tls && changed.port == 6667 &&
            config_validate(&changed, error, sizeof error) == 1;
        if (!explicit_valid) { fprintf(stderr, "bouncer type %d blocked explicit direct-host options\n", type); ++failures; }

        options = (ConnectionOptions){ .tls = -1, .server_set = 1 };
        strcpy(options.server, "BNC.EXAMPLE.INVALID");
        changed = expected = initial;
        strcpy(expected.server, options.server);
        connection_options_apply(&changed, &options);
        assert(memcmp(&changed, &expected, sizeof changed) == 0);

        options = (ConnectionOptions){ .tls = -1, .nick_set = 1 };
        strcpy(options.nick, "NewNick");
        changed = expected = initial;
        strcpy(expected.nick, options.nick);
        connection_options_apply(&changed, &options);
        assert(memcmp(&changed, &expected, sizeof changed) == 0);

        options = (ConnectionOptions){ .tls = -1, .server_set = 1 };
        strcpy(options.server, initial.server);
        changed = initial;
        changed.server[0] = 0;
        connection_options_apply(&changed, &options);
        assert(memcmp(&changed, &initial, sizeof changed) == 0);
    }
    assert(failures == 0);
    puts("bouncer CLI destination: 10 cases passed; different hosts reset identity, same host and first setup preserve it");
}

int main(void)
{
    char directory[] = "/tmp/session-cli-XXXXXX";
    assert(mkdtemp(directory));
    assert(chmod(directory, 0700) == 0);
    int length = snprintf(config_path, sizeof config_path, "%s/config.ini", directory);
    assert(length > 0 && (size_t)length < sizeof config_path);
    launch_cases();
    management_cases();
    bouncer_destination_cases();
    assert(rmdir(directory) == 0);
    puts("session CLI: saved startup mode, nonpersistent overrides, nickname compatibility, terminal-free management, detach/close status, teardown ordering and option isolation passed");
    return 0;
}
