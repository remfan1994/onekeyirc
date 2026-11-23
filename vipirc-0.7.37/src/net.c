/* Bounded transport for a protocol-independent byte stream.
 * Read in lifecycle order: failure classification; owned resolver child;
 * address/TLS setup; pinned trust-file loading; public open/poll/read/write.
 * Net owns every descriptor and TLS object here. App owns IRC registration,
 * output frame storage and the decision to reconnect after a failure.
 */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "net.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#define NET_ADDR_MAX 16
#define NET_SETUP_MS 30000
#define NET_ADDRESS_MS 5000
#define NET_FD_CLEANUP_MS 1000
#define NET_CA_BYTES_MAX (16u * 1024u * 1024u)

/* These extensions can be hidden by strict POSIX feature-test macros. */
#if defined(__OpenBSD__) || defined(__NetBSD__)
extern int closefrom(int);
#elif defined(__FreeBSD__)
extern void closefrom(int);
#endif

typedef struct ResolvedAddress {
    struct sockaddr_storage address;
    socklen_t length;
    int family;
} ResolvedAddress;

typedef struct ResolverResult {
    int count, retryable;
    char error[192];
    ResolvedAddress addresses[NET_ADDR_MAX];
} ResolverResult;

struct Net {
    int fd, tls, address_index, retryable, address_retryable;
    int write_retry;
    pid_t resolver;
    NetState state;
    short handshake_want, read_want, write_want;
    int64_t deadline, address_deadline;
    char host[256], error[256];
    ResolverResult resolved;
    size_t resolved_bytes;
    SSL_CTX *ctx;
    SSL *ssl;
};

int64_t net_now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void error_out(char *out, size_t cap, const char *message)
{
    if (out && cap)
        (void)snprintf(out, cap, "%s", message);
}

static int failed(Net *n, int retryable, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(n->error, sizeof n->error, fmt, ap);
    va_end(ap);
    n->retryable = !!retryable;
    n->state = NET_FAILED;
    return -1;
}

/* Retry a new connection only for identified transport failures. Invalid
 * descriptors, permissions, unsupported families and TLS policy failures are
 * held for the caller to correct, even when automatic reconnect is enabled. */
static int transient_errno(int error)
{
    switch (error) {
    case EINTR: case EAGAIN: case ECONNREFUSED: case ECONNRESET:
    case ECONNABORTED: case ETIMEDOUT: case EHOSTUNREACH:
    case ENETUNREACH: case ENETDOWN: case ENETRESET:
    case EPIPE: case EADDRNOTAVAIL: case ENOBUFS:
#ifdef EHOSTDOWN
    case EHOSTDOWN:
#endif
        return 1;
    default:
        return 0;
    }
}

static int tls_retryable(Net *n, int ssl_error, int result, int saved_errno)
{
    unsigned long reason;
    if (SSL_get_verify_result(n->ssl) != X509_V_OK)
        return 0;
    reason = ERR_peek_error();
    if (ssl_error == SSL_ERROR_ZERO_RETURN)
        return 1;
    if (ssl_error == SSL_ERROR_SYSCALL) {
        unsigned long queued;
        int queued_errno = 0, saw_system = 0;
        if (!reason)
            return (result == 0 && saved_errno == 0) || transient_errno(saved_errno);
        /* Newer OpenSSL can put the socket errno on the error queue as an
         * ERR_LIB_SYS entry while still returning SSL_ERROR_SYSCALL.  Treat
         * that as transport information only when every queued error is a
         * consistent system errno.  Any TLS/library error or disagreement is
         * held rather than widening the reconnect policy. */
        while ((queued = ERR_get_error()) != 0) {
            int current_errno;
            if (ERR_GET_LIB(queued) != ERR_LIB_SYS)
                return 0;
            current_errno = ERR_GET_REASON(queued);
            if (!current_errno || (queued_errno && queued_errno != current_errno))
                return 0;
            queued_errno = current_errno;
            saw_system = 1;
        }
        if (!saw_system || (saved_errno && saved_errno != queued_errno))
            return 0;
        return transient_errno(saved_errno ? saved_errno : queued_errno);
    }
#ifdef SSL_R_UNEXPECTED_EOF_WHILE_READING
    /* OpenSSL 3 reports an unnotified transport EOF as SSL_ERROR_SSL.
     * Other TLS protocol errors remain fatal; never weaken verification. */
    if (ssl_error == SSL_ERROR_SSL && ERR_GET_LIB(reason) == ERR_LIB_SSL &&
        ERR_GET_REASON(reason) == SSL_R_UNEXPECTED_EOF_WHILE_READING)
        return 1;
#endif
    return 0;
}

static int set_fd_flags(int fd, int nonblock)
{
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        return 0;
    if (nonblock) {
        flags = fcntl(fd, F_GETFL);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
            return 0;
    }
    return 1;
}

/* Resolver lifetime and child-only descriptor isolation. */
static void reap_resolver(Net *n, int stop)
{
    pid_t result;
    if (n->resolver <= 0)
        return;
    if (stop)
        (void)kill(n->resolver, SIGKILL);
    do {
        result = waitpid(n->resolver, NULL, stop ? 0 : WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == n->resolver || (result < 0 && errno == ECHILD))
        n->resolver = 0;
}

static int resolver_close_directory(const char *path)
{
    int64_t started = net_now_ms();
    if (started <= 0) {
        errno = EIO;
        return 0;
    }
    DIR *directory = opendir(path);
    if (!directory) return 0;
    int directory_fd = dirfd(directory), saw_directory = 0, saw_result = 0;
    int saved = 0;
    if (directory_fd < 0) saved = errno;
    while (!saved) {
        int64_t now = net_now_ms();
        if (now <= 0 || now - started >= NET_FD_CLEANUP_MS) {
            saved = ETIMEDOUT;
            break;
        }
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            saved = errno;
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char *end;
        errno = 0;
        long value = strtol(entry->d_name, &end, 10);
        if (errno || !*entry->d_name || *end || value < 0 || value > INT_MAX) {
            saved = EIO;
            break;
        }
        int fd = (int)value;
        if (fd == directory_fd) {
            saw_directory = 1;
            continue;
        }
        if (fd == 3) {
            saw_result = 1;
            continue;
        }
        if (fd > 3 && close(fd) < 0 && errno != EBADF) {
            saved = errno;
            break;
        }
    }
    if (closedir(directory) < 0 && !saved) saved = errno;
    if (!saved && (!saw_directory || !saw_result)) saved = EIO;
    if (saved) {
        errno = saved;
        return 0;
    }
    return 1;
}

static int resolver_isolate(int *result_fd)
{
    /* A fork without exec retains CLOEXEC descriptors. Keep only this pipe,
     * so a slow lookup cannot retain other sessions or partial-transfer files.
     * Neither RLIMIT_NOFILE nor _SC_OPEN_MAX bounds descriptors that were
     * already open before the limit was lowered. Never guess an upper fd. */
    if (*result_fd != 3) {
        if (dup2(*result_fd, 3) < 0) return 0;
        *result_fd = 3;
    }
    if (!set_fd_flags(3, 0)) return 0;
    for (int fd = 0; fd < 3; ++fd)
        if (close(fd) < 0 && errno != EBADF) return 0;
#if defined(__linux__) && defined(SYS_close_range)
    if (syscall(SYS_close_range, 4u, UINT_MAX, 0u) == 0) return 1;
#elif defined(__OpenBSD__) || defined(__NetBSD__)
    if (closefrom(4) == 0 || errno == EBADF) return 1;
#elif defined(__FreeBSD__)
    closefrom(4);
    return 1;
#endif
    /* Older kernels and other Unix systems can expose their actual fd table.
     * The deadline bounds this fallback; an unavailable/incomplete listing is
     * an explicit connection failure, never permission to resolve with leaks. */
    if (resolver_close_directory("/proc/self/fd")) return 1;
    return resolver_close_directory("/dev/fd");
}

/* The resolver reports a fixed-size address/result
 * structure over its private pipe. It must isolate inherited descriptors
 * before getaddrinfo, because the parent may have live sessions/files. */
static void resolve_child(int fd, const char *host, const char *service)
{
    struct addrinfo hints, *list = NULL, *p;
    ResolverResult r;
    size_t sent = 0;
    int result;
    memset(&hints, 0, sizeof hints);
    memset(&r, 0, sizeof r);
    if (!resolver_isolate(&fd)) {
        (void)snprintf(r.error, sizeof r.error, "Cannot isolate resolver descriptors: %s", strerror(errno));
        goto send_result;
    }
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    errno = 0;
    result = getaddrinfo(host, service, &hints, &list);
    if (result != 0) {
        r.retryable = result == EAI_AGAIN ||
            (result == EAI_SYSTEM && transient_errno(errno));
        (void)snprintf(r.error, sizeof r.error, "Name lookup failed: %s", gai_strerror(result));
    } else {
        for (p = list; p && r.count < NET_ADDR_MAX; p = p->ai_next) {
            ResolvedAddress *a;
            if ((p->ai_family != AF_INET && p->ai_family != AF_INET6) ||
                p->ai_addrlen > sizeof(struct sockaddr_storage))
                continue;
            a = &r.addresses[r.count++];
            memcpy(&a->address, p->ai_addr, p->ai_addrlen);
            a->length = (socklen_t)p->ai_addrlen;
            a->family = p->ai_family;
        }
        freeaddrinfo(list);
        if (r.count == 0)
            (void)snprintf(r.error, sizeof r.error, "No usable IPv4 or IPv6 server address");
    }
send_result:
    while (sent < sizeof r) {
        ssize_t wrote = write(fd, (const char *)&r + sent, sizeof r - sent);
        if (wrote > 0)
            sent += (size_t)wrote;
        else if (wrote < 0 && errno == EINTR)
            continue;
        else
            break;
    }
    close(fd);
    _exit(0);
}

static int begin_resolve(Net *n, int port)
{
    int fds[2];
    char service[6];
    pid_t pid;
    struct sigaction action;
    /* Cancellation and detach depend on owning an unreaped child. Inherited
     * auto-reaping can discard that identity before its result is collected. */
    if (sigaction(SIGCHLD, NULL, &action) < 0)
        return failed(n, 0, "Cannot inspect resolver child ownership: %s", strerror(errno));
    if (action.sa_handler == SIG_IGN
#ifdef SA_NOCLDWAIT
        || (action.sa_flags & SA_NOCLDWAIT)
#endif
       )
        return failed(n, 0, "Resolver requires waitable children; restore default SIGCHLD handling in the launcher");
    if (pipe(fds) < 0)
        return failed(n, 0, "Cannot create resolver pipe: %s", strerror(errno));
    if (!set_fd_flags(fds[0], 1) || !set_fd_flags(fds[1], 0)) {
        int saved = errno;
        close(fds[0]);
        close(fds[1]);
        return failed(n, 0, "Cannot protect resolver descriptors: %s", strerror(saved));
    }
    (void)snprintf(service, sizeof service, "%d", port);
    pid = fork();
    if (pid < 0) {
        int saved = errno;
        close(fds[0]);
        close(fds[1]);
        return failed(n, 0, "Cannot start resolver: %s", strerror(saved));
    }
    if (pid == 0) {
        close(fds[0]);
        resolve_child(fds[1], n->host, service);
    }
    close(fds[1]);
    n->fd = fds[0];
    n->resolver = pid;
    n->state = NET_RESOLVING;
    return 0;
}

/* Address progression and verified TLS setup. */
static int begin_tls(Net *n)
{
    X509_VERIFY_PARAM *param;
    unsigned char addr[sizeof(struct in6_addr)];
    n->ssl = SSL_new(n->ctx);
    if (!n->ssl)
        return failed(n, 0, "Cannot allocate TLS connection");
    if (SSL_set_fd(n->ssl, n->fd) != 1)
        return failed(n, 0, "Cannot bind TLS socket");
    SSL_set_mode(n->ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    /* Yield after TLS control records as well as application data. Otherwise
     * a peer can keep one SSL_read busy with an unbounded stream of TLS 1.3
     * KeyUpdates, bypassing the caller's per-turn read budget. The direct
     * socket BIO must not read ahead: unprocessed records stay poll-readable,
     * while SSL_pending reports any already decoded application bytes. */
    SSL_clear_mode(n->ssl, SSL_MODE_AUTO_RETRY);
    SSL_set_read_ahead(n->ssl, 0);
    param = SSL_get0_param(n->ssl);
    X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    if (inet_pton(AF_INET, n->host, addr) == 1 ||
        inet_pton(AF_INET6, n->host, addr) == 1) {
        if (X509_VERIFY_PARAM_set1_ip_asc(param, n->host) != 1)
            return failed(n, 0, "Cannot set TLS IP identity");
    } else {
        if (X509_VERIFY_PARAM_set1_host(param, n->host, 0) != 1 ||
            SSL_set_tlsext_host_name(n->ssl, n->host) != 1)
            return failed(n, 0, "Cannot set TLS server identity");
    }
    n->state = NET_HANDSHAKE;
    n->handshake_want = POLLOUT;
    return 0;
}

static int connected(Net *n)
{
    if (n->tls)
        return begin_tls(n);
    n->state = NET_READY;
    return 0;
}

static int next_address(Net *n, int64_t now, int previous_error)
{
    int saved = previous_error ? previous_error : ECONNREFUSED;
    if (previous_error && transient_errno(previous_error))
        n->address_retryable = 1;
    if (n->fd >= 0)
        close(n->fd);
    n->fd = -1;
    while (n->address_index < n->resolved.count) {
        ResolvedAddress *a = &n->resolved.addresses[n->address_index++];
        int result;
        n->fd = socket(a->family, SOCK_STREAM, IPPROTO_TCP);
        if (n->fd < 0) {
            saved = errno;
            if (transient_errno(saved)) n->address_retryable = 1;
            continue;
        }
#ifdef SO_NOSIGPIPE
        {
            int one = 1;
            (void)setsockopt(n->fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
        }
#endif
        if (!set_fd_flags(n->fd, 1)) {
            saved = errno;
            if (transient_errno(saved)) n->address_retryable = 1;
            close(n->fd);
            n->fd = -1;
            continue;
        }
        result = connect(n->fd, (struct sockaddr *)&a->address, a->length);
        if (result == 0)
            return connected(n);
        if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
            n->state = NET_CONNECTING;
            n->address_deadline = now + NET_ADDRESS_MS;
            return 0;
        }
        saved = errno;
        if (transient_errno(saved)) n->address_retryable = 1;
        close(n->fd);
        n->fd = -1;
    }
    /* One temporarily unavailable address can become usable later even if
     * the last candidate is permanently unsupported (for example IPv6).
     * This flag is consulted only after every candidate has failed; TLS
     * policy failures on an established candidate remain separate holds. */
    return failed(n, n->address_retryable, "Connection failed: %s", strerror(saved));
}

static int ca_no_password(char *buffer, int size, int writing, void *argument)
{
    (void)buffer;
    (void)size;
    (void)writing;
    (void)argument;
    return 0;
}

static int ca_store_added(int result)
{
    if (result == 1) return 1;
    /* Some supported TLS libraries report duplicate bundle entries as an
     * error. Accept only that specific condition, never other store errors. */
    unsigned long error = ERR_peek_last_error();
    if (ERR_GET_LIB(error) != ERR_LIB_X509 ||
        ERR_GET_REASON(error) != X509_R_CERT_ALREADY_IN_HASH_TABLE) return 0;
    ERR_clear_error();
    return 1;
}

/* Load a bounded custom trust store through one checked descriptor. */
static int load_ca_file(Net *n, const char *path)
{
    const char *message = "Cannot read custom TLS CA file";
    unsigned char *bytes = NULL;
    BIO *bio = NULL;
    STACK_OF(X509_INFO) *objects = NULL;
    size_t size = 0, used = 0;
    int fd = -1, ok = 0, recognized = 0;
    struct stat info;

    /* Open once and inspect that descriptor. O_NONBLOCK prevents a FIFO
     * from hanging before fstat; the regular-file and size checks exclude
     * devices and unbounded streams. Symlinks to regular trust files remain
     * supported, without reopening their path after the checks. This pins
     * file identity, not its contents: trust material must remain trusted.
     * O_NONBLOCK does not bound regular-file or filesystem lookup latency. */
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0 || fstat(fd, &info) < 0) goto done;
    message = "Custom TLS CA file must be a nonempty regular file, at most 16 MiB";
    if (!S_ISREG(info.st_mode) || info.st_size <= 0 ||
        info.st_size > (off_t)NET_CA_BYTES_MAX) goto done;
    size = (size_t)info.st_size;
    message = "Out of memory loading custom TLS CA file";
    bytes = malloc(size + 1);
    if (!bytes) goto done;
    message = "Cannot read custom TLS CA file, or its size changed while reading";
    while (used < size + 1) {
        ssize_t got = read(fd, bytes + used, size + 1 - used);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) goto done;
        if (!got) break;
        used += (size_t)got;
    }
    if (used != size) goto done;
    (void)close(fd);
    fd = -1;

    /* Let the TLS library decode certificates, AUX trust and CRLs, as its
     * file loader does. A memory BIO cannot reopen the path or wait for an
     * endless input stream; a rejecting callback prevents password prompts. */
    message = "Cannot parse trusted TLS certificates in custom CA file";
    ERR_clear_error();
    bio = BIO_new_mem_buf(bytes, (int)size);
    if (!bio) goto done;
    objects = PEM_X509_INFO_read_bio(bio, NULL, ca_no_password, NULL);
    if (!objects) goto done;
    X509_STORE *store = SSL_CTX_get_cert_store(n->ctx);
    if (!store) goto done;
    for (int i = 0; i < sk_X509_INFO_num(objects); ++i) {
        X509_INFO *object = sk_X509_INFO_value(objects, i);
        if (object->x509) {
            ERR_clear_error();
            if (!ca_store_added(X509_STORE_add_cert(store, object->x509))) goto done;
            recognized = 1;
        }
        if (object->crl) {
            ERR_clear_error();
            if (!ca_store_added(X509_STORE_add_crl(store, object->crl))) goto done;
            recognized = 1;
        }
    }
    ok = recognized;
done:
    sk_X509_INFO_pop_free(objects, X509_INFO_free);
    BIO_free(bio);
    if (bytes) {
        OPENSSL_cleanse(bytes, size + 1);
        free(bytes);
    }
    if (fd >= 0) (void)close(fd);
    ERR_clear_error();
    return ok ? 1 : failed(n, 0, "%s", message);
}

/* Opening is asynchronous: success returns an owned Net,
 * not an established IRC connection. Trust roots are loaded here;
 * peer identity is pinned at TLS setup before the handshake. Failure
 * cleans up every resource allocated by this call. */
Net *net_open(const char *host, int port, int tls, const char *ca_file,
              char *error, size_t error_cap)
{
    Net *n;
    const unsigned char *p;
    if (!host || !*host || strlen(host) >= 256 || port < 1 || port > 65535) {
        error_out(error, error_cap, "Invalid server name or port");
        return NULL;
    }
    for (p = (const unsigned char *)host; *p; ++p) {
        if (*p <= 32 || *p >= 127 || *p == '/' || *p == '\\' || *p == '%') {
            error_out(error, error_cap, "Server must be an ASCII hostname or IP address");
            return NULL;
        }
    }
    n = calloc(1, sizeof *n);
    if (!n) {
        error_out(error, error_cap, "Out of memory starting connection");
        return NULL;
    }
    n->fd = -1;
    n->tls = !!tls;
    n->deadline = net_now_ms() + NET_SETUP_MS;
    n->read_want = POLLIN;
    n->write_want = POLLOUT;
    (void)snprintf(n->host, sizeof n->host, "%s", host);
    if (tls) {
        n->ctx = SSL_CTX_new(TLS_client_method());
        if (!n->ctx) {
            failed(n, 0, "Cannot initialize TLS");
            goto fail;
        }
        SSL_CTX_set_verify(n->ctx, SSL_VERIFY_PEER, NULL);
        if (SSL_CTX_set_min_proto_version(n->ctx, TLS1_2_VERSION) != 1) {
            failed(n, 0, "Cannot enforce TLS 1.2 minimum");
            goto fail;
        }
        (void)SSL_CTX_set_options(n->ctx, SSL_OP_NO_COMPRESSION);
#ifdef SSL_OP_NO_RENEGOTIATION
        (void)SSL_CTX_set_options(n->ctx, SSL_OP_NO_RENEGOTIATION);
#endif
        if (ca_file && *ca_file) {
            if (load_ca_file(n, ca_file) < 0) goto fail;
        } else if (SSL_CTX_set_default_verify_paths(n->ctx) != 1) {
            failed(n, 0, "Cannot load trusted TLS certificates");
            goto fail;
        }
    }
    if (begin_resolve(n, port) < 0)
        goto fail;
    return n;
fail:
    error_out(error, error_cap, n->error);
    net_close(n);
    return NULL;
}

int net_fd(const Net *n)
{
    return n ? n->fd : -1;
}
NetState net_state(const Net *n)
{
    return n ? n->state : NET_FAILED;
}
const char *net_error(const Net *n)
{
    return n ? n->error : "No connection";
}
int net_retryable(const Net *n)
{
    return n && n->state == NET_FAILED && n->retryable;
}

/* Public poll contract and bounded nonblocking byte transfer. */
short net_events(const Net *n, int want_write)
{
    if (!n)
        return 0;
    switch (n->state) {
    case NET_RESOLVING: return POLLIN;
    case NET_CONNECTING: return POLLOUT;
    case NET_HANDSHAKE: return n->handshake_want;
    case NET_READY:
        /* SSL_write WANT_WRITE must be retried before any other TLS I/O.
         * Readable peer data must not spin the poll loop during that wait. */
        if (n->write_retry && n->write_want == POLLOUT) return POLLOUT;
        return n->read_want | (want_write ? n->write_want : 0);
    default: return 0;
    }
}

/* The caller supplies poll readiness plus a monotonic
 * timestamp. Zero means ready OR still progressing; check net_state.
 * After each transition, obtain net_fd/net_events again: DNS uses a pipe
 * while the connected transport uses a different descriptor. */
int net_step(Net *n, short revents, int64_t now)
{
    if (!n || n->state == NET_FAILED)
        return -1;
    reap_resolver(n, 0);
    if (n->state != NET_READY && now >= n->deadline) {
        reap_resolver(n, 1);
        return failed(n, 1, "Connection setup timed out after 30 seconds");
    }
    if (n->state == NET_RESOLVING && (revents & (POLLIN | POLLHUP | POLLERR))) {
        ssize_t got;
        do {
            got = read(n->fd, (char *)&n->resolved + n->resolved_bytes,
                       sizeof n->resolved - n->resolved_bytes);
            if (got > 0)
                n->resolved_bytes += (size_t)got;
        } while ((got > 0 || (got < 0 && errno == EINTR)) &&
                 n->resolved_bytes < sizeof n->resolved);
        if (n->resolved_bytes == sizeof n->resolved) {
            if (n->resolved.retryable != 0 && n->resolved.retryable != 1)
                return failed(n, 0, "Resolver returned an invalid retry status");
            if (n->resolved.count <= 0 || n->resolved.count > NET_ADDR_MAX)
                return failed(n, n->resolved.count == 0 && n->resolved.retryable,
                              "%s", n->resolved.error[0] ? n->resolved.error : "Name lookup failed");
            return next_address(n, now, 0);
        }
        if (got == 0 || (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
            return failed(n, 0, "Resolver stopped before returning a complete result");
    } else if (n->state == NET_CONNECTING) {
        if (revents & (POLLOUT | POLLERR | POLLHUP)) {
            int status = 0;
            socklen_t length = sizeof status;
            if (getsockopt(n->fd, SOL_SOCKET, SO_ERROR, &status, &length) < 0)
                return next_address(n, now, errno);
            if (status)
                return next_address(n, now, status);
            return connected(n);
        }
        if (now >= n->address_deadline)
            return next_address(n, now, ETIMEDOUT);
    } else if (n->state == NET_HANDSHAKE && (revents & (POLLIN | POLLOUT | POLLERR | POLLHUP))) {
        int result, ssl_error, saved_errno;
        ERR_clear_error();
        errno = 0;
        result = SSL_connect(n->ssl);
        saved_errno = errno;
        if (result == 1) {
            if (SSL_get_verify_result(n->ssl) != X509_V_OK)
                return failed(n, 0, "TLS peer certificate did not verify");
            n->state = NET_READY;
            return 0;
        }
        ssl_error = SSL_get_error(n->ssl, result);
        if (ssl_error == SSL_ERROR_WANT_READ)
            n->handshake_want = POLLIN;
        else if (ssl_error == SSL_ERROR_WANT_WRITE)
            n->handshake_want = POLLOUT;
        else {
            long verification = SSL_get_verify_result(n->ssl);
            if (verification != X509_V_OK)
                return failed(n, 0, "TLS certificate rejected: %s", X509_verify_cert_error_string(verification));
            return failed(n, tls_retryable(n, ssl_error, result, saved_errno),
                          "TLS handshake failed (no plaintext fallback)");
        }
    }
    if (revents & POLLNVAL)
        return failed(n, 0, "Connection descriptor became invalid");
    return 0;
}

ssize_t net_read(Net *n, void *out, size_t cap)
{
    ssize_t result;
    if (!n || n->state != NET_READY || cap == 0)
        return -1;
    if (n->write_retry && n->write_want == POLLOUT)
        return -2;
    if (cap > INT_MAX)
        cap = INT_MAX;
    if (!n->tls) {
        result = recv(n->fd, out, cap, 0);
        if (result >= 0)
            return result;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return -2;
        int saved = errno;
        return failed(n, transient_errno(saved), "Socket read failed: %s", strerror(saved));
    }
    ERR_clear_error();
    errno = 0;
    result = SSL_read(n->ssl, out, (int)cap);
    int saved_errno = errno;
    if (result > 0) {
        n->read_want = POLLIN;
        return result;
    }
    int ssl_error = SSL_get_error(n->ssl, (int)result);
    switch (ssl_error) {
    case SSL_ERROR_WANT_READ:
        n->read_want = POLLIN;
        return -2;
    case SSL_ERROR_WANT_WRITE:
        n->read_want = POLLOUT;
        return -2;
    case SSL_ERROR_ZERO_RETURN: return 0;
    default: return failed(n, tls_retryable(n, ssl_error, (int)result, saved_errno),
                           "TLS read failed or connection ended without a close notification");
    }
}

ssize_t net_write(Net *n, const void *data, size_t size)
{
    ssize_t result;
    if (!n || n->state != NET_READY || size == 0)
        return -1;
    if (size > INT_MAX)
        size = INT_MAX;
    if (!n->tls) {
#ifdef MSG_NOSIGNAL
        result = send(n->fd, data, size, MSG_NOSIGNAL);
#else
        result = send(n->fd, data, size, 0);
#endif
        if (result >= 0)
            return result;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return -2;
        int saved = errno;
        return failed(n, transient_errno(saved), "Socket write failed: %s", strerror(saved));
    }
    ERR_clear_error();
    errno = 0;
    result = SSL_write(n->ssl, data, (int)size);
    int saved_errno = errno;
    if (result > 0) {
        n->write_retry = 0;
        n->write_want = POLLOUT;
        return result;
    }
    int ssl_error = SSL_get_error(n->ssl, (int)result);
    switch (ssl_error) {
    case SSL_ERROR_WANT_READ:
        n->write_retry = 1;
        n->write_want = POLLIN;
        return -2;
    case SSL_ERROR_WANT_WRITE:
        n->write_retry = 1;
        n->write_want = POLLOUT;
        return -2;
    default: return failed(n, tls_retryable(n, ssl_error, (int)result, saved_errno),
                           "TLS write failed");
    }
}

/* Buffered TLS plaintext is not always safe to drain
 * immediately. A pending SSL_write retry can require a writable socket;
 * honoring that ordering avoids reentry and poll starvation. */
int net_pending(const Net *n)
{
    if (n && n->write_retry && n->write_want == POLLOUT) return 0;
    return n && n->ssl && n->state == NET_READY ? SSL_pending(n->ssl) : 0;
}

/* Detach may proceed only when the resolver child
 * has been reaped. A lingering resolver must not remain attached to
 * the caller-owned foreground process during handoff. */
int net_detach_ready(Net *n)
{
    if (!n) return 1;
    reap_resolver(n, 0);
    return n->resolver <= 0;
}

void net_close(Net *n)
{
    if (!n)
        return;
    reap_resolver(n, 1);
    /* Do not block the UI for TLS shutdown; close the transport immediately. */
    if (n->ssl)
        SSL_free(n->ssl);
    if (n->ctx)
        SSL_CTX_free(n->ctx);
    if (n->fd >= 0)
        close(n->fd);
    memset(n, 0, sizeof *n);
    free(n);
}
