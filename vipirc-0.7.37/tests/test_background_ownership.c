/* First-detach child ownership under real SIGCHLD policies, forks, pipes,
 * and waitpid. setsid is fault-injected so only this test's child exits during
 * startup. The poll wrapper lets that child finish automatic reaping before
 * the parent handles its failure; kill records and suppresses stale-PID
 * attempts. No unrelated process is signaled and PID reuse is not forced.
 * A dummy prepared listener avoids any pathname AF_UNIX dependency. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static pid_t counted_fork(void);
static int counted_pipe(int [2]);
static pid_t failed_setsid(void);
static int observing_poll(struct pollfd *, nfds_t, int);
static int observing_kill(pid_t, int);
#define fork counted_fork
#define pipe counted_pipe
#define setsid failed_setsid
#define poll observing_poll
#define kill observing_kill
#include "background.c"
#undef fork
#undef pipe
#undef setsid
#undef poll
#undef kill

static unsigned fork_calls, pipe_calls, kill_calls, stale_signal_calls;
static pid_t helper_pid;
static int automatic_reaping;
static volatile sig_atomic_t notifications;

int net_detach_ready(Net *n) { (void)n; return 1; }
void credential_reap(void) { }
int credential_cleanup_pending(void) { return 0; }
int64_t net_now_ms(void)
{
    struct timespec now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &now));
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static pid_t counted_fork(void)
{
    ++fork_calls;
    pid_t child = fork();
    if (child > 0) helper_pid = child;
    return child;
}
static int counted_pipe(int fds[2]) { ++pipe_calls; return pipe(fds); }
static pid_t failed_setsid(void) { errno = EPERM; return -1; }
static int observing_poll(struct pollfd *fds, nfds_t count, int timeout)
{
    int result = poll(fds, count, timeout);
    if (result > 0 && helper_pid > 0 && automatic_reaping) {
        int gone = 0;
        int64_t deadline = net_now_ms() + 2000;
        while (net_now_ms() < deadline) {
            if (kill(helper_pid, 0) < 0 && errno == ESRCH) { gone = 1; break; }
            struct timespec pause_time = {0, 1000000};
            (void)nanosleep(&pause_time, NULL);
        }
        assert(gone);
    }
    return result;
}
static int observing_kill(pid_t child, int signal_number)
{
    assert(child == helper_pid && signal_number == SIGKILL);
    ++kill_calls;
    if (automatic_reaping) {
        if (kill(child, 0) < 0 && errno == ESRCH) ++stale_signal_calls;
        /* Always suppress this attempted signal, even if the old PID has
         * since been reused: only a waitable child can pin its ownership. */
        errno = ESRCH;
        return -1;
    }
    return kill(child, signal_number);
}
static void notification(int signal_number) { (void)signal_number; ++notifications; }
static unsigned descriptors(void)
{
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) directory = opendir("/dev/fd");
    assert(directory);
    unsigned count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)))
        if (entry->d_name[0] != '.') ++count;
    assert(!closedir(directory));
    return count;
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
static void policy(void (*handler)(int), int flags)
{
    struct sigaction action;
    memset(&action, 0, sizeof action);
    assert(!sigemptyset(&action.sa_mask));
    action.sa_handler = handler;
    action.sa_flags = flags;
    assert(!sigaction(SIGCHLD, &action, NULL));
}
static void unchanged_policy(void (*handler)(int), int flags)
{
    struct sigaction action;
    assert(!sigaction(SIGCHLD, NULL, &action));
    assert(action.sa_handler == handler);
#ifdef SA_NOCLDWAIT
    assert((action.sa_flags & SA_NOCLDWAIT) == (flags & SA_NOCLDWAIT));
#else
    (void)flags;
#endif
}
static int check_policy(const char *name, void (*handler)(int), int flags, int allowed)
{
    policy(handler, flags);
    automatic_reaping = !allowed;
    fork_calls = pipe_calls = kill_calls = stale_signal_calls = 0;
    helper_pid = 0;
    unsigned before = descriptors();
    Background *b = prepared(0);
    Client *client = calloc(1, sizeof *client);
    assert(client);
    char prepare_error[512] = "", detach_error[512] = "";
    int ready = background_prepare(b, client, prepare_error, sizeof prepare_error);
    /* Exercise detach independently, including callers with old preparation. */
    int detached = background_detach(b, NULL, detach_error, sizeof detach_error);
    int passed = !detached && ready == allowed;
    if (allowed) {
        passed &= fork_calls == 1 && pipe_calls == 1 && kill_calls == 1 && !stale_signal_calls;
        assert(helper_pid > 0 && waitpid(helper_pid, NULL, WNOHANG) < 0 && errno == ECHILD);
    } else {
        passed &= !fork_calls && !pipe_calls && !kill_calls &&
                  strstr(prepare_error, "SIGCHLD") && strstr(detach_error, "SIGCHLD");
    }
    unchanged_policy(handler, flags);
    background_free(b);
    free(client);
    assert(descriptors() == before);
    printf("{\"case\":\"%s\",\"prepare_ready\":%d,\"fork_calls\":%u,\"pipe_calls\":%u,"
           "\"kill_calls\":%u,\"stale_pid_signal_calls\":%u,\"policy_unchanged\":true,"
           "\"descriptors_restored\":true,\"passed\":%s}\n",
           name, ready, fork_calls, pipe_calls, kill_calls, stale_signal_calls,
           passed ? "true" : "false");
    return passed;
}
static int changed_after_prepare(void)
{
    policy(SIG_DFL, 0);
    Background *b = prepared(0);
    Client *client = calloc(1, sizeof *client);
    assert(client);
    char error[512] = "";
    assert(background_prepare(b, client, error, sizeof error));
    policy(SIG_IGN, 0);
    automatic_reaping = 1;
    fork_calls = pipe_calls = kill_calls = stale_signal_calls = 0;
    helper_pid = 0;
    int result = background_detach(b, NULL, error, sizeof error);
    int passed = !result && !fork_calls && !pipe_calls && !kill_calls && strstr(error, "SIGCHLD");
    unchanged_policy(SIG_IGN, 0);
    background_free(b); free(client);
    printf("{\"case\":\"policy changed after preparation\",\"fork_calls\":%u,"
           "\"stale_pid_signal_calls\":%u,\"passed\":%s}\n",
           fork_calls, stale_signal_calls, passed ? "true" : "false");
    return passed;
}
static int managed_redetach(void)
{
    policy(SIG_IGN, 0);
    automatic_reaping = 1;
    fork_calls = pipe_calls = kill_calls = stale_signal_calls = 0;
    helper_pid = 0;
    unsigned before = descriptors();
    int saved[3];
    for (int i = 0; i < 3; ++i) { saved[i] = dup(i); assert(saved[i] >= 3); }
    Background *b = prepared(1);
    Client *client = calloc(1, sizeof *client);
    assert(client);
    char error[512] = "";
    assert(!fflush(stdout) && !fflush(stderr));
    int ready = background_prepare(b, client, error, sizeof error);
    int detached = background_detach(b, NULL, error, sizeof error);
    int passed = ready && detached && background_detached(b) && !fork_calls && !pipe_calls && !kill_calls;
    for (int i = 0; i < 3; ++i) { assert(dup2(saved[i], i) == i); close(saved[i]); }
    clearerr(stdin); clearerr(stdout); clearerr(stderr);
    unchanged_policy(SIG_IGN, 0);
    background_free(b); free(client);
    assert(descriptors() == before);
    printf("{\"case\":\"managed worker re-detaches with ignored SIGCHLD\","
           "\"fork_calls\":%u,\"passed\":%s}\n", fork_calls, passed ? "true" : "false");
    return passed;
}
int main(void)
{
    struct sigaction original;
    assert(!sigaction(SIGCHLD, NULL, &original));
    signal(SIGPIPE, SIG_IGN); alarm(20);
    int okay = 1;
    if (!check_policy("SIGCHLD ignored", SIG_IGN, 0, 0)) okay = 0;
#ifdef SA_NOCLDWAIT
    if (!check_policy("SA_NOCLDWAIT", SIG_DFL, SA_NOCLDWAIT, 0)) okay = 0;
    if (!check_policy("handler plus SA_NOCLDWAIT", notification, SA_NOCLDWAIT, 0)) okay = 0;
#endif
    if (!check_policy("default waitable children", SIG_DFL, 0, 1)) okay = 0;
    if (!check_policy("non-reaping handler", notification, 0, 1)) okay = 0;
    if (!changed_after_prepare()) okay = 0;
    if (!managed_redetach()) okay = 0;
    assert(!sigaction(SIGCHLD, &original, NULL));
    return okay ? 0 : 1;
}
