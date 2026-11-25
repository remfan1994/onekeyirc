/* Bounded GnuPG jobs driven by the caller's event loop.
 *
 * Shared child/descriptor isolation comes first, then decryption, then the
 * creation pipeline: choose/check destination, encrypt into bounded memory,
 * explicitly publish ciphertext, and finish or roll back owned output.
 * No shell or plaintext file is used. See credentials.h for job lifetimes.
 */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "credentials.h"
#include "config.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__OpenBSD__) || defined(__NetBSD__)
extern int closefrom(int);
#elif defined(__FreeBSD__)
extern void closefrom(int);
#endif
extern char **environ;

#define CREDENTIAL_JOBS 16
#define CREDENTIAL_STATUS_MAX 16384
/* Active jobs own their waitable child. Cancellation retires the table
 * entry before freeing its job; credential_reap collects it without waiting. */
typedef struct Child {
    pid_t pid;
    int retired;
} Child;
static Child children[CREDENTIAL_JOBS];

struct Credential {
    size_t child;
    int output, status, exited, exit_status, failed;
    int information, protected_data, okay, ended;
    int64_t began;
    size_t used, status_used, status_total;
    char plaintext[CONFIG_SECRET_MAX + 3];
    char status_line[1024];
};

static int failure(char *error, size_t cap, const char *message, ...)
{
    if (error && cap) {
        va_list args;
        va_start(args, message);
        (void)vsnprintf(error, cap, message, args);
        va_end(args);
    }
    return -1;
}

/* Paths are useful diagnostics, but must not inject terminal control bytes.
 * Preserve both ends of an unusually long path in bounded status messages. */
static int path_failure(char *error, size_t cap, const char *path, const char *message, ...)
{
    char detail[224], display[257];
    size_t length = path ? strnlen(path, CONFIG_PATH_MAX) : 0;
    if (length < sizeof display) {
        if (length) memcpy(display, path, length);
        display[length] = 0;
    } else {
        memcpy(display, path, 120);
        memcpy(display + 120, "...", 3);
        memcpy(display + 123, path + length - 133, 133);
        display[256] = 0;
    }
    for (unsigned char *p = (unsigned char *)display; *p; ++p)
        if (*p < 32 || *p == 127) *p = '?';
    va_list args;
    va_start(args, message);
    (void)vsnprintf(detail, sizeof detail, message, args);
    va_end(args);
    return failure(error, cap, "%s Path: %s", detail, display);
}

static int flags(int fd, int nonblock)
{
    int value = fcntl(fd, F_GETFD);
    if (value < 0 || fcntl(fd, F_SETFD, value | FD_CLOEXEC) < 0) return 0;
    if (!nonblock) return 1;
    value = fcntl(fd, F_GETFL);
    return value >= 0 && fcntl(fd, F_SETFL, value | O_NONBLOCK) >= 0;
}

static int child_ownership_safe(char *error, size_t cap)
{
    struct sigaction action;
    if (sigaction(SIGCHLD, NULL, &action) < 0)
        return failure(error, cap, "Cannot inspect SIGCHLD before starting GPG.");
    int automatic = action.sa_handler == SIG_IGN;
#ifdef SA_NOCLDWAIT
    automatic |= (action.sa_flags & SA_NOCLDWAIT) != 0;
#endif
    if (automatic)
        return failure(error, cap, "Cannot run GPG with automatic child reaping; start the client with SIGCHLD at its default disposition.");
    return 1;
}

/* Only retired helper children are collected here.
 * Do not add waitpid(-1): resolver and other subprocesses have separate
 * owners, and a live credential job may still need its process-group ID. */
void credential_reap(void)
{
    for (size_t i = 0; i < CREDENTIAL_JOBS; ++i)
        if (children[i].pid > 0 && children[i].retired) {
            pid_t found = waitpid(children[i].pid, NULL, WNOHANG);
            if (found == children[i].pid || (found < 0 && errno == ECHILD))
                children[i] = (Child){0, 0};
        }
}

int credential_cleanup_pending(void)
{
    for (size_t i = 0; i < CREDENTIAL_JOBS; ++i)
        if (children[i].pid > 0 && children[i].retired) return 1;
    return 0;
}

/* Resolve PATH before fork, then execve directly. Unlike execvp, an executable
 * text file without a shebang must never cause an implicit shell fallback. */
static int find_gpg(char path[CONFIG_PATH_MAX])
{
    const char *search = getenv("PATH");
    if (!search) search = "/usr/local/bin:/usr/bin:/bin";
    if (strnlen(search, 16385) > 16384) return 0;
    do {
        const char *next = strchr(search, ':');
        size_t length = next ? (size_t)(next - search) : strlen(search);
        if (length + sizeof("/gpg") <= CONFIG_PATH_MAX) {
            if (length) {
                memcpy(path, search, length);
                memcpy(path + length, "/gpg", sizeof("/gpg"));
            } else memcpy(path, "./gpg", sizeof("./gpg"));
            struct stat st;
            if (!stat(path, &st) && S_ISREG(st.st_mode) && !access(path, X_OK)) return 1;
        }
        if (!next) break;
        search = next + 1;
    } while (1);
    return 0;
}

static int close_directory(const char *path)
{
    DIR *directory = opendir(path);
    if (!directory) return 0;
    int own = dirfd(directory), okay = own >= 0, saw_own = 0;
    struct dirent *entry;
    errno = 0;
    while (okay && (entry = readdir(directory)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char *end;
        errno = 0;
        long value = strtol(entry->d_name, &end, 10);
        if (errno || !*entry->d_name || *end || value < 0 || value > INT_MAX) { okay = 0; break; }
        int fd = (int)value;
        if (fd == own) { saw_own = 1; continue; }
        if (fd > 3 && close(fd) < 0 && errno != EBADF) { okay = 0; break; }
        errno = 0;
    }
    if (errno) okay = 0;
    if (closedir(directory) < 0) okay = 0;
    return okay && saw_own;
}

static int isolate(void)
{
#if defined(__linux__) && defined(SYS_close_range)
    if (syscall(SYS_close_range, 4u, UINT_MAX, 0u) == 0) return 1;
#elif defined(__OpenBSD__) || defined(__NetBSD__)
    if (closefrom(4) == 0 || errno == EBADF) return 1;
#elif defined(__FreeBSD__)
    closefrom(4);
    return 1;
#endif
    /* Enumerate actual open descriptors, including those above a subsequently
     * reduced RLIMIT_NOFILE. Refuse to execute if isolation cannot be proved. */
    return close_directory("/proc/self/fd") || close_directory("/dev/fd");
}

static void isolate_gpg(int input, int output, int status, int quiet)
{
    const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGPIPE, SIGALRM,
                           SIGCHLD, SIGTSTP, SIGTTIN, SIGTTOU, SIGCONT, SIGUSR1, SIGUSR2};
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; ++i)
        if (sigaction(signals[i], &action, NULL) < 0) _exit(126);
    if (sigprocmask(SIG_SETMASK, &action.sa_mask, NULL) < 0 || setpgid(0, 0) < 0) _exit(126);
    struct rlimit no_core = {0, 0};
    if (setrlimit(RLIMIT_CORE, &no_core) < 0) _exit(126);
    int sources[4] = {input, output, quiet, status};
    /* Duplicate above the fixed target range before changing any target. */
    for (int i = 0; i < 4; ++i) {
        sources[i] = fcntl(sources[i], F_DUPFD_CLOEXEC, 4);
        if (sources[i] < 0) _exit(126);
    }
    for (int i = 0; i < 4; ++i)
        if (dup2(sources[i], i) < 0) _exit(126);
    if (!isolate()) _exit(126);
}

static void execute_gpg(const char *program, int input, int output, int status, int quiet)
{
    isolate_gpg(input, output, status, quiet);
    char maximum[24];
    (void)snprintf(maximum, sizeof maximum, "%d", CONFIG_SECRET_MAX + 2);
    char *const arguments[] = {
        (char *)"gpg", (char *)"--no-options", (char *)"--batch", (char *)"--no-tty",
        (char *)"--pinentry-mode", (char *)"error", (char *)"--no-autostart",
        (char *)"--no-auto-key-retrieve", (char *)"--no-auto-key-import",
        (char *)"--no-auto-key-locate", (char *)"--disable-dirmngr",
        (char *)"--status-fd", (char *)"3", (char *)"--max-output", maximum,
        (char *)"--output", (char *)"-", (char *)"--decrypt", NULL
    };
    execve(program, arguments, environ);
    _exit(127);
}

/* Local descriptors remain in cleanup variables until explicitly moved
 * into the job. Setting each moved descriptor to -1 prevents the common exit
 * path from closing resources now owned by the returned job. */
Credential *credential_start(const char *path, int64_t now, char *error, size_t cap)
{
    char program[CONFIG_PATH_MAX];
    int input = -1, quiet = -1, output[2] = {-1, -1}, status[2] = {-1, -1};
    Credential *job = NULL;
    if (child_ownership_safe(error, cap) < 0) return NULL;
    credential_reap();
    size_t slot;
    for (slot = 0; slot < CREDENTIAL_JOBS && children[slot].pid; ++slot) { }
    if (slot == CREDENTIAL_JOBS) {
        failure(error, cap, "GPG cleanup is still pending; retry shortly.");
        return NULL;
    }
    if (!find_gpg(program)) {
        failure(error, cap, "Cannot find the gpg executable in PATH.");
        return NULL;
    }
    input = config_open_gpg_file(path, error, cap);
    if (input < 0) return NULL;
    job = calloc(1, sizeof *job);
    if (!job) {
        failure(error, cap, "Cannot allocate GPG credential state.");
        goto done;
    }
    job->output = job->status = -1;
    quiet = open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (quiet < 0 || pipe(output) < 0 || pipe(status) < 0 ||
        !flags(output[0], 1) || !flags(output[1], 0) ||
        !flags(status[0], 1) || !flags(status[1], 0)) {
        failure(error, cap, "Cannot create isolated GPG pipes.");
        goto failed;
    }
    pid_t pid = fork();
    if (pid < 0) {
        failure(error, cap, "Cannot start GPG credential process.");
        goto failed;
    }
    if (!pid) execute_gpg(program, input, output[1], status[1], quiet);
    /* Establish the child's group from both sides to close the cancel race. */
    (void)setpgid(pid, pid);
    children[slot] = (Child){pid, 0};
    job->child = slot;
    job->began = now;
    job->output = output[0];
    output[0] = -1;
    job->status = status[0];
    status[0] = -1;
    goto done;
failed:
    config_wipe(job, sizeof *job);
    free(job);
    job = NULL;
done:
    if (input >= 0) close(input);
    if (quiet >= 0) close(quiet);
    for (int i = 0; i < 2; ++i) {
        if (output[i] >= 0) close(output[i]);
        if (status[i] >= 0) close(status[i]);
    }
    return job;
}

static void status_line(Credential *job)
{
    char *line = job->status_line;
    line[job->status_used] = 0;
    if (strncmp(line, "[GNUPG:] ", 9)) { job->failed = 1; return; }
    line += 9;
    if (!strncmp(line, "DECRYPTION_INFO ", 16)) {
        unsigned long numbers[4] = {0, 0, 0, 0};
        size_t count = 0;
        char *next = line + 16;
        while (*next && count < 4) {
            if (*next < '0' || *next > '9') { job->failed = 1; return; }
            char *end;
            errno = 0;
            numbers[count++] = strtoul(next, &end, 10);
            if (errno || numbers[count - 1] > UINT_MAX || (*end && *end != ' ')) { job->failed = 1; return; }
            next = end;
            while (*next == ' ') ++next;
        }
        if (*next || count < 2 || ++job->information != 1) job->failed = 1;
        job->protected_data = (numbers[0] != 0 || numbers[2] != 0) && numbers[1] != 0 && numbers[3] == 0;
    } else if (!strcmp(line, "DECRYPTION_OKAY")) {
        if (++job->okay != 1) job->failed = 1;
    } else if (!strcmp(line, "END_DECRYPTION")) {
        if (++job->ended != 1) job->failed = 1;
    } else if (!strcmp(line, "DECRYPTION_FAILED") || !strcmp(line, "BADMDC") ||
               !strcmp(line, "ERRMDC") || !strncmp(line, "FAILURE ", 8) || !strncmp(line, "ERROR ", 6))
        job->failed = 1;
}

static void read_status(Credential *job)
{
    char bytes[1024];
    for (int attempt = 0; attempt < 8 && job->status >= 0 && !job->failed; ++attempt) {
        ssize_t count = read(job->status, bytes, sizeof bytes);
        if (!count) {
            close(job->status); job->status = -1;
            if (job->status_used) job->failed = 1;
            break;
        }
        if (count < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) job->failed = 1;
            break;
        }
        job->status_total += (size_t)count;
        if (job->status_total > CREDENTIAL_STATUS_MAX) { job->failed = 1; break; }
        for (size_t i = 0; i < (size_t)count; ++i) {
            unsigned char byte = (unsigned char)bytes[i];
            if (byte == '\n') { status_line(job); job->status_used = 0; }
            else if (byte < 32 || byte == 127 || job->status_used + 1 >= sizeof job->status_line) {
                job->failed = 1; break;
            } else job->status_line[job->status_used++] = (char)byte;
        }
    }
    config_wipe(bytes, sizeof bytes);
}

static void read_output(Credential *job)
{
    for (int attempt = 0; attempt < 8 && job->output >= 0 && !job->failed; ++attempt) {
        ssize_t count = read(job->output, job->plaintext + job->used, sizeof job->plaintext - job->used);
        if (!count) { close(job->output); job->output = -1; break; }
        if (count < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) job->failed = 1;
            break;
        }
        job->used += (size_t)count;
        if (job->used == sizeof job->plaintext) { job->failed = 1; break; }
    }
}

/* A final poll result does not dispose of the job.
 * The owner must copy only the bounded success value, wipe temporary
 * plaintext, cancel the job, then continue later child reaping. */
int credential_poll(Credential *job, int64_t now, char *out, size_t cap, char *error, size_t error_cap)
{
    if (out && cap) config_wipe(out, cap);
    if (!job || !out || !cap) return failure(error, error_cap, "Invalid GPG credential request.");
    if (now < job->began || now - job->began >= CREDENTIAL_TIMEOUT_MS)
        return failure(error, error_cap, "GPG decryption timed out; unlock the key outside the client, then reconnect.");
    read_output(job);
    read_status(job);
    /* Keep the direct child unreaped while either pipe is still open. A
     * descendant may inherit a writer after the helper exits; its zombie
     * leader pins the process-group ID until cancellation can safely kill
     * that group. Reaping early would lose that ownership and strand the
     * writer on a later timeout. */
    if (!job->exited && job->output < 0 && job->status < 0) {
        pid_t pid = children[job->child].pid;
        pid_t found = waitpid(pid, &job->exit_status, WNOHANG);
        if (found == pid) {
            children[job->child] = (Child){0, 0};
            job->exited = 1;
        } else if (found < 0 && errno != EINTR) {
            job->failed = 1;
            if (errno == ECHILD) {
                children[job->child] = (Child){0, 0};
                job->exited = 1;
            }
        }
    }
    if (job->failed || (job->exited && (!WIFEXITED(job->exit_status) || WEXITSTATUS(job->exit_status))))
        return failure(error, error_cap, "GPG decryption failed; unlock the key outside the client, then reconnect.");
    if (!job->exited || job->output >= 0 || job->status >= 0) return 0;
    if (!job->okay || !job->ended || !job->information || !job->protected_data)
        return failure(error, error_cap, "GPG credential lacks successful integrity-checked decryption; nothing was sent.");
    size_t length = job->used;
    if (length && job->plaintext[length - 1] == '\n') {
        --length;
        if (length && job->plaintext[length - 1] == '\r') --length;
    }
    if (!length || length > CONFIG_SECRET_MAX || length >= cap)
        return failure(error, error_cap, "Decrypted credential is empty or exceeds its byte limit.");
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)job->plaintext[i] < 32 || (unsigned char)job->plaintext[i] == 127)
            return failure(error, error_cap, "Decrypted credential must be one line without control bytes.");
    memcpy(out, job->plaintext, length);
    out[length] = 0;
    config_wipe(job->plaintext, sizeof job->plaintext);
    return 1;
}

/* Cancellation releases the UI-visible job and
 * marks its child for later reaping. A descendant may still hold a pipe,
 * so interrupt its owned process group before closing local descriptors. */
void credential_cancel(Credential *job)
{
    if (!job) return;
    if (!job->exited) {
        Child *child = &children[job->child];
        if (child->pid > 0) {
            (void)kill(-child->pid, SIGKILL);
            (void)kill(child->pid, SIGKILL);
            child->retired = 1;
        }
    }
    if (job->output >= 0) close(job->output);
    if (job->status >= 0) close(job->status);
    config_wipe(job, sizeof *job);
    free(job);
    credential_reap();
}

/* Encryption result and publication state remain together through Save
 * retries. published is independent of a publish call's success result: the
 * link may exist even when directory synchronization subsequently fails. */
struct CredentialCreate {
    size_t child;
    int directory, input, input_guard, output, status;
    int exited, exit_status, failed, ready, began_encryption, ended_encryption;
    int invalid_recipient, published;
    int64_t began;
    size_t secret_length, written, used, status_used, status_total;
    struct stat identity;
    char path[CONFIG_PATH_MAX], leaf[256], temporary[96];
    char fingerprint[CREDENTIAL_FINGERPRINT_MAX + 1];
    char secret[CONFIG_SECRET_MAX + 1], status_line[1024];
    unsigned char ciphertext[CREDENTIAL_CIPHERTEXT_MAX + 1];
};

int credential_fingerprint_valid(const char *fingerprint)
{
    if (!fingerprint) return 0;
    size_t length = strnlen(fingerprint, CREDENTIAL_FINGERPRINT_MAX + 1u);
    if (length != 40 && length != 64) return 0;
    for (size_t i = 0; i < length; ++i)
        if (!((fingerprint[i] >= '0' && fingerprint[i] <= '9') ||
              (fingerprint[i] >= 'a' && fingerprint[i] <= 'f') ||
              (fingerprint[i] >= 'A' && fingerprint[i] <= 'F'))) return 0;
    return 1;
}

int credential_absolute_path(const char *path, char *out, size_t out_cap,
                             char *error, size_t cap)
{
    char joined[CONFIG_PATH_MAX], normalized[CONFIG_PATH_MAX];
    if (error && cap) *error = 0;
    if (!out || !out_cap) return failure(error, cap, "No space for the credential path.");
    size_t length = path ? strnlen(path, sizeof joined) : 0;
    if (!length || length >= sizeof joined)
        return failure(error, cap, "Enter a credential file path shorter than 4096 bytes.");
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)path[i] < 32 || (unsigned char)path[i] == 127)
            return failure(error, cap, "Credential paths cannot contain control characters.");
    const char *leaf = strrchr(path, '/');
    leaf = leaf ? leaf + 1 : path;
    if (!*leaf || !strcmp(leaf, ".") || !strcmp(leaf, "..") || strlen(leaf) >= 256)
        return path_failure(error, cap, path, "Enter a filename, not a directory (at most 255 bytes).");
    if (*path == '/') memcpy(joined, path, length + 1u);
    else {
        if (!getcwd(joined, sizeof joined))
            return failure(error, cap, "Cannot determine the absolute credential location: %s.", strerror(errno));
        size_t used = strlen(joined);
        if (used + 1u + length >= sizeof joined)
            return failure(error, cap, "The absolute credential path is too long.");
        joined[used++] = '/';
        memcpy(joined + used, path, length + 1u);
    }
    char *save = NULL;
    size_t used = 0;
    for (char *part = strtok_r(joined, "/", &save); part; part = strtok_r(NULL, "/", &save)) {
        if (!strcmp(part, ".")) continue;
        if (!strcmp(part, ".."))
            return path_failure(error, cap, path, "Parent traversal (..) is not allowed in credential paths.");
        size_t count = strlen(part);
        normalized[used++] = '/';
        memcpy(normalized + used, part, count); used += count;
    }
    normalized[used] = 0;
    if (!used || used >= out_cap)
        return failure(error, cap, "The absolute credential path does not fit this field.");
    memcpy(out, normalized, used + 1u);
    return 1;
}

/* path is already absolute and normalized. Return -2 only for a missing
 * component during a read-only suggestion. No existing mode is changed. */
static int directory_open(const char *path, int create, int allow_missing,
                          char *error, size_t cap)
{
    char copy[CONFIG_PATH_MAX], walked[CONFIG_PATH_MAX], *save = NULL;
    memcpy(copy, path, strlen(path) + 1u);
    size_t used = 0;
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return path_failure(error, cap, "/", "Cannot open credential directory: %s.", strerror(errno));
    for (char *part = strtok_r(copy, "/", &save); part; part = strtok_r(NULL, "/", &save)) {
        size_t length = strlen(part);
        walked[used++] = '/'; memcpy(walked + used, part, length + 1u); used += length;
        int made = 0;
        int next = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0 && errno == ENOENT && create) {
            if (mkdirat(fd, part, 0700) == 0) made = 1;
            else if (errno != EEXIST) {
                int saved = errno; close(fd);
                return path_failure(error, cap, walked, "Cannot create private credential directory: %s.", strerror(saved));
            }
            next = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        }
        if (next < 0) {
            int saved = errno;
            struct stat st;
            int link = !fstatat(fd, part, &st, AT_SYMLINK_NOFOLLOW) && S_ISLNK(st.st_mode);
            close(fd);
            if (link) return path_failure(error, cap, walked, "Symlink credential directories are refused.");
            if (allow_missing && saved == ENOENT) return -2;
            if (saved == ENOENT) return path_failure(error, cap, walked, "Credential directory does not exist; choose the offered default or an existing private directory.");
            if (saved == ENOTDIR) return path_failure(error, cap, walked, "This credential path component is not a directory.");
            return path_failure(error, cap, walked, "Cannot open credential directory: %s.", strerror(saved));
        }
        close(fd); fd = next;
        if (made) {
            struct stat st;
            if (fstat(fd, &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() ||
                (st.st_mode & 0777) != 0700) {
                close(fd);
                return path_failure(error, cap, walked, "New credential directory must be owned by you with mode 0700.");
            }
        }
    }
    return fd;
}

static int directory_private(int fd, const char *path, char *error, size_t cap)
{
    struct stat st;
    if (fstat(fd, &st) < 0)
        return path_failure(error, cap, path, "Cannot inspect credential directory: %s.", strerror(errno));
    if (!S_ISDIR(st.st_mode)) return path_failure(error, cap, path, "Credential parent is not a directory.");
    if (st.st_uid != getuid())
        return path_failure(error, cap, path, "Credential directory is owned by UID %ju; your UID is %ju. Choose a directory you own.",
                            (uintmax_t)st.st_uid, (uintmax_t)getuid());
    if ((st.st_mode & 077) || (st.st_mode & 0700) != 0700)
        return path_failure(error, cap, path, "Credential directory has mode %04o; it must be private and writable (mode 0700). Choose the offered default or correct this directory.",
                            (unsigned)(st.st_mode & 07777));
    return 1;
}

static void split_path(char path[CONFIG_PATH_MAX], char leaf[256])
{
    char *last = strrchr(path, '/');
    memcpy(leaf, last + 1, strlen(last + 1) + 1u);
    if (last == path) last[1] = 0;
    else *last = 0;
}

/* Keeping this descriptor pins the checked directory throughout encryption. */
static int create_directory(const char *path, char leaf[256], char *error, size_t cap)
{
    char directory[CONFIG_PATH_MAX];
    if (credential_absolute_path(path, directory, sizeof directory, error, cap) < 0) return -1;
    split_path(directory, leaf);
    int fd = directory_open(directory, 0, 0, error, cap);
    if (fd < 0) return -1;
    if (directory_private(fd, directory, error, cap) < 0) { close(fd); return -1; }
    return fd;
}

static int destination_free(int directory, const char *leaf, const char *path,
                            char *error, size_t cap)
{
    struct stat st;
    if (fstatat(directory, leaf, &st, AT_SYMLINK_NOFOLLOW) == 0)
        return path_failure(error, cap, path, "Credential path already exists; choose a new filename. Existing files and symlinks are never replaced.");
    if (errno != ENOENT)
        return path_failure(error, cap, path, "Cannot check the credential destination: %s.", strerror(errno));
    return 1;
}

int credential_create_check(const char *path, char *error, size_t cap)
{
    char absolute[CONFIG_PATH_MAX], leaf[256], program[CONFIG_PATH_MAX];
    if (credential_absolute_path(path, absolute, sizeof absolute, error, cap) < 0) return -1;
    int fd = create_directory(absolute, leaf, error, cap);
    if (fd < 0) return -1;
    int result = destination_free(fd, leaf, absolute, error, cap);
    close(fd);
    if (result < 0) return -1;
    if (!find_gpg(program)) return failure(error, cap, "Cannot find the gpg executable in PATH.");
    return 1;
}

static int default_directory(const char *config_path, char out[CONFIG_PATH_MAX],
                             char *error, size_t cap)
{
    char leaf[256];
    if (credential_absolute_path(config_path, out, CONFIG_PATH_MAX, error, cap) < 0) return -1;
    split_path(out, leaf);
    size_t length = strlen(out);
    if (length == 1) length = 0;
    if (length + sizeof "/credentials" > CONFIG_PATH_MAX)
        return failure(error, cap, "The default credential directory path is too long.");
    memcpy(out + length, "/credentials", sizeof "/credentials");
    return 1;
}

#define CREDENTIAL_DEFAULT_CHOICES 64
static void default_leaf(int server, unsigned choice, char out[64])
{
    const char *base = server ? "server-password" : "account-password";
    if (choice == 1) (void)snprintf(out, 64, "%s.gpg", base);
    else (void)snprintf(out, 64, "%s-%u.gpg", base, choice);
}

int credential_default_path(const char *config_path, int server_password,
                            char *out, size_t out_cap, char *error, size_t cap)
{
    char directory[CONFIG_PATH_MAX], leaf[64];
    if (error && cap) *error = 0;
    if (!out || !out_cap) return failure(error, cap, "No space for the default credential path.");
    if (default_directory(config_path, directory, error, cap) < 0) return -1;
    int fd = directory_open(directory, 0, 1, error, cap);
    if (fd == -1) return -1;
    if (fd >= 0 && directory_private(fd, directory, error, cap) < 0) { close(fd); return -1; }
    int result = -1;
    for (unsigned choice = 1; choice <= CREDENTIAL_DEFAULT_CHOICES; ++choice) {
        default_leaf(server_password, choice, leaf);
        struct stat st;
        if (fd >= 0 && fstatat(fd, leaf, &st, AT_SYMLINK_NOFOLLOW) == 0) continue;
        if (fd >= 0 && errno != ENOENT) {
            result = path_failure(error, cap, directory, "Cannot check default credential names: %s.", strerror(errno));
            break;
        }
        size_t length = strlen(directory), count = strlen(leaf);
        if (length + 1u + count >= out_cap || length + 1u + count >= CONFIG_PATH_MAX) {
            result = failure(error, cap, "The default credential path is too long."); break;
        }
        memcpy(out, directory, length); out[length++] = '/';
        memcpy(out + length, leaf, count + 1u);
        result = 1; break;
    }
    if (fd >= 0) close(fd);
    if (result < 0 && (!error || !cap || !*error))
        return path_failure(error, cap, directory, "All 64 default credential filenames exist; choose a new custom filename.");
    return result;
}

int credential_default_prepare(const char *config_path, const char *destination,
                               char *error, size_t cap)
{
    char directory[CONFIG_PATH_MAX], parent[CONFIG_PATH_MAX], absolute[CONFIG_PATH_MAX], leaf[256], candidate[64];
    if (default_directory(config_path, directory, error, cap) < 0 ||
        credential_absolute_path(destination, absolute, sizeof absolute, error, cap) < 0) return -1;
    memcpy(parent, absolute, strlen(absolute) + 1u); split_path(parent, leaf);
    int managed = 0;
    for (int server = 0; server < 2 && !managed; ++server)
        for (unsigned choice = 1; choice <= CREDENTIAL_DEFAULT_CHOICES && !managed; ++choice) {
            default_leaf(server, choice, candidate);
            managed = !strcmp(leaf, candidate);
        }
    if (strcmp(parent, directory) || !managed)
        return path_failure(error, cap, absolute, "Only the offered default credential directory can be created automatically.");
    int fd = directory_open(directory, 1, 0, error, cap);
    if (fd < 0) return -1;
    int result = directory_private(fd, directory, error, cap);
    if (result > 0) result = destination_free(fd, leaf, absolute, error, cap);
    close(fd);
    return result;
}

static void execute_encrypt(const char *program, int input, int output, int status,
                            int quiet, const char *fingerprint)
{
    isolate_gpg(input, output, status, quiet);
    char *const arguments[] = {
        (char *)"gpg", (char *)"--no-options", (char *)"--batch", (char *)"--no-tty",
        (char *)"--pinentry-mode", (char *)"error", (char *)"--no-autostart",
        (char *)"--no-auto-key-retrieve", (char *)"--no-auto-key-import",
        (char *)"--no-auto-key-locate", (char *)"--disable-dirmngr",
        (char *)"--no-auto-check-trustdb", (char *)"--trust-model", (char *)"always",
        (char *)"--compress-algo", (char *)"none", (char *)"--cipher-algo", (char *)"AES256",
        (char *)"--status-fd", (char *)"3", (char *)"--recipient", (char *)fingerprint,
        (char *)"--output", (char *)"-", (char *)"--encrypt", NULL
    };
    execve(program, arguments, environ);
    _exit(127);
}

CredentialCreate *credential_create_start(const char *path, const char *fingerprint,
                                           const char *secret, int64_t now,
                                           char *error, size_t cap)
{
    if (!credential_fingerprint_valid(fingerprint)) {
        failure(error, cap, "Use a full 40- or 64-digit hexadecimal fingerprint of a local GPG key.");
        return NULL;
    }
    size_t length = secret ? strnlen(secret, CONFIG_SECRET_MAX + 1u) : 0;
    if (!length || length > CONFIG_SECRET_MAX) {
        failure(error, cap, "The credential must contain 1 to 1024 bytes."); return NULL;
    }
    for (size_t i = 0; i < length; ++i) if ((unsigned char)secret[i] < 32 || (unsigned char)secret[i] == 127) {
        failure(error, cap, "The credential must be one line without control bytes."); return NULL;
    }
    if (child_ownership_safe(error, cap) < 0) return NULL;
    credential_reap();
    size_t slot;
    for (slot = 0; slot < CREDENTIAL_JOBS && children[slot].pid; ++slot) { }
    char program[CONFIG_PATH_MAX], absolute[CONFIG_PATH_MAX];
    if (slot == CREDENTIAL_JOBS) {
        failure(error, cap, "GPG cleanup is still pending; retry shortly.");
        return NULL;
    }
    if (!find_gpg(program)) {
        failure(error, cap, "Cannot find the gpg executable in PATH.");
        return NULL;
    }
    if (credential_absolute_path(path, absolute, sizeof absolute, error, cap) < 0) return NULL;
    CredentialCreate *job = calloc(1, sizeof *job);
    if (!job) {
        failure(error, cap, "Cannot allocate GPG encryption state.");
        return NULL;
    }
    job->directory = job->input = job->input_guard = job->output = job->status = -1;
    job->exited = 1;
    job->directory = create_directory(absolute, job->leaf, error, cap);
    if (job->directory < 0) {
        credential_create_cancel(job);
        return NULL;
    }
    if (destination_free(job->directory, job->leaf, absolute, error, cap) < 0) {
        credential_create_cancel(job);
        return NULL;
    }
    memcpy(job->path, absolute, strlen(absolute) + 1u);
    memcpy(job->fingerprint, fingerprint, strlen(fingerprint) + 1u);
    memcpy(job->secret, secret, length + 1u);
    job->secret_length = length;
    int input[2] = {-1, -1}, output[2] = {-1, -1}, status[2] = {-1, -1};
    int quiet = open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (quiet < 0 || pipe(input) < 0 || pipe(output) < 0 || pipe(status) < 0 ||
        !flags(input[0], 0) || !flags(input[1], 1) ||
        !flags(output[0], 1) || !flags(output[1], 0) ||
        !flags(status[0], 1) || !flags(status[1], 0)) {
        failure(error, cap, "Cannot create isolated GPG encryption pipes.");
        goto failed;
    }
    pid_t pid = fork();
    if (pid < 0) {
        failure(error, cap, "Cannot start GPG encryption.");
        goto failed;
    }
    if (!pid) execute_encrypt(program, input[0], output[1], status[1], quiet, job->fingerprint);
    (void)setpgid(pid, pid);
    children[slot] = (Child){pid, 0};
    job->child = slot;
    job->exited = 0;
    job->began = now;
    job->input = input[1];
    input[1] = -1;
    /* Keep a private reader until the short secret has been written. This
     * prevents SIGPIPE without changing the caller's signal disposition. */
    job->input_guard = input[0];
    input[0] = -1;
    job->output = output[0];
    output[0] = -1;
    job->status = status[0];
    status[0] = -1;
    goto done;
failed:
    credential_create_cancel(job);
    job = NULL;
done:
    if (quiet >= 0) close(quiet);
    for (int i = 0; i < 2; ++i) {
        if (input[i] >= 0) close(input[i]);
        if (output[i] >= 0) close(output[i]);
        if (status[i] >= 0) close(status[i]);
    }
    return job;
}

static void encryption_status(CredentialCreate *job)
{
    char *line = job->status_line;
    line[job->status_used] = 0;
    if (strncmp(line, "[GNUPG:] ", 9)) { job->failed = 1; return; }
    line += 9;
    if (!strncmp(line, "BEGIN_ENCRYPTION ", 17)) {
        unsigned long numbers[3] = {0, 0, 0};
        size_t count = 0;
        char *next = line + 17;
        while (*next && count < 3) {
            if (*next < '0' || *next > '9') { job->failed = 1; return; }
            char *end;
            errno = 0;
            numbers[count++] = strtoul(next, &end, 10);
            if (errno || numbers[count - 1u] > UINT_MAX || (*end && *end != ' ')) { job->failed = 1; return; }
            next = end;
            while (*next == ' ') ++next;
        }
        if (*next || count < 2 || ++job->began_encryption != 1 || job->ended_encryption ||
            (!numbers[0] && !numbers[2]) || !numbers[1]) job->failed = 1;
    } else if (!strcmp(line, "END_ENCRYPTION")) {
        if (!job->began_encryption || ++job->ended_encryption != 1) job->failed = 1;
    } else if (!strncmp(line, "INV_RECP ", 9) || !strncmp(line, "NO_RECP", 7) || !strncmp(line, "NO_PUBKEY", 9)) {
        job->invalid_recipient = 1; job->failed = 1;
    } else if (!strncmp(line, "FAILURE ", 8) || !strncmp(line, "ERROR ", 6)) job->failed = 1;
}

static void encryption_read(CredentialCreate *job)
{
    for (int attempt = 0; attempt < 8 && job->output >= 0 && !job->failed; ++attempt) {
        ssize_t count = read(job->output, job->ciphertext + job->used, sizeof job->ciphertext - job->used);
        if (!count) { close(job->output); job->output = -1; break; }
        if (count < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) job->failed = 1;
            break;
        }
        job->used += (size_t)count;
        if (job->used == sizeof job->ciphertext) job->failed = 1;
    }
    char bytes[1024];
    for (int attempt = 0; attempt < 8 && job->status >= 0 && !job->failed; ++attempt) {
        ssize_t count = read(job->status, bytes, sizeof bytes);
        if (!count) {
            close(job->status); job->status = -1;
            if (job->status_used) job->failed = 1;
            break;
        }
        if (count < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) job->failed = 1;
            break;
        }
        job->status_total += (size_t)count;
        if (job->status_total > CREDENTIAL_STATUS_MAX) { job->failed = 1; break; }
        for (size_t i = 0; i < (size_t)count; ++i) {
            unsigned char byte = (unsigned char)bytes[i];
            if (byte == '\n') { encryption_status(job); job->status_used = 0; }
            else if (byte < 32 || byte == 127 || job->status_used + 1u >= sizeof job->status_line) {
                job->failed = 1; break;
            } else job->status_line[job->status_used++] = (char)byte;
        }
    }
    config_wipe(bytes, sizeof bytes);
}

/* Encryption writes to a private staging
 * destination and publishes only completed ciphertext. Pending is not
 * success; setup must not reference the new file until ready is reported. */
int credential_create_poll(CredentialCreate *job, int64_t now, char *error, size_t cap)
{
    if (!job) return failure(error, cap, "Invalid GPG encryption request.");
    if (job->ready) return 1;
    if (now < job->began || now - job->began >= CREDENTIAL_TIMEOUT_MS)
        return path_failure(error, cap, job->path, "GPG encryption timed out; this credential file was not published.");
    if (job->input >= 0 && !job->failed) {
        ssize_t count = write(job->input, job->secret + job->written, job->secret_length - job->written);
        if (count < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) job->failed = 1;
        } else job->written += (size_t)count;
        if (job->written == job->secret_length) {
            close(job->input);
            job->input = -1;
            close(job->input_guard);
            job->input_guard = -1;
            config_wipe(job->secret, sizeof job->secret);
        }
    }
    encryption_read(job);
    /* As with decryption, an inherited pipe writer must not outlive the
     * process-group ownership anchor needed by timeout/cancel cleanup. */
    if (!job->exited && job->output < 0 && job->status < 0) {
        pid_t pid = children[job->child].pid;
        pid_t found = waitpid(pid, &job->exit_status, WNOHANG);
        if (found == pid) {
            children[job->child] = (Child){0, 0};
            job->exited = 1;
        }
        else if (found < 0 && errno != EINTR) {
            job->failed = 1;
            if (errno == ECHILD) {
                children[job->child] = (Child){0, 0};
                job->exited = 1;
            }
        }
    }
    if (job->invalid_recipient)
        return path_failure(error, cap, job->path, "The selected local GPG key is missing, expired, revoked, or unusable for encryption.");
    if (job->failed || (job->exited && (!WIFEXITED(job->exit_status) || WEXITSTATUS(job->exit_status))))
        return path_failure(error, cap, job->path, "GPG encryption failed; check the local key and GPG installation. This credential file was not published.");
    if (!job->exited || job->output >= 0 || job->status >= 0) return 0;
    if (job->written != job->secret_length || !job->used || !job->began_encryption || !job->ended_encryption)
        return path_failure(error, cap, job->path, "GPG did not confirm complete protected encryption; this credential file was not published.");
    job->ready = 1;
    return 1;
}

/* Check both inode ownership and bytes before a retry or rollback. A replaced
 * or edited destination must never be removed as if it were our own output. */
static int created_file_matches(const CredentialCreate *job, const char *name)
{
    int fd = openat(job->directory, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return 0;
    struct stat st;
    int okay = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == getuid() &&
               st.st_dev == job->identity.st_dev && st.st_ino == job->identity.st_ino &&
               (st.st_mode & 07777) == (job->identity.st_mode & 07777) &&
               st.st_size >= 0 && (uintmax_t)st.st_size == job->used;
    unsigned char bytes[4096];
    size_t offset = 0;
    while (okay && offset < job->used) {
        size_t length = job->used - offset;
        if (length > sizeof bytes) length = sizeof bytes;
        ssize_t count = read(fd, bytes, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0 || memcmp(bytes, job->ciphertext + offset, (size_t)count)) okay = 0;
        else offset += (size_t)count;
    }
    config_wipe(bytes, sizeof bytes);
    close(fd);
    return okay;
}

int credential_create_publish(CredentialCreate *job, char *error, size_t cap)
{
    if (!job || !job->ready) return failure(error, cap, "GPG encryption is not complete.");
    char leaf[256];
    int directory = create_directory(job->path, leaf, error, cap);
    if (directory < 0) return -1;
    struct stat current, held;
    int same = !fstat(directory, &current) && !fstat(job->directory, &held) &&
               current.st_dev == held.st_dev && current.st_ino == held.st_ino && !strcmp(leaf, job->leaf);
    close(directory);
    if (!same) return path_failure(error, cap, job->path, "The credential directory changed; cancel and reopen the panel.");
    if (job->published) {
        if (!created_file_matches(job, job->leaf))
            return path_failure(error, cap, job->path, "The newly created credential was changed or replaced; it was left untouched.");
        if (fsync(job->directory) < 0)
            return path_failure(error, cap, job->path, "The encrypted credential exists, but its directory could not be synchronized; retry Finish.");
        return 1;
    }
    static unsigned sequence;
    int fd = -1;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        (void)snprintf(job->temporary, sizeof job->temporary, ".irc-credential-%ld-%u", (long)getpid(), ++sequence);
        fd = openat(job->directory, job->temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) {
        job->temporary[0] = 0;
        return path_failure(error, cap, job->path, "Cannot create a private encrypted credential file: %s.", strerror(errno));
    }
    int okay = fchmod(fd, 0600) == 0;
    size_t offset = 0;
    while (okay && offset < job->used) {
        ssize_t count = write(fd, job->ciphertext + offset, job->used - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) okay = 0;
        else offset += (size_t)count;
    }
    if (okay) okay = fsync(fd) == 0 && fstat(fd, &job->identity) == 0;
    if (close(fd) < 0) okay = 0;
    if (!okay) {
        (void)unlinkat(job->directory, job->temporary, 0);
        job->temporary[0] = 0;
        return path_failure(error, cap, job->path, "Cannot write and synchronize this encrypted credential; this file was not published.");
    }
    if (linkat(job->directory, job->temporary, job->directory, job->leaf, 0) < 0) {
        int saved = errno;
        (void)unlinkat(job->directory, job->temporary, 0);
        job->temporary[0] = 0;
        return path_failure(error, cap, job->path, "Cannot publish this credential: %s. No existing file was replaced.", strerror(saved));
    }
    job->published = 1;
    if (unlinkat(job->directory, job->temporary, 0) == 0) job->temporary[0] = 0;
    if (fsync(job->directory) < 0)
        return path_failure(error, cap, job->path, "The encrypted credential exists, but its directory could not be synchronized; retry Finish.");
    return 1;
}

int credential_create_published(const CredentialCreate *job)
{
    return job && job->published;
}

/* Cleanup never trusts a pathname alone. Removing a published or temporary
 * file requires the held inode and ciphertext to match this job's output;
 * finish retains a published file, while cancel rolls back only that output. */
static void create_release(CredentialCreate *job, int keep)
{
    if (!job) return;
    if (!job->exited) {
        Child *child = &children[job->child];
        if (child->pid > 0) {
            (void)kill(-child->pid, SIGKILL);
            (void)kill(child->pid, SIGKILL);
            child->retired = 1;
        }
    }
    if (job->input >= 0) close(job->input);
    if (job->input_guard >= 0) close(job->input_guard);
    if (job->output >= 0) close(job->output);
    if (job->status >= 0) close(job->status);
    if (job->directory >= 0) {
        if (*job->temporary && created_file_matches(job, job->temporary))
            (void)unlinkat(job->directory, job->temporary, 0);
        if (!keep && job->published && created_file_matches(job, job->leaf))
            (void)unlinkat(job->directory, job->leaf, 0);
        close(job->directory);
    }
    config_wipe(job, sizeof *job);
    free(job);
    credential_reap();
}

void credential_create_finish(CredentialCreate *job)
{
    create_release(job, 1);
}
void credential_create_cancel(CredentialCreate *job)
{
    create_release(job, 0);
}
