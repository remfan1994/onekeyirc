/* Resolver child ownership with real fork/exit/wait and signal dispositions.
 * No DNS request is made. The overridden resolver returns locally or waits
 * for the production cancellation signal. Auto-reaping baseline probes allow
 * their own helper to exit, then observe an attempted signal to its stale PID;
 * no unrelated process is targeted and PID reuse is not forced. */
#define _POSIX_C_SOURCE 200809L
#include "net.h"
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static unsigned fork_calls, pipe_calls, stale_signal_calls;
static pid_t helper_pid;
static int blocked_lookup;
static volatile sig_atomic_t notifications;

pid_t resolver_ownership_fork(void)
{
    ++fork_calls;
    pid_t pid = fork();
    if (pid > 0) helper_pid = pid;
    return pid;
}
int resolver_ownership_pipe(int fds[2])
{
    ++pipe_calls;
    return pipe(fds);
}
int resolver_ownership_kill(pid_t pid, int signal_number)
{
    if (pid == helper_pid && signal_number == SIGKILL && kill(pid, 0) < 0 && errno == ESRCH)
        ++stale_signal_calls;
    return kill(pid, signal_number);
}
int resolver_ownership_getaddrinfo(const char *name, const char *service,
                                  const struct addrinfo *hints, struct addrinfo **out)
{
    (void)name; (void)service; (void)hints; (void)out;
    while (blocked_lookup) pause();
    return EAI_NONAME;
}
static void notification(int signo) { (void)signo; ++notifications; }
static void require(int condition, const char *label)
{
    if (!condition) { fprintf(stderr, "%s (errno=%d)\n", label, errno); exit(2); }
}
static void restore(const struct sigaction *saved)
{
    struct sigaction actual;
    require(sigaction(SIGCHLD, saved, NULL) == 0, "restore SIGCHLD disposition");
    require(sigaction(SIGCHLD, NULL, &actual) == 0, "inspect restored SIGCHLD");
    require(actual.sa_handler == saved->sa_handler, "restored SIGCHLD handler");
#ifdef SA_NOCLDWAIT
    require((actual.sa_flags & SA_NOCLDWAIT) == (saved->sa_flags & SA_NOCLDWAIT), "restored SIGCHLD auto-reaping flag");
#endif
    for (int signal_number = 1; signal_number <= SIGTERM; ++signal_number)
        require(sigismember(&actual.sa_mask, signal_number) == sigismember(&saved->sa_mask, signal_number),
                "restored handler mask");
}
static int check(const char *name, void (*handler)(int), int flags, int rejected)
{
    struct sigaction previous, desired;
    char error[512] = "";
    require(sigaction(SIGCHLD, NULL, &previous) == 0, "save SIGCHLD");
    memset(&desired, 0, sizeof desired);
    desired.sa_handler = handler; desired.sa_flags = flags;
    require(sigemptyset(&desired.sa_mask) == 0, "empty handler mask");
    require(sigaction(SIGCHLD, &desired, NULL) == 0, "install test SIGCHLD");
    fork_calls = pipe_calls = stale_signal_calls = 0; helper_pid = 0;
    blocked_lookup = !rejected;
    Net *net = net_open("resolver-ownership.invalid", 6667, 0, NULL, error, sizeof error);
    int correct;
    if (rejected) {
        correct = !net && !fork_calls && !pipe_calls && strstr(error, "SIGCHLD") != NULL;
        if (net) {
            /* Wait only for our auto-reaped helper. No waitable zombie is
             * available to pin its identity in these baseline cases. */
            int gone = 0;
            int64_t deadline = net_now_ms() + 2000;
            while (net_now_ms() < deadline) {
                if (kill(helper_pid, 0) < 0 && errno == ESRCH) { gone = 1; break; }
                struct timespec pause_time = {0, 1000000};
                (void)nanosleep(&pause_time, NULL);
            }
            require(gone, "auto-reaped fixture child exited");
            net_close(net);
        }
    } else {
        correct = net && fork_calls == 1 && pipe_calls == 1;
        require(net != NULL, "waitable resolver starts");
        require(!net_detach_ready(net), "live waitable resolver prevents detach");
        net_close(net);
        errno = 0;
        require(waitpid(helper_pid, NULL, WNOHANG) < 0 && errno == ECHILD, "cancelled resolver was collected");
        require(kill(helper_pid, 0) < 0 && errno == ESRCH, "cancelled resolver no longer exists");
    }
    restore(&previous);
    printf("{\"case\":\"%s\",\"rejected_before_fork\":%s,\"fork_calls\":%u,\"pipe_calls\":%u,\"stale_pid_signal_calls\":%u,\"policy_restored\":true,\"passed\":%s}\n",
           name, rejected && !net ? "true" : "false", fork_calls, pipe_calls,
           stale_signal_calls, correct ? "true" : "false");
    return correct;
}
int main(void)
{
    int okay = 1;
    signal(SIGPIPE, SIG_IGN); alarm(10);
    if (!check("SIGCHLD ignored", SIG_IGN, 0, 1)) okay = 0;
#ifdef SA_NOCLDWAIT
    if (!check("SA_NOCLDWAIT", SIG_DFL, SA_NOCLDWAIT, 1)) okay = 0;
    if (!check("handler plus SA_NOCLDWAIT", notification, SA_NOCLDWAIT, 1)) okay = 0;
#endif
    if (!check("default waitable children", SIG_DFL, 0, 0)) okay = 0;
    if (!check("non-reaping handler", notification, 0, 0)) okay = 0;
    return okay ? 0 : 1;
}
