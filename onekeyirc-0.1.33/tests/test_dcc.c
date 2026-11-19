#define _POSIX_C_SOURCE 200809L
#include "dcc.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int64_t milliseconds(void)
{
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void path_join(char *out, size_t cap, const char *directory, const char *name)
{
    int length = snprintf(out, cap, "%s/%s", directory, name);
    assert(length > 0 && (size_t)length < cap);
}

static void make_file(const char *path, const unsigned char *data, size_t length)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0);
    size_t used = 0;
    while (used < length) {
        ssize_t n = write(fd, data + used, length - used);
        assert(n > 0); used += (size_t)n;
    }
    assert(close(fd) == 0);
}

static void check_file(const char *path, const unsigned char *data, size_t length)
{
    unsigned char block[4096];
    struct stat st;
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    assert(fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode));
    assert(st.st_size >= 0 && (uintmax_t)st.st_size == length && (st.st_mode & 0777) == 0600);
    size_t used = 0;
    while (used < length) {
        ssize_t n = read(fd, block, sizeof(block));
        assert(n > 0 && (size_t)n <= length - used);
        assert(!memcmp(block, data + used, (size_t)n)); used += (size_t)n;
    }
    assert(read(fd, block, 1) == 0);
    close(fd);
}

static void process_one(DccManager *manager)
{
    struct pollfd fds[DCC_MAX_ACTIVE];
    uint64_t ids[DCC_MAX_ACTIVE];
    size_t count = dcc_pollfds(manager, fds, ids, DCC_MAX_ACTIVE);
    assert(poll(fds, (nfds_t)count, 20) >= 0);
    dcc_process(manager, fds, ids, count, milliseconds());
    (void)dcc_changed(manager);
}

static void process_pair(DccManager *first, DccManager *second, uint64_t first_id, uint64_t second_id)
{
    for (size_t step = 0; step < 1000; ++step) {
        struct pollfd fds[DCC_MAX_ACTIVE * 2];
        uint64_t ids[DCC_MAX_ACTIVE * 2];
        size_t first_count = dcc_pollfds(first, fds, ids, DCC_MAX_ACTIVE);
        size_t second_count = dcc_pollfds(second, fds + first_count, ids + first_count, DCC_MAX_ACTIVE);
        assert(poll(fds, (nfds_t)(first_count + second_count), 20) >= 0);
        int64_t now = milliseconds();
        dcc_process(first, fds, ids, first_count, now);
        dcc_process(second, fds + first_count, ids + first_count, second_count, now);
        (void)dcc_changed(first); (void)dcc_changed(second);
        const DccInfo *one = dcc_get(first, first_id), *two = dcc_get(second, second_id);
        assert(one && two);
        if (one->state == DCC_FAILED || two->state == DCC_FAILED)
            fprintf(stderr, "DCC pair failure: %s / %s\n", one->error, two->error);
        assert(one->state != DCC_FAILED && two->state != DCC_FAILED);
        if (one->state == DCC_DONE && two->state == DCC_DONE) return;
    }
    assert(!"DCC pair did not finish");
}

static int listener(uint16_t *port)
{
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(fd, (struct sockaddr *)&address, sizeof(address)) == 0 && listen(fd, 1) == 0);
    assert(getsockname(fd, (struct sockaddr *)&address, &length) == 0);
    *port = ntohs(address.sin_port);
    return fd;
}

static int peer_connect(uint16_t port)
{
    struct sockaddr_in address;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(port);
    assert(connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0);
    return fd;
}

static void parser_and_offline(void)
{
    DccInfo info, unchanged;
    char error[256], body[600];
    uint64_t ids[12], duplicate;
    static const char *const invalid[] = {
        "DCC CHAT chat 2130706433 9000", "DCC SEND file 2130706433 9000",
        "DCC SEND file 2130706433 0 4", "DCC SEND file 2130706433 65536 4",
        "DCC SEND file 0 9000 4", "DCC SEND file 3758096384 9000 4",
        "DCC SEND file 4294967296 9000 4", "DCC SEND file 2130706433 9000 4294967296",
        "DCC SEND file 2130706433 9000 -1", "DCC SEND file 127.0.0.1 9000 4",
        "DCC SEND file 2130706433 9000 4 T", "DCC SEND file 2130706433 9000 4 123",
        "DCC SEND /etc/file 2130706433 9000 4", "DCC SEND ../file 2130706433 9000 4",
        "DCC SEND .hidden 2130706433 9000 4", "DCC SEND a\\b 2130706433 9000 4",
        "DCC SEND \"unterminated 2130706433 9000 4", "DCC SEND \"file\"junk 2130706433 9000 4",
        "DCC SEND \"bad\033name\" 2130706433 9000 4", "DCC SEND \"bad\xe2\x80\xaename\" 2130706433 9000 4",
        "DCC SEND file 2130706433\n 9000 4", "DCC SEND file 2130706433 9000 4\001"
    };
    assert(dcc_parse_send("DCC SEND \"file with spaces.bin\" 2130706433 9000 4294967295", &info, error, sizeof(error)));
    assert(!strcmp(info.filename, "file with spaces.bin") && !strcmp(info.address, "127.0.0.1"));
    assert(info.port == 9000 && info.size == UINT32_MAX);
    assert(dcc_parse_send("DCC SEND caf\xc3\xa9.bin 2130706433 1 0", &info, error, sizeof(error)) && !info.size);
    unchanged = info;
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!dcc_parse_send(invalid[i], &info, error, sizeof(error)));
        assert(!memcmp(&info, &unchanged, sizeof(info)) && *error);
    }
    memset(body, 'a', sizeof(body) - 1); body[sizeof(body) - 1] = 0;
    assert(!dcc_parse_send(body, &info, error, sizeof(error)));
    DccManager *manager = dcc_create(1);
    assert(manager && !dcc_count(manager));
    for (size_t i = 0; i < DCC_MAX_OFFERS; ++i) {
        snprintf(body, sizeof(body), "DCC SEND file%zu 2130706433 9000 4", i);
        assert(dcc_offer(manager, i % 2, "sender", body, &ids[i], error, sizeof(error)));
    }
    assert(dcc_count(manager) == DCC_MAX_OFFERS);
    assert(dcc_offer(manager, 1, "sender", body, &duplicate, error, sizeof(error)) && duplicate == ids[7]);
    assert(!dcc_offer(manager, 9, "sender", "DCC SEND ninth 2130706433 9000 4", &duplicate, error, sizeof(error)));
    assert(!dcc_accept(manager, ids[0], "/must-not-create-offline-dcc", NULL, error, sizeof(error)));
    assert(strstr(error, "OFFLINE TEST MODE"));
    assert(!dcc_send(manager, 1, "recipient", "/must-not-read-offline-dcc", "127.0.0.1", 0, &duplicate, body, sizeof(body), error, sizeof(error)));
    assert(strstr(error, "OFFLINE TEST MODE"));
    struct pollfd fds[4]; uint64_t poll_ids[4];
    assert(!dcc_pollfds(manager, fds, poll_ids, 4));
    assert(dcc_reject(manager, ids[0], error, sizeof(error)) && !dcc_reject(manager, ids[0], error, sizeof(error)));
    dcc_cancel_server(manager, 1);
    assert(dcc_get(manager, ids[1])->state == DCC_CANCELLED && dcc_get(manager, ids[2])->state == DCC_OFFERED);
    dcc_process(manager, NULL, NULL, 0, milliseconds() + 300001);
    assert(dcc_get(manager, ids[2])->state == DCC_FAILED);
    for (size_t i = 0; i < 30; ++i) {
        assert(dcc_offer(manager, 3, "sender", "DCC SEND recycle 2130706433 9000 4", &duplicate, error, sizeof(error)));
        assert(dcc_reject(manager, duplicate, error, sizeof(error)));
    }
    assert(dcc_count(manager) == DCC_MAX_ITEMS && dcc_at(manager, DCC_MAX_ITEMS) == NULL);
    assert(dcc_changed(manager) && !dcc_changed(manager));
    dcc_free(manager);
}

static void round_trip(const char *root, const char *name, size_t size)
{
    unsigned char *data = malloc(size ? size : 1);
    char source[DCC_PATH_MAX], directory[DCC_PATH_MAX], target[DCC_PATH_MAX];
    char body[511], error[256];
    uint64_t sent, received;
    for (size_t i = 0; i < size; ++i) data[i] = (unsigned char)(i * 37u + 11u);
    path_join(source, sizeof(source), root, name);
    path_join(directory, sizeof(directory), root, "nested/private/downloads");
    path_join(target, sizeof(target), directory, name);
    make_file(source, data, size);
    DccManager *sender = dcc_create(0), *receiver = dcc_create(0);
    assert(sender && receiver);
    int offered = dcc_send(sender, 11, "recipient", source, "127.0.0.1", 0, &sent, body, sizeof(body), error, sizeof(error));
    if (!offered) fprintf(stderr, "DCC listener setup: %s\n", error);
    assert(offered);
    assert(dcc_get(sender, sent)->state == DCC_LISTENING);
    assert(dcc_offer(receiver, 22, "sender", body, &received, error, sizeof(error)));
    assert(access(target, F_OK) < 0 && errno == ENOENT);
    assert(dcc_accept(receiver, received, directory, NULL, error, sizeof(error)));
    process_pair(sender, receiver, sent, received);
    check_file(target, data, size); check_file(source, data, size);
    assert(dcc_get(sender, sent)->acknowledged == size && dcc_get(receiver, received)->transferred == size);
    struct stat st;
    assert(stat(directory, &st) == 0 && (st.st_mode & 0777) == 0700);
    assert(dcc_offer(receiver, 22, "sender", body, &received, error, sizeof(error)));
    assert(!dcc_accept(receiver, received, directory, NULL, error, sizeof(error)) && strstr(error, "no overwrite"));
    check_file(target, data, size);
    dcc_free(receiver); dcc_free(sender);
    assert(unlink(source) == 0 && unlink(target) == 0);
    free(data);
}

static void paths_and_limits(const char *root)
{
    static const unsigned char data[] = "selected source";
    char source[DCC_PATH_MAX], symlink_path[DCC_PATH_MAX], fifo[DCC_PATH_MAX], public_dir[DCC_PATH_MAX];
    char error[256], body[511], linked_child[DCC_PATH_MAX], oversized[DCC_PATH_MAX];
    uint64_t id, ids[4];
    DccManager *manager = dcc_create(0);
    path_join(source, sizeof(source), root, "source.bin"); make_file(source, data, sizeof(data));
    path_join(symlink_path, sizeof(symlink_path), root, "source-link.bin"); assert(symlink(source, symlink_path) == 0);
    path_join(fifo, sizeof(fifo), root, "source-fifo"); assert(mkfifo(fifo, 0600) == 0);
    assert(!dcc_send(manager, 1, "peer", symlink_path, "127.0.0.1", 0, &id, body, sizeof(body), error, sizeof(error)));
    assert(!dcc_send(manager, 1, "peer", fifo, "127.0.0.1", 0, &id, body, sizeof(body), error, sizeof(error)));
    assert(strstr(error, "regular file"));
    if (sizeof(off_t) >= 8) {
        path_join(oversized, sizeof(oversized), root, "oversized.bin");
        int fd = open(oversized, O_WRONLY | O_CREAT | O_EXCL, 0600);
        assert(fd >= 0 && ftruncate(fd, (off_t)((uint64_t)UINT32_MAX + 1u)) == 0);
        close(fd);
        assert(!dcc_send(manager, 1, "peer", oversized, "127.0.0.1", 0, &id, body, sizeof(body), error, sizeof(error)));
        assert(strstr(error, "4294967295") && unlink(oversized) == 0);
    }
    assert(unlink(symlink_path) == 0 && symlink(root, symlink_path) == 0);
    path_join(linked_child, sizeof(linked_child), symlink_path, "source.bin");
    assert(!dcc_send(manager, 1, "peer", linked_child, "127.0.0.1", 0, &id, body, sizeof(body), error, sizeof(error)));
    assert(dcc_offer(manager, 1, "peer", "DCC SEND incoming.bin 2130706433 9000 4", &id, error, sizeof(error)));
    assert(!dcc_accept(manager, id, symlink_path, NULL, error, sizeof(error)));
    path_join(public_dir, sizeof(public_dir), root, "public"); assert(mkdir(public_dir, 0755) == 0 && chmod(public_dir, 0755) == 0);
    assert(!dcc_accept(manager, id, public_dir, NULL, error, sizeof(error)));
    assert(strstr(error, "no group/other"));
    assert(!dcc_accept(manager, id, root, "../bad", error, sizeof(error)));
    assert(!dcc_accept(manager, id, "/tmp/bad\xe2\x80\xae-path", NULL, error, sizeof(error)));
    for (size_t i = 0; i < DCC_MAX_ACTIVE; ++i)
        assert(dcc_send(manager, i, "peer", source, "127.0.0.1", 0, &ids[i], body, sizeof(body), error, sizeof(error)));
    assert(!dcc_send(manager, 1, "peer", source, "127.0.0.1", 0, &id, body, sizeof(body), error, sizeof(error)));
    assert(strstr(error, "limit"));
    struct pollfd old[4]; uint64_t old_ids[4];
    size_t old_count = dcc_pollfds(manager, old, old_ids, 4);
    assert(old_count == 4);
    assert(dcc_cancel(manager, ids[0], error, sizeof(error)));
    assert(dcc_send(manager, 9, "peer", source, "127.0.0.1", 0, &id, body, sizeof(body), error, sizeof(error)));
    for (size_t i = 0; i < old_count; ++i) old[i].revents = old_ids[i] == ids[0] ? POLLERR : 0;
    dcc_process(manager, old, old_ids, old_count, milliseconds());
    assert(dcc_get(manager, id)->state == DCC_LISTENING);
    dcc_cancel_server(manager, 2);
    assert(dcc_get(manager, ids[2])->state == DCC_CANCELLED && dcc_get(manager, id)->state == DCC_LISTENING);
    dcc_process(manager, NULL, NULL, 0, milliseconds() + 120001);
    assert(dcc_get(manager, id)->state == DCC_FAILED);
    dcc_free(manager);
    assert(unlink(source) == 0 && unlink(symlink_path) == 0 && unlink(fifo) == 0 && rmdir(public_dir) == 0);
}

static void incoming_failures(const char *root)
{
    char body[200], error[256], path[DCC_PATH_MAX], moved[DCC_PATH_MAX];
    uint16_t port;
    uint64_t id;
    int listening = listener(&port);
    DccManager *manager = dcc_create(0);
    snprintf(body, sizeof(body), "DCC SEND partial.bin 2130706433 %u 10", (unsigned)port);
    assert(dcc_offer(manager, 1, "peer", body, &id, error, sizeof(error)));
    assert(dcc_accept(manager, id, root, NULL, error, sizeof(error)));
    int peer = accept(listening, NULL, NULL); assert(peer >= 0);
    process_one(manager);
    assert(write(peer, "abc", 3) == 3);
    process_one(manager);
    assert(dcc_get(manager, id)->transferred == 3);
    path_join(path, sizeof(path), root, "partial.bin");
    assert(dcc_cancel(manager, id, error, sizeof(error)));
    assert(access(path, F_OK) < 0 && errno == ENOENT);
    close(peer);

    assert(dcc_offer(manager, 1, "peer", body, &id, error, sizeof(error)));
    assert(dcc_accept(manager, id, root, NULL, error, sizeof(error)));
    peer = accept(listening, NULL, NULL); assert(peer >= 0);
    process_one(manager); assert(write(peer, "abc", 3) == 3); process_one(manager);
    path_join(moved, sizeof(moved), root, "moved-partial.bin");
    assert(rename(path, moved) == 0);
    make_file(path, (const unsigned char *)"replacement", 11);
    assert(dcc_cancel(manager, id, error, sizeof(error)));
    check_file(path, (const unsigned char *)"replacement", 11);
    check_file(moved, (const unsigned char *)"abc", 3);
    assert(unlink(path) == 0 && unlink(moved) == 0); close(peer);

    snprintf(body, sizeof(body), "DCC SEND overrun.bin 2130706433 %u 2", (unsigned)port);
    assert(dcc_offer(manager, 1, "peer", body, &id, error, sizeof(error)));
    assert(dcc_accept(manager, id, root, NULL, error, sizeof(error)));
    peer = accept(listening, NULL, NULL); assert(peer >= 0);
    assert(write(peer, "abc", 3) == 3); process_one(manager); process_one(manager);
    assert(dcc_get(manager, id)->state == DCC_FAILED);
    path_join(path, sizeof(path), root, "overrun.bin"); assert(access(path, F_OK) < 0 && errno == ENOENT);
    close(peer);
    snprintf(body, sizeof(body), "DCC SEND short.bin 2130706433 %u 10", (unsigned)port);
    assert(dcc_offer(manager, 1, "peer", body, &id, error, sizeof(error)));
    assert(dcc_accept(manager, id, root, NULL, error, sizeof(error)));
    peer = accept(listening, NULL, NULL); assert(peer >= 0);
    assert(write(peer, "abc", 3) == 3 && shutdown(peer, SHUT_WR) == 0);
    process_one(manager); process_one(manager);
    assert(dcc_get(manager, id)->state == DCC_FAILED);
    path_join(path, sizeof(path), root, "short.bin"); assert(access(path, F_OK) < 0 && errno == ENOENT);
    close(peer);
    snprintf(body, sizeof(body), "DCC SEND timeout.bin 2130706433 %u 10", (unsigned)port);
    assert(dcc_offer(manager, 1, "peer", body, &id, error, sizeof(error)));
    assert(dcc_accept(manager, id, root, NULL, error, sizeof(error)));
    peer = accept(listening, NULL, NULL); assert(peer >= 0);
    dcc_process(manager, NULL, NULL, 0, milliseconds() + 120001);
    assert(dcc_get(manager, id)->state == DCC_FAILED);
    path_join(path, sizeof(path), root, "timeout.bin"); assert(access(path, F_OK) < 0 && errno == ENOENT);
    close(peer); close(listening); dcc_free(manager);
}

static void outgoing_acknowledgements(const char *root)
{
    char source[DCC_PATH_MAX], body[511], error[256], received[16];
    uint64_t id;
    uint32_t acknowledgement;
    DccManager *manager = dcc_create(0);
    path_join(source, sizeof(source), root, "ack-source.bin"); make_file(source, (const unsigned char *)"abcdefgh", 8);
    assert(dcc_send(manager, 1, "peer", source, "127.0.0.1", 0, &id, body, sizeof(body), error, sizeof(error)));
    int peer = peer_connect(dcc_get(manager, id)->port);
    process_one(manager); process_one(manager);
    assert(read(peer, received, sizeof(received)) == 8 && !memcmp(received, "abcdefgh", 8));
    acknowledgement = htonl(8);
    assert(write(peer, &acknowledgement, 1) == 1); process_one(manager);
    assert(dcc_get(manager, id)->state == DCC_SENDING);
    assert(write(peer, (const unsigned char *)&acknowledgement + 1, 3) == 3); process_one(manager);
    assert(dcc_get(manager, id)->state == DCC_DONE && dcc_get(manager, id)->acknowledged == 8);
    close(peer);
    assert(dcc_send(manager, 1, "peer", source, "127.0.0.1", 0, &id, body, sizeof(body), error, sizeof(error)));
    peer = peer_connect(dcc_get(manager, id)->port); process_one(manager); process_one(manager);
    assert(read(peer, received, sizeof(received)) == 8);
    acknowledgement = htonl(9);
    assert(write(peer, &acknowledgement, 4) == 4); process_one(manager);
    assert(dcc_get(manager, id)->state == DCC_FAILED && strstr(dcc_get(manager, id)->error, "acknowledgement"));
    close(peer);
    assert(dcc_send(manager, 1, "peer", source, "127.0.0.1", 0, &id, body, sizeof(body), error, sizeof(error)));
    int shortened = open(source, O_WRONLY | O_TRUNC); assert(shortened >= 0); close(shortened);
    peer = peer_connect(dcc_get(manager, id)->port); process_one(manager); process_one(manager);
    assert(dcc_get(manager, id)->state == DCC_FAILED && strstr(dcc_get(manager, id)->error, "source ended"));
    close(peer);
    assert(dcc_send(manager, 1, "peer", source, "127.0.0.1", 0, &id, body, sizeof(body), error, sizeof(error)));
    peer = peer_connect(dcc_get(manager, id)->port); process_one(manager);
    struct pollfd fds[4]; uint64_t ids[4];
    assert(dcc_pollfds(manager, fds, ids, 4) == 1 && !(fds[0].events & POLLOUT));
    /* An empty outgoing file waits for ACK0, without a writable busy loop. */
    acknowledgement = 0;
    assert(write(peer, &acknowledgement, 4) == 4); process_one(manager);
    assert(dcc_get(manager, id)->state == DCC_DONE);
    close(peer); dcc_free(manager); assert(unlink(source) == 0);
}

int main(void)
{
    char root[] = "/tmp/vipirc-dcc-XXXXXX", path[DCC_PATH_MAX];
    signal(SIGPIPE, SIG_IGN);
    parser_and_offline();
    assert(mkdtemp(root));
    round_trip(root, "binary file.bin", 65573);
    round_trip(root, "empty.bin", 0);
    paths_and_limits(root);
    incoming_failures(root);
    outgoing_acknowledgements(root);
    path_join(path, sizeof(path), root, "nested/private/downloads"); assert(rmdir(path) == 0);
    path_join(path, sizeof(path), root, "nested/private"); assert(rmdir(path) == 0);
    path_join(path, sizeof(path), root, "nested"); assert(rmdir(path) == 0);
    assert(rmdir(root) == 0);
    puts("dcc: strict parsing, offline guards, bounded offers/transfers, binary/empty round trips, safe paths, cancellation, timeouts and fragmented/invalid acknowledgements passed");
    return 0;
}
