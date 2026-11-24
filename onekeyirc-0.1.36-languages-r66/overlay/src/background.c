/* Bounded local worker/controller transport.
 *
 * Worker half: registry/lease ownership, prepare/detach, bounded candidate
 * handshakes, and terminal events. Foreground half (after background_free):
 * discover/query a worker, pass terminal descriptors, and wait for control
 * replies. Numbered detach/close commands share that verified discovery.
 * The wire carries terminal/control state, never IRC commands or a serialized
 * Client. See background.h for frontend ordering and ownership contracts.
 */
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
/* Peer credentials, file locks and SIGWINCH are BSD extensions here. */
#define _BSD_SOURCE 1
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "background.h"
#include "l10n.h"
#include "credentials.h"
#include "net.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define BG_HANDSHAKE_MS 4000
#define BG_TERM_SIZE 128
#define BG_LOCALE_SIZE 128
#define BG_CONTROL_BATCH 32
#define BG_JOB_MAX 64
#define BG_LIST_MS 1500
#define BG_REGISTRY_LOCK_MS 250
#define BG_PATH_SIZE sizeof(((struct sockaddr_un *)0)->sun_path)
#ifdef ONEKEY_ONLY
#define BG_PROGRAM "onekeyirc"
#define BG_WIRE_PROGRAM "1keyirc"
#else
#define BG_PROGRAM "vipirc"
#define BG_WIRE_PROGRAM "vipirc"
#endif

typedef struct AttachRequest {
    char magic[8], term[BG_TERM_SIZE], locale[BG_LOCALE_SIZE];
    unsigned char token[16];
    unsigned char frontend;
    unsigned char reserved[7];
} AttachRequest;
/* The v1 reply identifies the standalone protocol with its original opaque
 * ID, "1keyirc". Keep the eight-byte field and packet layout stable for older
 * peers; the executable, registry and displayed name use "onekeyirc".
 * Q2 uses the same layout with S2/A2 magic for detached/attached state.
 * Q2 and C1 requests occupy a zero-filled AttachRequest with no descriptors;
 * C1 additionally carries the queried worker's token. */
typedef struct SessionReply {
    char magic[8], program[8];
    uint32_t job;
    unsigned char token[16];
    char label[192];
} SessionReply;

struct Background {
    int listener, directory_fd, controller, candidate, registry_fd, lease;
    int candidate_tty[3], candidate_count;
    size_t candidate_used;
    AttachRequest request;
    int64_t candidate_deadline;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    char directory[sizeof(((struct sockaddr_un *)0)->sun_path)];
    dev_t directory_device;
    ino_t directory_inode;
    int managed, detached, suspended, signal, frontend, closing, detaching;
    char program[16];
    unsigned job;
    unsigned char token[16];
    char job_name[24], label[192];
};

_Static_assert(sizeof BG_PROGRAM <= sizeof(((Background *)0)->program),
               "Background display name must fit without truncation");
_Static_assert(sizeof("onekeyirc") <= sizeof(((Background *)0)->program),
               "Shared background state must hold the standalone display name");
_Static_assert(sizeof BG_WIRE_PROGRAM <= sizeof(((SessionReply *)0)->program),
               "Background v1 protocol identifier must fit its stable field");

/* These are presentation-only status strings. Localize before storing the
 * error, not a path, credential, job ID, TERM value or worker protocol token.
 * Never use translated text as a printf format string. */
static int error_out(char *error, size_t cap, const char *message)
{
    if (error && cap) (void)snprintf(error, cap, "%s", irc_tr(message));
    return 0;
}

static void bg_stderr(const char *english)
{
    fputs(BG_PROGRAM ": ", stderr);
    fputs(irc_tr(english), stderr);
    fputc('\n', stderr);
}


/* Named placeholders are expanded as data, not printf directives. Validate
 * every translated template before substitution and preserve English as the
 * fallback if a catalog entry is malformed or unsupported. */
typedef struct BgField { const char *name; const char *value; } BgField;
static int bg_valid_template(const char *s, const BgField *fields, size_t n)
{
    unsigned used[4] = {0, 0, 0, 0};
    if (!s || n > 4) return 0;
    for (const char *p = s; *p;) {
        if (*p == '}') return 0;
        if (*p != '{') { ++p; continue; }
        const char *end = strchr(p, '}');
        if (!end) return 0;
        size_t i;
        for (i = 0; i < n; ++i)
            if ((size_t)(end - p - 1) == strlen(fields[i].name) &&
                !memcmp(p + 1, fields[i].name, (size_t)(end - p - 1))) break;
        if (i == n || ++used[i] != 1) return 0;
        p = end + 1;
    }
    for (size_t i = 0; i < n; ++i) if (used[i] != 1) return 0;
    return 1;
}

static size_t bg_render_fields(char *out, size_t cap, const char *english,
                               const BgField *fields, size_t n)
{
    const char *s = irc_tr(english);
    if (!bg_valid_template(s, fields, n)) s = english;
    if (!out || !cap || !bg_valid_template(s, fields, n)) return 0;
    size_t used = 0;
    for (const char *p = s; *p;) {
        const char *value;
        size_t len;
        if (*p == '{') {
            const char *end = strchr(p, '}');
            size_t i;
            for (i = 0; i < n; ++i)
                if ((size_t)(end - p - 1) == strlen(fields[i].name) &&
                    !memcmp(p + 1, fields[i].name, (size_t)(end - p - 1))) break;
            value = fields[i].value;
            len = strlen(value);
            p = end + 1;
        } else {
            value = p;
            len = 1;
            ++p;
        }
        if (len >= cap - used) return 0;
        memcpy(out + used, value, len);
        used += len;
    }
    out[used] = '\0';
    return used;
}

static void bg_stdout_fields(const char *english, const BgField *fields, size_t n)
{
    char display[768];
    if (bg_render_fields(display, sizeof display, english, fields, n))
        fputs(display, stdout);
}

static void bg_job_notice(const char *english, unsigned number, int resume)
{
    char number_text[24];
    (void)snprintf(number_text, sizeof number_text, "%u", number);
    const BgField fields[] = {{"job", number_text}};
    fputs(BG_PROGRAM " ", stdout);
    bg_stdout_fields(english, fields, 1);
    if (resume) printf(" " BG_PROGRAM " --resume %u\n", number);
    else fputc('\n', stdout);
}

static int fd_flags(int fd, int nonblock)
{
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) return 0;
    if (!nonblock) return 1;
    flags = fcntl(fd, F_GETFL);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

static int same_owner(int fd)
{
#if defined(__linux__)
    struct ucred peer;
    socklen_t size = sizeof peer;
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 &&
           size == sizeof peer && peer.uid == getuid();
#elif defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
      defined(__DragonFly__) || defined(__APPLE__)
    uid_t uid;
    gid_t gid;
    return getpeereid(fd, &uid, &gid) == 0 && uid == getuid();
#else
    /* The socket and its enclosing directory are owner-only. Platforms
     * without a peer-credential API retain filesystem access enforcement. */
    (void)fd;
    return 1;
#endif
}

static int notice(int fd, unsigned char kind, unsigned char detail)
{
    unsigned char bytes[2] = {kind, detail};
    size_t sent = 0;
    if (fd < 0) return 0;
    /* Every control descriptor is nonblocking. Complete the fixed two-byte
     * reply with a bounded number of attempts, or let the caller close the
     * connection. A partial acknowledgment must never strand the waiter. */
    for (int tries = 0; tries < 4 && sent < sizeof bytes; ++tries) {
        ssize_t wrote = send(fd, bytes + sent, sizeof bytes - sent, 0);
        if (wrote > 0) sent += (size_t)wrote;
        else if (wrote < 0 && errno == EINTR) continue;
        else return 0;
    }
    return sent == sizeof bytes;
}

/* A candidate owns every received SCM_RIGHTS descriptor until terminal
 * installation duplicates it. Rejection, timeout and successful handoff all
 * use this cleanup; clearing only the request bytes would leak descriptors. */
static void reset_candidate(Background *b)
{
    if (b->candidate >= 0) close(b->candidate);
    b->candidate = -1;
    for (size_t i = 0; i < 3; ++i) {
        if (b->candidate_tty[i] >= 0) close(b->candidate_tty[i]);
        b->candidate_tty[i] = -1;
    }
    b->candidate_count = 0;
    b->candidate_used = 0;
    b->candidate_deadline = 0;
    memset(&b->request, 0, sizeof b->request);
}

Background *background_create(void)
{
    Background *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->listener = b->directory_fd = b->controller = b->candidate = -1;
    b->registry_fd = b->lease = -1;
    for (size_t i = 0; i < 3; ++i) b->candidate_tty[i] = -1;
    (void)snprintf(b->program, sizeof b->program, "%s", BG_PROGRAM);
    return b;
}

void background_set_program(Background *b, const char *program)
{
    if (b && !b->managed)
        (void)snprintf(b->program, sizeof b->program, "%s",
                       program && !strcmp(program, "onekeyirc") ? "onekeyirc" : "vipirc");
}

static void remove_endpoint(Background *b)
{
    if (b->listener >= 0) close(b->listener);
    b->listener = -1;
    if (b->directory_fd >= 0) {
        (void)unlinkat(b->directory_fd, "session", 0);
        if (b->lease >= 0) (void)unlinkat(b->directory_fd, "lease", 0);
        struct stat now;
        if (b->registry_fd >= 0 &&
            fstatat(b->registry_fd, b->job_name, &now, AT_SYMLINK_NOFOLLOW) == 0 &&
            S_ISDIR(now.st_mode) &&
            now.st_uid == getuid() && now.st_dev == b->directory_device &&
            now.st_ino == b->directory_inode)
            (void)unlinkat(b->registry_fd, b->job_name, AT_REMOVEDIR);
        close(b->directory_fd);
    }
    if (b->lease >= 0) close(b->lease);
    if (b->registry_fd >= 0) close(b->registry_fd);
    b->lease = b->registry_fd = -1;
    b->directory_fd = -1;
    b->path[0] = b->directory[0] = '\0';
}

static int owned_regular(int fd)
{
    struct stat st;
    return fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == getuid() &&
           st.st_nlink == 1 && (st.st_mode & 0777) == 0600;
}

static int registry_open(const char *program, int create, char path[BG_PATH_SIZE])
{
    char *base = realpath("/tmp", NULL);
    if (!base) return -1;
    int root = open(base, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    char name[64];
    int n = snprintf(name, sizeof name, "%s-jobs-%lu", program, (unsigned long)getuid());
    int length = snprintf(path, BG_PATH_SIZE, "%s/%s", base, name);
    free(base);
    if (root < 0 || n < 0 || (size_t)n >= sizeof name || length < 0 ||
        (size_t)length + sizeof("/job-64/session") > BG_PATH_SIZE ||
        fstat(root, &st) < 0 || !S_ISDIR(st.st_mode) ||
        ((st.st_mode & 0022) && !(st.st_mode & S_ISVTX))) {
        if (root >= 0) close(root);
        errno = EACCES; return -1;
    }
    if (create && mkdirat(root, name, 0700) < 0 && errno != EEXIST) { close(root); return -1; }
    int fd = openat(root, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    int saved = errno; close(root); errno = saved;
    if (fd < 0) return -1;
    if (fstat(fd, &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() ||
        (st.st_mode & 0777) != 0700) { close(fd); errno = EACCES; return -1; }
    return fd;
}

static int registry_lock(int registry, int create)
{
    int fd = openat(registry, "lock", O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW |
                    (create ? O_CREAT : 0), 0600);
    if (fd < 0) return -1;
    if (!owned_regular(fd)) { close(fd); errno = EACCES; return -1; }
    int64_t deadline = net_now_ms() + BG_REGISTRY_LOCK_MS;
    for (;;) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) return fd;
        if ((errno != EINTR && errno != EWOULDBLOCK && errno != EAGAIN) ||
            net_now_ms() >= deadline) { close(fd); return -1; }
        (void)poll(NULL, 0, 10);
    }
}

static int private_job(int registry, const char *name, struct stat *st)
{
    int fd = openat(registry, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    if (fstat(fd, st) < 0 || !S_ISDIR(st->st_mode) || st->st_uid != getuid() ||
        (st->st_mode & 0777) != 0700) { close(fd); errno = EACCES; return -1; }
    return fd;
}

/* Caller holds the registry lock. A lifetime flock lease crosses fork and
 * disappears on worker death; no process ID is consulted or signalled. */
static int stale_job(int registry, const char *name)
{
    struct stat before, now, socket_stat;
    int directory = private_job(registry, name, &before);
    if (directory < 0) return 0;
    int lease = openat(directory, "lease", O_RDWR | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (lease >= 0 && (!owned_regular(lease) || flock(lease, LOCK_EX | LOCK_NB) < 0)) {
        close(lease); close(directory); return 0;
    }
    if (lease < 0 && errno != ENOENT) { close(directory); return 0; }
    int has_socket = fstatat(directory, "session", &socket_stat, AT_SYMLINK_NOFOLLOW) == 0;
    if ((has_socket && (lease < 0 || !S_ISSOCK(socket_stat.st_mode) ||
         socket_stat.st_uid != getuid() || (socket_stat.st_mode & 0777) != 0600)) ||
        (!has_socket && errno != ENOENT)) goto leave;
    int list_fd = fcntl(directory, F_DUPFD_CLOEXEC, 0);
    if (list_fd < 0) goto leave;
    DIR *entries = fdopendir(list_fd);
    if (!entries) { close(list_fd); goto leave; }
    struct dirent *entry;
    int safe = 1, count = 0;
    errno = 0;
    while ((entry = readdir(entries)) != NULL) {
        if (++count > 4 || (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..") &&
            strcmp(entry->d_name, "session") && strcmp(entry->d_name, "lease"))) { safe = 0; break; }
    }
    if (errno) safe = 0;
    closedir(entries);
    if (!safe || fstatat(registry, name, &now, AT_SYMLINK_NOFOLLOW) < 0 ||
        now.st_dev != before.st_dev || now.st_ino != before.st_ino) goto leave;
    if (has_socket && unlinkat(directory, "session", 0) < 0) goto leave;
    if (lease >= 0 && unlinkat(directory, "lease", 0) < 0) goto leave;
    if (unlinkat(registry, name, AT_REMOVEDIR) == 0) {
        if (lease >= 0) close(lease);
        close(directory); return 1;
    }
leave:
    if (lease >= 0) close(lease);
    close(directory); return 0;
}

static int session_token(unsigned char token[16])
{
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    size_t used = 0;
    while (used < 16) {
        ssize_t got = read(fd, token + used, 16 - used);
        if (got > 0) used += (size_t)got;
        else if (got < 0 && errno == EINTR) continue;
        else break;
    }
    close(fd); return used == 16;
}

/* Allocate registry identity while holding its lock. The worker retains
 * a lease and the original directory identity; cleanup must never remove a
 * replacement path merely because it has the same job number. */
static int create_endpoint(Background *b, int single_session, char *error, size_t cap)
{
    char registry[BG_PATH_SIZE];
    b->registry_fd = registry_open(b->program, 1, registry);
    if (b->registry_fd < 0) return error_out(error, cap, "Cannot open the private background-job registry.");
    int lock = registry_lock(b->registry_fd, 1);
    if (lock < 0) {
        close(b->registry_fd);
        b->registry_fd = -1;
        return error_out(error, cap, "The background-job registry is unavailable or busy; retry.");
    }
    if (single_session) {
        for (unsigned number = 1; number <= BG_JOB_MAX; ++number) {
            char name[24];
            (void)snprintf(name, sizeof name, "job-%u", number);
            (void)stale_job(b->registry_fd, name);
            struct stat existing;
            if (fstatat(b->registry_fd, name, &existing, AT_SYMLINK_NOFOLLOW) == 0) {
                close(lock);
                close(b->registry_fd);
                b->registry_fd = -1;
                return error_out(error, cap, "Single-session mode: another background session exists; resume it instead.");
            }
            if (errno != ENOENT) {
                close(lock);
                close(b->registry_fd);
                b->registry_fd = -1;
                return error_out(error, cap, "Single-session mode cannot verify the background-job registry safely.");
            }
        }
    }
    for (unsigned number = 1; number <= BG_JOB_MAX; ++number) {
        (void)snprintf(b->job_name, sizeof b->job_name, "job-%u", number);
        (void)stale_job(b->registry_fd, b->job_name);
        if (mkdirat(b->registry_fd, b->job_name, 0700) == 0) { b->job = number; break; }
        if (errno != EEXIST) break;
    }
    if (!b->job) goto failed;
    int length = snprintf(b->directory, sizeof b->directory, "%s/%s", registry, b->job_name);
    struct stat st;
    b->directory_fd = private_job(b->registry_fd, b->job_name, &st);
    if (length < 0 || (size_t)length >= sizeof b->directory || b->directory_fd < 0 ||
        st.st_uid != getuid() || (st.st_mode & 0777) != 0700) {
        goto failed;
    }
    b->directory_device = st.st_dev;
    b->directory_inode = st.st_ino;
    b->lease = openat(b->directory_fd, "lease", O_RDWR | O_NONBLOCK | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (b->lease < 0 || !owned_regular(b->lease) || flock(b->lease, LOCK_EX | LOCK_NB) < 0 ||
        !session_token(b->token)) goto failed;
    size_t directory_length = strlen(b->directory);
    if (directory_length + sizeof("/session") > sizeof b->path) goto failed;
    memcpy(b->path, b->directory, directory_length);
    memcpy(b->path + directory_length, "/session", sizeof("/session"));
    b->listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (b->listener < 0 || !fd_flags(b->listener, 1)) goto failed;
    struct sockaddr_un address;
    memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, b->path, strlen(b->path) + 1);
    mode_t old_mask = umask(0077);
    int bound = bind(b->listener, (struct sockaddr *)&address, sizeof address);
    (void)umask(old_mask);
    if (bound < 0 || chmod(b->path, 0600) < 0 || listen(b->listener, 4) < 0) goto failed;
    close(lock);
    return 1;
failed:
    remove_endpoint(b);
    close(lock);
    b->job = 0;
    return error_out(error, cap, "Cannot allocate a protected background job (limit 64); use --sessions.");
}

static int child_ownership_safe(char *error, size_t cap)
{
    struct sigaction action;
    if (sigaction(SIGCHLD, NULL, &action) < 0)
        return error_out(error, cap, "Cannot inspect SIGCHLD before starting the background worker.");
    int automatic = action.sa_handler == SIG_IGN;
#ifdef SA_NOCLDWAIT
    automatic |= (action.sa_flags & SA_NOCLDWAIT) != 0;
#endif
    if (automatic)
        return error_out(error, cap,
            "Background startup requires waitable children; restore default SIGCHLD handling in the launcher.");
    return 1;
}

/* Publish a private worker endpoint before
 * detaching terminal descriptors. This can fail even with an otherwise
 * valid Client; the caller must retain its foreground recovery path. */
int background_prepare(Background *b, const Client *client, char *error, size_t cap)
{
    if (!b) return error_out(error, cap, "Cannot allocate background state.");
    size_t servers = 0;
    const App *selected = client->current < CLIENT_MAX_SERVERS ? client->slots[client->current].app : NULL;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) if (client->slots[i].used) ++servers;
    (void)snprintf(b->label, sizeof b->label, "%.63s @ %.96s (%zu server%s)",
                   selected && *selected->nick ? selected->nick : "(no nick)",
                   selected && *selected->cfg.server ? selected->cfg.server : "disconnected",
                   servers, servers == 1 ? "" : "s");
    if (b->managed) return 1;
    if (!child_ownership_safe(error, cap)) return 0;
    credential_reap();
    if (credential_cleanup_pending())
        return error_out(error, cap, "A credential helper is finishing; retry Background in a moment.");
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        App *a = client->slots[i].app;
        if (a && (a->credential || !net_detach_ready(a->net)))
            return error_out(error, cap, "A connection or credential helper is finishing; retry Background in a moment.");
    }
    const Config *primary = client->slots[0].app ? &client->slots[0].app->cfg : &client->slots[0].seed;
    return b->listener >= 0 || create_endpoint(b, primary->single_session, error, cap);
}

static int quiet_terminal(void)
{
    int quiet = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (quiet < 0) return 0;
    for (int i = 0; i < 3; ++i) if (dup2(quiet, i) < 0) { close(quiet); return 0; }
    if (quiet > STDERR_FILENO) close(quiet);
    /* Discard obsolete presentation only after redirecting it to /dev/null.
     * Flushing the old terminal could wait forever under backpressure, and
     * retaining stdio buffers could replay them on a later attachment. */
    (void)fflush(stdout); (void)fflush(stderr);
    clearerr(stdin);
    clearerr(stdout);
    clearerr(stderr);
    return 1;
}

static void terminal_write_best_effort(const char *text, size_t length)
{
    int flags = fcntl(STDOUT_FILENO, F_GETFL);
    if (flags < 0 || fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) return;
    size_t used = 0;
    /* Terminal cleanup and notices must never delay a completed detach.
     * Drop unwritten presentation after a fixed number of attempts. */
    for (int attempt = 0; attempt < 4 && used < length; ++attempt) {
        ssize_t wrote = write(STDOUT_FILENO, text + used, length - used);
        if (wrote > 0) used += (size_t)wrote;
        else if (wrote < 0 && errno == EINTR) continue;
        else break;
    }
    /* This open file description may also belong to the invoking shell.
     * The first-detach worker has already redirected its standard fds before
     * its parent reaches this helper. Preserve the caller's original mode. */
    if (!(flags & O_NONBLOCK)) {
        for (int attempt = 0; attempt < 4; ++attempt)
            if (fcntl(STDOUT_FILENO, F_SETFL, flags) == 0 || errno != EINTR) break;
    }
}

static int wait_ready(int fd, int timeout, const volatile sig_atomic_t *cancel)
{
    int64_t deadline = net_now_ms() + timeout;
    for (;;) {
        if (cancel && *cancel) return 0;
        int64_t left = deadline - net_now_ms();
        if (left <= 0) return 0;
        struct pollfd p = {fd, POLLIN, 0};
        int result = poll(&p, 1, (int)left);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return 0;
        unsigned char status;
        ssize_t got = read(fd, &status, 1);
        if (got < 0 && errno == EINTR) continue;
        return got == 1 && status == 1;
    }
}

int background_detach(Background *b, const volatile sig_atomic_t *cancel, char *error, size_t cap)
{
    if (!b || b->listener < 0) return error_out(error, cap, "Background mode was not prepared.");
    if (cancel && *cancel) return error_out(error, cap, "Background startup interrupted.");
    if (b->managed) {
        if (!quiet_terminal()) {
            if (b->detaching) {
                (void)notice(b->candidate, 'C', 1);
                b->detaching = 0;
                reset_candidate(b);
            }
            return error_out(error, cap, "Cannot release the terminal for background mode.");
        }
        notice(b->controller, 'D', 0);
        if (b->controller >= 0) close(b->controller);
        b->controller = -1;
        b->detached = 1;
        b->suspended = 0;
        if (b->detaching) {
            (void)notice(b->candidate, 'C', 0);
            b->detaching = 0;
            reset_candidate(b);
        }
        return 1;
    }
    /* Startup failure may need to kill and collect our child. Automatic
     * reaping could release its PID before that cancellation. Check again
     * beside fork, including callers whose preparation predates a policy
     * change; managed re-detach above creates no child and needs no guard. */
    if (!child_ownership_safe(error, cap)) return 0;
    int ready[2];
    if (pipe(ready) < 0) return error_out(error, cap, "Cannot prepare the background process.");
    if (!fd_flags(ready[0], 0) || !fd_flags(ready[1], 0)) {
        close(ready[0]);
        close(ready[1]);
        return error_out(error, cap, "Cannot protect the background process handshake.");
    }
    pid_t child = fork();
    if (child < 0) {
        close(ready[0]);
        close(ready[1]);
        return error_out(error, cap, "Cannot start the background process.");
    }
    if (!child) {
        close(ready[0]);
        unsigned char okay = setsid() >= 0 && quiet_terminal();
        ssize_t sent;
        do { sent = write(ready[1], &okay, 1); } while (sent < 0 && errno == EINTR);
        close(ready[1]);
        if (!okay || sent != 1) _exit(1);
        b->managed = b->detached = 1;
        b->suspended = 0;
        return 1;
    }
    close(ready[1]);
    int okay = wait_ready(ready[0], BG_HANDSHAKE_MS, cancel);
    close(ready[0]);
    if (cancel && *cancel) okay = 0;
    if (!okay) {
        (void)kill(child, SIGKILL);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) { }
        return error_out(error, cap, "The background process could not release this terminal.");
    }
    char message[384];
    int length = snprintf(message, sizeof message,
                          "%s job %u is running in the background.\nResume: %s --resume %u\n"
                          "Use %s --resume for a sole detached job; %s --sessions lists jobs.\n",
                          b->program, b->job, b->program, b->job, b->program, b->program);
    if (length > 0 && (size_t)length < sizeof message)
        terminal_write_best_effort(message, (size_t)length);
    /* The live sockets and files belong to the worker's inherited state.
     * Running the parent's normal destructors would send or cancel work. */
    _exit(0);
}

int background_managed(const Background *b) { return b && b->managed; }
int background_detached(const Background *b) { return b && (b->detached || b->suspended); }
const char *background_path(const Background *b) { return b ? b->path : ""; }
int background_signal(const Background *b) { return b ? b->signal : 0; }
int background_frontend(const Background *b) { return b ? b->frontend : BACKGROUND_FRONTEND_CURRENT; }

/* These descriptors form a poll snapshot. Process
 * the matching snapshot before rebuilding it; accepting or closing a
 * candidate can reuse a numeric fd during the same event-loop turn. */
size_t background_pollfds(const Background *b, struct pollfd *fds, size_t cap)
{
    size_t count = 0;
    if (!b || !b->managed || cap < BACKGROUND_POLL_CAP) return 0;
    if (b->listener >= 0) fds[count++] = (struct pollfd){b->listener, POLLIN, 0};
    if (b->candidate >= 0) fds[count++] = (struct pollfd){b->candidate, POLLIN, 0};
    if (b->controller >= 0) fds[count++] = (struct pollfd){b->controller, POLLIN, 0};
    return count;
}

int background_timeout(const Background *b, int timeout)
{
    if (b && b->candidate >= 0) {
        int64_t left = b->candidate_deadline - net_now_ms();
        if (left < 0) left = 0;
        if (left < timeout) timeout = (int)left;
    }
    return timeout;
}

static int identifier(const char *s, size_t cap, int locale)
{
    size_t length = strnlen(s, cap);
    if (!length || length == cap) return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            (locale && c == '@') || (!locale && c == '+')) continue;
        return 0;
    }
    return 1;
}

/* Resuming transfers three descriptors, not a
 * pathname to open later. They must all identify the same real tty with
 * usable access modes before replacing the worker standard streams. */
static int valid_terminals(const int fds[3])
{
    struct stat first, other;
    struct termios attrs;
    if (fstat(fds[0], &first) < 0 || !S_ISCHR(first.st_mode) ||
        tcgetattr(fds[0], &attrs) < 0) return 0;
    for (size_t i = 0; i < 3; ++i) {
        int flags = fcntl(fds[i], F_GETFL);
        if (flags < 0 || (i == 0 && (flags & O_ACCMODE) == O_WRONLY) ||
            (i != 0 && (flags & O_ACCMODE) == O_RDONLY) ||
            fstat(fds[i], &other) < 0 || !S_ISCHR(other.st_mode) ||
            other.st_rdev != first.st_rdev || other.st_dev != first.st_dev ||
            other.st_ino != first.st_ino || tcgetattr(fds[i], &attrs) < 0) return 0;
    }
    return 1;
}

/* Validate the request, then promote it to controller. reset_candidate
 * closes the received descriptor copies after dup2 installs standard streams.
 * A partial installation failure first returns those streams to /dev/null. */
static int install_terminal(Background *b)
{
    if (!b->detached || b->controller >= 0 || b->closing) return 0;
    int legacy = !memcmp(b->request.magic, "VIPIRCB1", 8);
    int numbered = !memcmp(b->request.magic, "VIPIRCB2", 8) &&
                   !memcmp(b->request.token, b->token, sizeof b->token);
    int numbered_frontend = !memcmp(b->request.magic, "VIPIRCB3", 8) &&
                            !memcmp(b->request.token, b->token, sizeof b->token);
    int path_frontend = !memcmp(b->request.magic, "VIPIRCB4", 8);
    int requested = numbered_frontend || path_frontend ? b->request.frontend : BACKGROUND_FRONTEND_CURRENT;
#ifdef ONEKEY_ONLY
    if (requested == BACKGROUND_FRONTEND_FULL) return 0;
#endif
    if (requested < BACKGROUND_FRONTEND_CURRENT || requested > BACKGROUND_FRONTEND_IDLE) return 0;
    if ((!legacy && !numbered && !numbered_frontend && !path_frontend) || b->candidate_count != 3 ||
        !identifier(b->request.term, sizeof b->request.term, 0) ||
        !identifier(b->request.locale, sizeof b->request.locale, 1) ||
        !valid_terminals(b->candidate_tty)) return 0;
    char previous_locale[BG_LOCALE_SIZE];
    const char *current = setlocale(LC_CTYPE, NULL);
    if (!current || strlen(current) >= sizeof previous_locale) return 0;
    memcpy(previous_locale, current, strlen(current) + 1);
    if (!setlocale(LC_CTYPE, b->request.locale)) return 0;
    if (setenv("TERM", b->request.term, 1) < 0) {
        (void)setlocale(LC_CTYPE, previous_locale);
        return 0;
    }
    for (int i = 0; i < 3; ++i) if (dup2(b->candidate_tty[i], i) < 0) {
        (void)quiet_terminal();
        (void)setlocale(LC_CTYPE, previous_locale);
        return 0;
    }
    clearerr(stdin);
    clearerr(stdout);
    clearerr(stderr);
    b->controller = b->candidate;
    b->candidate = -1;
    b->detached = b->suspended = 0;
    b->frontend = requested;
    if (!notice(b->controller, 'A', 0)) {
        close(b->controller);
        b->controller = -1;
        b->detached = 1;
        (void)quiet_terminal();
        reset_candidate(b);
        return 0;
    }
    reset_candidate(b);
    return 1;
}

static size_t request_size(const AttachRequest *request)
{
    if (!memcmp(request->magic, "VIPIRCB1", 8)) return offsetof(AttachRequest, token);
    if (!memcmp(request->magic, "VIPIRCB2", 8)) return offsetof(AttachRequest, frontend);
    return sizeof *request;
}

static void answer_query(Background *b, int version)
{
    SessionReply reply;
    memset(&reply, 0, sizeof reply);
    memcpy(reply.magic, version == 1 ? "VIPIRCS1" :
           b->detached && b->controller < 0 ? "VIPIRCS2" : "VIPIRCA2", 8);
    const char *wire_program = !strcmp(b->program, "onekeyirc") ? "1keyirc" : "vipirc";
    memcpy(reply.program, wire_program, strlen(wire_program) + 1);
    reply.job = b->job;
    memcpy(reply.token, b->token, sizeof reply.token);
    memcpy(reply.label, b->label, sizeof reply.label);
    size_t sent = 0;
    for (int tries = 0; tries < 4 && sent < sizeof reply; ++tries) {
        ssize_t n = send(b->candidate, (char *)&reply + sent, sizeof reply - sent, 0);
        if (n > 0) sent += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else break;
    }
    reset_candidate(b);
}

static int control_request_valid(const Background *b, const char magic[8], int authenticated)
{
    AttachRequest expected;
    memset(&expected, 0, sizeof expected);
    memcpy(expected.magic, magic, 8);
    if (authenticated) memcpy(expected.token, b->token, sizeof expected.token);
    return !b->candidate_count && !memcmp(&b->request, &expected, sizeof expected);
}

/* A stream read may contain only part of a request, but received rights
 * already belong to us. Collect or close every descriptor before considering
 * packet validity; retain only a bounded partial request until its deadline. */
static unsigned receive_candidate(Background *b)
{
    union {
        struct cmsghdr align;
        unsigned char bytes[CMSG_SPACE(sizeof(int) * 8)];
    } ancillary;
    struct iovec vector = {(char *)&b->request + b->candidate_used,
                           sizeof b->request - b->candidate_used};
    struct msghdr message;
    memset(&message, 0, sizeof message);
    memset(&ancillary, 0, sizeof ancillary);
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = ancillary.bytes;
    message.msg_controllen = sizeof ancillary.bytes;
    ssize_t got = recvmsg(b->candidate, &message, 0);
    if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    int invalid = got <= 0 || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC));
    if (got >= 0) for (struct cmsghdr *c = CMSG_FIRSTHDR(&message); c; c = CMSG_NXTHDR(&message, c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS || c->cmsg_len < CMSG_LEN(0)) {
            invalid = 1;
            continue;
        }
        size_t bytes = c->cmsg_len - CMSG_LEN(0);
        if (bytes % sizeof(int)) invalid = 1;
        for (size_t i = 0; i < bytes / sizeof(int); ++i) {
            int fd;
            memcpy(&fd, (unsigned char *)CMSG_DATA(c) + i * sizeof(int), sizeof fd);
            if (b->candidate_count >= 3 || !fd_flags(fd, 0)) {
                close(fd);
                invalid = 1;
            } else b->candidate_tty[b->candidate_count++] = fd;
        }
    }
    if (got > 0) b->candidate_used += (size_t)got;
    if (!invalid && b->candidate_used < 8) return 0;
    if (!invalid && !memcmp(b->request.magic, "VIPIRCQ1", 8)) {
        if (b->candidate_used == 8 && !b->candidate_count) {
            /* Preserve the old query's attached/busy reply. New clients use
             * Q2 to obtain authenticated identity in either terminal state. */
            if (b->detached && b->controller < 0) answer_query(b, 1);
            else {
                notice(b->candidate, 'E', 1);
                reset_candidate(b);
            }
            return 0;
        }
        invalid = 1;
    }
    size_t wanted = request_size(&b->request);
    if (!invalid && b->candidate_used < wanted) return 0;
    if (b->candidate_used != wanted) invalid = 1;
    if (!invalid && !memcmp(b->request.magic, "VIPIRCQ2", 8)) {
        if (control_request_valid(b, "VIPIRCQ2", 0)) {
            answer_query(b, 2);
            return 0;
        }
        invalid = 1;
    }
    if (!invalid && !memcmp(b->request.magic, "VIPIRCD1", 8)) {
        if (control_request_valid(b, "VIPIRCD1", 1)) {
            /* Explicit shell recovery: remove only the terminal/controller.
             * Keep IRC sockets, application state, drafts and the job lease.
             * Completion is sent only after the frontend has released its tty
             * and background_detach has committed the detached state. */
            if (b->detached && b->controller < 0) {
                (void)notice(b->candidate, 'K', 0);
                (void)notice(b->candidate, 'C', 0);
                reset_candidate(b);
                return 0;
            }
            b->detaching = 1;
            (void)notice(b->candidate, 'K', 0);
            return BACKGROUND_DETACH;
        }
        invalid = 1;
    }
    if (!invalid && !memcmp(b->request.magic, "VIPIRCC1", 8)) {
        if (control_request_valid(b, "VIPIRCC1", 1)) {
            /* The authenticated operator is closing the session, so stale
             * terminal output must not hold up frontend cleanup. Keep both
             * control sockets; the foreground waiter restores its saved tty. */
            if (!quiet_terminal()) {
                notice(b->candidate, 'E', 3);
                reset_candidate(b);
                return 0;
            }
            /* Keep this authenticated connection until background_finish
             * releases the original job's lease. Acceptance is not completion. */
            b->closing = 1;
            b->signal = SIGTERM;
            (void)notice(b->candidate, 'K', 0);
            return BACKGROUND_STOP;
        }
        invalid = 1;
    }
    if (!invalid && (!b->detached || b->controller >= 0)) {
        notice(b->candidate, 'E', 1);
        reset_candidate(b);
        return 0;
    }
    if (!invalid && install_terminal(b)) return BACKGROUND_ATTACH;
    notice(b->candidate, 'E', 2);
    reset_candidate(b);
    return 0;
}

static void accept_candidate(Background *b)
{
    int fd = accept(b->listener, NULL, NULL);
    if (fd < 0) return;
    if (!fd_flags(fd, 1) || !same_owner(fd)) {
        close(fd);
        return;
    }
    if (b->candidate >= 0 || b->closing) {
        notice(fd, 'E', 1);
        close(fd);
        return;
    }
    b->candidate = fd;
    b->candidate_deadline = net_now_ms() + BG_HANDSHAKE_MS;
}

unsigned background_process(Background *b, const struct pollfd *fds, size_t count)
{
    if (!b || !b->managed) return 0;
    if (b->closing) return BACKGROUND_STOP;
    unsigned events = 0;
    if (b->candidate >= 0 && net_now_ms() >= b->candidate_deadline) {
        notice(b->candidate, 'E', 2);
        reset_candidate(b);
    }
    /* Process the old controller before any new accept. Closing a stale
     * descriptor must not make its poll events refer to a reused candidate. */
    short listener_events = 0, candidate_events = 0, controller_events = 0;
    for (size_t i = 0; i < count; ++i) {
        if (fds[i].fd < 0) continue;
        if (fds[i].fd == b->listener) listener_events |= fds[i].revents;
        else if (fds[i].fd == b->candidate) candidate_events |= fds[i].revents;
        else if (fds[i].fd == b->controller) controller_events |= fds[i].revents;
    }
    if (controller_events) {
        unsigned char commands[BG_CONTROL_BATCH];
        ssize_t got = recv(b->controller, commands, sizeof commands, 0);
        if (!got || (got < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
            close(b->controller);
            b->controller = -1;
            events |= BACKGROUND_DETACH;
        }
        for (ssize_t j = 0; j < got; ++j) switch (commands[j]) {
        case 'W': events |= BACKGROUND_RESIZE; break;
        case 'I': b->signal = SIGINT; events |= BACKGROUND_STOP; break;
        case 'T': b->signal = SIGTERM; events |= BACKGROUND_STOP; break;
        case 'Q': b->signal = SIGQUIT; events |= BACKGROUND_STOP; break;
        case 'H': events |= BACKGROUND_DETACH; break;
        case 'S': if (!b->suspended) events |= BACKGROUND_SUSPEND; break;
        case 'C':
            if (b->suspended) {
                b->suspended = 0;
                events |= BACKGROUND_CONTINUE;
            }
            break;
        default: events |= BACKGROUND_DETACH; break;
        }
    }
    if (b->candidate >= 0 && (candidate_events & (POLLIN | POLLHUP | POLLERR | POLLNVAL)))
        events |= receive_candidate(b);
    if (!b->closing && (listener_events & POLLIN)) accept_candidate(b);
    return events;
}

void background_suspend(Background *b)
{
    if (!b || b->controller < 0) return;
    b->suspended = 1;
    if (!notice(b->controller, 'S', 0)) {
        close(b->controller);
        b->controller = -1;
        b->suspended = 0;
        b->detached = 1;
        (void)quiet_terminal();
    }
}

void background_finish(Background *b, int status)
{
    if (!b) return;
    notice(b->controller, 'Q', (unsigned char)(status >= 0 && status <= 255 ? status : 1));
    if (b->controller >= 0) close(b->controller);
    b->controller = -1;
    remove_endpoint(b);
    if (b->closing) (void)notice(b->candidate, 'C', 0);
    if (b->detaching) (void)notice(b->candidate, 'C', 1);
    b->closing = b->detaching = 0;
    reset_candidate(b);
}

void background_free(Background *b)
{
    if (!b) return;
    background_finish(b, 1);
    memset(b, 0, sizeof *b);
    free(b);
}

/* The foreground waiter owns its shell job. The worker has its own session
 * and no controlling terminal, so signals and resize are sent as local
 * control bytes rather than directed at a reusable numeric process ID. */
static volatile sig_atomic_t foreground_signals;
enum ForegroundSignal { FG_INT=1, FG_TERM=2, FG_HUP=4, FG_QUIT=8, FG_RESIZE=16, FG_SUSPEND=32, FG_CONTINUE=64 };

static void foreground_signal(int signal_number)
{
    switch (signal_number) {
    case SIGINT: foreground_signals |= FG_INT; break;
    case SIGTERM: foreground_signals |= FG_TERM; break;
    case SIGHUP: foreground_signals |= FG_HUP; break;
    case SIGQUIT: foreground_signals |= FG_QUIT; break;
    case SIGWINCH: foreground_signals |= FG_RESIZE; break;
    case SIGTSTP: foreground_signals |= FG_SUSPEND; break;
    case SIGCONT: foreground_signals |= FG_CONTINUE; break;
    default: break;
    }
}

static int wait_socket(int fd, short wanted, int64_t deadline)
{
    for (;;) {
        int64_t left = deadline - net_now_ms();
        if (left <= 0) return 0;
        struct pollfd p = {fd, wanted, 0};
        int result = poll(&p, 1, (int)left);
        if (result < 0 && errno == EINTR) continue;
        return result > 0 && (p.revents & wanted);
    }
}

static int endpoint_valid(const char *path)
{
    if (!path || path[0] != '/' || strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) return 0;
    char parent[sizeof(((struct sockaddr_un *)0)->sun_path)];
    memcpy(parent, path, strlen(path) + 1);
    char *last = strrchr(parent, '/');
    if (!last || last == parent || strcmp(last + 1, "session")) return 0;
    *last = '\0';
    int fd = open(parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return 0;
    struct stat directory, socket_stat;
    int okay = fstat(fd, &directory) == 0 && S_ISDIR(directory.st_mode) &&
        directory.st_uid == getuid() && (directory.st_mode & 0777) == 0700 &&
        fstatat(fd, "session", &socket_stat, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISSOCK(socket_stat.st_mode) && socket_stat.st_uid == getuid() &&
        (socket_stat.st_mode & 0777) == 0600;
    close(fd);
    return okay;
}

enum JobState { JOB_UNKNOWN, JOB_DETACHED, JOB_BUSY };
typedef struct SessionJob {
    unsigned number;
    int fd, connecting, state, legacy, modern;
    size_t sent, received;
    char path[BG_PATH_SIZE];
    SessionReply reply;
} SessionJob;
typedef struct SessionList {
    SessionJob jobs[BG_JOB_MAX];
    size_t count, stale;
} SessionList;

static void probe_close(SessionJob *job, int state)
{
    if (job->fd >= 0) close(job->fd);
    job->fd = -1; job->state = state;
}

static int reply_valid(const SessionJob *job)
{
    const SessionReply *reply = &job->reply;
    const char program[8] = BG_WIRE_PROGRAM;
    if ((memcmp(reply->magic, "VIPIRCS1", 8) &&
         memcmp(reply->magic, "VIPIRCS2", 8) && memcmp(reply->magic, "VIPIRCA2", 8)) ||
        memcmp(reply->program, program, sizeof program) || reply->job != job->number ||
        strnlen(reply->label, sizeof reply->label) == sizeof reply->label) return 0;
    for (size_t i = 0; reply->label[i]; ++i)
        if ((unsigned char)reply->label[i] < 32 || (unsigned char)reply->label[i] > 126) return 0;
    return 1;
}

static size_t query_size(const SessionJob *job)
{
    return job->legacy ? 8u : sizeof(AttachRequest);
}

static int probe_open(SessionJob *job, int legacy)
{
    if (job->fd >= 0) close(job->fd);
    job->fd = -1;
    job->connecting = job->modern = 0;
    job->legacy = legacy;
    job->state = JOB_UNKNOWN;
    job->sent = job->received = 0;
    memset(&job->reply, 0, sizeof job->reply);
    if (!endpoint_valid(job->path)) return 0;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || !fd_flags(fd, 1)) { if (fd >= 0) close(fd); return 0; }
    struct sockaddr_un address;
    memset(&address, 0, sizeof address); address.sun_family = AF_UNIX;
    memcpy(address.sun_path, job->path, strlen(job->path) + 1);
    int connected = connect(fd, (struct sockaddr *)&address, sizeof address);
    if (connected < 0 && errno != EINPROGRESS) { close(fd); return 0; }
    if (connected == 0 && !same_owner(fd)) { close(fd); return 0; }
    job->fd = fd; job->connecting = connected < 0;
    return 1;
}

static void probe_drive(SessionJob *job)
{
    if (job->fd < 0) return;
    if (job->connecting) {
        int error = 0; socklen_t size = sizeof error;
        if (getsockopt(job->fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0 || error ||
            !same_owner(job->fd)) { probe_close(job, JOB_UNKNOWN); return; }
        job->connecting = 0;
    }
    size_t wanted = query_size(job);
    if (job->sent < wanted) {
        /* Pad Q2 to the established attach size. Older workers can reject
         * it immediately instead of waiting for their handshake timeout. */
        AttachRequest query;
        memset(&query, 0, sizeof query);
        memcpy(query.magic, job->legacy ? "VIPIRCQ1" : "VIPIRCQ2", 8);
        ssize_t n = send(job->fd, (char *)&query + job->sent, wanted - job->sent, 0);
        if (n > 0) job->sent += (size_t)n;
        else if (n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK &&
                 errno != EPIPE && errno != ECONNRESET) { probe_close(job, JOB_UNKNOWN); return; }
        /* Read an early busy reply even when its peer closed before send. */
    }
    ssize_t n = recv(job->fd, (char *)&job->reply + job->received,
                     sizeof job->reply - job->received, 0);
    if (n > 0) job->received += (size_t)n;
    if (job->received >= 2 && ((unsigned char *)&job->reply)[0] == 'E') {
        unsigned char detail = ((unsigned char *)&job->reply)[1];
        if (!job->legacy && detail == 2) (void)probe_open(job, 1);
        else probe_close(job, detail == 1 ? JOB_BUSY : JOB_UNKNOWN);
    } else if (job->received == sizeof job->reply) {
        int valid = reply_valid(job);
        job->modern = valid && (!memcmp(job->reply.magic, "VIPIRCS2", 8) ||
                                !memcmp(job->reply.magic, "VIPIRCA2", 8));
        probe_close(job, !valid ? JOB_UNKNOWN :
                    !memcmp(job->reply.magic, "VIPIRCA2", 8) ? JOB_BUSY : JOB_DETACHED);
    } else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
        probe_close(job, JOB_UNKNOWN);
    }
}

static int session_scan(SessionList *list)
{
    memset(list, 0, sizeof *list);
    int64_t deadline = net_now_ms() + BG_LIST_MS;
    char registry[BG_PATH_SIZE];
    int root = registry_open(BG_PROGRAM, 0, registry);
    if (root < 0) {
        if (errno == ENOENT) return 1;
        bg_stderr("cannot read the owned private background-job registry"); return 0;
    }
    int lock = registry_lock(root, 0);
    if (lock < 0) {
        close(root);
        bg_stderr("background-job registry is unavailable or busy; retry"); return 0;
    }
    for (unsigned number = 1; number <= BG_JOB_MAX; ++number) {
        char name[24]; struct stat st;
        (void)snprintf(name, sizeof name, "job-%u", number);
        if (stale_job(root, name)) { ++list->stale; continue; }
        if (fstatat(root, name, &st, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT) continue;
        SessionJob *job = &list->jobs[list->count++];
        job->number = number; job->fd = -1;
        int directory = private_job(root, name, &st);
        if (directory < 0) continue;
        int lease = openat(directory, "lease", O_RDWR | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
        int valid = lease >= 0 && owned_regular(lease);
        if (valid) {
            if (flock(lease, LOCK_EX | LOCK_NB) == 0) valid = 0;
            else if (errno != EAGAIN && errno != EWOULDBLOCK) valid = 0;
        }
        if (lease >= 0) close(lease);
        close(directory);
        size_t base_length = strlen(registry), name_length = strlen(name);
        if (!valid || base_length + name_length + 1 + sizeof("/session") > sizeof job->path) continue;
        memcpy(job->path, registry, base_length);
        job->path[base_length] = '/';
        memcpy(job->path + base_length + 1, name, name_length);
        memcpy(job->path + base_length + name_length + 1, "/session", sizeof("/session"));
        if (probe_open(job, 0) && !job->connecting) probe_drive(job);
    }
    close(lock); close(root);
    /* One deadline covers all 64 jobs. A stalled peer cannot multiply a
     * handshake timeout by the number of stored entries. */
    while (net_now_ms() < deadline) {
        struct pollfd fds[BG_JOB_MAX]; SessionJob *jobs[BG_JOB_MAX];
        size_t count = 0;
        for (size_t i = 0; i < list->count; ++i) if (list->jobs[i].fd >= 0) {
            jobs[count] = &list->jobs[i];
            fds[count].fd = jobs[count]->fd;
            fds[count].events = (short)(POLLIN | ((jobs[count]->connecting || jobs[count]->sent < query_size(jobs[count])) ? POLLOUT : 0));
            fds[count++].revents = 0;
        }
        if (!count) break;
        int64_t left = deadline - net_now_ms();
        if (left <= 0) break;
        int ready = poll(fds, (nfds_t)count, (int)left);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) break;
        for (size_t i = 0; i < count; ++i) if (fds[i].revents) probe_drive(jobs[i]);
    }
    for (size_t i = 0; i < list->count; ++i)
        if (list->jobs[i].fd >= 0) probe_close(&list->jobs[i], JOB_UNKNOWN);
    return 1;
}

static void sessions_print(const SessionList *list)
{
    fputs(BG_PROGRAM " ", stdout);
    puts(irc_tr("background jobs:"));
    if (!list->count) { fputs("  ", stdout); puts(irc_tr("No background jobs.")); }
    for (size_t i = 0; i < list->count; ++i) {
        const SessionJob *job = &list->jobs[i];
        const char *state = job->state == JOB_DETACHED ? "detached" : job->state == JOB_BUSY ?
                            (job->modern ? "attached" : "attached/busy") : "unverified";
        /* A byte-count column width breaks for translated multibyte words;
         * separate fields explicitly instead of guessing terminal cells. */
        printf("  %u  ", job->number);
        fputs(irc_tr(state), stdout);
        fputs("  ", stdout);
        puts(job->state == JOB_DETACHED || job->modern ? job->reply.label : "");
    }
    if (list->stale) {
        char count[32];
        (void)snprintf(count, sizeof count, "%zu", list->stale);
        const BgField fields[] = {{"count", count}};
        bg_stdout_fields(list->stale == 1 ? "Removed {count} stale local job entry." :
                         "Removed {count} stale local job entries.", fields, 1);
        fputc('\n', stdout);
    }
    fputs(irc_tr("Resume a listed detached job with:"), stdout);
    puts(" " BG_PROGRAM " --resume N");
    fputs(irc_tr("Detach an attached job without ending IRC:"), stdout);
    puts(" " BG_PROGRAM " --detach-session N");
    fputs(irc_tr("With one detached job, use:"), stdout);
    puts(" " BG_PROGRAM " --resume");
}

int background_sessions(void)
{
    SessionList list;
    if (!session_scan(&list)) return 1;
    sessions_print(&list); return ferror(stdout) ? 1 : 0;
}

static int send_attach(int fd, const AttachRequest *request, int64_t deadline)
{
    union { struct cmsghdr align; unsigned char bytes[CMSG_SPACE(sizeof(int) * 3)]; } ancillary;
    memset(&ancillary, 0, sizeof ancillary);
    size_t length = request_size(request);
    struct iovec vector = {(void *)request, length};
    struct msghdr message;
    memset(&message, 0, sizeof message);
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = ancillary.bytes;
    message.msg_controllen = sizeof ancillary.bytes;
    struct cmsghdr *control = CMSG_FIRSTHDR(&message);
    control->cmsg_level = SOL_SOCKET; control->cmsg_type = SCM_RIGHTS;
    control->cmsg_len = CMSG_LEN(sizeof(int) * 3);
    const int terminals[3] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
    memcpy(CMSG_DATA(control), terminals, sizeof terminals);
    size_t sent = 0;
    while (sent < length) {
        ssize_t wrote = sendmsg(fd, &message, 0);
        if (wrote > 0) {
            sent += (size_t)wrote;
            vector.iov_base = (char *)request + sent;
            vector.iov_len = length - sent;
            message.msg_control = NULL; message.msg_controllen = 0;
        } else if (wrote < 0 && errno == EINTR) continue;
        else if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!wait_socket(fd, POLLOUT, deadline)) return 0;
        } else return 0;
    }
    return 1;
}

static void restore_terminal_flags(const int flags[3])
{
    for (int fd = 0; fd < 3; ++fd)
        for (int attempt = 0; attempt < 4; ++attempt)
            if (fcntl(fd, F_SETFL, flags[fd]) == 0 || errno != EINTR) break;
}

static void restore_foreground(const struct termios *saved, const int flags[3])
{
    /* Fixed terminal cleanup also covers an unexpected worker death. The
     * normal worker exit has already emitted its own terminfo restoration. */
    (void)tcsetattr(STDIN_FILENO, TCSANOW, saved);
    static const char reset[] = "\033[?1049l\033[?2004l\033[?1000l\033[?1002l\033[?1003l\033[?1006l\033[?25h\033[0m";
    terminal_write_best_effort(reset, sizeof reset - 1u);
    /* Idle changes shared open-description flags. A dead worker or an
     * external close may quiet its fds before it can restore those flags. */
    restore_terminal_flags(flags);
}

static void attachment_error(unsigned char detail)
{
    bg_stderr(detail == 1 ? "that session already has a terminal attached" :
              "the background session rejected the terminal handshake");
}

static int initial_reply(int fd, unsigned char reply[2], int64_t deadline)
{
    size_t used = 0;
    while (used < 2 && net_now_ms() < deadline) {
        ssize_t got = recv(fd, reply + used, 2 - used, 0);
        if (got > 0) used += (size_t)got;
        else if (got < 0 && errno == EINTR) continue;
        else if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!wait_socket(fd, POLLIN, deadline)) break;
        } else break;
    }
    return used == 2;
}

static int resume_endpoint_once(const char *path, const unsigned char *token, unsigned job, int frontend, int legacy)
{
    const int terminals[3] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
    struct termios saved;
    int terminal_flags[3];
    for (int fd = 0; fd < 3; ++fd) terminal_flags[fd] = fcntl(fd, F_GETFL);
    if (terminal_flags[0] < 0 || terminal_flags[1] < 0 || terminal_flags[2] < 0 ||
        !valid_terminals(terminals) || tcgetattr(STDIN_FILENO, &saved) < 0) {
        bg_stderr("--resume requires one interactive terminal for input and output");
        return 2;
    }
    if (!endpoint_valid(path)) {
        bg_stderr("resume endpoint must be an owned 0600 socket in an owned 0700 directory");
        return 2;
    }
    AttachRequest request;
    memset(&request, 0, sizeof request);
    if (frontend < BACKGROUND_FRONTEND_CURRENT || frontend > BACKGROUND_FRONTEND_IDLE) {
        bg_stderr("invalid resume frontend request");
        return 2;
    }
#ifdef ONEKEY_ONLY
    frontend = BACKGROUND_FRONTEND_IDLE;
#endif
    memcpy(request.magic, legacy ? (token ? "VIPIRCB2" : "VIPIRCB1") :
           (token ? "VIPIRCB3" : "VIPIRCB4"), 8);
    if (token) memcpy(request.token, token, sizeof request.token);
    request.frontend = (unsigned char)frontend;
    const char *term = getenv("TERM"), *locale = setlocale(LC_CTYPE, NULL);
    if (!term || !locale || !identifier(term, sizeof request.term, 0) ||
        !identifier(locale, sizeof request.locale, 1)) {
        bg_stderr("--resume requires a supported TERM and LC_CTYPE name");
        return 2;
    }
    memcpy(request.term, term, strlen(term) + 1);
    memcpy(request.locale, locale, strlen(locale) + 1);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || !fd_flags(fd, 1)) {
        if (fd >= 0) close(fd);
        bg_stderr("cannot create the resume connection");
        return 1;
    }
    struct sockaddr_un address;
    memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, strlen(path) + 1);
    int connected = connect(fd, (struct sockaddr *)&address, sizeof address);
    int64_t deadline = net_now_ms() + BG_HANDSHAKE_MS;
    if (connected < 0 && errno == EINPROGRESS) {
        int pending = 0; socklen_t size = sizeof pending;
        connected = wait_socket(fd, POLLOUT, deadline) &&
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &pending, &size) == 0 && !pending ? 0 : -1;
    }
    if (connected < 0 || !same_owner(fd)) {
        close(fd);
        bg_stderr("cannot connect to that background session");
        return 1;
    }
    const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGWINCH, SIGTSTP, SIGCONT};
    struct sigaction action, previous[sizeof signals / sizeof signals[0]];
    memset(&action, 0, sizeof action); sigemptyset(&action.sa_mask);
    /* Block this entire set during a handler so flags cannot lose updates. */
    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; ++i) sigaddset(&action.sa_mask, signals[i]);
    action.sa_handler = foreground_signal;
    foreground_signals = 0;
    size_t installed = 0;
    for (; installed < sizeof signals / sizeof signals[0]; ++installed)
        if (sigaction(signals[installed], &action, &previous[installed]) < 0) break;
    int result = 1, attached = 0, done = 0, suspending = 0, draining = 0;
    unsigned char reply[2]; size_t reply_used = 0;
    if (installed != sizeof signals / sizeof signals[0]) goto finish;
    if (!send_attach(fd, &request, deadline)) {
        /* A busy worker can reject immediately after accept, before this
         * foreground process sends its request. An EPIPE here must not hide
         * the rejection already queued in the opposite socket direction. */
        if (initial_reply(fd, reply, deadline) && reply[0] == 'E') {
            if (!legacy && reply[1] == 2) result = 3;
            else attachment_error(reply[1]);
            done = 1;
        }
        goto finish;
    }
    while (!done) {
        sigset_t blocked, old;
        sigemptyset(&blocked);
        for (size_t i = 0; i < sizeof signals / sizeof signals[0]; ++i) sigaddset(&blocked, signals[i]);
        if (sigprocmask(SIG_BLOCK, &blocked, &old) < 0) break;
        sig_atomic_t pending = foreground_signals; foreground_signals = 0;
        (void)sigprocmask(SIG_SETMASK, &old, NULL);
        const int bits[] = {FG_INT, FG_TERM, FG_HUP, FG_QUIT, FG_RESIZE, FG_SUSPEND, FG_CONTINUE};
        const unsigned char bytes[] = {'I', 'T', 'H', 'Q', 'W', 'S', 'C'};
        for (size_t i = 0; !draining && i < sizeof bits / sizeof bits[0]; ++i) if (pending & bits[i]) {
            if (bits[i] == FG_SUSPEND) {
                if (suspending) continue;
                suspending = 1;
            }
            if (send(fd, bytes + i, 1, 0) != 1) {
                /* Terminal EOF can make the worker acknowledge detach and
                 * close before a pending HUP/CONT is forwarded. Preserve the
                 * partial reply decoder and drain under a bounded deadline;
                 * a failed write alone is never proof of successful detach. */
                draining = 1;
                if (attached) deadline = net_now_ms() + BG_HANDSHAKE_MS;
                break;
            }
            if (bits[i] & (FG_INT | FG_TERM | FG_HUP | FG_QUIT)) {
                /* A successful send only queues a request. If a hung worker
                 * cannot acknowledge it, release this waiter and its control
                 * connection under the same bounded drain deadline. */
                draining = 1;
                deadline = net_now_ms() + BG_HANDSHAKE_MS;
                break;
            }
        }
        int timeout = 1000;
        if (!attached || draining) {
            int64_t left = deadline - net_now_ms();
            if (left <= 0) break;
            if (left < timeout) timeout = (int)left;
        }
        struct pollfd p = {fd, POLLIN, 0};
        int ready = poll(&p, 1, timeout);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) break;
        if (!ready) continue;
        ssize_t got = recv(fd, reply + reply_used, sizeof reply - reply_used, 0);
        if (got < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (got <= 0) break;
        reply_used += (size_t)got;
        if (reply_used < sizeof reply) continue;
        reply_used = 0;
        switch (reply[0]) {
        case 'A': attached = 1; break;
        case 'D': if (attached) result = 0; done = 1; break;
        case 'Q': if (attached) result = reply[1]; done = 1; break;
        case 'S':
            if (draining) break;
            restore_foreground(&saved, terminal_flags);
            {
                struct sigaction stop;
                memset(&stop, 0, sizeof stop); sigemptyset(&stop.sa_mask); stop.sa_handler = SIG_DFL;
                (void)sigaction(SIGTSTP, &stop, NULL);
                (void)raise(SIGTSTP);
                (void)sigaction(SIGTSTP, &action, NULL);
                suspending = 0;
                unsigned char resume = 'C';
                if (send(fd, &resume, 1, 0) != 1) {
                    draining = 1;
                    deadline = net_now_ms() + BG_HANDSHAKE_MS;
                }
            }
            break;
        case 'E':
            if (!legacy && !attached && reply[1] == 2) result = 3;
            else attachment_error(reply[1]);
            done = 1;
            break;
        default: done = 1; break;
        }
    }
finish:
    if (attached) restore_foreground(&saved, terminal_flags);
    else restore_terminal_flags(terminal_flags);
    close(fd);
    while (installed) { --installed; (void)sigaction(signals[installed], &previous[installed], NULL); }
    if (!attached && !done) bg_stderr("the background session did not complete its terminal handshake");
    if (attached && result == 0 && reply[0] == 'D') {
        char message[384];
        int length;
        if (job) {
            char number[24], line[768];
            (void)snprintf(number, sizeof number, "%u", job);
            const BgField fields[] = {{"job", number}};
            size_t size = bg_render_fields(line, sizeof line, "job {job} is running in the background.", fields, 1);
            length = size ? snprintf(message, sizeof message, BG_PROGRAM " %s\n%s " BG_PROGRAM " --resume %u\n",
                                     line, irc_tr("Resume:"), job) : -1;
        } else {
            length = snprintf(message, sizeof message, BG_PROGRAM " %s\n  " BG_PROGRAM " --resume %s\n",
                              irc_tr("is running in the background. Resume with:"), path);
        }
        if (length > 0 && (size_t)length < sizeof message)
            terminal_write_best_effort(message, (size_t)length);
    }
    return result;
}

static int resume_endpoint(const char *path, const unsigned char *token, unsigned job, int frontend)
{
    int result = resume_endpoint_once(path, token, job, frontend, 0);
    if (result != 3) return result;
    bg_stderr("background worker uses the older resume protocol; retrying with its existing frontend (frontend switching unavailable for this attachment)");
    return resume_endpoint_once(path, token, job, BACKGROUND_FRONTEND_CURRENT, 1);
}

/* The displayed job number is a discovery choice,
 * never the worker identity. Recheck the selected socket/token during
 * handoff; a stale selection must not attach to another job. */
int background_resume(const char *selection, int frontend)
{
    if (selection && selection[0] == '/') return resume_endpoint(selection, NULL, 0, frontend);
    unsigned number = 0;
    if (selection) {
        if (!*selection) { bg_stderr("--resume expects a job number or absolute socket path"); return 2; }
        for (const unsigned char *p = (const unsigned char *)selection; *p; ++p) {
            if (*p < '0' || *p > '9' || number > BG_JOB_MAX / 10u) { number = 0; break; }
            number = number * 10u + (unsigned)(*p - '0');
            if (number > BG_JOB_MAX) { number = 0; break; }
        }
        if (!number) { bg_stderr("job numbers are 1..64; use --sessions"); return 2; }
    }
    SessionList list;
    if (!session_scan(&list)) return 1;
    const SessionJob *chosen = NULL;
    size_t available = 0, unknown = 0;
    for (size_t i = 0; i < list.count; ++i) {
        const SessionJob *job = &list.jobs[i];
        if (job->state == JOB_UNKNOWN) ++unknown;
        if (job->state == JOB_DETACHED) {
            ++available;
            if (!number || job->number == number) chosen = job;
        }
    }
    if ((!number && (available != 1 || unknown)) || (number && !chosen)) {
        sessions_print(&list);
        bg_stderr(number ? "that job is not available; choose a verified detached job" :
                  "no unique verified detached job; choose its number with --resume N");
        return 1;
    }
    return resume_endpoint(chosen->path, chosen->reply.token, chosen->number, frontend);
}

static int detach_endpoint(const SessionJob *job)
{
    if (!job->modern || !reply_valid(job) || !endpoint_valid(job->path)) {
        bg_stderr("cannot verify this session's identity for detaching");
        return 1;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || !fd_flags(fd, 1)) {
        if (fd >= 0) close(fd);
        bg_stderr("cannot create the session-control connection");
        return 1;
    }
    struct sockaddr_un address;
    memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, job->path, strlen(job->path) + 1);
    int64_t deadline = net_now_ms() + BG_HANDSHAKE_MS;
    int connected = connect(fd, (struct sockaddr *)&address, sizeof address);
    if (connected < 0 && errno == EINPROGRESS) {
        int pending = 0; socklen_t size = sizeof pending;
        connected = wait_socket(fd, POLLOUT, deadline) &&
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &pending, &size) == 0 && !pending ? 0 : -1;
    }
    if (connected < 0 || !same_owner(fd)) {
        close(fd);
        bg_stderr("cannot connect to the verified session; nothing was detached");
        return 1;
    }
    AttachRequest request;
    memset(&request, 0, sizeof request);
    memcpy(request.magic, "VIPIRCD1", 8);
    memcpy(request.token, job->reply.token, sizeof request.token);
    size_t sent = 0;
    while (sent < sizeof request && net_now_ms() < deadline) {
        ssize_t wrote = send(fd, (char *)&request + sent, sizeof request - sent, 0);
        if (wrote > 0) sent += (size_t)wrote;
        else if (wrote < 0 && errno == EINTR) continue;
        else if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!wait_socket(fd, POLLOUT, deadline)) break;
        } else break;
    }
    unsigned char reply[2];
    int accepted = sent == sizeof request && initial_reply(fd, reply, deadline) &&
                   reply[0] == 'K' && reply[1] == 0;
    int completed = accepted && initial_reply(fd, reply, deadline) &&
                    reply[0] == 'C' && reply[1] == 0;
    close(fd);
    memset(&request, 0, sizeof request);
    if (!completed) {
        bg_stderr(accepted ? "detach was accepted, but terminal release was not confirmed before the deadline; check --sessions" :
                  "detach was not acknowledged; the session may be replaced, unverified, or unable to cooperate; check --sessions");
        return 1;
    }
    bg_job_notice("job {job} detached. Resume it with:", job->number, 1);
    return 0;
}

/* K acknowledges acceptance; C acknowledges completed worker teardown.
 * A timeout after K is an uncertain outcome, never permission to unlink the
 * worker's endpoint or signal a PID inferred from a displayed job number. */
static int close_endpoint(const SessionJob *job)
{
    if (!job->modern || !reply_valid(job) || !endpoint_valid(job->path)) {
        bg_stderr("cannot verify this session's identity for closing");
        return 1;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || !fd_flags(fd, 1)) {
        if (fd >= 0) close(fd);
        bg_stderr("cannot create the session-control connection");
        return 1;
    }
    struct sockaddr_un address;
    memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, job->path, strlen(job->path) + 1);
    int64_t deadline = net_now_ms() + BG_HANDSHAKE_MS;
    int connected = connect(fd, (struct sockaddr *)&address, sizeof address);
    if (connected < 0 && errno == EINPROGRESS) {
        int pending = 0; socklen_t size = sizeof pending;
        connected = wait_socket(fd, POLLOUT, deadline) &&
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &pending, &size) == 0 && !pending ? 0 : -1;
    }
    if (connected < 0 || !same_owner(fd)) {
        close(fd);
        bg_stderr("cannot connect to the verified session; nothing was closed");
        return 1;
    }
    AttachRequest request;
    memset(&request, 0, sizeof request);
    memcpy(request.magic, "VIPIRCC1", 8);
    memcpy(request.token, job->reply.token, sizeof request.token);
    size_t sent = 0;
    while (sent < sizeof request && net_now_ms() < deadline) {
        ssize_t wrote = send(fd, (char *)&request + sent, sizeof request - sent, 0);
        if (wrote > 0) sent += (size_t)wrote;
        else if (wrote < 0 && errno == EINTR) continue;
        else if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!wait_socket(fd, POLLOUT, deadline)) break;
        } else break;
    }
    unsigned char reply[2];
    int accepted = sent == sizeof request && initial_reply(fd, reply, deadline) &&
                   reply[0] == 'K' && reply[1] == 0;
    int completed = accepted && initial_reply(fd, reply, deadline) &&
                    reply[0] == 'C' && reply[1] == 0;
    close(fd);
    memset(&request, 0, sizeof request);
    if (!completed) {
        bg_stderr(accepted ? "close was accepted, but session teardown was not confirmed before the deadline; check --sessions" :
                  "close was not acknowledged; the session may be busy, replaced, or unable to cooperate; check --sessions");
        return 1;
    }
    bg_job_notice("job {job} closed.", job->number, 0);
    return 0;
}

int background_detach_session(const char *selection)
{
    unsigned number = 0;
    if (!selection || !*selection) goto invalid;
    for (const unsigned char *p = (const unsigned char *)selection; *p; ++p) {
        if (*p < '0' || *p > '9' || number > BG_JOB_MAX / 10u) goto invalid;
        number = number * 10u + (unsigned)(*p - '0');
        if (number > BG_JOB_MAX) goto invalid;
    }
    if (!number) goto invalid;
    SessionList list;
    if (!session_scan(&list)) return 1;
    for (size_t i = 0; i < list.count; ++i) {
        const SessionJob *job = &list.jobs[i];
        if (job->number != number) continue;
        if (!job->modern || job->state == JOB_UNKNOWN) {
            bg_stderr("that job cannot be authenticated for non-destructive detach; use a current worker or close it explicitly");
            return 1;
        }
        if (job->state == JOB_DETACHED) {
            bg_job_notice("job {job} is already detached. Resume it with:", number, 1);
            return 0;
        }
        return detach_endpoint(job);
    }
    bg_stderr("no verified background job has that number; use --sessions");
    return 1;
invalid:
    bg_stderr("--detach-session requires one job number from 1 to 64");
    return 2;
}

int background_close_session(const char *selection)
{
    unsigned number = 0;
    if (!selection || !*selection) goto invalid;
    for (const unsigned char *p = (const unsigned char *)selection; *p; ++p) {
        if (*p < '0' || *p > '9' || number > BG_JOB_MAX / 10u) goto invalid;
        number = number * 10u + (unsigned)(*p - '0');
        if (number > BG_JOB_MAX) goto invalid;
    }
    if (!number) goto invalid;
    SessionList list;
    if (!session_scan(&list)) return 1;
    for (size_t i = 0; i < list.count; ++i) {
        const SessionJob *job = &list.jobs[i];
        if (job->number != number) continue;
        if (!job->modern || job->state == JOB_UNKNOWN) {
            bg_stderr(job->legacy && job->state == JOB_DETACHED ?
                      "that job uses an older worker without authenticated close support; resume it and quit normally" :
                      "that job is busy, unverified, or running an older worker without authenticated close support; retry or resume it and quit normally");
            return 1;
        }
        return close_endpoint(job);
    }
    bg_stderr("no verified background job has that number; use --sessions");
    return 1;
invalid:
    bg_stderr("--close-session requires one job number from 1 to 64");
    return 2;
}
