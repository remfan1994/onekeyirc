/* Deterministic adversarial and lifecycle stress. Local loopback/tmp only.
 * Build: cc -std=c11 -O2 -Isrc tests/test_dcc_stress.c src/dcc.c
 *        src/protocol.c -o build/test_dcc_stress
 * Run: build/test_dcc_stress --seconds 60
 * A hard alarm also bounds accidental blocking in the harness itself. */
#define _POSIX_C_SOURCE 200809L
#include "dcc.h"
#include "protocol.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PAYLOAD 65573u
#define PEERS 4u

static uint32_t rng = UINT32_C(0x9a3d17c5);
static uint64_t lifecycles, bytes_checked, engine_calls;
static int64_t longest_call_ns;
static size_t peak_fds;

static uint64_t resident_kib(void)
{
    unsigned long total, resident;
    FILE *file = fopen("/proc/self/statm", "r");
    if (!file) return 0;
    int fields = fscanf(file, "%lu %lu", &total, &resident);
    assert(fclose(file) == 0);
    long page_size = sysconf(_SC_PAGESIZE);
    return fields == 2 && page_size > 0 ? (uint64_t)resident * (uint64_t)page_size / 1024u : 0;
}

static int64_t nanoseconds(void)
{
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (int64_t)now.tv_sec * INT64_C(1000000000) + now.tv_nsec;
}

static int64_t milliseconds(void) { return nanoseconds() / INT64_C(1000000); }

static uint32_t random_next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void join_path(char *out, size_t cap, const char *base, const char *leaf)
{
    int n = snprintf(out, cap, "%s/%s", base, leaf);
    assert(n > 0 && (size_t)n < cap);
}

static unsigned char pattern(size_t n)
{
    return (unsigned char)((n * 37u + n / 257u + 11u) & 255u);
}

static void fill(unsigned char *data, size_t count, size_t offset)
{
    for (size_t i = 0; i < count; ++i) data[i] = pattern(offset + i);
}

static void write_all(int fd, const void *data, size_t count)
{
    const unsigned char *bytes = data;
    size_t offset = 0;
    while (offset < count) {
        ssize_t n = write(fd, bytes + offset, count - offset);
        if (n < 0 && errno == EINTR) continue;
        assert(n > 0);
        offset += (size_t)n;
    }
}

static void create_file(const char *path, const void *bytes, size_t count)
{
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
    assert(fd >= 0);
    write_all(fd, bytes, count);
    assert(close(fd) == 0);
}

static void check_pattern_file(const char *path, size_t count)
{
    struct stat st;
    unsigned char data[8192];
    size_t offset = 0;
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    assert(fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode));
    assert(st.st_size >= 0 && (uintmax_t)st.st_size == count);
    assert((st.st_mode & 0777) == 0600);
    for (;;) {
        ssize_t n = read(fd, data, sizeof data);
        if (n < 0 && errno == EINTR) continue;
        assert(n >= 0 && (size_t)n <= count - offset);
        if (!n) break;
        for (size_t i = 0; i < (size_t)n; ++i) assert(data[i] == pattern(offset + i));
        offset += (size_t)n;
    }
    assert(offset == count && close(fd) == 0);
    bytes_checked += count;
}

static void absent(const char *path)
{
    struct stat st;
    assert(lstat(path, &st) < 0 && errno == ENOENT);
}

static size_t descriptors(void)
{
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) directory = opendir("/dev/fd");
    assert(directory);
    int own = dirfd(directory);
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (*entry->d_name == '.') continue;
        char *end;
        long value = strtol(entry->d_name, &end, 10);
        assert(*entry->d_name && !*end && value >= 0);
        if (value != own) ++count;
    }
    assert(closedir(directory) == 0);
    if (count > peak_fds) peak_fds = count;
    return count;
}

static int would_block(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

static void make_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    assert(flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
    int one = 1;
    assert(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one) == 0);
}

static int listener(uint16_t *port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    socklen_t length = sizeof address;
    assert(fd >= 0);
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(fd, (struct sockaddr *)&address, sizeof address) == 0);
    assert(listen(fd, 8) == 0);
    assert(getsockname(fd, (struct sockaddr *)&address, &length) == 0);
    *port = ntohs(address.sin_port);
    return fd;
}

static int connect_peer(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    assert(fd >= 0);
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    assert(connect(fd, (struct sockaddr *)&address, sizeof address) == 0);
    make_nonblocking(fd);
    return fd;
}

static void process(DccManager *manager, struct pollfd *fds,
                    uint64_t *ids, size_t count, int64_t when)
{
    uint32_t before[DCC_MAX_ACTIVE];
    for (size_t i = 0; i < count; ++i) {
        const DccInfo *info = dcc_get(manager, ids[i]);
        before[i] = info ? info->transferred : 0;
    }
    int64_t start = nanoseconds();
    dcc_process(manager, fds, ids, count, when);
    int64_t elapsed = nanoseconds() - start;
    if (elapsed > longest_call_ns) longest_call_ns = elapsed;
    ++engine_calls;
    for (size_t i = 0; i < count; ++i) {
        const DccInfo *info = dcc_get(manager, ids[i]);
        if (info) {
            assert(info->transferred >= before[i]);
            assert(info->transferred - before[i] <= 65536u);
            assert(info->acknowledged <= info->transferred && info->transferred <= info->size);
        }
    }
    assert(dcc_count(manager) <= DCC_MAX_ITEMS);
    assert(dcc_timeout(manager, when) >= 0 && dcc_timeout(manager, when) <= 1000);
    (void)dcc_changed(manager);
}

static void step(DccManager *manager)
{
    struct pollfd fds[DCC_MAX_ACTIVE];
    uint64_t ids[DCC_MAX_ACTIVE];
    size_t count = dcc_pollfds(manager, fds, ids, DCC_MAX_ACTIVE);
    assert(count <= DCC_MAX_ACTIVE);
    int rc = poll(fds, (nfds_t)count, 1);
    assert(rc >= 0 || errno == EINTR);
    process(manager, fds, ids, count, milliseconds());
}

static void parser_mutations(void)
{
    static const char *const corpus[] = {
        "DCC SEND file.bin 2130706433 9000 65573",
        "DCC SEND \"two words.bin\" 2130706433 65535 4294967295",
        "DCC SEND caf\xc3\xa9.bin 2130706433 1 0",
        "DCC SEND .hidden 2130706433 9000 1",
        "DCC SEND ../escape 2130706433 9000 1",
        "@a=b :n!u@h PRIVMSG #channel :hello Nick",
        ":server 353 me = #channel :@me +user",
        "\00312,4Ni\002ck\017 \004ffffff,000000text\033[2J"
    };
    for (size_t trial = 0; trial < 200000; ++trial) {
        char scratch[IRC_WIRE_MAX + 16];
        size_t length;
        if (trial % 257 == 0) {
            size_t tags = 8188u + random_next() % 7u;
            size_t body = 507u + random_next() % 7u;
            memset(scratch, 'a', tags + body + 2);
            scratch[0] = '@'; scratch[tags] = ' ';
            memcpy(scratch + tags + 1, "PRIVMSG x :", 11);
            length = tags + 1 + body;
        } else if (trial % 3) {
            const char *base = corpus[random_next() % (sizeof corpus / sizeof corpus[0])];
            length = strlen(base);
            memcpy(scratch, base, length);
            unsigned edits = 1 + random_next() % 7u;
            while (edits--) scratch[random_next() % length] = (char)(1u + random_next() % 255u);
        } else {
            length = random_next() % 1200u;
            for (size_t i = 0; i < length; ++i) scratch[i] = (char)(1u + random_next() % 255u);
        }
        char *exact = malloc(length + 1);
        assert(exact);
        memcpy(exact, scratch, length); exact[length] = 0;
        DccInfo info, before;
        memset(&info, 0xa5, sizeof info); before = info;
        unsigned char errors[35]; memset(errors, 0x5a, sizeof errors);
        size_t error_cap = random_next() % 34u;
        int accepted = dcc_parse_send(exact, &info, (char *)errors + 1, error_cap);
        assert(errors[0] == 0x5a && errors[1 + error_cap] == 0x5a);
        if (!accepted) assert(!memcmp(&info, &before, sizeof info));
        else {
            struct in_addr ip;
            assert(info.state == DCC_OFFERED && info.port && info.filename[0]);
            assert(strlen(info.filename) <= DCC_NAME_MAX && irc_utf8_valid(info.filename));
            assert(inet_pton(AF_INET, info.address, &ip) == 1);
            assert(!strchr(info.filename, '/') && !strchr(info.filename, '\\'));
        }
        size_t cap = random_next() % 1025u;
        unsigned char *output = malloc(cap + 2);
        assert(output); memset(output, 0x5a, cap + 2);
        size_t used = irc_sanitize((char *)output + 1, cap, exact);
        assert(output[0] == 0x5a && output[cap + 1] == 0x5a);
        if (cap) {
            assert(used < cap && output[used + 1] == 0 && irc_utf8_valid((char *)output + 1));
            char again[1025];
            assert(irc_sanitize(again, sizeof again, (char *)output + 1) == used);
            assert(!memcmp(again, output + 1, used + 1));
            IrcChunk chunks[64]; size_t count;
            size_t budget = 1u + random_next() % 512u;
            if (irc_split((char *)output + 1, budget, chunks, 64, &count)) {
                size_t offset = 0;
                for (size_t i = 0; i < count; ++i) {
                    assert(chunks[i].offset == offset && chunks[i].length <= budget);
                    char piece[513];
                    memcpy(piece, output + 1 + offset, chunks[i].length);
                    piece[chunks[i].length] = 0;
                    assert(irc_utf8_valid(piece));
                    offset += chunks[i].length;
                }
                assert(offset == used);
            } else assert(count == 0);
        } else assert(!used);
        free(output);
        IrcMessage parsed;
        if (irc_parse(exact, &parsed)) {
            assert(parsed.command >= exact && parsed.command <= exact + length);
            assert(parsed.nparams <= 15);
            for (size_t i = 0; i < parsed.nparams; ++i)
                assert(parsed.params[i] >= exact && parsed.params[i] <= exact + length);
        }
        free(exact);
    }
    puts("stress: 200000 exact-allocation DCC/IRC mutations, output canaries, sanitizer idempotence and UTF-8 split properties passed");
}

static void offer_churn(void)
{
    DccManager *manager = dcc_create(1);
    char body[160], error[160];
    uint64_t last = 0, old = 0;
    assert(manager);
    size_t before = descriptors();
    for (size_t cycle = 0; cycle < 10000; ++cycle) {
        uint64_t ids[8];
        for (size_t i = 0; i < 8; ++i) {
            snprintf(body, sizeof body, "DCC SEND item-%zu-%zu 2130706433 9000 4", cycle, i);
            assert(dcc_offer(manager, i % 3u + 1u, "sender", body, &ids[i], error, sizeof error));
            assert(ids[i] > last); last = ids[i];
            uint64_t duplicate;
            assert(dcc_offer(manager, i % 3u + 1u, "sender", body, &duplicate, error, sizeof error));
            assert(duplicate == ids[i]);
        }
        uint64_t rejected;
        assert(!dcc_offer(manager, 1, "sender", "DCC SEND excess 2130706433 9000 4", &rejected, error, sizeof error));
        assert(!rejected);
        assert(dcc_reject(manager, ids[0], error, sizeof error));
        assert(dcc_cancel(manager, ids[1], error, sizeof error));
        dcc_cancel_server(manager, 3);
        assert(dcc_get(manager, ids[2])->state == DCC_CANCELLED);
        assert(dcc_get(manager, ids[3])->state == DCC_OFFERED);
        dcc_process(manager, NULL, NULL, 0, milliseconds() + 300001);
        for (size_t i = 0; i < 8; ++i) {
            const DccInfo *info = dcc_get(manager, ids[i]);
            assert(info && (info->state == DCC_FAILED || info->state == DCC_CANCELLED || info->state == DCC_REJECTED));
            assert(!dcc_cancel(manager, ids[i], error, sizeof error));
            assert(!dcc_reject(manager, ids[i], error, sizeof error));
        }
        if (cycle > 1) assert(!dcc_get(manager, old));
        old = ids[0];
        assert(dcc_count(manager) <= DCC_MAX_ITEMS);
        lifecycles += 8;
    }
    dcc_free(manager);
    assert(descriptors() == before);
    puts("stress: 80000 unique offers plus duplicate/cap/reject/cancel/token/expiry/recycle checks passed without opening descriptors");
}

typedef struct Peer {
    int fd, mode, write_closed, regression, truncated;
    uint64_t id;
    uint32_t received, acked, ack_target, expected;
    unsigned char ack[4];
    size_t ack_offset, ack_length;
} Peer;

static void prepare_ack(Peer *peer, uint32_t count)
{
    uint32_t wire = htonl(count);
    memcpy(peer->ack, &wire, 4);
    peer->ack_target = count; peer->ack_offset = 0; peer->ack_length = 4;
}

static void outgoing_round(DccManager *manager, const char *source, const char *empty,
                           const char *changing, size_t cycle)
{
    Peer peers[PEERS];
    char error[160], body[511];
    struct pollfd stale[4]; uint64_t stale_ids[4];
    memset(peers, 0, sizeof peers);
    for (size_t i = 0; i < PEERS; ++i) {
        Peer *peer = &peers[i];
        peer->expected = i == 3 && cycle % 3 == 0 ? 0 : PAYLOAD;
        peer->mode = i == 2 ? 1 + (int)(cycle % 5u) : 0;
        const char *path = peer->mode == 5 ? changing : peer->expected ? source : empty;
        assert(dcc_send(manager, i + 1, "peer", path, "127.0.0.1", 0,
                        &peer->id, body, sizeof body, error, sizeof error));
        peer->fd = connect_peer(dcc_get(manager, peer->id)->port);
    }
    size_t old_count = dcc_pollfds(manager, stale, stale_ids, 4);
    assert(old_count == 4);
    uint64_t extra;
    assert(!dcc_send(manager, 9, "peer", source, "127.0.0.1", 0, &extra, body, sizeof body, error, sizeof error));
    int64_t deadline = milliseconds() + 5000;
    size_t steps = 0;
    for (;;) {
        assert(milliseconds() < deadline);
        struct pollfd fds[8]; uint64_t ids[4];
        size_t count = dcc_pollfds(manager, fds, ids, 4);
        for (size_t i = 0; i < PEERS; ++i) {
            fds[count + i].fd = peers[i].fd;
            fds[count + i].events = POLLIN | (peers[i].ack_length ? POLLOUT : 0);
            fds[count + i].revents = 0;
        }
        assert(poll(fds, (nfds_t)(count + PEERS), 1) >= 0);
        process(manager, fds, ids, count, milliseconds());
        int unfinished = 0;
        for (size_t i = 0; i < PEERS; ++i) {
            Peer *peer = &peers[i];
            const DccInfo *info = dcc_get(manager, peer->id);
            assert(info);
            if (info->state == DCC_FAILED) { assert(peer->mode); continue; }
            if (info->state == DCC_DONE) { assert(!peer->mode && peer->received == peer->expected); continue; }
            if (peer->mode != 4) unfinished = 1;
            if (peer->mode == 5 && info->transferred && !peer->truncated) {
                int fd = open(changing, O_WRONLY | O_TRUNC | O_NOFOLLOW);
                assert(fd >= 0 && close(fd) == 0);
                peer->truncated = 1;
            }
            unsigned char data[4096];
            ssize_t n = recv(peer->fd, data, sizeof data, 0);
            if (n < 0) assert(would_block());
            else if (n > 0) {
                assert((size_t)n <= peer->expected - peer->received);
                for (size_t k = 0; k < (size_t)n; ++k) assert(data[k] == pattern(peer->received + k));
                peer->received += (uint32_t)n; bytes_checked += (uint64_t)n;
            }
            if (peer->write_closed) continue;
            if (!peer->ack_length && (peer->received > peer->acked || !peer->expected)) {
                uint32_t value = peer->received;
                if (peer->mode == 1) value = PAYLOAD + 1u;
                else if (peer->mode == 4) value = 0;
                else if (peer->mode == 2 && peer->regression) value = peer->acked - 1u;
                prepare_ack(peer, value);
            } else if (!peer->ack_length && peer->mode == 2 && peer->regression && peer->acked) {
                prepare_ack(peer, peer->acked - 1u);
            }
            if (peer->ack_length) {
                size_t amount = peer->ack_length - peer->ack_offset;
                size_t fragment = 1u + (cycle + i + steps) % 3u;
                if (amount > fragment) amount = fragment;
                if (peer->mode == 3 && amount > 1) amount = 1;
                ssize_t sent = send(peer->fd, peer->ack + peer->ack_offset, amount, 0);
                if (sent < 0) assert(would_block() || errno == EPIPE || errno == ECONNRESET);
                else { assert(sent > 0); peer->ack_offset += (size_t)sent; }
                if (peer->mode == 3 && peer->ack_offset == 2) {
                    assert(shutdown(peer->fd, SHUT_WR) == 0); peer->write_closed = 1;
                }
                if (peer->ack_offset == peer->ack_length) {
                    peer->acked = peer->ack_target; peer->ack_length = 0;
                    if (peer->mode == 2) peer->regression = 1;
                }
            }
        }
        ++steps;
        if (!unfinished) break;
    }
    if (peers[2].mode == 4) {
        assert(dcc_get(manager, peers[2].id)->state == DCC_SENDING);
        assert(dcc_get(manager, peers[2].id)->acknowledged == 0);
        dcc_process(manager, NULL, NULL, 0, milliseconds() + 120001);
        assert(dcc_get(manager, peers[2].id)->state == DCC_FAILED);
    }
    for (size_t i = 0; i < PEERS; ++i) {
        const DccInfo *info = dcc_get(manager, peers[i].id);
        assert(info->state == (peers[i].mode ? DCC_FAILED : DCC_DONE));
        assert(close(peers[i].fd) == 0);
    }
    if (peers[2].mode == 5) {
        unsigned char data[PAYLOAD]; fill(data, sizeof data, 0);
        int fd = open(changing, O_WRONLY | O_TRUNC | O_NOFOLLOW);
        assert(fd >= 0); write_all(fd, data, sizeof data); assert(close(fd) == 0);
    }
    /* Reused descriptors cannot make a stale old poll event affect a new ID. */
    uint64_t fresh;
    assert(dcc_send(manager, 99, "peer", source, "127.0.0.1", 0, &fresh, body, sizeof body, error, sizeof error));
    for (size_t i = 0; i < old_count; ++i) stale[i].revents = POLLERR | POLLHUP;
    process(manager, stale, stale_ids, old_count, milliseconds());
    assert(dcc_get(manager, fresh)->state == DCC_LISTENING);
    assert(dcc_cancel(manager, fresh, error, sizeof error));
    lifecycles += 5;
}

static void incoming_round(DccManager *manager, const char *root, int listening, uint16_t port)
{
    int peers[4]; uint64_t ids[4];
    char dirs[4][DCC_PATH_MAX], paths[4][DCC_PATH_MAX], body[160], error[160];
    static const uint32_t sizes[4] = {65537, 7, 10, 100};
    size_t sent[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < 4; ++i) {
        char name[32], subdir[16];
        snprintf(name, sizeof name, "incoming%zu.bin", i);
        snprintf(subdir, sizeof subdir, "receive%zu", i);
        join_path(dirs[i], sizeof dirs[i], root, subdir);
        join_path(paths[i], sizeof paths[i], dirs[i], name);
        snprintf(body, sizeof body, "DCC SEND %s 2130706433 %u %" PRIu32, name, (unsigned)port, sizes[i]);
        assert(dcc_offer(manager, i + 1, "peer", body, &ids[i], error, sizeof error));
        absent(paths[i]);
        assert(dcc_accept(manager, ids[i], dirs[i], NULL, error, sizeof error));
        struct pollfd ready = {listening, POLLIN, 0};
        assert(poll(&ready, 1, 1000) > 0);
        peers[i] = accept(listening, NULL, NULL); assert(peers[i] >= 0);
        make_nonblocking(peers[i]);
    }
    assert(descriptors() >= 3 + 4 * 3);
    int renamed = 0;
    int64_t deadline = milliseconds() + 5000;
    for (;;) {
        assert(milliseconds() < deadline);
        step(manager);
        int unfinished = 0;
        for (size_t i = 0; i < 4; ++i) {
            const DccInfo *info = dcc_get(manager, ids[i]);
            assert(info);
            if (info->state == DCC_DONE || info->state == DCC_FAILED || info->state == DCC_CANCELLED) continue;
            unfinished = 1;
            size_t wanted = i == 0 ? sizes[0] : i == 1 ? 8 : i == 2 ? 3 : 7;
            if (sent[i] < wanted) {
                unsigned char data[8192];
                size_t amount = wanted - sent[i];
                if (amount > sizeof data) amount = sizeof data;
                fill(data, amount, sent[i]);
                ssize_t n = send(peers[i], data, amount, 0);
                if (n < 0) assert(would_block());
                else { assert(n > 0); sent[i] += (size_t)n; }
                if (i == 2 && sent[i] == wanted) assert(shutdown(peers[i], SHUT_WR) == 0);
            }
            unsigned char ack[64];
            ssize_t n = recv(peers[i], ack, sizeof ack, 0);
            if (n < 0) assert(would_block());
            if (i == 3 && info->transferred == 7 && !renamed) {
                char moved[DCC_PATH_MAX], old_partial[DCC_PATH_MAX];
                join_path(moved, sizeof moved, root, "renamed-receive");
                assert(rename(dirs[i], moved) == 0 && mkdir(dirs[i], 0700) == 0);
                create_file(paths[i], "replacement", 11);
                assert(dcc_cancel(manager, ids[i], error, sizeof error));
                join_path(old_partial, sizeof old_partial, moved, "incoming3.bin");
                absent(old_partial);
                char replacement[12];
                int fd = open(paths[i], O_RDONLY | O_NOFOLLOW);
                assert(fd >= 0 && read(fd, replacement, sizeof replacement) == 11);
                assert(!memcmp(replacement, "replacement", 11) && close(fd) == 0);
                assert(unlink(paths[i]) == 0 && rmdir(dirs[i]) == 0 && rename(moved, dirs[i]) == 0);
                renamed = 1;
            }
        }
        if (!unfinished) break;
    }
    assert(renamed && dcc_get(manager, ids[0])->state == DCC_DONE);
    assert(dcc_get(manager, ids[1])->state == DCC_FAILED && dcc_get(manager, ids[2])->state == DCC_FAILED);
    check_pattern_file(paths[0], sizes[0]);
    assert(unlink(paths[0]) == 0);
    for (size_t i = 1; i < 4; ++i) absent(paths[i]);
    for (size_t i = 0; i < 4; ++i) assert(close(peers[i]) == 0);
    lifecycles += 4;
}

static void free_active(const char *root, int listening, uint16_t port)
{
    DccManager *manager = dcc_create(0);
    char body[160], error[160], path[DCC_PATH_MAX]; uint64_t id;
    snprintf(body, sizeof body, "DCC SEND free-active.bin 2130706433 %u 100", (unsigned)port);
    assert(dcc_offer(manager, 1, "peer", body, &id, error, sizeof error));
    assert(dcc_accept(manager, id, root, NULL, error, sizeof error));
    struct pollfd ready = {listening, POLLIN, 0};
    assert(poll(&ready, 1, 1000) > 0);
    int peer = accept(listening, NULL, NULL); assert(peer >= 0);
    write_all(peer, "partial", 7);
    for (size_t tries = 0; tries < 100 && dcc_get(manager, id)->transferred != 7; ++tries) step(manager);
    assert(dcc_get(manager, id)->transferred == 7);
    dcc_free(manager);
    join_path(path, sizeof path, root, "free-active.bin"); absent(path);
    assert(close(peer) == 0);
    ++lifecycles;
}

int main(int argc, char **argv)
{
    unsigned seconds = 60;
    if (argc == 3 && !strcmp(argv[1], "--seconds")) {
        char *end; unsigned long value = strtoul(argv[2], &end, 10);
        if (!*argv[2] || *end || value < 1 || value > 300) return 2;
        seconds = (unsigned)value;
    } else if (argc != 1) return 2;
    signal(SIGPIPE, SIG_IGN);
    alarm(seconds + 60);
    setvbuf(stdout, NULL, _IOLBF, 0);
    int64_t started = nanoseconds();
    struct rusage begin, end; assert(getrusage(RUSAGE_SELF, &begin) == 0);
    size_t baseline = descriptors();
    printf("dcc/protocol stress seed=%" PRIu32 " live_seconds=%u burst_cycles=1000 later_cycle_floor_ms=25\n", rng, seconds);
    parser_mutations();
    offer_churn();
    char root[] = "/tmp/vipirc-dcc-stress-XXXXXX", source[DCC_PATH_MAX], empty[DCC_PATH_MAX], changing[DCC_PATH_MAX];
    assert(mkdtemp(root));
    unsigned char data[PAYLOAD]; fill(data, sizeof data, 0);
    join_path(source, sizeof source, root, "source.bin"); create_file(source, data, sizeof data);
    join_path(empty, sizeof empty, root, "empty.bin"); create_file(empty, "", 0);
    join_path(changing, sizeof changing, root, "changing.bin"); create_file(changing, data, sizeof data);
    for (size_t i = 0; i < 4; ++i) {
        char leaf[32], path[DCC_PATH_MAX];
        snprintf(leaf, sizeof leaf, "receive%zu", i); join_path(path, sizeof path, root, leaf);
        assert(mkdir(path, 0700) == 0);
    }
    uint16_t port; int listening = listener(&port);
    DccManager *sender = dcc_create(0), *receiver = dcc_create(0); assert(sender && receiver);
    size_t steady_fds = descriptors(), cycles = 0;
    uint64_t starting_rss = resident_kib(), peak_rss = starting_rss;
    int64_t live_started = nanoseconds(), until = live_started + (int64_t)seconds * INT64_C(1000000000);
    int64_t report_at = live_started + INT64_C(10000000000);
    do {
        int64_t cycle_started = nanoseconds();
        outgoing_round(sender, source, empty, changing, cycles);
        incoming_round(receiver, root, listening, port);
        assert(descriptors() == steady_fds);
        ++cycles;
        /* After the burst, keep sustained connection churn below common
         * ephemeral-port/TIME_WAIT limits. This wait belongs to the fixture,
         * not the engine; each cycle still exercises all four active slots. */
        if (cycles > 1000) {
            int64_t remaining = 25 - (nanoseconds() - cycle_started) / INT64_C(1000000);
            if (remaining > 0) (void)poll(NULL, 0, (int)remaining);
        }
        if (nanoseconds() >= report_at) {
            uint64_t rss = resident_kib(); if (rss > peak_rss) peak_rss = rss;
            printf("progress live_seconds=%.3f cycles=%zu completed_lifecycles=%" PRIu64 " bytes_checked=%" PRIu64 " fds=%zu rss_kib=%" PRIu64 "\n",
                   (double)(nanoseconds() - live_started) / 1e9, cycles, lifecycles, bytes_checked, steady_fds, rss);
            report_at += INT64_C(10000000000);
        }
    } while (nanoseconds() < until);
    free_active(root, listening, port);
    assert(descriptors() == steady_fds);
    dcc_free(sender); dcc_free(receiver); assert(close(listening) == 0);
    assert(unlink(source) == 0 && unlink(empty) == 0 && unlink(changing) == 0);
    for (size_t i = 0; i < 4; ++i) {
        char leaf[32], path[DCC_PATH_MAX];
        snprintf(leaf, sizeof leaf, "receive%zu", i); join_path(path, sizeof path, root, leaf);
        assert(rmdir(path) == 0);
    }
    assert(rmdir(root) == 0 && descriptors() == baseline);
    assert(getrusage(RUSAGE_SELF, &end) == 0);
    double cpu = (double)(end.ru_utime.tv_sec - begin.ru_utime.tv_sec) +
                 (double)(end.ru_utime.tv_usec - begin.ru_utime.tv_usec) / 1e6 +
                 (double)(end.ru_stime.tv_sec - begin.ru_stime.tv_sec) +
                 (double)(end.ru_stime.tv_usec - begin.ru_stime.tv_usec) / 1e6;
    printf("PASS elapsed_seconds=%.6f cpu_seconds=%.6f cycles=%zu completed_lifecycles=%" PRIu64
           " bytes_checked=%" PRIu64 " engine_calls=%" PRIu64 " max_engine_call_ms=%.6f"
           " baseline_fds=%zu final_fds=%zu peak_observed_fds=%zu starting_live_rss_kib=%" PRIu64
           " final_rss_kib=%" PRIu64 " peak_sampled_live_rss_kib=%" PRIu64 " maxrss_native_units=%ld\n",
           (double)(nanoseconds() - started) / 1e9, cpu, cycles, lifecycles, bytes_checked, engine_calls,
           (double)longest_call_ns / 1e6, baseline, descriptors(), peak_fds,
           starting_rss, resident_kib(), peak_rss, end.ru_maxrss);
    alarm(0);
    return 0;
}
