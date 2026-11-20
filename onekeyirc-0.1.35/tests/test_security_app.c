#define main legacy_app_test_main
#include "test_app.c"
#undef main

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

/* Reuse the deterministic transport fixture, exercising actual application
 * entry points. Each case can also be compiled against the previous release. */
#ifndef APP_PERSISTENT_LOG_MAX
#define APP_PERSISTENT_LOG_MAX (16u * 1024u * 1024u)
#endif

static App *logging_app(const char *config_path)
{
    Config c;
    memset(&c, 0, sizeof c);
    strcpy(c.server, "fixture.invalid");
    strcpy(c.nick, "tester");
    strcpy(c.username, "tester");
    strcpy(c.auth, "none");
    c.port = 6667;
    c.history_lines = 50;
    c.persistent_logging = 1;
    App *a = calloc(1, sizeof *a);
    assert(a && app_init(a, &c, config_path));
    return a;
}

static void logging_fifo(const char *config_path, const char *log_path)
{
    assert(mkfifo(log_path, 0600) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        signal(SIGALRM, SIG_DFL);
        alarm(2);
        App *a = logging_app(config_path);
        app_log(a, 0, "FIFO_MUST_NOT_BLOCK");
        assert(a->persistent_log_failed);
        destroy(a);
        _exit(0);
    }
    int status;
    while (waitpid(child, &status, 0) < 0) assert(errno == EINTR);
    assert(unlink(log_path) == 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void logging_bound(const char *config_path, const char *log_path)
{
    int fd = open(log_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0);
    assert(ftruncate(fd, (off_t)APP_PERSISTENT_LOG_MAX - 8) == 0);
    assert(close(fd) == 0);
    App *a = logging_app(config_path);
    app_log(a, 0, "REMOTE_TRAFFIC_MUST_NOT_GROW_A_FULL_LOG");
    struct stat st;
    assert(stat(log_path, &st) == 0);
    assert(st.st_size == (off_t)APP_PERSISTENT_LOG_MAX - 8);
    assert(a->persistent_log_failed);
    assert(strstr(app_status_text(a), "limit"));
    /* Failure of optional disk logging must leave live chat functional. */
    assert(a->buffers[0].line_count > 0);
    destroy(a);
    assert(unlink(log_path) == 0);
}

static void logging_services(const char *config_path, const char *log_path)
{
    App *a = fresh(0);
    register_plain(a);
    output_clear(a);
    strcpy(a->config_path, config_path);
    a->cfg.persistent_logging = 1;
    feed(a, ":NickServ!service@services.invalid NOTICE tester :reset-token-should-not-hit-disk\r\n");
    feed(a, ":NickServ!service@services.invalid PRIVMSG tester :\001TOKEN ctcp-token-should-not-hit-disk\001\r\n");
    feed(a, ":Bob!u@h PRIVMSG tester :ordinary-incoming-chat\r\n");
    FILE *f = fopen(log_path, "rb");
    assert(f);
    char data[8192];
    size_t n = fread(data, 1, sizeof data - 1, f);
    data[n] = 0;
    assert(fclose(f) == 0);
    assert(!strstr(data, "should-not-hit-disk"));
    assert(strstr(data, "ordinary-incoming-chat"));
    /* Sensitive service messages remain available to the live interface. */
    size_t view = app_buffer(a, "NickServ", B_QUERY, 0);
    assert(view != SIZE_MAX && a->buffers[view].line_count);
    destroy(a);
    assert(unlink(log_path) == 0);
}

static void logging_preferences(const char *config_path, const char *log_path)
{
    App *a = logging_app(config_path);
    app_log(a, 0, "BEFORE_DISABLE");
    struct stat before, after;
    assert(stat(log_path, &before) == 0);
    Config next;
    char error[512];
    app_config_effective(a, &next);
    next.persistent_logging = 0;
    next.program_messages = 0;
    next.single_session = 1;
    assert(app_config_apply(a, &next, 0, error, sizeof error));
    app_log(a, 0, "AFTER_DISABLE_MUST_NOT_BE_SAVED");
    assert(stat(log_path, &after) == 0 && before.st_size == after.st_size);
    assert(!a->cfg.persistent_logging && !a->cfg.program_messages && a->cfg.single_session);
    /* An explicit off/on toggle permits recovery after the user fixes a log. */
    a->persistent_log_failed = 1;
    next.persistent_logging = 1;
    assert(app_config_apply(a, &next, 0, error, sizeof error));
    assert(!a->persistent_log_failed);
    app_log(a, 0, "AFTER_ENABLE");
    assert(stat(log_path, &after) == 0 && after.st_size > before.st_size);
    destroy(a);
    assert(unlink(log_path) == 0);
}

int main(int argc, char **argv)
{
    char directory[] = "/tmp/vipirc-security-app-XXXXXX";
    assert(mkdtemp(directory));
    char config_path[CONFIG_PATH_MAX], log_path[CONFIG_PATH_MAX + 5];
    assert(snprintf(config_path, sizeof config_path, "%s/config.ini", directory) > 0);
    assert(snprintf(log_path, sizeof log_path, "%s.log", config_path) > 0);
    const char *test = argc > 1 ? argv[1] : "all";
    if (!strcmp(test, "all") || !strcmp(test, "fifo")) logging_fifo(config_path, log_path);
    if (!strcmp(test, "all") || !strcmp(test, "bound")) logging_bound(config_path, log_path);
    if (!strcmp(test, "all") || !strcmp(test, "services")) logging_services(config_path, log_path);
    if (!strcmp(test, "all") || !strcmp(test, "preferences")) logging_preferences(config_path, log_path);
    assert(rmdir(directory) == 0);
    puts("security app regressions: passed");
    return 0;
}
