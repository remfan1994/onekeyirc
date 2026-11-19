#!/usr/bin/env python3
"""Local-only tests for typed transport reconnect decisions; no actual DNS."""
import argparse
import os
from pathlib import Path
import shlex
import socket
import ssl
import struct
import subprocess
import tempfile
import threading

COMPILER = shlex.split(os.environ.get("CC", "cc"))
BUILD_FLAGS = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror"] + \
    shlex.split(os.environ.get("CPPFLAGS", "")) + shlex.split(os.environ.get("CFLAGS", ""))
LINK_FLAGS = shlex.split(os.environ.get("LDFLAGS", ""))

PROBE = r'''
#define _POSIX_C_SOURCE 200809L
#include "net.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int unavailable_ipv6;

/* net.c alone is compiled with socket renamed to this fixture. The real IPv4
 * sockets are unchanged; controlled hosts can model a disabled IPv6 family. */
int retry_test_socket(int family, int type, int protocol)
{
    if (unavailable_ipv6 && family == AF_INET6) { errno = EAFNOSUPPORT; return -1; }
    return socket(family, type, protocol);
}

/* A real fork/pipe resolver carrying controlled getaddrinfo results. */
int getaddrinfo(const char *host, const char *service,
                const struct addrinfo *hints, struct addrinfo **result)
{
    (void)hints;
    *result = NULL;
    if (!strcmp(host, "again.invalid")) return EAI_AGAIN;
    if (!strcmp(host, "noname.invalid")) return EAI_NONAME;
    if (!strcmp(host, "failure.invalid")) return EAI_FAIL;
    if (!strcmp(host, "empty.invalid")) return 0;
    if (!strcmp(host, "system-network.invalid")) { errno = ENETUNREACH; return EAI_SYSTEM; }
    if (!strcmp(host, "system-interrupted.invalid")) { errno = EINTR; return EAI_SYSTEM; }
    if (!strcmp(host, "system-permission.invalid")) { errno = EACCES; return EAI_SYSTEM; }
    if (!strcmp(host, "system-invalid.invalid")) { errno = EINVAL; return EAI_SYSTEM; }
    int mixed = !strcmp(host, "mixed-address.invalid");
    int unsupported = !strcmp(host, "unsupported-only.invalid");
    int mixed_tls = !strcmp(host, "mixed-tls.invalid");
    if (strcmp(host, "127.0.0.1") && !mixed && !unsupported && !mixed_tls) return EAI_NONAME;
    struct addrinfo *item = calloc(1, sizeof *item);
    struct sockaddr_in *address = calloc(1, sizeof *address);
    if (!item || !address) { free(item); free(address); return EAI_MEMORY; }
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address->sin_port = htons((uint16_t)atoi(service));
    item->ai_family = AF_INET;
    item->ai_socktype = SOCK_STREAM;
    item->ai_protocol = IPPROTO_TCP;
    item->ai_addr = (struct sockaddr *)address;
    item->ai_addrlen = sizeof *address;
    if (mixed || unsupported || mixed_tls) {
        struct addrinfo *other = calloc(1, sizeof *other);
        struct sockaddr_storage *storage = calloc(1, sizeof *storage);
        if (!other || !storage) { free(other); free(storage); free(address); free(item); return EAI_MEMORY; }
        other->ai_socktype = SOCK_STREAM;
        other->ai_protocol = IPPROTO_TCP;
        other->ai_addr = (struct sockaddr *)storage;
        if (mixed_tls) {
            const char *first_port = getenv("TEST_FIRST_PORT");
            if (!first_port) { free(other); free(storage); free(address); free(item); return EAI_FAIL; }
            struct sockaddr_in *second = (struct sockaddr_in *)storage;
            *second = *address;
            address->sin_port = htons((uint16_t)atoi(first_port));
            other->ai_family = AF_INET;
            other->ai_addrlen = sizeof *second;
        } else {
            struct sockaddr_in6 *second = (struct sockaddr_in6 *)storage;
            second->sin6_family = AF_INET6;
            second->sin6_port = htons((uint16_t)atoi(service));
            second->sin6_addr = in6addr_loopback;
            other->ai_family = AF_INET6;
            other->ai_addrlen = sizeof *second;
        }
        item->ai_next = other;
        if (unsupported) { free(address); free(item); item = other; }
    }
    *result = item;
    return 0;
}

void freeaddrinfo(struct addrinfo *item)
{
    while (item) { struct addrinfo *next = item->ai_next; free(item->ai_addr); free(item); item = next; }
}

int main(int argc, char **argv)
{
    if (argc != 8) return 2;
    const char *mode = argv[1];
    const int expected = atoi(argv[6]);
    const char *outcome = argv[7];
    signal(SIGPIPE, SIG_IGN);
    unavailable_ipv6 = !strcmp(argv[2], "mixed-address.invalid") || !strcmp(argv[2], "unsupported-only.invalid");
    if (net_retryable(NULL)) return 3;
    char error[256], data[16384];
    memset(data, 'a', sizeof data);
    Net *net = net_open(argv[2], atoi(argv[3]), atoi(argv[4]), argv[5], error, sizeof error);
    if (!net) {
        printf("NULL: %s\n", error);
        return !strcmp(outcome, "null") && !expected ? 0 : 4;
    }
    if (net_retryable(net)) { net_close(net); return 5; }
    if (!strcmp(mode, "deadline")) net_step(net, 0, net_now_ms() + 31000);
    else if (!strcmp(mode, "invalid")) net_step(net, POLLNVAL, net_now_ms());
    int64_t end = net_now_ms() + 5000;
    int eof = 0, invalidated = 0;
    while (net_state(net) != NET_FAILED && !eof && net_now_ms() < end) {
        struct pollfd p = {net_fd(net), net_events(net, !strcmp(mode, "write")), 0};
        if (poll(&p, 1, net_pending(net) ? 0 : 20) < 0) continue;
        net_step(net, p.revents, net_now_ms());
        if (net_state(net) != NET_READY) continue;
        if (!strcmp(mode, "ready")) break;
        if (!strcmp(mode, "bad-read") && !invalidated) { close(net_fd(net)); invalidated = 1; }
        ssize_t n = !strcmp(mode, "write") ? net_write(net, data, sizeof data) : net_read(net, data, sizeof data);
        if (!n) eof = 1;
    }
    int retry = net_retryable(net);
    const char *actual = net_state(net) == NET_FAILED ? "failed" : eof ? "eof" :
        net_state(net) == NET_READY && !strcmp(mode, "ready") ? "ready" : "timeout";
    printf("%s: retry=%d: %s\n", actual, retry, net_error(net));
    int ok = !strcmp(actual, outcome) && retry == expected;
    net_close(net);
    return ok ? 0 : 6;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path, nargs="?", default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    with tempfile.TemporaryDirectory(prefix="irc-retry-") as tmp:
        tmp = Path(tmp)
        source = tmp / "probe.c"
        source.write_text(PROBE, encoding="ascii")
        binary = tmp / "probe"
        net_object = tmp / "net.o"
        subprocess.run([
            *COMPILER, *BUILD_FLAGS,
            "-Dsocket=retry_test_socket", "-I", str(root / "src"), "-c", str(root / "src/net.c"),
            "-o", str(net_object),
        ], check=True)
        subprocess.run([
            *COMPILER, *BUILD_FLAGS,
            "-I", str(root / "src"), str(source), str(net_object), *LINK_FLAGS, "-lssl", "-lcrypto", "-o", str(binary),
        ], check=True)

        def run(label, mode="setup", host="127.0.0.1", port=9, tls=0, ca="", retry=0, outcome="failed", first_port=None):
            environment = os.environ.copy()
            environment.pop("TEST_FIRST_PORT", None)
            if first_port is not None:
                environment["TEST_FIRST_PORT"] = str(first_port)
            result = subprocess.run([str(binary), mode, host, str(port), str(tls), str(ca), str(retry), outcome],
                                    capture_output=True, text=True, timeout=7, env=environment)
            assert result.returncode == 0, (label, result.returncode, result.stdout, result.stderr)
            print(label + ": " + result.stdout.strip(), flush=True)

        for host, retry in [
            ("again.invalid", 1), ("noname.invalid", 0), ("failure.invalid", 0), ("empty.invalid", 0),
            ("system-network.invalid", 1), ("system-interrupted.invalid", 1),
            ("system-permission.invalid", 0), ("system-invalid.invalid", 0),
        ]:
            run("DNS " + host, host=host, retry=retry)
        run("setup timeout", mode="deadline", retry=1)
        run("invalid descriptor event", mode="invalid", retry=0)
        run("invalid host", host="bad host", outcome="null")
        run("missing trust file", tls=1, ca=tmp / "missing.pem", outcome="null")
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            port = reserved.getsockname()[1]
            run("refused connection", port=port, retry=1)
            run("refusal followed by unsupported IPv6 retries", host="mixed-address.invalid", port=port, retry=1)
            run("unsupported IPv6 alone stays held", host="unsupported-only.invalid", port=port, retry=0)

        def cert(name, san):
            key = tmp / (name + ".key")
            pem = tmp / (name + ".pem")
            subprocess.run([
                "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", str(key),
                "-out", str(pem), "-days", "1", "-subj", "/CN=" + name, "-addext", "subjectAltName=" + san,
            ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return pem, key

        good, good_key = cert("localhost", "IP:127.0.0.1")
        other, other_key = cert("unrelated", "DNS:unrelated.invalid")

        def endpoint(label, behavior, mode="read", tls=0, ca="", retry=1, outcome="failed", certificate=None,
                     host="127.0.0.1", first_port=None):
            listener = socket.socket()
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(6)
            port = listener.getsockname()[1]
            errors = []

            def serve():
                conn = None
                try:
                    conn, _ = listener.accept()
                    conn.settimeout(5)
                    if certificate:
                        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                        context.minimum_version = ssl.TLSVersion.TLSv1_2
                        context.load_cert_chain(*map(str, certificate))
                        conn = context.wrap_socket(conn, server_side=True)
                    if behavior == "handshake-eof":
                        conn.recv(4096)
                    elif behavior == "protocol":
                        conn.recv(4096)
                        conn.sendall(b"HTTP/1.0 200 OK\r\n\r\n")
                    elif behavior == "reset":
                        conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
                    elif behavior == "write-reset":
                        conn.recv(16384)
                        conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
                    elif behavior == "tls-eof":
                        fd = conn.detach()
                        os.close(fd)
                    elif behavior == "tls-protocol":
                        with socket.socket(fileno=conn.detach()) as raw:
                            raw.sendall(b"invalid TLS record after a verified handshake")
                    elif behavior == "tls-notify":
                        conn.unwrap()
                    elif behavior == "wait":
                        conn.recv(16384)
                except (ssl.SSLError, ConnectionError):
                    pass
                except BaseException as exc:
                    errors.append(exc)
                finally:
                    if conn is not None:
                        conn.close()
                    listener.close()

            thread = threading.Thread(target=serve)
            thread.start()
            try:
                run(label, mode=mode, host=host, port=port, tls=tls, ca=ca, retry=retry, outcome=outcome,
                    first_port=first_port)
            finally:
                thread.join(timeout=7)
                listener.close()
            assert not thread.is_alive(), label + " server leaked"
            assert not errors, (label, errors)

        endpoint("plain EOF separate from retry flag", "eof", retry=0, outcome="eof")
        endpoint("plain read reset", "reset")
        endpoint("plain write reset", "write-reset", mode="write")
        endpoint("bad read descriptor held", "wait", mode="bad-read", retry=0)
        endpoint("TLS handshake EOF", "handshake-eof", tls=1, ca=good)
        endpoint("TLS protocol violation held", "protocol", tls=1, ca=good, retry=0)
        endpoint("TLS untrusted certificate held", "wait", tls=1, ca=other, retry=0, certificate=(good, good_key))
        endpoint("TLS hostname mismatch held", "wait", tls=1, ca=other, retry=0, certificate=(other, other_key))
        endpoint("TLS unnotified EOF", "tls-eof", tls=1, ca=good, certificate=(good, good_key))
        endpoint("TLS read reset", "reset", tls=1, ca=good, certificate=(good, good_key))
        endpoint("TLS post-handshake protocol violation held", "tls-protocol", tls=1, ca=good,
                 retry=0, certificate=(good, good_key))
        endpoint("TLS write reset", "write-reset", mode="write", tls=1, ca=good, certificate=(good, good_key))
        endpoint("TLS bad descriptor held", "wait", mode="bad-read", tls=1, ca=good, retry=0, certificate=(good, good_key))
        endpoint("TLS clean EOF separate from retry flag", "tls-notify", tls=1, ca=good,
                 retry=0, outcome="eof", certificate=(good, good_key))
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            first_port = reserved.getsockname()[1]
            endpoint("earlier refusal does not override TLS certificate hold", "wait", tls=1, ca=other,
                     retry=0, certificate=(good, good_key), host="mixed-tls.invalid", first_port=first_port)
            endpoint("earlier refusal does not override TLS protocol hold", "protocol", tls=1, ca=good,
                     retry=0, host="mixed-tls.invalid", first_port=first_port)
        print("31 transport classification scenarios passed", flush=True)


if __name__ == "__main__":
    main()
