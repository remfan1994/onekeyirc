/* Real loopback sockets: a full ACK send buffer must not turn readable
 * incoming data into a busy loop while the receiver waits to flush an ACK. */
#define _POSIX_C_SOURCE 200809L
#include "dcc.c"
#include <assert.h>
#include <signal.h>

static int listener(uint16_t *port)
{
    struct sockaddr_in address;
    socklen_t length = sizeof address;
    int fd = socket(AF_INET, SOCK_STREAM, 0), small = 1024;
    assert(fd >= 0);
    assert(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &small, sizeof small) == 0);
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(fd, (struct sockaddr *)&address, sizeof address) == 0);
    assert(listen(fd, 1) == 0);
    assert(getsockname(fd, (struct sockaddr *)&address, &length) == 0);
    *port = ntohs(address.sin_port);
    return fd;
}

static DccTransfer *active_transfer(DccManager *manager, uint64_t id)
{
    for (size_t i = 0; i < DCC_MAX_ACTIVE; ++i)
        if (manager->transfers[i].used &&
            manager->entries[manager->transfers[i].entry].info.id == id)
            return &manager->transfers[i];
    return NULL;
}

/* Fill the actual kernel socket buffer with ACK-shaped bytes. The peer does
 * not consume them. This establishes real EAGAIN without mocking send/poll. */
static size_t fill_ack_buffer(int fd)
{
    unsigned char zero[4] = {0};
    size_t queued = 0;
    int quiet = 0;
    int64_t deadline = now_ms() + 5000;
    while (quiet < 3 && now_ms() < deadline) {
        ssize_t n;
        do {
            n = socket_write(fd, zero, sizeof zero);
            if (n > 0) queued += (size_t)n;
            assert(queued < 16u * 1024u * 1024u);
        } while (n > 0 || (n < 0 && errno == EINTR));
        assert(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        struct pollfd writable = {fd, POLLOUT, 0};
        int ready = poll(&writable, 1, 50);
        assert(ready >= 0);
        quiet = ready ? 0 : quiet + 1;
    }
    assert(quiet == 3 && queued);
    return queued;
}

int main(void)
{
    char root[] = "/tmp/onekeyirc-dcc-pressure-XXXXXX";
    char body[128], error[256], path[DCC_PATH_MAX];
    struct pollfd fds[DCC_MAX_ACTIVE];
    uint64_t ids[DCC_MAX_ACTIVE], id;
    uint16_t port;
    signal(SIGPIPE, SIG_IGN);
    assert(mkdtemp(root));
    int listening = listener(&port);
    DccManager *manager = dcc_create(0);
    assert(manager);
    (void)snprintf(body, sizeof body, "DCC SEND pressure.bin 2130706433 %u 8", (unsigned)port);
    assert(dcc_offer(manager, 1, "sender", body, &id, error, sizeof error));
    assert(dcc_accept(manager, id, root, NULL, error, sizeof error));
    int peer = accept(listening, NULL, NULL);
    assert(peer >= 0 && nonblocking(peer));
    close(listening);
    size_t count = dcc_pollfds(manager, fds, ids, DCC_MAX_ACTIVE);
    assert(count == 1 && poll(fds, 1, 1000) > 0);
    dcc_process(manager, fds, ids, count, now_ms());
    DccTransfer *transfer = active_transfer(manager, id);
    assert(transfer && dcc_get(manager, id)->state == DCC_RECEIVING);
    int small = 1024;
    assert(setsockopt(transfer->fd, SOL_SOCKET, SO_SNDBUF, &small, sizeof small) == 0);
    size_t queued = fill_ack_buffer(transfer->fd);

    assert(write(peer, "abcd", 4) == 4);
    count = dcc_pollfds(manager, fds, ids, DCC_MAX_ACTIVE);
    assert(count == 1 && poll(fds, 1, 1000) > 0);
    dcc_process(manager, fds, ids, count, now_ms());
    assert(dcc_get(manager, id)->transferred == 4);
    assert(transfer->ack_length == 4 && transfer->offset < transfer->ack_length);
    assert(write(peer, "efgh", 4) == 4);
    struct pollfd readable = {transfer->fd, POLLIN, 0};
    assert(poll(&readable, 1, 1000) > 0 && (readable.revents & POLLIN));

    count = dcc_pollfds(manager, fds, ids, DCC_MAX_ACTIVE);
    assert(count == 1);
    int unwanted_input = !!(fds[0].events & POLLIN);
    unsigned wakes_without_progress = 0;
    for (unsigned i = 0; i < 256; ++i) {
        fds[0].revents = 0;
        if (poll(fds, 1, 0) > 0) {
            dcc_process(manager, fds, ids, count, now_ms());
            if (dcc_get(manager, id)->transferred == 4 && transfer->ack_length)
                ++wakes_without_progress;
        }
    }
    printf("DCC blocked ACK: queued=%zu input_interest=%d ready_without_progress=%u\n",
           queued, unwanted_input, wakes_without_progress);

    /* Once the peer drains ACKs, normal readiness must resume the transfer.
     * Check the file as well, so avoiding wakeups cannot hide a deadlock. */
    unsigned char drain[4096];
    int64_t deadline = now_ms() + 5000;
    while (dcc_get(manager, id)->state != DCC_DONE && now_ms() < deadline) {
        ssize_t n;
        do { n = read(peer, drain, sizeof drain); } while (n > 0 || (n < 0 && errno == EINTR));
        assert(n == 0 || (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)));
        count = dcc_pollfds(manager, fds, ids, DCC_MAX_ACTIVE);
        assert(poll(fds, (nfds_t)count, 20) >= 0);
        dcc_process(manager, fds, ids, count, now_ms());
        assert(dcc_get(manager, id)->state != DCC_FAILED);
    }
    assert(dcc_get(manager, id)->state == DCC_DONE);
    (void)snprintf(path, sizeof path, "%s/pressure.bin", root);
    int saved = open(path, O_RDONLY | O_NOFOLLOW);
    unsigned char contents[9];
    assert(saved >= 0 && read(saved, contents, sizeof contents) == 8);
    assert(!memcmp(contents, "abcdefgh", 8));
    assert(close(saved) == 0 && unlink(path) == 0 && rmdir(root) == 0);
    close(peer);
    dcc_free(manager);
    if (unwanted_input || wakes_without_progress) {
        fputs("FAIL: blocked DCC acknowledgement kept waking on unconsumed input.\n", stderr);
        return 1;
    }
    puts("dcc: blocked ACK sleeps, resumes on writable readiness and preserves exact file bytes");
    return 0;
}
