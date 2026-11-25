/* Credential helper resource-failure regressions. Production credentials.c is included
 * under narrow allocation/syscall fault wrappers; the helper is this test
 * executable, never a real GPG installation or keyring. */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum Fault { NO_FAULT, ALLOCATION, PIPE, FORK, FLAGS, OPEN, OPENAT, FAULT_COUNT };
static enum Fault fault;
static unsigned fault_at, fault_calls, fault_hits;
static pid_t test_owner;
static int inject(enum Fault kind)
{
    if (getpid() != test_owner || fault != kind) return 0;
    ++fault_calls;
    if (!fault_at || fault_calls != fault_at) return 0;
    ++fault_hits;
    errno = kind == ALLOCATION ? ENOMEM : kind == FORK ? EAGAIN : EMFILE;
    return 1;
}
static void *review_calloc(size_t n, size_t size)
{
    return inject(ALLOCATION) ? NULL : calloc(n, size);
}
static int review_pipe(int fds[2]) { return inject(PIPE) ? -1 : pipe(fds); }
static pid_t review_fork(void) { return inject(FORK) ? -1 : fork(); }
static int review_fcntl(int fd, int command, ...)
{
    if (inject(FLAGS)) return -1;
    if (command == F_GETFD || command == F_GETFL) return fcntl(fd, command);
    assert(command == F_SETFD || command == F_SETFL || command == F_DUPFD_CLOEXEC);
    va_list args; va_start(args, command); int value = va_arg(args, int); va_end(args);
    return fcntl(fd, command, value);
}
static int review_open(const char *path, int flags, ...)
{
    if (inject(OPEN)) return -1;
    if (!(flags & O_CREAT)) return open(path, flags);
    va_list args; va_start(args, flags); int mode = va_arg(args, int); va_end(args);
    return open(path, flags, mode);
}
static int review_openat(int directory, const char *path, int flags, ...)
{
    if (inject(OPENAT)) return -1;
    if (!(flags & O_CREAT)) return openat(directory, path, flags);
    va_list args; va_start(args, flags); int mode = va_arg(args, int); va_end(args);
    return openat(directory, path, flags, mode);
}
#define calloc review_calloc
#define pipe review_pipe
#define fork review_fork
#define fcntl review_fcntl
#define open review_open
#define openat review_openat
#include "credentials.c"
#undef calloc
#undef pipe
#undef fork
#undef fcntl
#undef open
#undef openat
#define main existing_credential_suite
#include "test_credentials.c"
#undef main

static void no_children(void)
{
    reap_all();
    for (size_t i = 0; i < CREDENTIAL_JOBS; ++i) assert(!children[i].pid);
}

static unsigned start_and_cancel(int encryption, enum Fault kind, unsigned at,
                                 const char *source, const char *destination)
{
    char error[512] = {0};
    int before = descriptors();
    fault = kind; fault_at = at; fault_calls = fault_hits = 0;
    void *job = encryption ? (void *)credential_create_start(destination, fingerprint,
                        "okay", milliseconds(), error, sizeof error) :
                        (void *)credential_start(source, milliseconds(), error, sizeof error);
    unsigned calls = fault_calls, hits = fault_hits;
    fault = NO_FAULT;
    if (at) assert(hits == 1 && !job && *error);
    else assert(job);
    if (encryption) credential_create_cancel(job); else credential_cancel(job);
    no_children();
    assert(descriptors() == before && access(destination, F_OK) < 0);
    return calls;
}

static void retry_after_failures(const char *source, const char *destination)
{
    char error[512], output[CONFIG_SECRET_MAX + 1];
    int before = descriptors();
    Credential *read_job = credential_start(source, milliseconds(), error, sizeof error);
    assert(read_job && finish(read_job, output, sizeof output, error, sizeof error) == 1);
    assert(!strcmp(output, "  secret #;=  "));
    config_wipe(output, sizeof output); credential_cancel(read_job); no_children();
    CredentialCreate *write_job = credential_create_start(destination, fingerprint, "okay",
                                                           milliseconds(), error, sizeof error);
    assert(write_job && finish_creation(write_job, error, sizeof error) == 1);
    assert(credential_create_publish(write_job, error, sizeof error) == 1);
    credential_create_cancel(write_job); no_children();
    assert(descriptors() == before && access(destination, F_OK) < 0);
}

int main(int argc, char **argv)
{
    if (!strcmp(argv[0], "gpg")) return fake_gpg(argc, argv);
    test_owner = getpid();
    struct rlimit no_core = {0, 0}; assert(setrlimit(RLIMIT_CORE, &no_core) == 0);
    char directory[] = "/tmp/irc-resource-review-XXXXXX";
    char executable[CONFIG_PATH_MAX], program[CONFIG_PATH_MAX], source[CONFIG_PATH_MAX];
    char destination[CONFIG_PATH_MAX];
    assert(realpath(argv[0], executable) && mkdtemp(directory));
    assert(snprintf(program, sizeof program, "%s/gpg", directory) > 0);
    assert(snprintf(source, sizeof source, "%s/input.gpg", directory) > 0);
    assert(snprintf(destination, sizeof destination, "%s/output.gpg", directory) > 0);
    assert(symlink(executable, program) == 0 && setenv("PATH", directory, 1) == 0);
    file_write(source, "okay", 4, 0600);
    unsigned failures = 0;
    for (int encryption = 0; encryption < 2; ++encryption) {
        for (enum Fault kind = ALLOCATION; kind < FAULT_COUNT; ++kind) {
            unsigned calls = start_and_cancel(encryption, kind, 0, source, destination);
            for (unsigned at = 1; at <= calls; ++at) {
                (void)start_and_cancel(encryption, kind, at, source, destination);
                ++failures;
            }
            retry_after_failures(source, destination);
        }
    }
    assert(unlink(source) == 0 && unlink(program) == 0 && rmdir(directory) == 0);
    printf("credential resource failures: %u injected allocation, pipe, fork, fcntl and path-open failures; all descriptors/children released and executable-fixture retry succeeded\n", failures);
    return 0;
}
