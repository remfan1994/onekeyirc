/* Production helper lifecycle with real private fork/exec/pipes/signals.
 * This executable supplies an adversarial helper, not GnuPG or a keyring.
 * On Linux, test-only subreaping also collects every created descendant. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <time.h>
#include <unistd.h>

static int limited_output = -1, limited_status = -1;
static ssize_t boundary_read(int fd, void *data, size_t count)
{
    if ((fd == limited_output || fd == limited_status) && count > 1) count = 1;
    return read(fd, data, count);
}
#define read boundary_read
#include "credentials.c"
#undef read

static volatile sig_atomic_t child_exited;
static void child_notice(int number) { (void)number; child_exited = 1; }

static int64_t milliseconds(void)
{
    struct timespec now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &now));
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void put_bytes(int fd, const void *data, size_t length)
{
    const char *bytes = data;
    for (size_t offset = 0; offset < length; ) {
        ssize_t n = write(fd, bytes + offset, length - offset);
        if (n < 0 && errno == EINTR) continue;
        assert(n > 0); offset += (size_t)n;
    }
}

static int helper(int encrypt)
{
    char request[128];
    assert(read(0, request, sizeof request) > 0);
    const char *hold = getenv("IRC_TEST_HOLD_PIPE");
    int buffered = !strcmp(hold, "buffered");
    if (!buffered) {
        pid_t child = fork(); assert(child >= 0);
        if (!child) {
            if (!strcmp(hold, "stdout")) close(3);
            if (!strcmp(hold, "status")) close(1);
            close(0); close(2);
            int fd = open(getenv("IRC_TEST_DESCENDANT_PATH"), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
            assert(fd >= 0 && !flock(fd, LOCK_EX));
            char identity[80];
            int count = snprintf(identity, sizeof identity, "%ld %ld\n", (long)getpid(), (long)getpgrp());
            assert(count > 0 && (size_t)count < sizeof identity);
            put_bytes(fd, identity, (size_t)count);
            for (;;) pause();
        }
    }
    if (buffered) {
        char bytes[1000]; memset(bytes, encrypt ? 'E' : 's', sizeof bytes);
        put_bytes(1, bytes, sizeof bytes);
    } else put_bytes(1, "fixture-secret", 14);
    const char *status = encrypt ?
        "[GNUPG:] BEGIN_ENCRYPTION 2 9\n[GNUPG:] END_ENCRYPTION\n" :
        "[GNUPG:] DECRYPTION_INFO 2 9\n[GNUPG:] DECRYPTION_OKAY\n[GNUPG:] END_DECRYPTION\n";
    if (getenv("IRC_TEST_BAD_STATUS")) status = "[GNUPG:] FAILURE decrypt 1\n";
    put_bytes(3, status, strlen(status));
    return 0;
}

static int descriptors(void)
{
    int count = 0;
    for (int fd = 0; fd < 256; ++fd) if (fcntl(fd, F_GETFD) >= 0) ++count;
    return count;
}

static void reap_direct(void)
{
    int64_t end = milliseconds() + 2000;
    do {
        credential_reap();
        if (!credential_cleanup_pending()) return;
        (void)poll(NULL, 0, 2);
    } while (milliseconds() < end);
    assert(!"direct helper was not collected");
}

static const char fingerprint[] = "0123456789abcdef0123456789abcdef01234567";

static int held_pipe_case(int encrypt, const char *hold, int timeout, int bad,
                          const char *source, const char *destination, const char *report)
{
    assert(!setenv("IRC_TEST_HOLD_PIPE", hold, 1));
    if (bad) assert(!setenv("IRC_TEST_BAD_STATUS", "1", 1));
    else assert(!unsetenv("IRC_TEST_BAD_STATUS"));
    child_exited = 0;
    int before = descriptors();
    int64_t began = milliseconds();
    char error[512], out[CONFIG_SECRET_MAX + 1];
    Credential *r = encrypt ? NULL : credential_start(source, began, error, sizeof error);
    CredentialCreate *w = encrypt ? credential_create_start(destination, fingerprint, "fixture-secret", began, error, sizeof error) : NULL;
    assert(r || w);
    size_t slot = encrypt ? w->child : r->child;
    pid_t leader = children[slot].pid;
    int result = 0;
    struct stat st;
    while (milliseconds() - began < 2000) {
        result = encrypt ? credential_create_poll(w, milliseconds(), error, sizeof error) :
                           credential_poll(r, milliseconds(), out, sizeof out, error, sizeof error);
        assert(result <= 0);
        if (child_exited && !stat(report, &st) && st.st_size > 0) break;
        (void)poll(NULL, 0, 2);
    }
    assert(child_exited && !stat(report, &st) && st.st_size > 0);
    result = encrypt ? credential_create_poll(w, milliseconds(), error, sizeof error) :
                       credential_poll(r, milliseconds(), out, sizeof out, error, sizeof error);
    assert(bad ? result == -1 : result == 0);
    int retained = !(encrypt ? w->exited : r->exited) && children[slot].pid == leader;
    FILE *file = fopen(report, "r"); assert(file);
    long descendant, group; assert(fscanf(file, "%ld %ld", &descendant, &group) == 2);
    assert(group == (long)leader);
    assert(flock(fileno(file), LOCK_EX | LOCK_NB) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
    if (timeout) {
        result = encrypt ? credential_create_poll(w, began + CREDENTIAL_TIMEOUT_MS, error, sizeof error) :
                           credential_poll(r, began + CREDENTIAL_TIMEOUT_MS, out, sizeof out, error, sizeof error);
        assert(result == -1 && strstr(error, "timed out"));
    }
    if (encrypt) credential_create_cancel(w); else credential_cancel(r);
    int stopped = 0;
    int64_t end = milliseconds() + 1000;
    do {
        if (!flock(fileno(file), LOCK_EX | LOCK_NB)) { stopped = 1; break; }
        assert(errno == EAGAIN || errno == EWOULDBLOCK);
        (void)poll(NULL, 0, 2);
    } while (milliseconds() < end);
    /* Clean up even on the vulnerable baseline, before reporting failure. */
    if (!stopped) assert(!kill((pid_t)descendant, SIGKILL));
#ifdef __linux__
    int status;
    assert(waitpid((pid_t)descendant, &status, 0) == (pid_t)descendant && WIFSIGNALED(status));
#endif
    reap_direct();
    assert(!fclose(file) && !unlink(report));
    assert(access(destination, F_OK) < 0 && descriptors() == before);
    printf("%s hold=%s action=%s leader_retained=%d descendant_stopped=%d\n",
           encrypt ? "encrypt" : "decrypt", hold, bad ? "failed-status cancel" : timeout ? "timeout cancel" : "cancel", retained, stopped);
    return retained && stopped;
}

static void buffered_case(int encrypt, const char *source, const char *destination)
{
    assert(!setenv("IRC_TEST_HOLD_PIPE", "buffered", 1) && !unsetenv("IRC_TEST_BAD_STATUS"));
    child_exited = 0;
    char error[512], out[CONFIG_SECRET_MAX + 1];
    int before = descriptors();
    int64_t began = milliseconds();
    Credential *r = encrypt ? NULL : credential_start(source, began, error, sizeof error);
    CredentialCreate *w = encrypt ? credential_create_start(destination, fingerprint, "fixture-secret", began, error, sizeof error) : NULL;
    assert(r || w);
    size_t slot = encrypt ? w->child : r->child;
    pid_t leader = children[slot].pid;
    limited_output = encrypt ? w->output : r->output;
    limited_status = encrypt ? w->status : r->status;
    if (encrypt) assert(!credential_create_poll(w, milliseconds(), error, sizeof error));
    while (!child_exited && milliseconds() - began < 2000) (void)poll(NULL, 0, 2);
    assert(child_exited);
    int result = 0;
    unsigned pending = 0;
    do {
        result = encrypt ? credential_create_poll(w, milliseconds(), error, sizeof error) :
                           credential_poll(r, milliseconds(), out, sizeof out, error, sizeof error);
        assert(result >= 0);
        if (!result) {
            assert(!(encrypt ? w->exited : r->exited) && children[slot].pid == leader);
            ++pending;
        }
    } while (!result && milliseconds() - began < 2000);
    assert(result == 1 && pending > 100 && !children[slot].pid);
    if (encrypt) {
        assert(w->exited && w->used == 1000);
        for (size_t i = 0; i < w->used; ++i) assert(w->ciphertext[i] == 'E');
        credential_create_cancel(w);
    } else {
        assert(r->exited && strlen(out) == 1000);
        for (size_t i = 0; out[i]; ++i) assert(out[i] == 's');
        config_wipe(out, sizeof out); credential_cancel(r);
    }
    limited_output = limited_status = -1;
    reap_direct();
    assert(descriptors() == before && access(destination, F_OK) < 0);
    printf("%s buffered output: child exits before final reads; %u pending polls; all 1000 bytes retained; normal EOF releases slot\n",
           encrypt ? "encrypt" : "decrypt", pending);
}

int main(int argc, char **argv)
{
    if (!strcmp(argv[0], "gpg")) return helper(argc > 1 && !strcmp(argv[argc - 1], "--encrypt"));
#ifdef __linux__
    assert(!prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0));
#endif
    struct sigaction action;
    memset(&action, 0, sizeof action); sigemptyset(&action.sa_mask);
    action.sa_handler = child_notice; action.sa_flags = SA_NOCLDSTOP;
    assert(!sigaction(SIGCHLD, &action, NULL));
    char directory[] = "/tmp/irc-credential-descendants-XXXXXX", executable[4096];
    char program[4096], source[4096], destination[4096], report[4096];
    assert(realpath(argv[0], executable) && mkdtemp(directory));
    assert(snprintf(program, sizeof program, "%s/gpg", directory) > 0);
    assert(snprintf(source, sizeof source, "%s/input.gpg", directory) > 0);
    assert(snprintf(destination, sizeof destination, "%s/output.gpg", directory) > 0);
    assert(snprintf(report, sizeof report, "%s/descendant.pid", directory) > 0);
    assert(!symlink(executable, program) && !setenv("PATH", directory, 1));
    assert(!setenv("IRC_TEST_DESCENDANT_PATH", report, 1));
    int input = open(source, O_WRONLY | O_CREAT | O_EXCL, 0600); assert(input >= 0);
    put_bytes(input, "fixture", 7); assert(!close(input));
    int okay = 1;
    static const char *const held[] = {"stdout", "status", "both"};
    for (int encrypt = 0; encrypt < 2 && okay; ++encrypt) {
        for (size_t i = 0; i < sizeof held / sizeof held[0] && okay; ++i)
            for (int timeout = 0; timeout < 2 && okay; ++timeout)
                okay = held_pipe_case(encrypt, held[i], timeout, 0, source, destination, report);
        if (okay) okay = held_pipe_case(encrypt, "both", 0, 1, source, destination, report);
        if (okay) buffered_case(encrypt, source, destination);
    }
    assert(!unlink(source) && !unlink(program) && !rmdir(directory));
    if (!okay) fputs("FAIL: direct helper ownership was released while an inherited pipe writer remained alive\n", stderr);
    return okay ? 0 : 1;
}
