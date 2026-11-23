/* Native socketpair/file tests at the 32-bit DCC counter boundary.
 * Seed completed byte counts, then transfer only a short tail. This does
 * not perform a full 4 GiB transfer. All files are newly created privately.
 * Build: cc -std=c11 -O2 -Isrc tests/test_dcc_counter_edges.c
 *        src/protocol.c -o build/test_dcc_counter_edges
 */
#define _POSIX_C_SOURCE 200809L
#include "dcc.c"
#include <assert.h>
#include <signal.h>

static size_t completed, failed, wire_bytes;

static unsigned char pattern(size_t offset)
{
    return (unsigned char)(offset * 47u + 19u);
}

static void write_exact(int fd, const void *data, size_t length)
{
    const unsigned char *bytes = data;
    while (length) {
        ssize_t n = write(fd, bytes, length);
        assert(n > 0);
        bytes += (size_t)n;
        length -= (size_t)n;
    }
}

static void check_released(const int owned[3])
{
    for (size_t i = 0; i < 3; ++i) {
        errno = 0;
        assert(fcntl(owned[i], F_GETFD) < 0 && errno == EBADF);
    }
}

static DccManager *prepare(const char *root, const char *leaf, DccState state,
                           uint32_t size, uint32_t tail, int *peer, int owned[3])
{
    assert(size >= tail && tail <= DCC_BLOCK + 1u);
    DccManager *manager = dcc_create(0);
    assert(manager);
    DccInfo *info = &manager->entries[0].info;
    info->id = 17;
    info->server_token = 91;
    info->state = state;
    info->outgoing = state == DCC_SENDING;
    info->size = size;
    info->transferred = info->acknowledged = size - tail;
    snprintf(info->filename, sizeof info->filename, "%s", leaf);
    snprintf(info->path, sizeof info->path, "%s/%s", root, leaf);
    snprintf(info->nick, sizeof info->nick, "peer");
    DccTransfer *transfer = &manager->transfers[0];
    transfer->entry = 0;
    transfer->used = 1;
    transfer->last_progress = now_ms();
    transfer->dir_fd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    assert(transfer->dir_fd >= 0);
    transfer->file_fd = openat(transfer->dir_fd, leaf,
        O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    assert(transfer->file_fd >= 0);
    struct stat st;
    assert(fstat(transfer->file_fd, &st) == 0 && S_ISREG(st.st_mode));
    transfer->created_file = state == DCC_RECEIVING;
    transfer->device = st.st_dev;
    transfer->inode = st.st_ino;
    snprintf(transfer->leaf, sizeof transfer->leaf, "%s", leaf);
    if (state == DCC_SENDING) {
        unsigned char data[DCC_BLOCK + 1u];
        for (size_t i = 0; i < tail; ++i) data[i] = pattern(i);
        write_exact(transfer->file_fd, data, tail);
        assert(lseek(transfer->file_fd, 0, SEEK_SET) == 0);
    }
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(nonblocking(sockets[0]) && nonblocking(sockets[1]));
    transfer->fd = sockets[0];
    *peer = sockets[1];
    owned[0] = transfer->fd;
    owned[1] = transfer->file_fd;
    owned[2] = transfer->dir_fd;
    return manager;
}

static void step(DccManager *manager)
{
    struct pollfd fds[DCC_MAX_ACTIVE];
    uint64_t ids[DCC_MAX_ACTIVE];
    size_t count = dcc_pollfds(manager, fds, ids, DCC_MAX_ACTIVE);
    assert(count <= 1);
    assert(poll(fds, (nfds_t)count, 1) >= 0);
    uint32_t before = manager->entries[0].info.transferred;
    dcc_process(manager, fds, ids, count, now_ms());
    const DccInfo *info = &manager->entries[0].info;
    assert(info->acknowledged <= info->transferred && info->transferred <= info->size);
    assert(info->transferred >= before && info->transferred - before <= DCC_BATCH * DCC_BLOCK);
}

static void incoming(const char *root, uint32_t size, uint32_t tail)
{
    int peer, owned[3];
    DccManager *manager = prepare(root, "receive.bin", DCC_RECEIVING, size, tail, &peer, owned);
    unsigned char data[DCC_BLOCK + 1u];
    for (size_t i = 0; i < tail; ++i) data[i] = pattern(i);
    write_exact(peer, data, tail);
    unsigned char partial[4];
    size_t partial_length = 0;
    uint32_t acknowledged = size - tail;
    for (unsigned round = 0; round < 1000; ++round) {
        step(manager);
        unsigned char ack[64];
        ssize_t n = recv(peer, ack, sizeof ack, 0);
        assert(n >= 0 || would_block());
        for (ssize_t i = 0; i < n; ++i) {
            partial[partial_length++] = ack[i];
            if (partial_length == 4) {
                uint32_t value;
                memcpy(&value, partial, sizeof value);
                value = ntohl(value);
                assert(value >= acknowledged && value <= size);
                acknowledged = value;
                partial_length = 0;
            }
        }
        if (manager->entries[0].info.state == DCC_DONE && acknowledged == size) break;
        assert(manager->entries[0].info.state == DCC_RECEIVING || manager->entries[0].info.state == DCC_DONE);
        assert(round < 999);
    }
    assert(!partial_length && acknowledged == size);
    assert(manager->entries[0].info.transferred == size && manager->entries[0].info.acknowledged == size);
    check_released(owned);
    char path[DCC_PATH_MAX];
    snprintf(path, sizeof path, "%s/receive.bin", root);
    int file = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    assert(file >= 0 && fstat(file, &st) == 0 && st.st_size == (off_t)tail);
    size_t at = 0;
    while (at < tail) {
        ssize_t n = read(file, data, sizeof data);
        assert(n > 0 && (size_t)n <= tail - at);
        for (ssize_t i = 0; i < n; ++i) assert(data[i] == pattern(at + (size_t)i));
        at += (size_t)n;
    }
    assert(read(file, data, 1) == 0 && close(file) == 0);
    assert(unlink(path) == 0 && close(peer) == 0);
    dcc_free(manager);
    wire_bytes += tail;
    ++completed;
}

static void outgoing(const char *root, uint32_t size, uint32_t tail)
{
    int peer, owned[3];
    DccManager *manager = prepare(root, "source.bin", DCC_SENDING, size, tail, &peer, owned);
    size_t received = 0;
    for (unsigned round = 0; round < 1000; ++round) {
        step(manager);
        unsigned char data[4096];
        ssize_t n = recv(peer, data, sizeof data, 0);
        assert(n >= 0 || would_block());
        if (n > 0) {
            assert((size_t)n <= tail - received);
            for (ssize_t i = 0; i < n; ++i) assert(data[i] == pattern(received + (size_t)i));
            received += (size_t)n;
            uint32_t ack = htonl(size - tail + (uint32_t)received);
            const unsigned char *bytes = (const unsigned char *)&ack;
            /* A high-bit counter cannot take effect before all four bytes.
             * Exercise 1/2/1 fragmentation over the real local socket. */
            write_exact(peer, bytes, 1);
            step(manager);
            assert(manager->entries[0].info.state == DCC_SENDING);
            write_exact(peer, bytes + 1, 2);
            step(manager);
            assert(manager->entries[0].info.state == DCC_SENDING);
            write_exact(peer, bytes + 3, 1);
        }
        if (manager->entries[0].info.state == DCC_DONE) break;
        assert(manager->entries[0].info.state == DCC_SENDING && round < 999);
    }
    assert(received == tail && manager->entries[0].info.acknowledged == size);
    check_released(owned);
    char path[DCC_PATH_MAX];
    snprintf(path, sizeof path, "%s/source.bin", root);
    assert(unlink(path) == 0 && close(peer) == 0);
    dcc_free(manager);
    wire_bytes += tail;
    ++completed;
}

static void invalid_counters(const char *root)
{
    const uint32_t values[] = {0, UINT32_MAX - 10u, UINT32_MAX};
    for (size_t i = 0; i < sizeof values / sizeof values[0]; ++i) {
        int peer, owned[3];
        DccManager *manager = prepare(root, "source.bin", DCC_SENDING, UINT32_MAX, 9, &peer, owned);
        uint32_t ack = htonl(values[i]);
        write_exact(peer, &ack, sizeof ack);
        step(manager);
        assert(manager->entries[0].info.state == DCC_FAILED);
        assert(manager->entries[0].info.transferred == UINT32_MAX - 9u);
        check_released(owned);
        char path[DCC_PATH_MAX];
        snprintf(path, sizeof path, "%s/source.bin", root);
        assert(unlink(path) == 0 && close(peer) == 0);
        dcc_free(manager);
        ++failed;
    }
    const uint32_t tails[] = {1, 3, DCC_BLOCK - 1u};
    for (size_t i = 0; i < sizeof tails / sizeof tails[0]; ++i) {
        int peer, owned[3];
        DccManager *manager = prepare(root, "receive.bin", DCC_RECEIVING, UINT32_MAX, tails[i], &peer, owned);
        unsigned char excess[DCC_BLOCK];
        memset(excess, 'x', sizeof excess);
        write_exact(peer, excess, tails[i] + 1u);
        step(manager);
        assert(manager->entries[0].info.state == DCC_FAILED);
        assert(manager->entries[0].info.transferred == UINT32_MAX - tails[i]);
        check_released(owned);
        char path[DCC_PATH_MAX];
        struct stat st;
        snprintf(path, sizeof path, "%s/receive.bin", root);
        assert(lstat(path, &st) < 0 && errno == ENOENT);
        assert(close(peer) == 0);
        dcc_free(manager);
        ++failed;
    }
}

static void exhausted_ids(void)
{
    DccManager *manager = dcc_create(1);
    assert(manager);
    manager->next_id = UINT64_MAX;
    uint64_t id = 0;
    char error[256];
    assert(dcc_offer(manager, 4, "sender", "DCC SEND file 2130706433 9 1", &id, error, sizeof error));
    assert(id == UINT64_MAX && !manager->next_id);
    assert(dcc_reject(manager, id, error, sizeof error));
    id = 55;
    assert(!dcc_offer(manager, 4, "sender", "DCC SEND other 2130706433 9 1", &id, error, sizeof error));
    assert(!id && !manager->next_id && dcc_count(manager) == 1);
    assert(dcc_get(manager, UINT64_MAX)->state == DCC_REJECTED);
    dcc_free(manager);
}

int main(void)
{
    alarm(30);
    char root[] = "/tmp/irc-dcc-counter-XXXXXX";
    assert(mkdtemp(root));
    const uint32_t tails[] = {1, 3, DCC_BLOCK - 1u, DCC_BLOCK, DCC_BLOCK + 1u};
    const uint32_t sizes[] = {UINT32_MAX - 1u, UINT32_MAX};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; ++i)
        for (size_t j = 0; j < sizeof tails / sizeof tails[0]; ++j) {
            incoming(root, sizes[i], tails[j]);
            outgoing(root, sizes[i], tails[j]);
        }
    invalid_counters(root);
    exhausted_ids();
    assert(rmdir(root) == 0);
    printf("dcc counter edges: completed tails=%zu rejected malformed peers=%zu actual payload bytes=%zu; native socketpair I/O, high-bit ACK fragmentation, counter bounds, cleanup, and exhausted IDs passed\n",
           completed, failed, wire_bytes);
    return 0;
}
