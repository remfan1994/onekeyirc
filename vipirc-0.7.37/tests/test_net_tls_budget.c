/* Real, verified loopback TLS with controlled records and socket backpressure.
 * net.c is compiled with SSL_new/read/write/get_error renamed to the small
 * observation wrappers below. Only the explicit want-read case injects an
 * outcome; all other TLS operations and retry errors are from the library. */
#define _POSIX_C_SOURCE 200809L
#include "net.h"
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

static const char ping[] = "PING :control-budget\r\n";
static const char pong[] = "PONG :control-budget\r\n";
static const char *scenario = "TLS fixture";
static unsigned updates, ssl_read_calls;
static int inject_write_error, injected_error;
static SSL *observed_ssl;
static pid_t server_pid;

static void fail(const char *label)
{
    fprintf(stderr, "%s: %s (errno=%d)\n", scenario, label, errno);
    ERR_print_errors_fp(stderr);
    if (server_pid > 0) {
        (void)kill(server_pid, SIGKILL);
        while (waitpid(server_pid, NULL, 0) < 0 && errno == EINTR) { }
    }
    exit(2);
}
static void require(int condition, const char *label)
{
    if (!condition) fail(label);
}
static int64_t monotonic_us(void)
{
    struct timespec now;
    require(clock_gettime(CLOCK_MONOTONIC, &now) == 0, "monotonic clock");
    return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}
static void message(int sending, int version, int content, const void *data,
                    size_t size, SSL *ssl, void *arg)
{
    (void)version; (void)ssl; (void)arg;
    if (!sending && content == SSL3_RT_HANDSHAKE && size &&
        ((const unsigned char *)data)[0] == SSL3_MT_KEY_UPDATE) ++updates;
}
SSL *tls_budget_SSL_new(SSL_CTX *ctx)
{
    SSL *ssl = SSL_new(ctx);
    observed_ssl = ssl;
    if (ssl) SSL_set_msg_callback(ssl, message);
    return ssl;
}
int tls_budget_SSL_read(SSL *ssl, void *bytes, int size)
{
    ++ssl_read_calls;
    return SSL_read(ssl, bytes, size);
}
int tls_budget_SSL_write(SSL *ssl, const void *bytes, int size)
{
    if (inject_write_error) {
        injected_error = inject_write_error;
        inject_write_error = 0;
        return -1;
    }
    return SSL_write(ssl, bytes, size);
}
int tls_budget_SSL_get_error(const SSL *ssl, int result)
{
    if (injected_error) {
        int error = injected_error;
        injected_error = 0;
        return error;
    }
    return SSL_get_error(ssl, result);
}
static void send_all(int fd, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    while (size) {
        ssize_t n = write(fd, bytes, size);
        if (n < 0 && errno == EINTR) continue;
        require(n > 0, "write all"); bytes += n; size -= (size_t)n;
    }
}
static void read_all(int fd, void *data, size_t size)
{
    unsigned char *bytes = data;
    while (size) {
        ssize_t n = read(fd, bytes, size);
        if (n < 0 && errno == EINTR) continue;
        require(n > 0, "read all"); bytes += n; size -= (size_t)n;
    }
}
static void signal_byte(int fd, char value) { send_all(fd, &value, 1); }
static void expect_byte(int fd, char value)
{
    char got;
    read_all(fd, &got, 1); require(got == value, "control marker");
}
static void ssl_send(SSL *ssl, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    while (size) {
        int n = SSL_write(ssl, bytes, (int)size);
        require(n > 0, "server TLS write"); bytes += n; size -= (size_t)n;
    }
}
static void ssl_expect(SSL *ssl, const char *expected, size_t size)
{
    char bytes[1024];
    require(size <= sizeof bytes, "server expected length");
    size_t got = 0;
    while (got < size) {
        int n = SSL_read(ssl, bytes + got, (int)(size - got));
        require(n > 0, "server TLS read"); got += (size_t)n;
    }
    require(!memcmp(bytes, expected, size), "server received exact reply");
}
static void key_update(SSL *ssl, int requested)
{
    require(SSL_key_update(ssl, requested) == 1, "server key update");
    require(SSL_do_handshake(ssl) == 1, "server key update flush");
}
static void server_body(int listener, int control, int status,
                         const char *cert, const char *key, unsigned offered)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    SSL *ssl;
    int sock = accept(listener, NULL, NULL), large = 4 * 1024 * 1024;
    unsigned char bytes[65536];
    alarm(25);
    require(sock >= 0, "accept"); close(listener);
    require(setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &large, sizeof large) == 0, "server send buffer");
    int version = strstr(scenario, "12") ? TLS1_2_VERSION : TLS1_3_VERSION;
    require(ctx != NULL, "server context");
    require(SSL_CTX_set_min_proto_version(ctx, version) == 1, "server TLS minimum");
    require(SSL_CTX_set_max_proto_version(ctx, version) == 1, "server TLS maximum");
    require(SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM) == 1, "server certificate");
    require(SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) == 1, "server private key");
    require(SSL_CTX_set_num_tickets(ctx, 0) == 1, "server ticket count");
    ssl = SSL_new(ctx); require(ssl != NULL, "server SSL");
    require(SSL_set_fd(ssl, sock) == 1, "server socket BIO");
    require(SSL_accept(ssl) == 1, "verified TLS server handshake");
    signal_byte(status, 'R'); expect_byte(control, 'S');
    if (!strcmp(scenario, "budget")) {
        for (unsigned i = 0; i < offered; ++i) key_update(ssl, SSL_KEY_UPDATE_NOT_REQUESTED);
        ssl_send(ssl, ping, sizeof ping - 1);
        signal_byte(status, 'K');
        ssl_expect(ssl, pong, sizeof pong - 1);
        signal_byte(status, 'A');
    } else if (!strcmp(scenario, "coalesced")) {
        BIO *output = BIO_new(BIO_s_mem()); char *wire;
        require(output != NULL, "memory output"); SSL_set0_wbio(ssl, output);
        key_update(ssl, SSL_KEY_UPDATE_NOT_REQUESTED);
        ssl_send(ssl, ping, sizeof ping - 1);
        long length = BIO_get_mem_data(output, &wire);
        require(length > (long)sizeof ping, "coalesced TLS records");
        send_all(sock, wire, (size_t)length); signal_byte(status, 'K');
        ssl_expect(ssl, pong, sizeof pong - 1); signal_byte(status, 'A');
    } else if (!strncmp(scenario, "fragment", 8) || !strcmp(scenario, "truncated") || !strcmp(scenario, "corrupt")) {
        BIO *output = BIO_new(BIO_s_mem()); char *wire;
        require(output != NULL, "memory output"); SSL_set0_wbio(ssl, output);
        for (size_t i = 0; i < 8192; ++i) bytes[i] = (unsigned char)(i % 251u);
        ssl_send(ssl, bytes, 8192);
        long length = BIO_get_mem_data(output, &wire);
        require(length > 8192, "encrypted record length");
        if (!strcmp(scenario, "corrupt")) wire[length - 1] ^= 1;
        send_all(sock, wire, 7); signal_byte(status, 'F'); expect_byte(control, 'D');
        if (!strcmp(scenario, "truncated")) {
            SSL_free(ssl); SSL_CTX_free(ctx); close(sock); close(control); close(status); _exit(0);
        }
        send_all(sock, wire + 7, (size_t)length - 7); signal_byte(status, 'W');
    } else {
        int small = 4096;
        require(setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &small, sizeof small) == 0, "server receive buffer");
        memset(bytes, 'I', 8192); ssl_send(ssl, bytes, 8192); signal_byte(status, 'I');
        uint64_t expected, received = 0;
        read_all(control, &expected, sizeof expected);
        if (version == TLS1_3_VERSION) key_update(ssl, SSL_KEY_UPDATE_REQUESTED);
        signal_byte(status, 'K'); expect_byte(control, 'D');
        if (!strcmp(scenario, "reset-write")) {
            struct linger reset = {1, 0};
            require(setsockopt(sock, SOL_SOCKET, SO_LINGER, &reset, sizeof reset) == 0, "reset linger");
            SSL_free(ssl); SSL_CTX_free(ctx); close(sock); close(control); close(status); _exit(0);
        }
        while (received < expected) {
            int n = SSL_read(ssl, bytes, sizeof bytes);
            require(n > 0, "server receives stalled stream");
            for (int i = 0; i < n; ++i)
                require(bytes[i] == (unsigned char)((received + (uint64_t)i) % 251u), "byte-exact stalled stream");
            received += (uint64_t)n;
        }
        require(received == expected, "no duplicated stalled bytes");
        ssl_send(ssl, "OK", 2);
    }
    expect_byte(control, 'Q');
    SSL_free(ssl); SSL_CTX_free(ctx); close(sock); close(control); close(status); _exit(0);
}
typedef struct Fixture { Net *net; int control, status; } Fixture;
static Fixture start(const char *cert, const char *key, unsigned offered)
{
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), control[2], status[2];
    struct sockaddr_in addr;
    socklen_t length = sizeof addr;
    char error[512];
    Fixture f;
    require(listener >= 0, "listener");
    memset(&addr, 0, sizeof addr); addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    require(bind(listener, (struct sockaddr *)&addr, sizeof addr) == 0, "loopback bind");
    require(getsockname(listener, (struct sockaddr *)&addr, &length) == 0, "listener address");
    require(listen(listener, 1) == 0, "listen");
    require(pipe(control) == 0 && pipe(status) == 0, "control pipes");
    server_pid = fork(); require(server_pid >= 0, "server fork");
    if (!server_pid) { close(control[1]); close(status[0]); server_body(listener, control[0], status[1], cert, key, offered); }
    close(listener); close(control[0]); close(status[1]);
    f.control = control[1]; f.status = status[0];
    f.net = net_open("127.0.0.1", ntohs(addr.sin_port), 1, cert, error, sizeof error);
    require(f.net != NULL, error);
    int64_t deadline = net_now_ms() + 8000;
    while (net_state(f.net) != NET_READY && net_state(f.net) != NET_FAILED && net_now_ms() < deadline) {
        struct pollfd p = {net_fd(f.net), net_events(f.net, 0), 0};
        int n = poll(&p, 1, 50);
        if (n < 0 && errno == EINTR) continue;
        require(n >= 0, "setup poll"); (void)net_step(f.net, p.revents, net_now_ms());
    }
    require(net_state(f.net) == NET_READY, net_error(f.net));
    require(SSL_get_verify_result(observed_ssl) == X509_V_OK, "certificate remains verified");
    expect_byte(f.status, 'R'); signal_byte(f.control, 'S'); return f;
}
static short ready(Net *net, int write_pending, int timeout)
{
    struct pollfd p = {net_fd(net), net_events(net, write_pending), 0};
    int n;
    do { n = poll(&p, 1, net_pending(net) ? 0 : timeout); } while (n < 0 && errno == EINTR);
    require(n >= 0, "transport poll");
    require(net_step(net, p.revents, net_now_ms()) == 0, net_error(net));
    return p.revents;
}
static void reply(Fixture *f)
{
    size_t sent = 0;
    int64_t deadline = net_now_ms() + 5000;
    while (sent < sizeof pong - 1 && net_now_ms() < deadline) {
        (void)ready(f->net, 1, 50);
        ssize_t n = net_write(f->net, pong + sent, sizeof pong - 1 - sent);
        if (n == -2) continue;
        require(n > 0, net_error(f->net)); sent += (size_t)n;
    }
    require(sent == sizeof pong - 1, "PONG reply progress"); expect_byte(f->status, 'A');
}
static int budget(Fixture *f, unsigned offered)
{
    char bytes[sizeof ping]; size_t received = 0;
    unsigned calls = 0, maximum_updates = 0, first_updates = 0, timers = 0;
    int64_t first_us = 0, max_us = 0, maximum_timer_late_us = 0;
    expect_byte(f->status, 'K');
    int64_t started = monotonic_us(), next_timer = started + 10000, deadline = started + 15000000;
    while (received < sizeof ping - 1 && monotonic_us() < deadline) {
        short events = ready(f->net, 0, 10);
        if (!events && !net_pending(f->net)) continue;
        unsigned before_updates = updates;
        int64_t before = monotonic_us();
        ssize_t n = net_read(f->net, bytes + received, sizeof ping - 1 - received);
        int64_t now = monotonic_us(), duration = now - before;
        unsigned processed = updates - before_updates;
        if (!calls) { first_us = duration; first_updates = processed; }
        ++calls; if (processed > maximum_updates) maximum_updates = processed;
        if (duration > max_us) max_us = duration;
        if (now >= next_timer) {
            if (now - next_timer > maximum_timer_late_us) maximum_timer_late_us = now - next_timer;
            ++timers; next_timer = now + 10000;
        }
        if (n == -2) continue;
        require(n > 0, net_error(f->net)); received += (size_t)n;
    }
    require(received == sizeof ping - 1 && !memcmp(bytes, ping, sizeof ping - 1), "exact final PING delivered");
    require(updates == offered, "all offered control records processed"); reply(f);
    printf("{\"case\":\"budget\",\"offered\":%u,\"read_calls\":%u,\"first_updates\":%u,\"max_updates_per_read\":%u,\"first_read_us\":%lld,\"max_read_us\":%lld,\"timer_callbacks\":%u,\"max_timer_late_us\":%lld,\"ping_pong\":true}\n",
           offered, calls, first_updates, maximum_updates, (long long)first_us, (long long)max_us, timers, (long long)maximum_timer_late_us);
    return maximum_updates <= 1 ? 0 : 1;
}
static int coalesced(Fixture *f)
{
    char bytes[sizeof ping];
    expect_byte(f->status, 'K'); (void)ready(f->net, 0, 1000);
    ssize_t first = net_read(f->net, bytes, sizeof bytes);
    int decoded = net_pending(f->net), buffered = SSL_has_pending(observed_ssl);
    struct pollfd p = {net_fd(f->net), POLLIN, 0};
    require(poll(&p, 1, 0) >= 0, "coalesced poll");
    int bounded = first == -2 && updates == 1 && decoded == 0 && (p.revents & POLLIN);
    ssize_t n = first > 0 ? first : net_read(f->net, bytes, sizeof bytes);
    require(n == (ssize_t)(sizeof ping - 1) && !memcmp(bytes, ping, sizeof ping - 1), "coalesced PING progress"); reply(f);
    printf("{\"case\":\"coalesced\",\"first_read\":%zd,\"decoded_after_yield\":%d,\"has_pending_after_yield\":%d,\"socket_still_readable\":%s,\"ping_pong\":true}\n",
           first, decoded, buffered, p.revents & POLLIN ? "true" : "false");
    return bounded ? 0 : 1;
}
static int fragment(Fixture *f)
{
    unsigned char bytes[8192]; size_t got = 0;
    expect_byte(f->status, 'F'); (void)ready(f->net, 0, 1000);
    require(net_read(f->net, bytes, 73) == -2, "partial TLS record is not application data");
    require(net_pending(f->net) == 0, "partial ciphertext cannot force zero timeout");
    int buffered = SSL_has_pending(observed_ssl);
    int64_t before = monotonic_us();
    require(ready(f->net, 0, 80) == 0, "partial record waits for network data");
    int64_t waited = monotonic_us() - before;
    require(waited >= 50000, "no partial-record busy poll");
    signal_byte(f->control, 'D');
    if (strcmp(scenario, "truncated")) expect_byte(f->status, 'W');
    if (!strcmp(scenario, "truncated") || !strcmp(scenario, "corrupt")) {
        ssize_t n = -2;
        int64_t deadline = net_now_ms() + 5000;
        while (n == -2 && net_now_ms() < deadline) { (void)ready(f->net, 0, 100); n = net_read(f->net, bytes, sizeof bytes); }
        int retry = !strcmp(scenario, "truncated");
        require(n == -1 && net_state(f->net) == NET_FAILED && net_retryable(f->net) == retry,
                "truncated/corrupt TLS record fails with correct retry policy");
        printf("{\"case\":\"%s\",\"partial_has_pending\":%d,\"idle_wait_us\":%lld,\"returned_application_bytes\":0,\"retryable\":%s}\n", scenario, buffered, (long long)waited, retry ? "true" : "false");
        return 0;
    }
    int saw_buffer_without_readiness = 0;
    int64_t deadline = net_now_ms() + 5000;
    while (got < sizeof bytes && net_now_ms() < deadline) {
        short events = ready(f->net, 0, 100);
        if (net_pending(f->net) && !events) saw_buffer_without_readiness = 1;
        ssize_t n = net_read(f->net, bytes + got, sizeof bytes - got < 73 ? sizeof bytes - got : 73);
        if (n == -2) continue;
        require(n > 0, net_error(f->net)); got += (size_t)n;
    }
    require(got == sizeof bytes, "all fragmented plaintext delivered");
    for (size_t i = 0; i < got; ++i) require(bytes[i] == (unsigned char)(i % 251u), "fragmented plaintext integrity");
    require(saw_buffer_without_readiness && net_pending(f->net) == 0, "decoded TLS bytes drain without fresh socket event");
    printf("{\"case\":\"%s\",\"partial_has_pending\":%d,\"idle_wait_us\":%lld,\"decoded_drain_without_socket_event\":true,\"bytes\":%zu}\n", scenario, buffered, (long long)waited, got);
    return 0;
}
static int write_priority(Fixture *f)
{
    unsigned char bytes[65536], incoming[8194];
    size_t sent = 0, incoming_size = 0, pending_done = 0;
    ssize_t n;
    expect_byte(f->status, 'I'); (void)ready(f->net, 0, 1000);
    n = net_read(f->net, incoming, 1);
    require(n == 1 && incoming[0] == 'I', "prime decoded read buffer"); incoming_size = 1;
    require(net_pending(f->net) == 8191, "decoded bytes initially readable");
    int injected = !strcmp(scenario, "want-read");
    if (injected) {
        for (size_t i = 0; i < sizeof bytes; ++i) bytes[i] = (unsigned char)(i % 251u);
        inject_write_error = SSL_ERROR_WANT_READ;
        require(net_write(f->net, bytes, sizeof bytes) == -2, "injected write needs read");
    } else {
        int small = 4096;
        require(setsockopt(net_fd(f->net), SOL_SOCKET, SO_SNDBUF, &small, sizeof small) == 0, "client send buffer");
        for (unsigned i = 0; i < 1024; ++i) {
            for (size_t j = 0; j < sizeof bytes; ++j) bytes[j] = (unsigned char)((sent + j) % 251u);
            n = net_write(f->net, bytes, sizeof bytes);
            if (n == -2) break;
            require(n > 0, net_error(f->net)); sent += (size_t)n;
        }
        require(n == -2, "real TLS write backpressure reached");
    }
    unsigned before_calls = ssl_read_calls;
    int eligible = net_pending(f->net), internal = SSL_pending(observed_ssl);
    short events = net_events(f->net, 1);
    n = net_read(f->net, incoming + incoming_size, 1);
    int precedence = injected ? n == 1 && ssl_read_calls == before_calls + 1 && eligible == internal && (events & POLLIN) :
                               n == -2 && ssl_read_calls == before_calls && eligible == 0 && events == POLLOUT;
    if (n > 0) incoming_size += (size_t)n;
    uint64_t expected = sent + sizeof bytes;
    send_all(f->control, &expected, sizeof expected); expect_byte(f->status, 'K'); signal_byte(f->control, 'D');
    int64_t deadline = net_now_ms() + 8000;
    while (pending_done < sizeof bytes && net_now_ms() < deadline) {
        (void)ready(f->net, 1, 50);
        n = net_write(f->net, bytes + pending_done, sizeof bytes - pending_done);
        if (n == -2) continue;
        if (!strcmp(scenario, "reset-write")) break;
        require(n > 0, net_error(f->net)); pending_done += (size_t)n;
    }
    if (!strcmp(scenario, "reset-write")) {
        require(n == -1 && net_state(f->net) == NET_FAILED && net_retryable(f->net), "blocked write reset terminates with transient classification");
        printf("{\"case\":\"reset-write\",\"precedence_correct\":%s,\"failed_boundedly\":true,\"retryable\":true}\n", precedence ? "true" : "false");
        return precedence ? 0 : 1;
    }
    require(pending_done == sizeof bytes, "stalled write completes with same retry bytes");
    while (incoming_size < sizeof incoming && net_now_ms() < deadline) {
        short revents = ready(f->net, 0, 50);
        if (!revents && !net_pending(f->net)) continue;
        n = net_read(f->net, incoming + incoming_size, sizeof incoming - incoming_size);
        if (n == -2) continue;
        require(n > 0, net_error(f->net)); incoming_size += (size_t)n;
    }
    require(incoming_size == sizeof incoming, "deferred input and acknowledgement delivered");
    for (size_t i = 0; i < 8192; ++i) require(incoming[i] == 'I', "deferred input integrity");
    require(!memcmp(incoming + 8192, "OK", 2), "stream acknowledgement");
    printf("{\"case\":\"%s\",\"injected_want_read\":%s,\"precedence_correct\":%s,\"pending_internal\":%d,\"pending_eligible\":%d,\"poll_events\":%d,\"sent\":%llu,\"received\":%zu}\n",
           scenario, injected ? "true" : "false", precedence ? "true" : "false", internal, eligible, (int)events, (unsigned long long)expected, incoming_size);
    return precedence ? 0 : 1;
}
int main(int argc, char **argv)
{
    require(argc == 5, "arguments: certificate key scenario updates");
    scenario = argv[3];
    unsigned offered = (unsigned)strtoul(argv[4], NULL, 10);
    require(offered > 0 && offered <= 100000, "bounded fixture update count");
    signal(SIGPIPE, SIG_IGN); alarm(30);
    Fixture f = start(argv[1], argv[2], offered);
    int result;
    if (!strcmp(scenario, "budget")) result = budget(&f, offered);
    else if (!strcmp(scenario, "coalesced")) result = coalesced(&f);
    else if (!strncmp(scenario, "fragment", 8) || !strcmp(scenario, "truncated") || !strcmp(scenario, "corrupt")) result = fragment(&f);
    else result = write_priority(&f);
    if (strcmp(scenario, "truncated") && strcmp(scenario, "reset-write")) signal_byte(f.control, 'Q');
    net_close(f.net); close(f.control); close(f.status);
    int status; pid_t found;
    do { found = waitpid(server_pid, &status, 0); } while (found < 0 && errno == EINTR);
    require(found == server_pid, "server collection"); server_pid = 0;
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "server successful exit");
    return result;
}
