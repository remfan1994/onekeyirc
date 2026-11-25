#define _POSIX_C_SOURCE 200809L
#include "net.h"

#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static int fd_count(void)
{
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) directory = opendir("/dev/fd");
    if (!directory) return -1;
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)))
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) ++count;
    closedir(directory);
    return count;
}

static int step(Net *net, int timeout)
{
    struct pollfd ready = {net_fd(net), net_events(net, 0), 0};
    int result = poll(&ready, 1, timeout);
    if (result < 0 && errno != EINTR) return 0;
    (void)net_step(net, ready.revents, net_now_ms());
    return 1;
}

static int ready(Net *net)
{
    int64_t end = net_now_ms() + 3000;
    while (net_state(net) != NET_READY && net_state(net) != NET_FAILED && net_now_ms() < end)
        if (!step(net, 10)) return 0;
    return net_state(net) == NET_READY;
}

static int clean(int before)
{
    int status;
    if (waitpid(-1, &status, WNOHANG) != -1 || errno != ECHILD) {
        fputs("resolver child still exists after close\n", stderr); return 0;
    }
    int after = fd_count();
    if (before >= 0 && before != after) {
        fprintf(stderr, "descriptor count changed: %d -> %d\n", before, after); return 0;
    }
    return 1;
}

static int cycle(Net *net, unsigned index)
{
    if (index % 3 == 0) return 1; /* Cancel immediately after fork. */
    if (index % 3 == 1)
        return net_step(net, 0, net_now_ms() + 31000) < 0 && net_state(net) == NET_FAILED;
    int64_t end = net_now_ms() + 2000;
    while (net_state(net) != NET_FAILED && net_now_ms() < end)
        if (!step(net, 10)) return 0;
    return net_state(net) == NET_FAILED;
}

static int stalled(Net *net, int real_deadline, unsigned *polls)
{
    int64_t began = net_now_ms(), end = began + (real_deadline ? 33000 : 120);
    int saw_handshake = 0;
    while (net_state(net) != NET_FAILED && net_now_ms() < end) {
        if (!step(net, 20)) return 0;
        ++*polls;
        if (net_state(net) == NET_HANDSHAKE) saw_handshake = 1;
        if (net_state(net) == NET_READY) return 0;
        if (*polls > (real_deadline ? 3000u : 200u)) {
            fputs("stalled handshake spun on readiness\n", stderr); return 0;
        }
    }
    int64_t elapsed = net_now_ms() - began;
    if (!real_deadline) (void)net_step(net, 0, net_now_ms() + 31000);
    return saw_handshake && net_state(net) == NET_FAILED &&
           strstr(net_error(net), "timed out") && (!real_deadline || (elapsed >= 29000 && elapsed < 33000));
}

static int abrupt(Net *net)
{
    if (!ready(net)) return net_state(net) == NET_FAILED;
    char data[4096];
    int64_t end = net_now_ms() + 2000;
    while (net_state(net) != NET_FAILED && net_now_ms() < end) {
        if (!step(net, 10)) return 0;
        ssize_t got = net_read(net, data, sizeof(data));
        if (got == 0) {
            fputs("TLS truncation was incorrectly reported as clean EOF\n", stderr); return 0;
        }
    }
    return net_state(net) == NET_FAILED;
}

static int bulk(Net *net, unsigned *would_block)
{
    if (!ready(net)) return 0;
    int size = 16384;
    if (setsockopt(net_fd(net), SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) < 0) return 0;
    unsigned char data[16384];
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = (unsigned char)(i & 255u);
    const size_t total = 1024u * 1024u;
    size_t sent = 0, received = 0;
    char reply[2];
    int64_t end = net_now_ms() + 12000;
    while (net_state(net) == NET_READY && net_now_ms() < end) {
        struct pollfd event = {net_fd(net), net_events(net, sent < total), 0};
        int polled = poll(&event, 1, net_pending(net) ? 0 : 10);
        if (polled < 0 && errno != EINTR) return 0;
        (void)net_step(net, event.revents, net_now_ms());
        if (sent < total) {
            size_t offset = sent % sizeof(data);
            ssize_t wrote = net_write(net, data + offset, sizeof(data) - offset);
            if (wrote > 0) sent += (size_t)wrote;
            else if (wrote == -2) ++*would_block;
            else return 0;
        }
        ssize_t got = net_read(net, reply + received, sizeof(reply) - received);
        if (got > 0) received += (size_t)got;
        else if (got != -2) return 0;
        if (received == sizeof(reply))
            return sent == total && !memcmp(reply, "OK", sizeof(reply)) && *would_block > 0;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 6) return 2;
    const char *mode = argv[1];
    int rounds = atoi(argv[5]);
    if (rounds < 1 || rounds > 1000) return 2;
    int tls = strcmp(mode, "cycles") && strcmp(mode, "bulk-plain");
    signal(SIGPIPE, SIG_IGN);
    int before = fd_count();
    int64_t began = net_now_ms();
    unsigned polls = 0, would_block = 0;
    for (int i = 0; i < rounds; ++i) {
        char error[256];
        Net *net = net_open(argv[2], atoi(argv[3]), tls, argv[4], error, sizeof(error));
        if (!net) { fprintf(stderr, "%s\n", error); return 3; }
        unsigned round_polls = 0;
        int ok = !strcmp(mode, "cycles") ? cycle(net, (unsigned)i) :
                 !strcmp(mode, "stall") ? stalled(net, 0, &round_polls) :
                 !strcmp(mode, "deadline") ? stalled(net, 1, &round_polls) :
                 !strcmp(mode, "abrupt") ? abrupt(net) : bulk(net, &would_block);
        polls += round_polls;
        if (!ok) fprintf(stderr, "mode=%s round=%d state=%d error=%s\n", mode, i, (int)net_state(net), net_error(net));
        net_close(net);
        if (!ok || !clean(before)) return 4;
    }
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) < 0) return 5;
    printf("mode=%s rounds=%d elapsed_ms=%lld fd_start=%d fd_end=%d polls=%u would_block=%u maxrss=%ld\n",
           mode, rounds, (long long)(net_now_ms() - began), before, fd_count(), polls, would_block, usage.ru_maxrss);
    return 0;
}
