/* Native socketpair/SCM_RIGHTS boundary tests for the production decoder.
 * No socket/bind/recvmsg wrappers are used. This is not an end-to-end
 * pathname-listener test and does not claim native AF_UNIX bind coverage. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif
#include "background.c"
#include <assert.h>
#include <time.h>

int net_detach_ready(Net *net) { (void)net; return 1; }
void credential_reap(void) { }
int credential_cleanup_pending(void) { return 0; }
int64_t net_now_ms(void)
{
    struct timespec now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &now));
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static size_t descriptors(void)
{
    /* /dev/fd can be a static device directory on BSD. Count live entries
     * with F_GETFD instead; every fixture-owned source/control descriptor
     * is asserted below 1024. A leak is caught before exhausting that range. */
    size_t count = 0;
    for (int fd = 0; fd < 1024; ++fd)
        if (fcntl(fd, F_GETFD) >= 0) ++count;
    return count;
}

static Background *worker(void)
{
    Background *b = background_create(); assert(b);
    b->managed = b->detached = 1; b->job = 17;
    for (size_t i = 0; i < sizeof b->token; ++i) b->token[i] = (unsigned char)(i + 1);
    memcpy(b->label, "private fixture", sizeof "private fixture");
    return b;
}

static int candidate(Background *b)
{
    int pair[2];
    assert(b->candidate < 0 && b->controller < 0);
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(pair[0] < 1024 && pair[1] < 1024);
    assert(fd_flags(pair[0], 1) && fd_flags(pair[1], 1) && same_owner(pair[0]));
    b->candidate = pair[0]; b->candidate_deadline = net_now_ms() + BG_HANDSHAKE_MS;
    return pair[1];
}

static void packet(int fd, const void *data, size_t length, int source, size_t rights)
{
    union { struct cmsghdr align; unsigned char bytes[CMSG_SPACE(sizeof(int) * 40)]; } ancillary;
    struct iovec vector = {(void *)data, length};
    struct msghdr message;
    memset(&message, 0, sizeof message); memset(&ancillary, 0, sizeof ancillary);
    message.msg_iov = &vector; message.msg_iovlen = 1;
    if (rights) {
        assert(rights <= 40);
        assert(source >= 0 && source < 1024);
        message.msg_control = ancillary.bytes;
        message.msg_controllen = CMSG_SPACE(sizeof(int) * rights);
        struct cmsghdr *c = CMSG_FIRSTHDR(&message);
        c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int) * rights);
        for (size_t i = 0; i < rights; ++i)
            memcpy((char *)CMSG_DATA(c) + i * sizeof(int), &source, sizeof source);
    }
    assert(sendmsg(fd, &message, 0) == (ssize_t)length);
}

static void rejected(Background *b, int peer)
{
    unsigned char reply[2];
    assert(b->candidate < 0 && b->controller < 0 && b->detached && !b->candidate_count);
    assert(recv(peer, reply, sizeof reply, 0) == 2 && reply[0] == 'E' && reply[1] == 2);
    assert(!close(peer));
}

int main(void)
{
    assert(signal(SIGPIPE, SIG_IGN) != SIG_ERR);
    size_t initial = descriptors();
    Background *b = worker();
    int quiet = open("/dev/null", O_RDWR | O_CLOEXEC); assert(quiet >= 0 && quiet < 1024);
    size_t baseline = descriptors();
    AttachRequest request;
    memset(&request, 0, sizeof request); memcpy(request.magic, "VIPIRCB3", 8);
    memcpy(request.token, b->token, sizeof request.token);
    memcpy(request.term, "xterm", sizeof "xterm"); memcpy(request.locale, "C", sizeof "C");
    request.frontend = BACKGROUND_FRONTEND_IDLE;

    /* 0..40 real rights includes missing/excess descriptors, non-TTY devices
     * and kernel ancillary truncation beyond the receiver's eight-fd buffer. */
    for (size_t count = 0; count <= 40; ++count) {
        int peer = candidate(b);
        packet(peer, &request, sizeof request, quiet, count);
        assert(!receive_candidate(b)); rejected(b, peer);
        assert(descriptors() == baseline);
    }
    for (int expiry = 0; expiry < 2; ++expiry) for (int count = 1; count <= 3; ++count) {
        int peer = candidate(b);
        packet(peer, &request, 1, quiet, (size_t)count);
        assert(!receive_candidate(b) && b->candidate_count == count && b->candidate_used == 1);
        for (int i = 0; i < count; ++i) {
            assert(b->candidate_tty[i] < 1024);
            assert(fcntl(b->candidate_tty[i], F_GETFD) & FD_CLOEXEC);
        }
        assert(descriptors() == baseline + 2 + (size_t)count);
        if (expiry) {
            b->candidate_deadline = 1;
            assert(!background_process(b, NULL, 0)); rejected(b, peer);
        } else {
            assert(!close(peer)); assert(!receive_candidate(b));
            assert(b->candidate < 0 && !b->candidate_count);
        }
        assert(descriptors() == baseline);
    }

    /* Exercise every query byte boundary over the real stream transport. */
    AttachRequest query;
    memset(&query, 0, sizeof query); memcpy(query.magic, "VIPIRCQ2", 8);
    for (size_t split = 1; split < sizeof query; ++split) {
        int peer = candidate(b);
        packet(peer, &query, split, -1, 0);
        assert(!receive_candidate(b) && b->candidate >= 0 && b->candidate_used == split);
        packet(peer, (char *)&query + split, sizeof query - split, -1, 0);
        assert(!receive_candidate(b) && b->candidate < 0);
        SessionReply reply;
        assert(recv(peer, &reply, sizeof reply, 0) == (ssize_t)sizeof reply);
        assert(!memcmp(reply.magic, "VIPIRCS2", 8) && reply.job == b->job &&
               !memcmp(reply.token, b->token, sizeof reply.token));
        assert(!close(peer) && descriptors() == baseline);
    }
    for (size_t count = 1; count <= 3; ++count) {
        int peer = candidate(b);
        packet(peer, &query, 1, quiet, count); assert(!receive_candidate(b));
        packet(peer, (char *)&query + 1, sizeof query - 1, -1, 0);
        assert(!receive_candidate(b)); rejected(b, peer);
        assert(descriptors() == baseline);
    }
#ifdef SO_PASSCRED
    {
        int peer = candidate(b), enabled = 1;
        assert(!setsockopt(b->candidate, SOL_SOCKET, SO_PASSCRED, &enabled, sizeof enabled));
        packet(peer, &request, sizeof request, quiet, 3);
        assert(!receive_candidate(b)); rejected(b, peer);
        assert(descriptors() == baseline);
    }
#endif

    /* Real tty descriptors exercise the successful fragmented attachment.
     * Save/restore stdio around production dup2; no frontend is started. */
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    assert(master >= 0 && master < 1024 && !grantpt(master) && !unlockpt(master));
    char *name = ptsname(master); assert(name);
    int terminal = open(name, O_RDWR | O_NOCTTY | O_CLOEXEC); assert(terminal >= 0 && terminal < 1024);
    baseline = descriptors();
    request.token[0] ^= 1;
    int peer = candidate(b);
    packet(peer, &request, sizeof request, terminal, 3);
    assert(!receive_candidate(b)); rejected(b, peer); assert(descriptors() == baseline);
    request.token[0] ^= 1;
    int saved[3];
    for (int i = 0; i < 3; ++i) {
        saved[i] = fcntl(i, F_DUPFD_CLOEXEC, 16); assert(saved[i] >= 0 && saved[i] < 1024);
    }
    peer = candidate(b);
    packet(peer, &request, 1, terminal, 1);
    assert(!receive_candidate(b) && b->candidate_count == 1);
    packet(peer, (char *)&request + 1, sizeof request - 1, terminal, 2);
    unsigned event = receive_candidate(b);
    for (int i = 0; i < 3; ++i) { assert(dup2(saved[i], i) == i); assert(!close(saved[i])); }
    assert(event == BACKGROUND_ATTACH && b->controller >= 0 && !b->detached && !b->candidate_count);
    unsigned char reply[2];
    assert(recv(peer, reply, sizeof reply, 0) == 2 && reply[0] == 'A' && !reply[1]);
    background_finish(b, 0);
    assert(recv(peer, reply, sizeof reply, 0) == 2 && reply[0] == 'Q' && !reply[1]);
    assert(!close(peer) && descriptors() == baseline);
    assert(!close(terminal) && !close(master) && !close(quiet)); background_free(b);
    assert(descriptors() == initial);
    printf("native socketpair/SCM_RIGHTS: 41 rights-count cases, 6 partial EOF/timeout cleanups, %zu query splits, control-rights rejection, peer ownership, token rejection and real-TTY attachment passed; no descriptor leaks\n", sizeof query - 1);
    puts("Scope: pathname AF_UNIX bind/listen/connect and full frontend resume are not executed by this test.");
    return 0;
}
