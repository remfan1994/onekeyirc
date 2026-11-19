/* Adversarial lifetime and I/O checks use real private files and loopback TCP.
 * The named wrappers inject deterministic short-I/O and errno results at the
 * production syscall boundary; they do not replace connection setup or poll. */
#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int send_fd = -1, file_fd = -1;
static unsigned send_interrupts, send_blocks, file_interrupts, socket_failures;
static size_t send_chunk, file_chunk, file_written;
static size_t file_limit = SIZE_MAX;
static unsigned injected_send_interrupts, injected_send_blocks, injected_file_interrupts;
static unsigned injected_file_failures, injected_socket_failures;
static ssize_t fault_send(int, const void *, size_t, int);
static ssize_t fault_write(int, const void *, size_t);
static int fault_socket(int, int, int);
#define send fault_send
#define write fault_write
#define socket fault_socket
#include "dcc.c"
#undef send
#undef write
#undef socket

static ssize_t fault_send(int fd, const void *bytes, size_t size, int flags)
{
    if (fd == send_fd) {
        if (send_interrupts) {
            --send_interrupts; ++injected_send_interrupts; errno = EINTR; return -1;
        }
        if (send_blocks) {
            --send_blocks; ++injected_send_blocks; errno = EAGAIN; return -1;
        }
        if (send_chunk && size > send_chunk) size = send_chunk;
    }
    return send(fd, bytes, size, flags);
}

static ssize_t fault_write(int fd, const void *bytes, size_t size)
{
    if (fd == file_fd) {
        if (file_interrupts) {
            --file_interrupts; ++injected_file_interrupts; errno = EINTR; return -1;
        }
        if (file_written == file_limit) {
            ++injected_file_failures; errno = ENOSPC; return -1;
        }
        if (size > file_limit - file_written) size = file_limit - file_written;
        if (file_chunk && size > file_chunk) size = file_chunk;
    }
    ssize_t n = write(fd, bytes, size);
    if (fd == file_fd && n > 0) file_written += (size_t)n;
    return n;
}

static int fault_socket(int domain, int type, int protocol)
{
    if (socket_failures) {
        --socket_failures; ++injected_socket_failures; errno = EMFILE; return -1;
    }
    return socket(domain, type, protocol);
}

static void clear_faults(void)
{
    send_fd = file_fd = -1;
    send_interrupts = send_blocks = file_interrupts = socket_failures = 0;
    send_chunk = file_chunk = file_written = 0;
    file_limit = SIZE_MAX;
}

static void path(char *out, size_t cap, const char *root, const char *leaf)
{
    int n = snprintf(out, cap, "%s/%s", root, leaf);
    assert(n > 0 && (size_t)n < cap);
}

static void put_file(const char *name, const unsigned char *bytes, size_t size)
{
    int fd = open(name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    assert(fd >= 0);
    size_t at = 0;
    while (at < size) {
        ssize_t n = write(fd, bytes + at, size - at);
        assert(n > 0); at += (size_t)n;
    }
    assert(close(fd) == 0);
}

static void check_file(const char *name, const unsigned char *bytes, size_t size)
{
    struct stat st;
    int fd = open(name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    assert(fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode));
    assert(st.st_size >= 0 && (uintmax_t)st.st_size == size && (st.st_mode & 0777) == 0600);
    unsigned char block[1024];
    size_t at = 0;
    while (at < size) {
        ssize_t n = read(fd, block, sizeof block);
        assert(n > 0 && (size_t)n <= size - at && !memcmp(bytes + at, block, (size_t)n));
        at += (size_t)n;
    }
    assert(read(fd, block, 1) == 0 && close(fd) == 0);
}

static void absent(const char *name)
{
    struct stat st;
    assert(lstat(name, &st) < 0 && errno == ENOENT);
}

static void closed_fd(int fd)
{
    assert(fcntl(fd, F_GETFD) < 0 && errno == EBADF);
}

static unsigned descriptor_count(void)
{
    unsigned count = 0;
    for (int fd = 0; fd < 1024; ++fd)
        if (fcntl(fd, F_GETFD) >= 0) ++count;
    return count;
}

static DccTransfer *active(DccManager *m, uint64_t id)
{
    for (size_t i = 0; i < DCC_MAX_ACTIVE; ++i)
        if (m->transfers[i].used && m->entries[m->transfers[i].entry].info.id == id)
            return &m->transfers[i];
    assert(!"active transfer not found"); return NULL;
}

static void step_at(DccManager *m, int64_t now)
{
    struct pollfd fds[DCC_MAX_ACTIVE]; uint64_t ids[DCC_MAX_ACTIVE];
    size_t count = dcc_pollfds(m, fds, ids, DCC_MAX_ACTIVE);
    assert(poll(fds, (nfds_t)count, 2) >= 0);
    dcc_process(m, fds, ids, count, now);
}

static void step(DccManager *m) { step_at(m, now_ms()); }

static void wait_state(DccManager *m, uint64_t id, DccState state)
{
    for (size_t i = 0; i < 1000 && dcc_get(m, id)->state != state; ++i) {
        assert(!terminal(dcc_get(m, id)->state)); step(m);
    }
    assert(dcc_get(m, id)->state == state);
}

static int listen_local(uint16_t *port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0 && fd < 1024);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(fd, (struct sockaddr *)&address, sizeof address) == 0 && listen(fd, 4) == 0);
    socklen_t length = sizeof address;
    assert(getsockname(fd, (struct sockaddr *)&address, &length) == 0);
    *port = ntohs(address.sin_port);
    return fd;
}

static int receive_start(DccManager *m, const char *root, const char *name, uint32_t size, uint64_t *id)
{
    uint16_t port;
    int listener = listen_local(&port);
    char body[512], error[256];
    assert(snprintf(body, sizeof body, "DCC SEND %s 2130706433 %u %u", name, (unsigned)port, size) > 0);
    assert(dcc_offer(m, 1, "peer", body, id, error, sizeof error));
    assert(dcc_accept(m, *id, root, NULL, error, sizeof error));
    struct pollfd ready = {listener, POLLIN, 0};
    assert(poll(&ready, 1, 1000) == 1);
    int peer = accept(listener, NULL, NULL);
    assert(peer >= 0 && peer < 1024 && close(listener) == 0 && nonblocking(peer));
    wait_state(m, *id, DCC_RECEIVING);
    return peer;
}

static int send_start(DccManager *m, const char *source, uint64_t *id)
{
    char body[512], error[256];
    assert(dcc_send(m, 1, "peer", source, "127.0.0.1", 0, id, body, sizeof body, error, sizeof error));
    int peer = socket(AF_INET, SOCK_STREAM, 0);
    assert(peer >= 0 && peer < 1024);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(dcc_get(m, *id)->port);
    assert(connect(peer, (struct sockaddr *)&address, sizeof address) == 0 && nonblocking(peer));
    wait_state(m, *id, DCC_SENDING);
    return peer;
}

static void send_bytes(int peer, const void *bytes, size_t size)
{
    assert(send(peer, bytes, size, 0) == (ssize_t)size);
}

static void read_bytes(DccManager *m, int peer, unsigned char *bytes, size_t size)
{
    size_t at = 0;
    for (size_t attempt = 0; attempt < 2000 && at < size; ++attempt) {
        step(m);
        ssize_t n = recv(peer, bytes + at, size - at, 0);
        if (n < 0) { assert(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR); continue; }
        assert(n > 0); at += (size_t)n;
    }
    assert(at == size);
}

static void expect_ack(DccManager *m, int peer, uint32_t expected)
{
    unsigned char bytes[4]; uint32_t value;
    read_bytes(m, peer, bytes, sizeof bytes);
    memcpy(&value, bytes, sizeof value);
    assert(ntohl(value) == expected);
}

static void outgoing_short_io_and_pinned_source(const char *root)
{
    unsigned char bytes[4099], received[4099];
    for (size_t i = 0; i < sizeof bytes; ++i) bytes[i] = (unsigned char)(i * 173u + 31u);
    char source[DCC_PATH_MAX], original[DCC_PATH_MAX];
    path(source, sizeof source, root, "source.bin"); path(original, sizeof original, root, "source-original.bin");
    put_file(source, bytes, sizeof bytes);
    DccManager *m = dcc_create(0); assert(m);
    uint64_t id; int peer = send_start(m, source, &id);
    DccTransfer *t = active(m, id);
    int outgoing_fd = t->fd, source_fd = t->file_fd;
    assert(rename(source, original) == 0);
    const unsigned char replacement[] = "replacement must never be disclosed";
    put_file(source, replacement, sizeof replacement - 1u);
    send_fd = outgoing_fd; send_chunk = 7; send_interrupts = send_blocks = 1;
    read_bytes(m, peer, received, sizeof received);
    assert(!memcmp(received, bytes, sizeof bytes));
    assert(dcc_get(m, id)->state == DCC_SENDING && dcc_get(m, id)->acknowledged == 0);
    uint32_t ack = htonl((uint32_t)sizeof bytes);
    const unsigned char *wire = (const unsigned char *)&ack;
    send_bytes(peer, wire, 1); step(m);
    assert(dcc_get(m, id)->state == DCC_SENDING);
    send_bytes(peer, wire + 1, 2); step(m);
    assert(dcc_get(m, id)->state == DCC_SENDING);
    send_bytes(peer, wire + 3, 1); wait_state(m, id, DCC_DONE);
    closed_fd(outgoing_fd); closed_fd(source_fd);
    clear_faults(); assert(close(peer) == 0); dcc_free(m);
    check_file(source, replacement, sizeof replacement - 1u); check_file(original, bytes, sizeof bytes);
    assert(unlink(source) == 0 && unlink(original) == 0);
    puts("dcc adversarial: short socket writes, EINTR/EAGAIN, fragmented final ACK and pinned source bytes passed");
}

static void receive_short_io(const char *root)
{
    unsigned char bytes[217];
    for (size_t i = 0; i < sizeof bytes; ++i) bytes[i] = (unsigned char)(i * 19u + 7u);
    DccManager *m = dcc_create(0); assert(m);
    uint64_t id; int peer = receive_start(m, root, "short-write.bin", sizeof bytes, &id);
    DccTransfer *t = active(m, id);
    int receive_fd = t->fd, receive_file = t->file_fd, receive_dir = t->dir_fd;
    file_fd = receive_file; file_chunk = 3; file_interrupts = 1;
    send_fd = receive_fd; send_chunk = 1; send_interrupts = send_blocks = 1;
    send_bytes(peer, bytes, sizeof bytes);
    expect_ack(m, peer, sizeof bytes); wait_state(m, id, DCC_DONE);
    assert(file_written == sizeof bytes);
    closed_fd(receive_fd); closed_fd(receive_file); closed_fd(receive_dir);
    clear_faults(); assert(close(peer) == 0); dcc_free(m);
    char target[DCC_PATH_MAX]; path(target, sizeof target, root, "short-write.bin");
    check_file(target, bytes, sizeof bytes); assert(unlink(target) == 0);
    puts("dcc adversarial: short file writes, retryable interruption and four fragmented ACK bytes passed");
}

static void failed_receive_isolation(const char *root)
{
    const unsigned char bytes[] = "abcdefgh";
    DccManager *m = dcc_create(0); assert(m);
    uint64_t bad_id, good_id;
    int bad = receive_start(m, root, "disk-full.bin", 8, &bad_id);
    int good = receive_start(m, root, "healthy.bin", 8, &good_id);
    DccTransfer *t = active(m, bad_id);
    int failed_fd = t->fd, failed_file = t->file_fd, failed_dir = t->dir_fd;
    file_fd = failed_file; file_chunk = 3; file_limit = 5;
    send_bytes(bad, bytes, 8); send_bytes(good, bytes, 8);
    wait_state(m, bad_id, DCC_FAILED);
    assert(file_written == 5 && strstr(dcc_get(m, bad_id)->error, "write failed"));
    closed_fd(failed_fd); closed_fd(failed_file); closed_fd(failed_dir);
    clear_faults(); expect_ack(m, good, 8); wait_state(m, good_id, DCC_DONE);
    char failed[DCC_PATH_MAX], healthy[DCC_PATH_MAX];
    path(failed, sizeof failed, root, "disk-full.bin"); path(healthy, sizeof healthy, root, "healthy.bin");
    absent(failed); check_file(healthy, bytes, 8);
    assert(close(bad) == 0 && close(good) == 0 && unlink(healthy) == 0); dcc_free(m);
    puts("dcc adversarial: partial disk write plus ENOSPC removes only the failed file; concurrent transfer completes passed");
}

static void acknowledgement_failures(const char *root)
{
    const unsigned char bytes[] = "abcdefgh";
    char source[DCC_PATH_MAX]; path(source, sizeof source, root, "ack-source.bin");
    put_file(source, bytes, 8);
    for (unsigned kind = 0; kind < 4; ++kind) {
        DccManager *m = dcc_create(0); assert(m);
        uint64_t id; int peer = send_start(m, source, &id);
        unsigned char actual[8]; read_bytes(m, peer, actual, sizeof actual);
        assert(!memcmp(actual, bytes, 8));
        int transfer_fd = active(m, id)->fd, source_fd = active(m, id)->file_fd;
        uint32_t ack = htonl(4);
        if (kind == 0 || kind == 1) {
            send_bytes(peer, &ack, 4);
            for (size_t i = 0; i < 1000 && dcc_get(m, id)->acknowledged != 4; ++i) step(m);
            assert(dcc_get(m, id)->acknowledged == 4 && dcc_get(m, id)->state == DCC_SENDING);
            ack = htonl(kind == 0 ? 3 : 9); send_bytes(peer, &ack, 4);
            wait_state(m, id, DCC_FAILED);
            assert(strstr(dcc_get(m, id)->error, "invalid byte acknowledgement"));
        } else if (kind == 2) {
            send_bytes(peer, &ack, 1); assert(shutdown(peer, SHUT_WR) == 0);
            wait_state(m, id, DCC_FAILED);
            assert(strstr(dcc_get(m, id)->error, "before acknowledging"));
        } else {
            ack = 0; send_bytes(peer, &ack, 4);
            step_at(m, now_ms() + DCC_IDLE_MS + 1);
            assert(dcc_get(m, id)->state == DCC_FAILED && strstr(dcc_get(m, id)->error, "timed out"));
        }
        closed_fd(transfer_fd); closed_fd(source_fd);
        check_file(source, bytes, 8); assert(close(peer) == 0); dcc_free(m);
    }
    assert(unlink(source) == 0);
    puts("dcc adversarial: backward/oversized ACK, mid-ACK EOF and duplicate ACK timeout passed");
}

static void disconnect_isolation(const char *root)
{
    const unsigned char bytes[] = "abcdefgh";
    for (unsigned reset = 0; reset < 2; ++reset) {
        DccManager *m = dcc_create(0); assert(m);
        uint64_t bad_id, good_id;
        int bad = receive_start(m, root, "disconnect.bin", 8, &bad_id);
        int good = receive_start(m, root, "disconnect-healthy.bin", 8, &good_id);
        send_bytes(bad, bytes, 3); expect_ack(m, bad, 3);
        DccTransfer *t = active(m, bad_id);
        int failed_fd = t->fd, failed_file = t->file_fd, failed_dir = t->dir_fd;
        if (reset) {
            struct linger immediate = {1, 0};
            assert(setsockopt(bad, SOL_SOCKET, SO_LINGER, &immediate, sizeof immediate) == 0);
            assert(close(bad) == 0); bad = -1;
        } else assert(shutdown(bad, SHUT_WR) == 0);
        send_bytes(good, bytes, 8);
        wait_state(m, bad_id, DCC_FAILED);
        assert(dcc_get(m, bad_id)->transferred == 3);
        closed_fd(failed_fd); closed_fd(failed_file); closed_fd(failed_dir);
        expect_ack(m, good, 8); wait_state(m, good_id, DCC_DONE);
        char partial[DCC_PATH_MAX], healthy[DCC_PATH_MAX];
        path(partial, sizeof partial, root, "disconnect.bin"); absent(partial);
        path(healthy, sizeof healthy, root, "disconnect-healthy.bin"); check_file(healthy, bytes, 8);
        assert(unlink(healthy) == 0 && close(good) == 0);
        if (bad >= 0) assert(close(bad) == 0);
        dcc_free(m);
    }
    puts("dcc adversarial: real TCP EOF/reset after partial receive removes its file and preserves concurrent transfer passed");
}

static void cancellation_and_emfile(const char *root)
{
    DccManager *m = dcc_create(0); assert(m);
    char error[256], body[512], target[DCC_PATH_MAX];
    path(target, sizeof target, root, "allocation-failure.bin");
    uint16_t port; int listener = listen_local(&port);
    assert(snprintf(body, sizeof body, "DCC SEND allocation-failure.bin 2130706433 %u 8", (unsigned)port) > 0);
    uint64_t id;
    assert(dcc_offer(m, 2, "peer", body, &id, error, sizeof error));
    unsigned before = descriptor_count();
    for (size_t i = 0; i < 32; ++i) {
        socket_failures = 1;
        assert(!dcc_accept(m, id, root, NULL, error, sizeof error));
        assert(!socket_failures && dcc_get(m, id)->state == DCC_OFFERED);
        assert(strstr(error, "Cannot open DCC connection")); absent(target);
        assert(descriptor_count() == before);
    }
    clear_faults(); assert(close(listener) == 0 && dcc_reject(m, id, error, sizeof error));
    uint64_t cancelled_id, remaining_id;
    int cancelled = receive_start(m, root, "cancel-partial.bin", 8, &cancelled_id);
    int remaining = receive_start(m, root, "remaining.bin", 8, &remaining_id);
    const unsigned char bytes[] = "abcdefgh";
    send_bytes(cancelled, bytes, 4); expect_ack(m, cancelled, 4);
    DccTransfer *t = active(m, cancelled_id);
    int old_socket = t->fd, old_file = t->file_fd, old_dir = t->dir_fd;
    struct pollfd stale[DCC_MAX_ACTIVE]; uint64_t stale_ids[DCC_MAX_ACTIVE];
    size_t stale_count = dcc_pollfds(m, stale, stale_ids, DCC_MAX_ACTIVE);
    for (size_t i = 0; i < stale_count; ++i) stale[i].revents = POLLIN | POLLOUT | POLLHUP;
    assert(dcc_cancel(m, cancelled_id, error, sizeof error));
    closed_fd(old_socket); closed_fd(old_file); closed_fd(old_dir);
    path(target, sizeof target, root, "cancel-partial.bin"); absent(target);
    /* Isolate the cancelled item's stale event; a ready event for the still
     * active peer would legitimately advance it. */
    for (size_t i = 0; i < stale_count; ++i)
        if (stale_ids[i] == cancelled_id) dcc_process(m, stale + i, stale_ids + i, 1, now_ms());
    assert(dcc_get(m, cancelled_id)->state == DCC_CANCELLED);
    send_bytes(remaining, bytes, 8); expect_ack(m, remaining, 8); wait_state(m, remaining_id, DCC_DONE);
    path(target, sizeof target, root, "remaining.bin"); check_file(target, bytes, 8);
    assert(unlink(target) == 0 && close(cancelled) == 0 && close(remaining) == 0);
    dcc_free(m);
    puts("dcc adversarial: 32 EMFILE retries leave no file/descriptor leak; cancellation isolates stale events and peer transfer passed");
}

int main(void)
{
    unsigned before = descriptor_count();
    char root[] = "/tmp/onekey-dcc-adversarial-XXXXXX";
    assert(mkdtemp(root));
    outgoing_short_io_and_pinned_source(root);
    receive_short_io(root);
    failed_receive_isolation(root);
    acknowledgement_failures(root);
    disconnect_isolation(root);
    cancellation_and_emfile(root);
    assert(injected_send_interrupts == 2 && injected_send_blocks == 2);
    assert(injected_file_interrupts == 1 && injected_file_failures == 1 && injected_socket_failures == 32);
    assert(rmdir(root) == 0 && descriptor_count() == before);
    puts("dcc adversarial: all private-loopback and explicitly injected fault checks passed; no descriptor leaks");
    return 0;
}
