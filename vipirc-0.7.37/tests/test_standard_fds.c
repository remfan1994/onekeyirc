/* Production main's earliest descriptor boundary. Every standard stream is
 * a private fixture file or /dev/null. Config loading stops at an observer;
 * no config, keyring, IRC connection, terminal, or live session is accessed.
 * Inspection/open/race faults affect only the reservation system calls. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "config.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int startup_fcntl(int, int, ...);
int startup_open(const char *, int, ...);
static int observe_config(Config *, const char *, char *, size_t);
#define main standard_fds_client_main
#define fcntl startup_fcntl
#define open startup_open
#define config_load observe_config
#include "main.c"
#undef main
#undef fcntl
#undef open
#undef config_load

/* These declarations satisfy main's unused frontend paths. This fixture
 * always stops before creating application or frontend state. */
int idle_welcome(const char *program, int forced)
{ (void)program; (void)forced; abort(); }
int idle_run_session(Client *client, const char *program, Background *background,
                     DccManager *dcc, int start, int connect, int no_connect, int *next)
{
    (void)client; (void)program; (void)background; (void)dcc; (void)start;
    (void)connect; (void)no_connect; (void)next; abort();
}
#ifndef ONEKEY_ONLY
const char *const vipirc_manual[] = {"standard-descriptor fixture", NULL};
int ui_welcome(int forced) { (void)forced; abort(); }
int ui_run_session(Client *client, Background *background, DccManager *dcc,
                   int start, int connect, int no_connect, int *next)
{
    (void)client; (void)background; (void)dcc; (void)start;
    (void)connect; (void)no_connect; (void)next; abort();
}
#endif

enum Fault { NO_FAULT, INSPECT_ERROR, OPEN_ERROR, ALLOCATION_RACE };
static enum Fault fault;
static int fault_fd, missing_fd, race_source, raced_fd, new_fd;
static unsigned mask, open_calls, inspection_calls, config_calls;
static struct stat inherited[3], null_device, race_identity;
static int descriptor_flags[3], status_flags[3];
static int verified;

int startup_fcntl(int fd, int operation, ...)
{
    assert(fd >= 0 && fd <= 2 && operation == F_GETFD);
    ++inspection_calls;
    if (fault == INSPECT_ERROR && fd == fault_fd) { errno = EIO; return -1; }
    int result = fcntl(fd, operation);
    if (result < 0 && errno == EBADF) missing_fd = fd;
    return result;
}
int startup_open(const char *path, int flags, ...)
{
    assert(!strcmp(path, "/dev/null") && flags == O_RDWR && missing_fd >= 0);
    ++open_calls;
    if (fault == OPEN_ERROR && missing_fd == fault_fd) { errno = EMFILE; return -1; }
    if (fault == ALLOCATION_RACE && missing_fd == fault_fd) {
        raced_fd = dup2(race_source, missing_fd);
        assert(raced_fd == missing_fd);
    }
    int fd = open(path, flags);
    if (fault == ALLOCATION_RACE && missing_fd == fault_fd) new_fd = fd;
    return fd;
}
static int same_identity(int fd, const struct stat *before)
{
    struct stat after;
    return !fstat(fd, &after) && after.st_dev == before->st_dev &&
           after.st_ino == before->st_ino && after.st_mode == before->st_mode &&
           after.st_rdev == before->st_rdev;
}
static int verify_standard_streams(void)
{
    int okay = 1;
    for (int fd = 0; fd < 3; ++fd) {
        if (mask & (1u << fd)) {
            int flags = fcntl(fd, F_GETFL);
            okay &= same_identity(fd, &null_device) && fcntl(fd, F_GETFD) == 0 &&
                    flags >= 0 && (flags & O_ACCMODE) == O_RDWR;
        } else {
            okay &= same_identity(fd, &inherited[fd]) &&
                    fcntl(fd, F_GETFD) == descriptor_flags[fd] &&
                    fcntl(fd, F_GETFL) == status_flags[fd];
        }
    }
    return okay;
}
static int observe_config(Config *cfg, const char *path, char *error, size_t cap)
{
    (void)cfg;
    assert(!strcmp(path, "/unused-standard-fd-fixture"));
    ++config_calls;
    verified = verify_standard_streams();
    snprintf(error, cap, "intentional stop before configuration access");
    return -1;
}
static unsigned descriptors(void)
{
    unsigned count = 0;
    for (int fd = 0; fd < 1024; ++fd) if (fcntl(fd, F_GETFD) >= 0) ++count;
    return count;
}
static void reset_options(void)
{
#if defined(__GLIBC__)
    optind = 0;
#else
    optind = 1;
#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__APPLE__)
    optreset = 1;
#endif
#endif
}
static int run_case(const char *directory, unsigned closed, enum Fault which,
                    int fail_at, int information)
{
    int saved[3], saved_flags[3];
    unsigned before = descriptors();
    for (int fd = 0; fd < 3; ++fd) {
        saved_flags[fd] = fcntl(fd, F_GETFD);
        saved[fd] = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        assert(saved[fd] >= 3 && saved[fd] < 1024 && saved_flags[fd] >= 0);
    }
    assert(!fflush(stdout) && !fflush(stderr));
    for (int fd = 0; fd < 3; ++fd) {
        char path[512];
        assert(snprintf(path, sizeof path, "%s/standard-%d", directory, fd) > 0);
        int temporary = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        assert(temporary >= 3 && temporary < 1024 && !close(temporary));
        const int access_modes[] = {O_RDONLY, O_WRONLY, O_RDWR};
        temporary = open(path, access_modes[fd] | O_CLOEXEC);
        assert(temporary >= 3 && temporary < 1024 && dup2(temporary, fd) == fd);
        close(temporary);
        assert(!fcntl(fd, F_SETFD, fd ? FD_CLOEXEC : 0));
        int old = fcntl(fd, F_GETFL);
        assert(old >= 0 && !fcntl(fd, F_SETFL, old | O_NONBLOCK | (fd == 1 ? O_APPEND : 0)));
        assert(!fstat(fd, &inherited[fd]));
        descriptor_flags[fd] = fcntl(fd, F_GETFD);
        status_flags[fd] = fcntl(fd, F_GETFL);
    }
    race_source = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 3);
    assert(race_source >= 3 && race_source < 1024 && !fstat(race_source, &race_identity));
    for (int fd = 0; fd < 3; ++fd) if (closed & (1u << fd)) assert(!close(fd));
    clearerr(stdin); clearerr(stdout); clearerr(stderr);
    mask = closed; fault = which; fault_fd = fail_at;
    missing_fd = raced_fd = new_fd = -1;
    open_calls = inspection_calls = config_calls = 0;
    verified = 0;
    reset_options();
    char *check[] = {"standard-fds-client", "--check-config", "--config", "/unused-standard-fd-fixture", NULL};
    char *version[] = {"standard-fds-client", "--version", NULL};
    int result = standard_fds_client_main(information ? 2 : 4, information ? version : check);
    int passed;
    if (which == NO_FAULT) {
        passed = result == (information ? 0 : 2) && config_calls == (information ? 0u : 1u) &&
                 (information ? verify_standard_streams() : verified);
        unsigned expected_opens = ((closed & 1u) != 0) + ((closed & 2u) != 0) + ((closed & 4u) != 0);
        passed &= open_calls == expected_opens;
    } else {
        passed = result == 1 && !config_calls;
        if (which == INSPECT_ERROR) {
            passed &= !open_calls;
            for (int fd = 0; fd < 3; ++fd)
                passed &= same_identity(fd, &inherited[fd]) &&
                          fcntl(fd, F_GETFD) == descriptor_flags[fd] &&
                          fcntl(fd, F_GETFL) == status_flags[fd];
        } else {
            for (int fd = 0; fd < fail_at; ++fd)
                passed &= same_identity(fd, &null_device) && fcntl(fd, F_GETFD) == 0;
            for (int fd = fail_at + 1; fd < 3; ++fd)
                passed &= fcntl(fd, F_GETFD) < 0 && errno == EBADF;
            if (which == OPEN_ERROR)
                passed &= fcntl(fail_at, F_GETFD) < 0 && errno == EBADF;
            else
                passed &= raced_fd == fail_at && same_identity(raced_fd, &race_identity) &&
                          new_fd >= 0 && new_fd != fail_at &&
                          fcntl(new_fd, F_GETFD) < 0 && errno == EBADF;
        }
    }
    /* Discard any informational stdio buffer through its fixture stream
     * before restoring the test runner's original descriptors. */
    (void)fflush(stdout); (void)fflush(stderr);
    close(race_source);
    for (int fd = 0; fd < 3; ++fd) {
        assert(dup2(saved[fd], fd) == fd && !fcntl(fd, F_SETFD, saved_flags[fd]));
        close(saved[fd]);
    }
    clearerr(stdin); clearerr(stdout); clearerr(stderr);
    assert(descriptors() == before);
    printf("{\"closed_std_mask\":%u,\"fault\":%d,\"fault_fd\":%d,\"information\":%s,"
           "\"result\":%d,\"inspection_calls\":%u,\"open_calls\":%u,\"config_calls\":%u,"
           "\"descriptors_restored\":true,\"passed\":%s}\n",
           closed, (int)which, fail_at, information ? "true" : "false", result,
           inspection_calls, open_calls, config_calls, passed ? "true" : "false");
    return passed;
}
int main(void)
{
    char directory[] = "/tmp/irc-standard-fds-XXXXXX";
    assert(mkdtemp(directory));
    int null = open("/dev/null", O_RDWR | O_CLOEXEC);
    assert(null >= 3 && !fstat(null, &null_device) && !close(null));
    int okay = 1;
    for (unsigned closed = 0; closed < 8; ++closed)
        if (!run_case(directory, closed, NO_FAULT, -1, 0)) okay = 0;
    if (!run_case(directory, 7, NO_FAULT, -1, 1)) okay = 0;
    for (int fd = 0; fd < 3; ++fd) {
        if (!run_case(directory, 0, INSPECT_ERROR, fd, 0)) okay = 0;
        if (!run_case(directory, 7, OPEN_ERROR, fd, 0)) okay = 0;
        if (!run_case(directory, 7, ALLOCATION_RACE, fd, 0)) okay = 0;
    }
    for (int fd = 0; fd < 3; ++fd) {
        char path[512];
        assert(snprintf(path, sizeof path, "%s/standard-%d", directory, fd) > 0);
        assert(!unlink(path));
    }
    assert(!rmdir(directory));
    return okay ? 0 : 1;
}
