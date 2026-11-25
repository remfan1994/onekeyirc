#define _POSIX_C_SOURCE 200809L
#include "credentials.h"
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int64_t milliseconds(void)
{
    struct timespec value;
    assert(!clock_gettime(CLOCK_MONOTONIC, &value));
    return (int64_t)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}

static void put(int fd, const char *data, size_t size)
{
    while (size) {
        ssize_t count = write(fd, data, size);
        if (count < 0 && errno == EINTR) continue;
        assert(count > 0);
        data += count; size -= (size_t)count;
    }
}

static void file_write(const char *path, const char *data, size_t size, mode_t mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0 && !fchmod(fd, mode));
    put(fd, data, size);
    assert(!close(fd));
}

static void reap_all(void)
{
    int64_t end = milliseconds() + 2000;
    do {
        credential_reap();
        if (!credential_cleanup_pending()) return;
        (void)poll(NULL, 0, 5);
    } while (milliseconds() < end);
    assert(!"Credential child was not collected");
}

static int descriptors(void)
{
    int count = 0;
    for (int fd = 0; fd < 256; ++fd) if (fcntl(fd, F_GETFD) >= 0) ++count;
    return count;
}

/* The same executable serves as an adversarial gpg fixture. It is invoked
 * directly through PATH, with production fd/signal isolation and fixed argv. */
static int fake_encrypt(int argc, char **argv)
{
    static const char *const expected[] = {
        "gpg", "--no-options", "--batch", "--no-tty", "--pinentry-mode", "error", "--no-autostart",
        "--no-auto-key-retrieve", "--no-auto-key-import", "--no-auto-key-locate", "--disable-dirmngr",
        "--no-auto-check-trustdb", "--trust-model", "always", "--compress-algo", "none", "--cipher-algo", "AES256",
        "--status-fd", "3", "--recipient", NULL, "--output", "-", "--encrypt"
    };
    assert(argc == (int)(sizeof expected / sizeof expected[0]));
    for (int i = 0; i < argc; ++i) {
        if (expected[i]) assert(!strcmp(argv[i], expected[i]));
        else assert(credential_fingerprint_valid(argv[i]));
    }
    for (int fd = 4; fd < 1024; ++fd) assert(fcntl(fd, F_GETFD) < 0 && errno == EBADF);
    assert(fcntl(65535, F_GETFD) < 0 && errno == EBADF);
    struct sigaction action;
    assert(!sigaction(SIGINT, NULL, &action) && action.sa_handler == SIG_DFL);
    sigset_t mask;
    assert(!sigprocmask(SIG_SETMASK, NULL, &mask) && !sigismember(&mask, SIGUSR1));
    struct rlimit limit; assert(!getrlimit(RLIMIT_CORE, &limit) && !limit.rlim_cur);
    struct stat st;
    assert(!fstat(0, &st) && S_ISFIFO(st.st_mode));
    assert(!fstat(1, &st) && S_ISFIFO(st.st_mode));
    assert(!fstat(2, &st) && S_ISCHR(st.st_mode));
    assert(!fstat(3, &st) && S_ISFIFO(st.st_mode));
    char mode[CONFIG_SECRET_MAX + 1];
    size_t used = 0;
    for (;;) {
        ssize_t count = read(0, mode + used, sizeof mode - used);
        if (count < 0 && errno == EINTR) continue;
        assert(count >= 0);
        if (!count) break;
        used += (size_t)count; assert(used < sizeof mode);
    }
    mode[used] = 0;
    if (!strcmp(mode, "hang")) for (;;) pause();
    if (!strcmp(mode, "recipient")) {
        put(3, "[GNUPG:] INV_RECP 1 private-recipient\n", strlen("[GNUPG:] INV_RECP 1 private-recipient\n"));
        return 2;
    }
    if (!strcmp(mode, "status-overflow"))
        for (int i = 0; i < 1000; ++i) put(3, "[GNUPG:] UNKNOWN safe\n", strlen("[GNUPG:] UNKNOWN safe\n"));
    if (strcmp(mode, "no-status")) {
        const char *begin = !strcmp(mode, "no-integrity") ? "[GNUPG:] BEGIN_ENCRYPTION 0 9\n" :
                            !strcmp(mode, "bad-status") ? "[GNUPG:] BEGIN_ENCRYPTION nope\n" :
                            "[GNUPG:] BEGIN_ENCRYPTION 2 9\n";
        put(3, begin, strlen(begin));
        if (!strcmp(mode, "duplicate")) put(3, begin, strlen(begin));
    }
    if (!strcmp(mode, "maximum") || !strcmp(mode, "overflow")) {
        char bytes[CREDENTIAL_CIPHERTEXT_MAX + 1]; memset(bytes, 'E', sizeof bytes);
        put(1, bytes, !strcmp(mode, "maximum") ? CREDENTIAL_CIPHERTEXT_MAX : sizeof bytes);
    } else if (strcmp(mode, "empty")) put(1, "ENCRYPTED-TEST-PACKET", 21);
    if (strcmp(mode, "no-status")) put(3, "[GNUPG:] END_ENCRYPTION\n", strlen("[GNUPG:] END_ENCRYPTION\n"));
    return !strcmp(mode, "bad-exit") ? 2 : 0;
}

static int fake_gpg(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[argc - 1], "--encrypt")) return fake_encrypt(argc, argv);
    static const char *const expected[] = {
        "gpg", "--no-options", "--batch", "--no-tty", "--pinentry-mode", "error", "--no-autostart",
        "--no-auto-key-retrieve", "--no-auto-key-import", "--no-auto-key-locate", "--disable-dirmngr",
        "--status-fd", "3", "--max-output", "1026", "--output", "-", "--decrypt"
    };
    assert(argc == (int)(sizeof expected / sizeof expected[0]));
    for (int i = 0; i < argc; ++i) assert(!strcmp(argv[i], expected[i]));
    for (int fd = 4; fd < 1024; ++fd) assert(fcntl(fd, F_GETFD) < 0 && errno == EBADF);
    assert(fcntl(65535, F_GETFD) < 0 && errno == EBADF);
    struct sigaction action;
    assert(!sigaction(SIGINT, NULL, &action) && action.sa_handler == SIG_DFL);
    sigset_t mask;
    assert(!sigprocmask(SIG_SETMASK, NULL, &mask) && !sigismember(&mask, SIGUSR1));
    struct rlimit limit;
    assert(!getrlimit(RLIMIT_CORE, &limit) && !limit.rlim_cur);
    struct stat st;
    assert(!fstat(0, &st) && S_ISREG(st.st_mode));
    assert(!fstat(1, &st) && S_ISFIFO(st.st_mode));
    assert(!fstat(2, &st) && S_ISCHR(st.st_mode));
    assert(!fstat(3, &st) && S_ISFIFO(st.st_mode));
    char mode[128] = {0};
    ssize_t size = read(0, mode, sizeof mode - 1);
    assert(size > 0);
    if (!strcmp(mode, "hang") || !strcmp(mode, "partial")) {
        if (!strcmp(mode, "partial")) put(1, "must-stay-private", 17);
        (void)signal(SIGTERM, SIG_IGN);
        for (;;) pause();
    }
    if (!strcmp(mode, "status-overflow")) {
        for (int i = 0; i < 1000; ++i) put(3, "[GNUPG:] UNKNOWN safe data\n", strlen("[GNUPG:] UNKNOWN safe data\n"));
        return 0;
    }
    if (!strcmp(mode, "no-status")) { put(1, "secret", 6); return 0; }
    const char *information = !strcmp(mode, "no-integrity") ?
        "[GNUPG:] DECRYPTION_INFO 0 9 0\n" : "[GNUPG:] DECRYPTION_INFO 2 9 0\n";
    put(3, information, strlen(information));
    if (!strcmp(mode, "overflow") || !strcmp(mode, "maximum")) {
        char bytes[1027]; memset(bytes, 'p', sizeof bytes);
        if (!strcmp(mode, "maximum")) { bytes[1024] = '\r'; bytes[1025] = '\n'; }
        put(1, bytes, !strcmp(mode, "maximum") ? 1026u : sizeof bytes);
    } else if (!strcmp(mode, "nul")) put(1, "sec\0ret", 7);
    else if (!strcmp(mode, "multiline")) put(1, "one\ntwo\n", 8);
    else if (strcmp(mode, "empty")) put(1, "  secret #;=  \n", 15);
    if (!strcmp(mode, "bad-mdc")) put(3, "[GNUPG:] BADMDC\n", 16);
    put(3, "[GNUPG:] DECRYPTION_OKAY\n[GNUPG:] END_DECRYPTION\n", strlen("[GNUPG:] DECRYPTION_OKAY\n[GNUPG:] END_DECRYPTION\n"));
    return !strcmp(mode, "bad-exit") ? 2 : 0;
}

static int finish(Credential *job, char *out, size_t cap, char *error, size_t error_cap)
{
    int result;
    int64_t end = milliseconds() + 6000;
    do {
        result = credential_poll(job, milliseconds(), out, cap, error, error_cap);
        if (result) return result;
        (void)poll(NULL, 0, 5);
    } while (milliseconds() < end);
    assert(!"Credential did not reach its deadline");
    return -1;
}

static int finish_creation(CredentialCreate *job, char *error, size_t cap)
{
    int64_t end = milliseconds() + CREDENTIAL_TIMEOUT_MS + 1000;
    for (;;) {
        int result = credential_create_poll(job, milliseconds(), error, cap);
        if (result) return result;
        assert(milliseconds() < end);
        (void)poll(NULL, 0, 5);
    }
}

static const char fingerprint[] = "0123456789ABCDEF0123456789ABCDEF01234567";

static void path_cases(const char *directory)
{
    char path[CONFIG_PATH_MAX], config[CONFIG_PATH_MAX], offered[CONFIG_PATH_MAX];
    char parent[CONFIG_PATH_MAX], managed[CONFIG_PATH_MAX], other[CONFIG_PATH_MAX];
    char normalized[CONFIG_PATH_MAX], current[CONFIG_PATH_MAX], error[512];
    struct stat st;
    int before = descriptors();
    assert(!credential_create_published(NULL));
    assert(getcwd(current, sizeof current));
    assert(credential_absolute_path("./relative//new.gpg", normalized, sizeof normalized, error, sizeof error) == 1);
    assert(snprintf(path, sizeof path, "%s/relative/new.gpg", current) > 0 && !strcmp(path, normalized));
    assert(credential_absolute_path("bad/../new.gpg", normalized, sizeof normalized, error, sizeof error) < 0 && strstr(error, ".."));
    assert(credential_absolute_path("bad\033path", normalized, sizeof normalized, error, sizeof error) < 0 && !strchr(error, '\033'));
    assert(credential_absolute_path("directory/", normalized, sizeof normalized, error, sizeof error) < 0);
    assert(credential_absolute_path("new.gpg", normalized, 3, error, sizeof error) < 0);

    assert(snprintf(path, sizeof path, "%s/missing/custom.gpg", directory) > 0);
    assert(credential_create_check(path, error, sizeof error) < 0 && strstr(error, "does not exist"));
    assert(snprintf(parent, sizeof parent, "%s/missing", directory) > 0);
    assert(strstr(error, parent) && access(parent, F_OK) < 0);
    assert(snprintf(config, sizeof config, "%s/fresh/app/config.ini", directory) > 0);
    assert(credential_default_path(config, 0, offered, sizeof offered, error, sizeof error) == 1);
    assert(snprintf(path, sizeof path, "%s/fresh/app/credentials/account-password.gpg", directory) > 0);
    assert(!strcmp(offered, path));
    assert(snprintf(parent, sizeof parent, "%s/fresh", directory) > 0 && access(parent, F_OK) < 0);
    /* Viewing the offer has not created any ancestor. Acceptance may create
     * only its managed path; the encrypted file is still absent afterward. */
    assert(credential_default_prepare(config, offered, error, sizeof error) == 1);
    assert(!stat(parent, &st) && (st.st_mode & 0777) == 0700 && st.st_uid == getuid());
    assert(snprintf(parent, sizeof parent, "%s/fresh/app", directory) > 0);
    assert(!stat(parent, &st) && (st.st_mode & 0777) == 0700 && st.st_uid == getuid());
    assert(snprintf(managed, sizeof managed, "%s/credentials", parent) > 0);
    assert(!stat(managed, &st) && (st.st_mode & 0777) == 0700 && st.st_uid == getuid());
    assert(access(offered, F_OK) < 0 && access(config, F_OK) < 0);
    assert(credential_create_check(offered, error, sizeof error) == 1);
    assert(!credential_cleanup_pending() && access(offered, F_OK) < 0);
    file_write(offered, "original encrypted fixture", 26, 0600);
    assert(credential_create_check(offered, error, sizeof error) < 0 && strstr(error, offered) && strstr(error, "already exists"));
    assert(credential_default_path(config, 0, path, sizeof path, error, sizeof error) == 1 && strstr(path, "/account-password-2.gpg"));
    assert(credential_default_prepare(config, offered, error, sizeof error) < 0);
    assert(!stat(offered, &st) && st.st_size == 26);
    assert(credential_default_path(config, 1, path, sizeof path, error, sizeof error) == 1 && strstr(path, "/server-password.gpg"));
    /* Existing parent modes are deliberately left alone; only the credential
     * directory itself needs private owner read/write/search access. */
    assert(!chmod(parent, 0755));
    assert(credential_default_prepare(config, path, error, sizeof error) == 1);
    assert(!stat(parent, &st) && (st.st_mode & 0777) == 0755);
    assert(!chmod(managed, 0755));
    assert(credential_create_check(path, error, sizeof error) < 0 && strstr(error, managed) && strstr(error, "0755") && strstr(error, "0700"));
    assert(credential_default_path(config, 1, normalized, sizeof normalized, error, sizeof error) < 0 && strstr(error, "0755"));
    assert(credential_default_prepare(config, path, error, sizeof error) < 0);
    assert(!stat(managed, &st) && (st.st_mode & 0777) == 0755);
    assert(!chmod(managed, 0500));
    assert(credential_create_check(path, error, sizeof error) < 0 && strstr(error, "0500"));
    assert(!chmod(managed, 0700));
    if (getuid() == 0) {
        assert(!chown(managed, 1, (gid_t)-1));
        assert(credential_create_check(path, error, sizeof error) < 0 && strstr(error, "owned by UID 1") && strstr(error, managed));
        assert(!chown(managed, getuid(), (gid_t)-1));
    }
    assert(snprintf(other, sizeof other, "%s/custom/new.gpg", directory) > 0);
    assert(credential_default_prepare(config, other, error, sizeof error) < 0);
    assert(snprintf(other, sizeof other, "%s/custom", directory) > 0 && access(other, F_OK) < 0);
    assert(snprintf(other, sizeof other, "%s/not-managed.gpg", managed) > 0);
    assert(credential_default_prepare(config, other, error, sizeof error) < 0 && access(other, F_OK) < 0);
    assert(!unlink(offered));
    /* All 64 slots are bounded and collisions never overwrite a file. */
    for (unsigned i = 1; i <= 64; ++i) {
        if (i == 1) assert(snprintf(path, sizeof path, "%s/account-password.gpg", managed) > 0);
        else assert(snprintf(path, sizeof path, "%s/account-password-%u.gpg", managed, i) > 0);
        file_write(path, "keep", 4, 0600);
    }
    strcpy(error, "old message");
    assert(credential_default_path(config, 0, path, sizeof path, error, sizeof error) < 0 && strstr(error, "All 64"));
    for (unsigned i = 1; i <= 64; ++i) {
        if (i == 1) assert(snprintf(path, sizeof path, "%s/account-password.gpg", managed) > 0);
        else assert(snprintf(path, sizeof path, "%s/account-password-%u.gpg", managed, i) > 0);
        assert(!stat(path, &st) && st.st_size == 4 && !unlink(path));
    }
    assert(!rmdir(managed));
    assert(snprintf(other, sizeof other, "%s/outside", directory) > 0 && !mkdir(other, 0700));
    assert(!symlink(other, managed));
    assert(credential_default_path(config, 0, path, sizeof path, error, sizeof error) < 0 && strstr(error, "Symlink") && strstr(error, managed));
    assert(credential_default_prepare(config, offered, error, sizeof error) < 0 && strstr(error, "Symlink"));
    assert(!unlink(managed));
    file_write(managed, "not a directory", 15, 0600);
    assert(credential_default_path(config, 0, path, sizeof path, error, sizeof error) < 0 && strstr(error, "not a directory"));
    assert(!unlink(managed) && !rmdir(parent));
    assert(snprintf(parent, sizeof parent, "%s/fresh", directory) > 0 && !rmdir(parent));
    assert(!rmdir(other));

    /* A relative path is pinned as absolute before asynchronous encryption,
     * so changing cwd cannot redirect publication or cleanup. */
    int cwd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(cwd >= 0 && !chdir(directory));
    CredentialCreate *job = credential_create_start("relative-created.gpg", fingerprint, "okay", milliseconds(), error, sizeof error);
    assert(job && finish_creation(job, error, sizeof error) == 1 && !chdir("/"));
    assert(credential_create_publish(job, error, sizeof error) == 1);
    assert(snprintf(path, sizeof path, "%s/relative-created.gpg", directory) > 0 && !access(path, F_OK));
    credential_create_cancel(job); reap_all();
    assert(access(path, F_OK) < 0 && !fchdir(cwd) && !close(cwd));
    assert(descriptors() == before);
}

static void creation_cases(const char *directory)
{
    char destination[CONFIG_PATH_MAX], error[512];
    assert(snprintf(destination, sizeof destination, "%s/new.gpg", directory) > 0);
    int before = descriptors();
    assert(credential_fingerprint_valid(fingerprint));
    assert(credential_fingerprint_valid("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    assert(!credential_fingerprint_valid("0123456789ABCDEF"));
    assert(!credential_fingerprint_valid("--recipient anything"));
    assert(!credential_fingerprint_valid("0123456789ABCDEF0123456789ABCDEF0123456G"));
    assert(!credential_create_start(destination, "0123456789ABCDEF", "private", milliseconds(), error, sizeof error));
    assert(!credential_create_start(destination, fingerprint, "", milliseconds(), error, sizeof error));
    assert(!credential_create_start(destination, fingerprint, "bad\nline", milliseconds(), error, sizeof error));
    char oversized[CONFIG_SECRET_MAX + 2]; memset(oversized, 'x', sizeof oversized - 1u); oversized[sizeof oversized - 1u] = 0;
    assert(!credential_create_start(destination, fingerprint, oversized, milliseconds(), error, sizeof error));
    static const char *const modes[] = {"okay", "maximum", "empty", "no-status", "no-integrity", "bad-status", "duplicate", "bad-exit", "overflow", "status-overflow", "recipient"};
    for (size_t i = 0; i < sizeof modes / sizeof modes[0]; ++i) {
        CredentialCreate *job = credential_create_start(destination, fingerprint, modes[i], milliseconds(), error, sizeof error);
        assert(job && !credential_create_published(job) && access(destination, F_OK) < 0);
        assert(credential_create_publish(job, error, sizeof error) < 0 && access(destination, F_OK) < 0);
        int result = finish_creation(job, error, sizeof error);
        if (i < 2) {
            if (result != 1) fprintf(stderr, "encryption fixture %s failed: %s\n", modes[i], error);
            assert(result == 1 && access(destination, F_OK) < 0);
            assert(credential_create_publish(job, error, sizeof error) == 1);
            assert(credential_create_published(job));
            assert(credential_create_publish(job, error, sizeof error) == 1);
            struct stat st; assert(!lstat(destination, &st) && S_ISREG(st.st_mode) && (st.st_mode & 0777) == 0600);
            assert(st.st_size == (i ? CREDENTIAL_CIPHERTEXT_MAX : 21));
            credential_create_finish(job);
            assert(!access(destination, F_OK));
            assert(!credential_create_start(destination, fingerprint, "replacement", milliseconds(), error, sizeof error));
            assert(!unlink(destination));
        } else {
            assert(result < 0 && !strstr(error, "private-recipient"));
            assert(access(destination, F_OK) < 0);
            credential_create_cancel(job);
        }
        reap_all(); assert(descriptors() == before);
    }
    /* Cancellation before publication leaves no file; after publication it
     * removes only this job's unchanged inode and ciphertext. */
    for (int action = 0; action < 5; ++action) {
        CredentialCreate *job = credential_create_start(destination, fingerprint, "okay", milliseconds(), error, sizeof error);
        assert(job && finish_creation(job, error, sizeof error) == 1);
        if (action) assert(credential_create_publish(job, error, sizeof error) == 1);
        if (action == 2) file_write(destination, "edited", 6, 0600);
        if (action == 3) { assert(!unlink(destination)); file_write(destination, "replaced", 8, 0600); }
        if (action == 4) {
            assert(!chmod(destination, 0644));
            assert(credential_create_publish(job, error, sizeof error) < 0);
        }
        credential_create_cancel(job); reap_all();
        if (action < 2) assert(access(destination, F_OK) < 0);
        else { struct stat st; assert(!stat(destination, &st) && st.st_size == (action == 2 ? 6 : action == 3 ? 8 : 21)); assert(!unlink(destination)); }
    }
    /* An existing target racing publication is never truncated/replaced. */
    CredentialCreate *job = credential_create_start(destination, fingerprint, "okay", milliseconds(), error, sizeof error);
    assert(job && finish_creation(job, error, sizeof error) == 1);
    file_write(destination, "keep", 4, 0600);
    assert(credential_create_publish(job, error, sizeof error) < 0);
    assert(!credential_create_published(job));
    credential_create_cancel(job); reap_all();
    struct stat st; assert(!stat(destination, &st) && st.st_size == 4);
    char link[CONFIG_PATH_MAX]; assert(snprintf(link, sizeof link, "%s/link.gpg", directory) > 0);
    assert(!symlink(destination, link));
    assert(!credential_create_start(link, fingerprint, "okay", milliseconds(), error, sizeof error));
    assert(!unlink(link) && !unlink(destination));
    assert(!chmod(directory, 0755));
    assert(!credential_create_start(destination, fingerprint, "okay", milliseconds(), error, sizeof error));
    assert(!chmod(directory, 0700));
    char real[CONFIG_PATH_MAX], moved[CONFIG_PATH_MAX], nested[CONFIG_PATH_MAX], linked[CONFIG_PATH_MAX];
    assert(snprintf(real, sizeof real, "%s/private", directory) > 0);
    assert(snprintf(moved, sizeof moved, "%s/moved", directory) > 0);
    assert(snprintf(nested, sizeof nested, "%s/private/new.gpg", directory) > 0);
    assert(snprintf(linked, sizeof linked, "%s/link/new.gpg", directory) > 0);
    assert(snprintf(link, sizeof link, "%s/link", directory) > 0);
    assert(!mkdir(real, 0700) && !symlink(real, link));
    assert(!credential_create_start(linked, fingerprint, "okay", milliseconds(), error, sizeof error));
    assert(!unlink(link));
    job = credential_create_start(nested, fingerprint, "okay", milliseconds(), error, sizeof error);
    assert(job && finish_creation(job, error, sizeof error) == 1);
    assert(!rename(real, moved) && !mkdir(real, 0700));
    assert(credential_create_publish(job, error, sizeof error) < 0);
    credential_create_cancel(job); reap_all();
    assert(!rmdir(real) && !rmdir(moved));
    job = credential_create_start(destination, fingerprint, "hang", 1000, error, sizeof error);
    assert(job);
    assert(!credential_create_poll(job, 1001, error, sizeof error));
    assert(credential_create_poll(job, 1000 + CREDENTIAL_TIMEOUT_MS, error, sizeof error) < 0);
    credential_create_cancel(job); reap_all();
    for (int i = 0; i < 64; ++i) {
        job = credential_create_start(destination, fingerprint, "hang", milliseconds(), error, sizeof error);
        assert(job); credential_create_cancel(job); reap_all();
    }
    assert(access(destination, F_OK) < 0 && descriptors() == before);
}

static void fake_cases(const char *path)
{
    static const char *const cases[] = {"okay", "maximum", "empty", "no-status", "no-integrity", "bad-mdc", "bad-exit", "overflow", "status-overflow", "nul", "multiline"};
    char out[CONFIG_SECRET_MAX + 1], error[512];
    int before = descriptors();
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        file_write(path, cases[i], strlen(cases[i]), 0600);
        Credential *job = credential_start(path, milliseconds(), error, sizeof error);
        assert(job);
        int result = finish(job, out, sizeof out, error, sizeof error);
        if (i < 2) {
            if (result != 1) fprintf(stderr, "fixture %s failed: %s\n", cases[i], error);
            assert(result == 1);
            if (!i) assert(!strcmp(out, "  secret #;=  "));
            else assert(strlen(out) == CONFIG_SECRET_MAX);
        } else {
            assert(result == -1 && !out[0]);
            assert(!strstr(error, "secret #;="));
        }
        config_wipe(out, sizeof out);
        credential_cancel(job); reap_all();
        assert(descriptors() == before);
    }
    file_write(path, "partial", 7, 0600);
    Credential *job = credential_start(path, 1000, error, sizeof error);
    assert(job);
    (void)poll(NULL, 0, 30);
    memset(out, 'X', sizeof out);
    assert(!credential_poll(job, 1001, out, sizeof out, error, sizeof error) && !out[0]);
    int64_t began = milliseconds();
    assert(credential_poll(job, 6000, out, sizeof out, error, sizeof error) == -1 && strstr(error, "timed out"));
    credential_cancel(job); reap_all();
    assert(milliseconds() - began < 1000 && descriptors() == before);
    file_write(path, "hang", 4, 0600);
    for (int i = 0; i < 64; ++i) {
        job = credential_start(path, milliseconds(), error, sizeof error);
        assert(job); credential_cancel(job); reap_all();
    }
    assert(descriptors() == before);
    Credential *jobs[16];
    for (size_t i = 0; i < 16; ++i) {
        jobs[i] = credential_start(path, milliseconds(), error, sizeof error);
        assert(jobs[i]);
    }
    assert(!credential_start(path, milliseconds(), error, sizeof error));
    for (size_t i = 0; i < 16; ++i) credential_cancel(jobs[i]);
    reap_all(); assert(descriptors() == before);
}

static void configuration_cases(const char *path, const char *secret_path)
{
    Config config, loaded, before;
    char error[512];
    config_defaults(&config);
    strcpy(config.auth, "sasl"); strcpy(config.auth_user, "account");
    strcpy(config.password, "  literal #;= bytes  ");
    strcpy(config.server_password, " ;server#= ");
    assert(config_save(&config, path, error, sizeof error) == 1);
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof error) == 1);
    assert(!strcmp(loaded.password, config.password) && !strcmp(loaded.server_password, config.server_password));
    assert(!chmod(path, 0644)); before = loaded;
    assert(config_load(&loaded, path, error, sizeof error) == -1 && !memcmp(&before, &loaded, sizeof loaded));
    assert(strstr(error, "0600") && !strstr(error, "literal"));
    assert(!chmod(path, 0600));
    strcpy(config.password_file, secret_path);
    assert(config_validate(&config, error, sizeof error) == -1);
    config.password_file[0] = 0;
    strcpy(config.password_gpg_file, secret_path);
    assert(config_validate(&config, error, sizeof error) == -1);
    config.password[0] = 0;
    assert(config_validate(&config, error, sizeof error) == 1);
    strcpy(config.server_password_gpg_file, secret_path);
    assert(config_validate(&config, error, sizeof error) == -1);
    config.server_password[0] = 0;
    assert(config_validate(&config, error, sizeof error) == 1);
    config.tls = 0;
    assert(config_validate(&config, error, sizeof error) == -1);
    config.tls = 1;
    assert(config_save(&config, path, error, sizeof error) == 1);
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof error) == 1);
    assert(!strcmp(loaded.password_gpg_file, secret_path) && !strcmp(loaded.server_password_gpg_file, secret_path));
    const char literal[] = "[connection]\r\npassword=  #;=  \r\nserver_password=  PASS;#=  \r\n\r\n";
    file_write(path, literal, sizeof literal - 1, 0600);
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof error) == 1);
    assert(!strcmp(loaded.password, "  #;=  ") && !strcmp(loaded.server_password, "  PASS;#=  "));
    strcpy(loaded.password, "previous-secret-with-a-long-tail");
    assert(config_load(&loaded, path, error, sizeof error) == 1);
    for (size_t i = strlen(loaded.password) + 1; i < sizeof loaded.password; ++i) assert(!loaded.password[i]);
    config_defaults(&config);
    strcpy(config.password, "bad\nvalue");
    assert(config_validate(&config, error, sizeof error) == -1 && !strstr(error, "bad"));
    file_write(secret_path, "legacy-secret\n", 14, 0600);
    char out[CONFIG_SECRET_MAX + 1];
    assert(config_read_secret(secret_path, out, sizeof out, error, sizeof error) == 1 && !strcmp(out, "legacy-secret"));
    assert(!chmod(secret_path, 0644));
    assert(config_open_gpg_file(secret_path, error, sizeof error) == -1 &&
           strstr(error, secret_path) && strstr(error, "0644") && strstr(error, "0600"));
    assert(!strstr(error, "legacy-secret"));
    assert(!chmod(secret_path, 0600));
    assert(!unlink(path) && !symlink(secret_path, path));
    assert(config_open_gpg_file(path, error, sizeof error) == -1 && strstr(error, path) && strstr(error, "Symlink"));
    assert(!unlink(path));
    assert(config_open_gpg_file(path, error, sizeof error) == -1 && strstr(error, path) && strstr(error, "does not exist"));
    config_wipe(&config, sizeof config); config_wipe(&loaded, sizeof loaded); config_wipe(&before, sizeof before);
    config_wipe(out, sizeof out);
}

static int probe(const char *path)
{
    char expected[CONFIG_SECRET_MAX + 1], out[CONFIG_SECRET_MAX + 1], error[512];
    size_t used = 0;
    for (;;) {
        ssize_t count = read(0, expected + used, sizeof expected - used);
        if (count < 0 && errno == EINTR) continue;
        assert(count >= 0);
        if (!count) break;
        used += (size_t)count;
        assert(used < sizeof expected);
    }
    expected[used] = 0;
    Credential *job = credential_start(path, milliseconds(), error, sizeof error);
    int result = job ? finish(job, out, sizeof out, error, sizeof error) : -1;
    if (job) credential_cancel(job);
    reap_all();
    int okay = result == 1 && strlen(out) == used && !memcmp(out, expected, used);
    if (okay) puts("credential probe: authenticated plaintext matched");
    else fprintf(stderr, "credential probe: %s\n", result < 0 ? error : "plaintext mismatch");
    config_wipe(out, sizeof out); config_wipe(expected, sizeof expected);
    return okay ? 0 : 1;
}

static int create_probe(const char *path, const char *key)
{
    char secret[CONFIG_SECRET_MAX + 1], error[512];
    size_t used = 0;
    for (;;) {
        ssize_t count = read(0, secret + used, sizeof secret - used);
        if (count < 0 && errno == EINTR) continue;
        assert(count >= 0);
        if (!count) break;
        used += (size_t)count; assert(used < sizeof secret);
    }
    secret[used] = 0;
    CredentialCreate *job = credential_create_start(path, key, secret, milliseconds(), error, sizeof error);
    config_wipe(secret, sizeof secret);
    int okay = job && finish_creation(job, error, sizeof error) == 1 &&
               credential_create_publish(job, error, sizeof error) == 1;
    if (okay) { credential_create_finish(job); puts("credential creation: encrypted file published"); }
    else { credential_create_cancel(job); fprintf(stderr, "credential creation: %s\n", error); }
    reap_all();
    return okay ? 0 : 1;
}

static int default_create_probe(const char *config_path, const char *key)
{
    char path[CONFIG_PATH_MAX], error[512];
    if (credential_default_path(config_path, 0, path, sizeof path, error, sizeof error) < 0 ||
        credential_default_prepare(config_path, path, error, sizeof error) < 0 ||
        credential_create_check(path, error, sizeof error) < 0) {
        fprintf(stderr, "credential default: %s\n", error); return 1;
    }
    int result = create_probe(path, key);
    if (!result) printf("credential default: %s\n", path);
    return result;
}

int main(int argc, char **argv)
{
    if (!strcmp(argv[0], "gpg")) return fake_gpg(argc, argv);
    if (argc == 3 && !strcmp(argv[1], "--probe")) return probe(argv[2]);
    if (argc == 4 && !strcmp(argv[1], "--create")) return create_probe(argv[2], argv[3]);
    if (argc == 4 && !strcmp(argv[1], "--create-default")) return default_create_probe(argv[2], argv[3]);
    if (argc == 3 && !strcmp(argv[1], "--check")) {
        char error[512];
        int result = credential_create_check(argv[2], error, sizeof error);
        if (result < 0) fprintf(stderr, "credential preflight: %s\n", error);
        return result < 0 ? 1 : 0;
    }
    char directory[] = "/tmp/vipirc-credentials-XXXXXX";
    char executable[CONFIG_PATH_MAX], program[CONFIG_PATH_MAX], source[CONFIG_PATH_MAX], config[CONFIG_PATH_MAX];
    assert(realpath(argv[0], executable) && mkdtemp(directory));
    assert(snprintf(program, sizeof program, "%s/gpg", directory) > 0);
    assert(snprintf(source, sizeof source, "%s/credential", directory) > 0);
    assert(snprintf(config, sizeof config, "%s/config", directory) > 0);
    assert(!symlink(executable, program) && !setenv("PATH", directory, 1));
    configuration_cases(config, source);
    /* Inherited high fds must be closed even after the soft limit is lowered. */
    int low = open("/dev/null", O_RDONLY), high = -1;
    assert(low >= 0);
    struct rlimit old, reduced;
    assert(!getrlimit(RLIMIT_NOFILE, &old)); reduced = old;
    if (old.rlim_cur > 65535) { high = fcntl(low, F_DUPFD, 65535); assert(high == 65535); }
    if (reduced.rlim_cur > 256) reduced.rlim_cur = 256;
    assert(!setrlimit(RLIMIT_NOFILE, &reduced));
    struct sigaction ignored, previous;
    memset(&ignored, 0, sizeof ignored); ignored.sa_handler = SIG_IGN; sigemptyset(&ignored.sa_mask);
    assert(!sigaction(SIGINT, &ignored, &previous));
    sigset_t blocked, original;
    sigemptyset(&blocked); sigaddset(&blocked, SIGUSR1);
    assert(!sigprocmask(SIG_BLOCK, &blocked, &original));
    fake_cases(source);
    path_cases(directory);
    creation_cases(directory);
    assert(!sigprocmask(SIG_SETMASK, &original, NULL) && !sigaction(SIGINT, &previous, NULL));
    assert(!setrlimit(RLIMIT_NOFILE, &old));
    if (high >= 0) assert(!close(high));
    assert(!close(low) && !unlink(program));
    /* execve must not silently reinterpret an executable text file as a
     * shell script. A shell fallback would create this harmless marker. */
    char marker[CONFIG_PATH_MAX], script[CONFIG_PATH_MAX + 64], error[512], out[1025];
    assert(snprintf(marker, sizeof marker, "%s/shell-ran", directory) > 0);
    int length = snprintf(script, sizeof script, "printf unexpected > %s\n", marker);
    assert(length > 0 && (size_t)length < sizeof script);
    file_write(program, script, (size_t)length, 0700);
    Credential *job = credential_start(source, milliseconds(), error, sizeof error);
    assert(job && finish(job, out, sizeof out, error, sizeof error) == -1);
    credential_cancel(job); reap_all();
    assert(access(marker, F_OK) < 0 && errno == ENOENT);
    CredentialCreate *created = credential_create_start(config, fingerprint, "private-input", milliseconds(), error, sizeof error);
    assert(created && finish_creation(created, error, sizeof error) < 0);
    credential_create_cancel(created); reap_all();
    assert(access(marker, F_OK) < 0 && access(config, F_OK) < 0);
    assert(!setenv("PATH", "/no-vipirc-gpg-fixture-executable-here", 1));
    assert(!credential_start(source, milliseconds(), error, sizeof error) && strstr(error, "gpg executable"));
    assert(!credential_create_start(config, fingerprint, "private-input", milliseconds(), error, sizeof error) && strstr(error, "gpg executable"));
    assert(credential_create_check(config, error, sizeof error) < 0 && strstr(error, "gpg executable"));
    assert(!unlink(source) && !unlink(program) && !rmdir(directory));
    puts("credentials: literal/private config, source conflicts, legacy files, authentic-result gate, hostile decrypt/encrypt bounds, full fingerprints, early path diagnostics, read-only defaults, explicit private directory creation, absolute publication, bounded collisions, private atomic/no-clobber publication, unchanged-inode rollback, timeout/cancel, signals, high-fd isolation and child/fd cleanup passed");
    return 0;
}
