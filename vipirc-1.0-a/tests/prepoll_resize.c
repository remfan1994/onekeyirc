/* Linux-only test interposer; not linked into the application. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <unistd.h>

static int fired;
static unsigned long calls_after_marker;
static struct rusage usage_before;

int poll(struct pollfd *fds, nfds_t count, int timeout)
{
    static int (*real_poll)(struct pollfd *, nfds_t, int);
    if (!real_poll) {
        void *symbol = dlsym(RTLD_NEXT, "poll");
        _Static_assert(sizeof real_poll == sizeof symbol, "POSIX dlsym pointer size");
        memcpy(&real_poll, &symbol, sizeof real_poll);
        if (!real_poll) _exit(90);
    }
    const char *marker = getenv("VIPIRC_PREPOLL_MARKER");
    const char *log_path = getenv("VIPIRC_PREPOLL_LOG");
    if (fired) ++calls_after_marker;
    if (!fired && marker && log_path && count == 1 && fds[0].fd == STDIN_FILENO && timeout >= 500) {
        int marker_fd = open(marker, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (marker_fd >= 0) {
            close(marker_fd); unlink(marker); fired = 1; calls_after_marker = 1;
            int log_fd = open(log_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
            if (log_fd < 0 || getrusage(RUSAGE_SELF, &usage_before) < 0) _exit(91);
            sigset_t blocked, previous, pending;
            sigemptyset(&blocked); sigaddset(&blocked, SIGWINCH);
            if (sigprocmask(SIG_BLOCK, &blocked, &previous) < 0) _exit(92);
            struct winsize size = {15, 50, 0, 0};
            int changed = ioctl(STDIN_FILENO, TIOCSWINSZ, &size);
            sigpending(&pending);
            int queued = sigismember(&pending, SIGWINCH);
            if (sigprocmask(SIG_SETMASK, &previous, NULL) < 0) _exit(93);
            sigpending(&pending);
            int remaining = sigismember(&pending, SIGWINCH);
            dprintf(log_fd, "ioctl=%d pending_while_blocked=%d pending_after_unblock=%d timeout=%d rows=15 cols=50\n",
                    changed, queued, remaining, timeout);
            int result = real_poll(fds, count, timeout);
            int saved = errno;
            dprintf(log_fd, "real_poll_result=%d stdin_revents=%d\n", result, fds[0].revents);
            close(log_fd); errno = saved;
            return result;
        }
    }
    return real_poll(fds, count, timeout);
}

/* glibc fortifies variable-length calls even when fixed-size calls use poll. */
int __poll_chk(struct pollfd *fds, nfds_t count, int timeout, size_t bytes)
{
    if (count > bytes / sizeof(*fds)) _exit(94);
    return poll(fds, count, timeout);
}

__attribute__((destructor)) static void record_idle_work(void)
{
    if (!fired) return;
    const char *path = getenv("VIPIRC_PREPOLL_LOG");
    if (!path) return;
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) < 0) return;
    double cpu = (double)(usage.ru_utime.tv_sec - usage_before.ru_utime.tv_sec) +
                 (double)(usage.ru_stime.tv_sec - usage_before.ru_stime.tv_sec) +
                 (double)(usage.ru_utime.tv_usec - usage_before.ru_utime.tv_usec) / 1000000.0 +
                 (double)(usage.ru_stime.tv_usec - usage_before.ru_stime.tv_usec) / 1000000.0;
    int fd = open(path, O_WRONLY | O_APPEND | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return;
    dprintf(fd, "poll_calls_after_marker=%lu cpu_seconds=%.6f\n", calls_after_marker, cpu);
    close(fd);
}
