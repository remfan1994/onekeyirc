/* Exercise terminal release with real pipes, forks and a stopped PTY.
 * A dummy prepared listener avoids creating any session or IRC socket.
 * These are output-boundary tests, not an AF_UNIX lifecycle test. */
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
/* Include the generated localization lookup for this single-file IPC fixture.
 * The production executable instead defines it in idle_setup.c. */
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#undef IRC_L10N_IMPLEMENTATION
#include "../src/background.c"
#include <assert.h>
#include <time.h>

int net_detach_ready(Net *n) { (void)n; return 1; }
void credential_reap(void) { }
int credential_cleanup_pending(void) { return 0; }
int64_t net_now_ms(void)
{
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int open_terminal(int *master, int stopped, int nonblocking)
{
    /* OpenBSD accepts only the POSIX access flags at allocation time. */
    *master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(*master >= 0 && fd_flags(*master, 1) &&
           grantpt(*master) == 0 && unlockpt(*master) == 0);
    const char *name = ptsname(*master);
    assert(name);
    int slave = open(name, O_RDWR | O_NOCTTY | O_CLOEXEC);
    assert(slave >= 0);
    if (nonblocking) {
        int flags = fcntl(slave, F_GETFL);
        assert(flags >= 0 && fcntl(slave, F_SETFL, flags | O_NONBLOCK) == 0);
    }
    if (stopped) assert(tcflow(slave, TCOOFF) == 0);
    return slave;
}

static Background *prepared(int managed)
{
    Background *b = background_create();
    assert(b);
    b->listener = open("/dev/null", O_RDWR | O_CLOEXEC);
    assert(b->listener >= 0);
    b->managed = managed;
    b->job = 7;
    return b;
}

static void child_succeeded(pid_t child, const char *name)
{
    int status;
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    assert(result == child);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        fprintf(stderr, "%s did not complete successfully (wait status %d)\n", name, status);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void managed_buffer_discard(void)
{
    int master, report[2];
    int slave = open_terminal(&master, 1, 0);
    int flags = fcntl(slave, F_GETFL);
    assert(flags >= 0 && pipe(report) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(master); close(report[0]);
        assert(dup2(slave, STDOUT_FILENO) == STDOUT_FILENO);
        char buffer[1024], error[256];
        assert(setvbuf(stdout, buffer, _IOFBF, sizeof buffer) == 0);
        assert(fputs("OLD_PRESENTATION", stdout) >= 0);
        alarm(3);
        Background *b = prepared(1);
        assert(background_detach(b, NULL, error, sizeof error));
        assert(background_detached(b) && !ferror(stdout) && !ferror(stderr));
        /* Simulate a later attachment: old buffered presentation must have
         * been discarded through /dev/null before the descriptor changes. */
        assert(dup2(report[1], STDOUT_FILENO) == STDOUT_FILENO);
        assert(fputs("AFTER_ATTACH", stdout) >= 0 && fflush(stdout) == 0);
        _exit(0);
    }
    close(report[1]);
    child_succeeded(child, "managed detach with buffered output");
    char output[64] = {0};
    ssize_t got = read(report[0], output, sizeof output - 1u);
    assert(got == 12 && !strcmp(output, "AFTER_ATTACH"));
    assert(fcntl(slave, F_GETFL) == flags);
    close(report[0]); close(slave); close(master);
}

static void first_detach_notice(int stopped, int nonblocking)
{
    int master, report[2];
    int slave = open_terminal(&master, stopped, nonblocking);
    int flags = fcntl(slave, F_GETFL);
    assert(flags >= 0 && pipe(report) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(master); close(report[0]);
        assert(dup2(slave, STDOUT_FILENO) == STDOUT_FILENO);
        alarm(3);
        char error[256];
        Background *b = prepared(0);
        int detached = background_detach(b, NULL, error, sizeof error);
        /* Only the continuing worker returns. The original foreground
         * process must exit zero, even if its success notice is dropped. */
        assert(detached && background_detached(b));
        assert((fcntl(STDOUT_FILENO, F_GETFL) & O_NONBLOCK) == 0);
        assert(write(report[1], "W", 1) == 1);
        _exit(0);
    }
    close(report[1]);
    child_succeeded(child, "first-detach foreground notice");
    struct pollfd p = {report[0], POLLIN, 0};
    assert(poll(&p, 1, 1000) > 0);
    char worker;
    assert(read(report[0], &worker, 1) == 1 && worker == 'W');
    /* slave shares the foreground process's open file description. */
    assert(fcntl(slave, F_GETFL) == flags);
    if (!stopped) {
        char output[1024] = {0};
        p = (struct pollfd){master, POLLIN, 0};
        assert(poll(&p, 1, 1000) > 0);
        ssize_t got = read(master, output, sizeof output - 1u);
        assert(got > 0 && strstr(output, "job 7 is running in the background."));
        assert(strstr(output, "--resume 7") && strstr(output, "--sessions"));
    }
    close(report[0]); close(slave); close(master);
}

static void resume_cleanup_output(void)
{
    int master;
    int slave = open_terminal(&master, 1, 0);
    int flags = fcntl(slave, F_GETFL);
    struct termios saved;
    assert(flags >= 0 && tcgetattr(slave, &saved) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(master);
        assert(dup2(slave, STDIN_FILENO) == STDIN_FILENO);
        assert(dup2(slave, STDOUT_FILENO) == STDOUT_FILENO);
        int saved_flags[3] = {flags, flags, fcntl(STDERR_FILENO, F_GETFL)};
        assert(saved_flags[2] >= 0);
        alarm(3);
        restore_foreground(&saved, saved_flags);
        assert(fcntl(slave, F_GETFL) == flags);
        _exit(0);
    }
    child_succeeded(child, "resume foreground cleanup");
    assert(fcntl(slave, F_GETFL) == flags);
    close(slave); close(master);
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    if (argc == 2) {
        if (!strcmp(argv[1], "managed")) managed_buffer_discard();
        else if (!strcmp(argv[1], "parent")) first_detach_notice(1, 0);
        else if (!strcmp(argv[1], "resume")) resume_cleanup_output();
        else return 2;
        return 0;
    }
    assert(argc == 1);
    managed_buffer_discard();
    first_detach_notice(1, 0);
    first_detach_notice(1, 1);
    first_detach_notice(0, 0);
    resume_cleanup_output();
    puts("background terminal output: blocked detach notice/cleanup, parent success, worker readiness, shared flags, and buffered-presentation discard passed");
    return 0;
}
