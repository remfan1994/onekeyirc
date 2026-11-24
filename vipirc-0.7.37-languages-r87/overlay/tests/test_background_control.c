/* Deterministic control-protocol tests against the production implementation.
 * Socket operations and socket-file type are fixtures; files, descriptors,
 * leases, terminal descriptors and process signal handlers are real. This
 * suite does not claim native AF_UNIX or SCM_RIGHTS execution. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

static int fixture_socket(int, int, int);
static int fixture_connect(int, const struct sockaddr *, socklen_t);
static int fixture_accept(int, struct sockaddr *, socklen_t *);
static int fixture_getsockopt(int, int, int, void *, socklen_t *);
#if !defined(__linux__)
static int fixture_getpeereid(int, uid_t *, gid_t *);
#endif
static ssize_t fixture_send(int, const void *, size_t, int);
static ssize_t fixture_recv(int, void *, size_t, int);
static ssize_t fixture_sendmsg(int, const struct msghdr *, int);
static ssize_t fixture_recvmsg(int, struct msghdr *, int);
static int fixture_poll(struct pollfd *, nfds_t, int);
static int fixture_fstatat(int, const char *, struct stat *, int);
#define socket fixture_socket
#define connect fixture_connect
#define accept fixture_accept
#define getsockopt fixture_getsockopt
#if !defined(__linux__)
#define getpeereid fixture_getpeereid
#endif
#define send fixture_send
#define recv fixture_recv
#define sendmsg fixture_sendmsg
#define recvmsg fixture_recvmsg
#define poll fixture_poll
#define fstatat fixture_fstatat
/* Include the generated localization lookup for this single-file IPC fixture.
 * The production executable instead defines it in idle_setup.c. */
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#undef IRC_L10N_IMPLEMENTATION
#include "../src/background.c"
#undef socket
#undef connect
#undef accept
#undef getsockopt
#if !defined(__linux__)
#undef getpeereid
#endif
#undef send
#undef recv
#undef sendmsg
#undef recvmsg
#undef poll
#undef fstatat

static int quiet_fd, accepted_fd = -1, last_socket = -1, sockets_opened;
static int peer_allowed = 1, virtual_endpoint, legacy_peer, hup_peer, hup_injected;
static int mutate_terminal_flags;
static int receive_eof, completion_lease = -1, completion_checked;
static unsigned poll_calls;
static int64_t fixture_time;
static unsigned char received[1024], packet[sizeof(AttachRequest)];
static size_t received_size, received_used, packet_size, packet_used;
static size_t read_fragment, packet_fragment, write_fragment;
static int rights[4];
static size_t rights_count;
static unsigned char sent_request[sizeof(AttachRequest)];
static size_t sent_request_size;
static char query_magics[4][9];
static size_t query_count;
static char endpoint[BG_PATH_SIZE];
static unsigned char expected_token[16];
static struct { int fd; size_t size; unsigned char bytes[sizeof(AttachRequest)]; } writes[256];
static size_t write_count;

int net_detach_ready(Net *n) { (void)n; return 1; }
void credential_reap(void) { }
int credential_cleanup_pending(void) { return 0; }
int64_t net_now_ms(void) { fixture_time += 25; return fixture_time; }

static void reset_fixture(void)
{
    assert(accepted_fd < 0);
    last_socket = -1; sockets_opened = 0; peer_allowed = 1;
    virtual_endpoint = legacy_peer = hup_peer = hup_injected = receive_eof = 0;
    mutate_terminal_flags = 0;
    completion_lease = -1; completion_checked = 0;
    poll_calls = 0; fixture_time = 10000;
    received_size = received_used = packet_size = packet_used = 0;
    read_fragment = packet_fragment = write_fragment = rights_count = 0;
    sent_request_size = query_count = write_count = 0;
    endpoint[0] = '\0';
}

static int fixture_socket(int domain, int type, int protocol)
{
    assert(domain == AF_UNIX && type == SOCK_STREAM && !protocol);
    last_socket = dup(quiet_fd);
    assert(last_socket >= 0);
    sent_request_size = 0;
    ++sockets_opened;
    return last_socket;
}
static int fixture_connect(int fd, const struct sockaddr *address, socklen_t size)
{
    const struct sockaddr_un *local = (const struct sockaddr_un *)address;
    assert(fd == last_socket && size == sizeof *local && local->sun_family == AF_UNIX);
    assert(!strcmp(local->sun_path, endpoint));
    return 0;
}
static int fixture_accept(int fd, struct sockaddr *address, socklen_t *size)
{
    (void)fd; (void)address; (void)size;
    int result = accepted_fd; accepted_fd = -1;
    if (result < 0) errno = EAGAIN;
    return result;
}
static int fixture_getsockopt(int fd, int level, int option, void *value, socklen_t *size)
{
    (void)fd;
    assert(level == SOL_SOCKET);
#ifdef __linux__
    if (option == SO_PEERCRED) {
        struct ucred peer = {getpid(), peer_allowed ? getuid() : getuid() + 1u, getgid()};
        assert(*size >= sizeof peer);
        memcpy(value, &peer, sizeof peer); *size = sizeof peer;
        return 0;
    }
#endif
    assert(option == SO_ERROR && *size >= sizeof(int));
    *(int *)value = 0; *size = sizeof(int);
    return 0;
}
#ifndef __linux__
static int fixture_getpeereid(int fd, uid_t *uid, gid_t *gid)
{
    (void)fd; *uid = peer_allowed ? getuid() : getuid() + 1u; *gid = getgid(); return 0;
}
#endif
static ssize_t fixture_send(int fd, const void *data, size_t size, int flags)
{
    assert(!flags && write_count < sizeof writes / sizeof writes[0]);
    if (write_fragment && size > write_fragment) size = write_fragment;
    assert(size <= sizeof writes[0].bytes);
    writes[write_count].fd = fd; writes[write_count].size = size;
    memcpy(writes[write_count++].bytes, data, size);
    if (size == 2 && !memcmp(data, "C\0", 2) && completion_lease >= 0) {
        assert(fcntl(completion_lease, F_GETFD) < 0 && errno == EBADF);
        completion_checked = 1;
    }
    if (fd == last_socket && size > 1) {
        assert(sent_request_size + size <= sizeof sent_request);
        memcpy(sent_request + sent_request_size, data, size); sent_request_size += size;
        if (legacy_peer && ((sent_request_size == sizeof(AttachRequest) &&
                             !memcmp(sent_request, "VIPIRCQ2", 8)) ||
                            (sent_request_size == 8 && !memcmp(sent_request, "VIPIRCQ1", 8)))) {
            assert(query_count < 4);
            memcpy(query_magics[query_count], sent_request, 8); query_magics[query_count++][8] = 0;
            received_used = 0;
            if (!memcmp(sent_request, "VIPIRCQ2", 8)) {
                memcpy(received, "E\2", 2); received_size = 2;
            } else {
                SessionReply reply;
                memset(&reply, 0, sizeof reply);
                memcpy(reply.magic, "VIPIRCS1", 8);
                memcpy(reply.program, BG_WIRE_PROGRAM, sizeof BG_WIRE_PROGRAM);
                reply.job = 7; strcpy(reply.label, "legacy fixture");
                memcpy(received, &reply, sizeof reply); received_size = sizeof reply;
            }
        }
    }
    return (ssize_t)size;
}
static ssize_t fixture_recv(int fd, void *data, size_t size, int flags)
{
    (void)fd; assert(!flags);
    if (received_used == received_size) {
        if (receive_eof) return 0;
        errno = EAGAIN; return -1;
    }
    if (size > received_size - received_used) size = received_size - received_used;
    if (read_fragment && size > read_fragment) size = read_fragment;
    memcpy(data, received + received_used, size); received_used += size;
    if (hup_peer && !hup_injected && received_used == received_size) {
        foreground_signals |= FG_HUP;
        hup_injected = 1;
    }
    return (ssize_t)size;
}
static ssize_t fixture_sendmsg(int fd, const struct msghdr *message, int flags)
{
    assert(message->msg_iovlen == 1 && message->msg_control && !flags);
    assert(message->msg_iov[0].iov_len <= sizeof(AttachRequest));
    if (mutate_terminal_flags) {
        for (int terminal = 0; terminal < 2; ++terminal) {
            int current = fcntl(terminal, F_GETFL);
            assert(current >= 0 && fcntl(terminal, F_SETFL, current | O_NONBLOCK) == 0);
        }
    }
    return fixture_send(fd, message->msg_iov[0].iov_base, message->msg_iov[0].iov_len, 0);
}
static ssize_t fixture_recvmsg(int fd, struct msghdr *message, int flags)
{
    (void)fd; assert(!flags && message->msg_iovlen == 1);
    size_t size = packet_size - packet_used;
    if (size > message->msg_iov[0].iov_len) size = message->msg_iov[0].iov_len;
    if (packet_fragment && size > packet_fragment) size = packet_fragment;
    memcpy(message->msg_iov[0].iov_base, packet + packet_used, size); packet_used += size;
    message->msg_flags = 0;
    if (rights_count) {
        assert(message->msg_controllen >= CMSG_SPACE(rights_count * sizeof(int)));
        struct cmsghdr *c = CMSG_FIRSTHDR(message);
        c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(rights_count * sizeof(int));
        memcpy(CMSG_DATA(c), rights, rights_count * sizeof(int));
        message->msg_controllen = CMSG_SPACE(rights_count * sizeof(int));
        rights_count = 0;
    } else message->msg_controllen = 0;
    return (ssize_t)size;
}
static int fixture_poll(struct pollfd *fds, nfds_t count, int timeout)
{
    (void)timeout;
    assert(++poll_calls < 500);
    for (nfds_t i = 0; i < count; ++i) fds[i].revents = fds[i].events;
    return (int)count;
}
static int fixture_fstatat(int directory, const char *name, struct stat *st, int flags)
{
    int result = fstatat(directory, name, st, flags);
    if (result == 0 && virtual_endpoint && !strcmp(name, "session"))
        st->st_mode = (st->st_mode & 07777) | S_IFSOCK;
    return result;
}

static Background *worker(int attached)
{
    Background *b = background_create(); assert(b);
    b->listener = dup(quiet_fd); assert(b->listener >= 0);
    b->managed = 1; b->detached = !attached; b->job = 7;
    memset(b->token, 0x7a, sizeof b->token);
    strcpy(b->label, "fixture @ localhost (1 server)");
    if (attached) { b->controller = dup(quiet_fd); assert(b->controller >= 0); }
    return b;
}
static int start_request(Background *b, const char *magic, int token)
{
    assert(b->candidate < 0);
    accepted_fd = dup(quiet_fd); assert(accepted_fd >= 0);
    int fd = accepted_fd;
    accept_candidate(b); assert(b->candidate == fd);
    AttachRequest request;
    memset(&request, 0, sizeof request); memcpy(request.magic, magic, 8);
    if (token) memcpy(request.token, b->token, sizeof request.token);
    memcpy(packet, &request, sizeof request); packet_size = sizeof request; packet_used = 0;
    return fd;
}
static unsigned drive_request(Background *b)
{
    unsigned events = 0;
    for (unsigned n = 0; b->candidate >= 0 && !events; ++n) {
        assert(n < 100);
        events = receive_candidate(b);
    }
    return events;
}
static void expect_reply(size_t index, int fd, const char *bytes, size_t length)
{
    assert(index < write_count && writes[index].fd == fd && writes[index].size == length);
    assert(!memcmp(writes[index].bytes, bytes, length));
}

static void attached_status_and_controller_loss(void)
{
    reset_fixture();
    Background *b = worker(1);
    int controller = b->controller;
    int candidate = start_request(b, "VIPIRCQ2", 0);
    struct pollfd fds[BACKGROUND_POLL_CAP];
    assert(background_pollfds(b, fds, BACKGROUND_POLL_CAP) == 3);
    assert(!background_pollfds(b, fds, 2));
    assert(!drive_request(b));
    assert(write_count == 1 && writes[0].size == sizeof(SessionReply));
    SessionReply reply; memcpy(&reply, writes[0].bytes, sizeof reply);
    assert(!memcmp(reply.magic, "VIPIRCA2", 8) && reply.job == 7);
    assert(!memcmp(reply.token, b->token, sizeof reply.token));
    assert(b->controller == controller && !b->detached);

    write_count = 0; candidate = start_request(b, "VIPIRCB3", 1);
    assert(!drive_request(b)); expect_reply(0, candidate, "E\1", 2);
    assert(b->controller == controller && !b->detached);

    write_count = 0; candidate = start_request(b, "VIPIRCQ2", 0);
    size_t count = background_pollfds(b, fds, BACKGROUND_POLL_CAP);
    for (size_t i = 0; i < count; ++i)
        fds[i].revents = fds[i].fd == controller ? POLLHUP : fds[i].fd == candidate ? POLLIN : 0;
    receive_eof = 1;
    assert(background_process(b, fds, count) & BACKGROUND_DETACH);
    assert(b->controller < 0 && fcntl(controller, F_GETFD) < 0 && errno == EBADF);
    assert(b->candidate < 0 && write_count == 1);
    background_free(b);

    reset_fixture(); b = worker(0);
    candidate = start_request(b, "VIPIRCQ2", 0); packet_fragment = 11;
    assert(!drive_request(b));
    assert(writes[0].fd == candidate && !memcmp(writes[0].bytes, "VIPIRCS2", 8));
    background_free(b);
}

static void detach_completion_keeps_worker(void)
{
    reset_fixture();
    Background *b = worker(1);
    int controller = b->controller;
    int candidate = start_request(b, "VIPIRCD1", 1);
    assert(drive_request(b) == BACKGROUND_DETACH);
    assert(b->detaching && b->candidate == candidate && b->controller == controller);
    expect_reply(0, candidate, "K\0", 2);

    int saved[3];
    for (int i = 0; i < 3; ++i) { saved[i] = dup(i); assert(saved[i] >= 0); }
    char error[128];
    assert(background_detach(b, NULL, error, sizeof error));
    assert(b->managed && b->detached && b->controller < 0 && b->candidate < 0 && !b->detaching);
    assert(fcntl(controller, F_GETFD) < 0 && errno == EBADF);
    expect_reply(1, controller, "D\0", 2);
    expect_reply(2, candidate, "C\0", 2);
    for (int i = 0; i < 3; ++i) { assert(dup2(saved[i], i) == i); close(saved[i]); }
    clearerr(stdin); clearerr(stdout); clearerr(stderr);
    background_free(b);

    reset_fixture(); b = worker(1); controller = b->controller;
    candidate = start_request(b, "VIPIRCD1", 1);
    packet[offsetof(AttachRequest, token)] ^= 1;
    assert(!drive_request(b)); expect_reply(0, candidate, "E\2", 2);
    assert(!b->detaching && b->controller == controller && !b->detached);
    background_free(b);
}

static void rejected_close_requests(void)
{
    reset_fixture();
    Background *b = worker(1);
    int controller = b->controller;
    int candidate = start_request(b, "VIPIRCC1", 1);
    packet[offsetof(AttachRequest, token)] ^= 1;
    assert(!drive_request(b)); expect_reply(0, candidate, "E\2", 2);
    assert(!b->closing && b->controller == controller);

    write_count = 0; candidate = start_request(b, "VIPIRCC1", 1);
    packet[offsetof(AttachRequest, reserved)] = 1;
    assert(!drive_request(b)); expect_reply(0, candidate, "E\2", 2);
    assert(!b->closing && b->controller == controller);

    write_count = 0; candidate = start_request(b, "VIPIRCC1", 1);
    rights[0] = dup(quiet_fd); assert(rights[0] >= 0); rights_count = 1;
    int passed = rights[0];
    assert(!drive_request(b)); expect_reply(0, candidate, "E\2", 2);
    assert(fcntl(passed, F_GETFD) < 0 && errno == EBADF);
    assert(!b->closing && b->controller == controller);

    write_count = 0; candidate = start_request(b, "VIPIRCC1", 1);
    memset(b->token, 0x55, sizeof b->token); /* Slot was reused after a query. */
    assert(!drive_request(b)); expect_reply(0, candidate, "E\2", 2);
    assert(!b->closing && b->controller == controller);

    peer_allowed = 0; accepted_fd = dup(quiet_fd); assert(accepted_fd >= 0);
    int rejected = accepted_fd;
    accept_candidate(b);
    assert(b->candidate < 0 && fcntl(rejected, F_GETFD) < 0 && errno == EBADF);
    background_free(b);
}

static void close_completion_after_teardown(void)
{
    reset_fixture();
    Background *b = worker(1);
    char directory[] = "/tmp/irc-close-unit-XXXXXX";
    assert(mkdtemp(directory));
    b->registry_fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(b->registry_fd >= 0);
    strcpy(b->job_name, "job-7");
    assert(mkdirat(b->registry_fd, b->job_name, 0700) == 0);
    b->directory_fd = openat(b->registry_fd, b->job_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(b->directory_fd >= 0);
    struct stat st; assert(fstat(b->directory_fd, &st) == 0);
    b->directory_device = st.st_dev; b->directory_inode = st.st_ino;
    b->lease = openat(b->directory_fd, "lease", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    assert(b->lease >= 0 && flock(b->lease, LOCK_EX | LOCK_NB) == 0);
    completion_lease = b->lease;
    int candidate = start_request(b, "VIPIRCC1", 1);
    int saved[3];
    for (int i = 0; i < 3; ++i) { saved[i] = dup(i); assert(saved[i] >= 0); }
    assert(drive_request(b) == BACKGROUND_STOP && background_signal(b) == SIGTERM);
    struct stat output, quiet;
    assert(fstat(STDOUT_FILENO, &output) == 0 && fstat(quiet_fd, &quiet) == 0);
    assert(output.st_rdev == quiet.st_rdev && output.st_ino == quiet.st_ino);
    for (int i = 0; i < 3; ++i) { assert(dup2(saved[i], i) == i); close(saved[i]); }
    clearerr(stdin); clearerr(stdout); clearerr(stderr);
    expect_reply(0, candidate, "K\0", 2);
    assert(b->closing && b->candidate == candidate && !completion_checked);
    assert(fcntl(b->lease, F_GETFD) >= 0);
    background_finish(b, 128 + SIGTERM);
    assert(completion_checked && b->candidate < 0 && b->lease < 0);
    assert(fcntl(candidate, F_GETFD) < 0 && errno == EBADF);
    assert(rmdir(directory) == 0);
    background_free(b);
}

static void make_endpoint(char directory[64], SessionJob *job)
{
    strcpy(directory, "/tmp/irc-control-unit-XXXXXX");
    assert(mkdtemp(directory));
    int n = snprintf(endpoint, sizeof endpoint, "%s/session", directory);
    assert(n > 0 && (size_t)n < sizeof endpoint);
    int fd = open(endpoint, O_RDWR | O_CREAT | O_EXCL, 0600); assert(fd >= 0); close(fd);
    virtual_endpoint = 1;
    memset(job, 0, sizeof *job); job->fd = -1; job->number = 7; job->modern = 1;
    strcpy(job->path, endpoint);
    memcpy(job->reply.magic, "VIPIRCA2", 8);
    memcpy(job->reply.program, BG_WIRE_PROGRAM, sizeof BG_WIRE_PROGRAM);
    job->reply.job = 7; strcpy(job->reply.label, "client fixture");
    memset(job->reply.token, 0x4b, sizeof job->reply.token);
    memcpy(expected_token, job->reply.token, sizeof expected_token);
}
static void remove_fixture_endpoint(const char *directory)
{
    assert(unlink(endpoint) == 0 && rmdir(directory) == 0);
    virtual_endpoint = 0;
}

static void client_completion_and_legacy(void)
{
    reset_fixture();
    SessionJob job; char directory[64]; make_endpoint(directory, &job);
    memcpy(received, "K\0C\0", 4); received_size = 4;
    read_fragment = 1; write_fragment = 13;
    assert(close_endpoint(&job) == 0 && sockets_opened == 1);
    assert(sent_request_size == sizeof(AttachRequest));
    assert(!memcmp(sent_request, "VIPIRCC1", 8));
    assert(!memcmp(sent_request + offsetof(AttachRequest, token), expected_token, 16));
    assert(fcntl(last_socket, F_GETFD) < 0 && errno == EBADF);

    sent_request_size = received_used = write_count = 0; received_size = 2; poll_calls = 0;
    memcpy(received, "K\0", 2); receive_eof = 1;
    assert(close_endpoint(&job) == 1); /* Acceptance alone is not success. */
    assert(fcntl(last_socket, F_GETFD) < 0 && errno == EBADF);

    sent_request_size = received_used = write_count = 0; received_size = 2; poll_calls = 0;
    memcpy(received, "E\2", 2); receive_eof = 1;
    assert(close_endpoint(&job) == 1); /* Reused token or refusal. */

    sent_request_size = received_used = write_count = 0; received_size = 2; poll_calls = 0;
    memcpy(received, "K\0", 2); receive_eof = 0;
    int64_t before = fixture_time;
    assert(close_endpoint(&job) == 1 && fixture_time - before <= BG_HANDSHAKE_MS + 100);
    assert(poll_calls && poll_calls < 500);

    sent_request_size = received_used = write_count = 0; received_size = 4; poll_calls = 0;
    memcpy(received, "K\0C\0", 4); receive_eof = 0;
    assert(detach_endpoint(&job) == 0);
    assert(sent_request_size == sizeof(AttachRequest));
    assert(!memcmp(sent_request, "VIPIRCD1", 8));
    assert(!memcmp(sent_request + offsetof(AttachRequest, token), expected_token, 16));

    sent_request_size = received_used = write_count = 0; received_size = 2; poll_calls = 0;
    memcpy(received, "K\0", 2); receive_eof = 1;
    assert(detach_endpoint(&job) == 1); /* Acceptance alone is not success. */

    sent_request_size = received_used = write_count = received_size = query_count = 0;
    legacy_peer = 1; receive_eof = 0; write_fragment = 0;
    job.fd = -1;
    assert(probe_open(&job, 0));
    for (unsigned i = 0; job.fd >= 0; ++i) { assert(i < 300); probe_drive(&job); }
    assert(job.state == JOB_DETACHED && job.legacy && !job.modern);
    assert(query_count == 2 && !strcmp(query_magics[0], "VIPIRCQ2") && !strcmp(query_magics[1], "VIPIRCQ1"));
    int opened = sockets_opened;
    assert(close_endpoint(&job) == 1 && sockets_opened == opened);
    remove_fixture_endpoint(directory);
}

static void successful_hup_send_has_a_deadline(void)
{
    reset_fixture();
    SessionJob job; char directory[64]; make_endpoint(directory, &job);
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    const char *name = ptsname(master); assert(name);
    int slave = open(name, O_RDWR | O_NOCTTY | O_CLOEXEC); assert(slave >= 0);
    int original_flags = fcntl(slave, F_GETFL); assert(original_flags >= 0);
    int saved[3];
    for (int i = 0; i < 3; ++i) { saved[i] = dup(i); assert(saved[i] >= 0); assert(dup2(slave, i) == i); }
    assert(setenv("TERM", "xterm", 1) == 0 && setlocale(LC_CTYPE, "C"));
    memcpy(received, "A\0", 2); received_size = 2; hup_peer = 1; mutate_terminal_flags = 1;
    int result = resume_endpoint_once(endpoint, NULL, 0, BACKGROUND_FRONTEND_CURRENT, 0);
    assert(fcntl(slave, F_GETFL) == original_flags);
    for (int i = 0; i < 3; ++i) { assert(dup2(saved[i], i) == i); close(saved[i]); }
    clearerr(stdin); clearerr(stdout); clearerr(stderr);
    assert(result == 1 && hup_injected && poll_calls < 500);
    int saw_hup = 0;
    for (size_t i = 0; i < write_count; ++i)
        if (writes[i].size == 1 && writes[i].bytes[0] == 'H') saw_hup = 1;
    assert(saw_hup && fcntl(last_socket, F_GETFD) < 0 && errno == EBADF);
    assert(fd_flags(master, 1));
    struct pollfd output_ready = {master, POLLIN, 0};
    assert(poll(&output_ready, 1, 200) > 0);
    char cleanup[512];
    ssize_t got = read(master, cleanup, sizeof cleanup - 1u);
    assert(got > 0); cleanup[got] = '\0';
    assert(strstr(cleanup, "\033[?1049l"));
    close(slave); close(master); remove_fixture_endpoint(directory);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    quiet_fd = open("/dev/null", O_RDWR | O_CLOEXEC); assert(quiet_fd >= 0);
    attached_status_and_controller_loss();
    detach_completion_keeps_worker();
    rejected_close_requests();
    close_completion_after_teardown();
    client_completion_and_legacy();
    successful_hup_send_has_a_deadline();
    reset_fixture();
    const char *invalid[] = {NULL, "", "0", "65", "-1", "+1", "1x", "1 2", "/tmp/session", "99999999999999999999"};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        assert(background_close_session(invalid[i]) == 2);
        assert(background_detach_session(invalid[i]) == 2);
    }
    assert(!sockets_opened);
    close(quiet_fd);
    puts("background control fixtures: attached status, authenticated non-destructive detach, independent controller EOF, second-attach refusal, token/UID/FD/reserved-byte rejection, close completion after lease teardown, legacy fallback, bounded HUP, detach and close, strict selection passed");
    return 0;
}
