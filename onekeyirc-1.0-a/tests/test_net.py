#!/usr/bin/env python3
"""Local-only transport tests: TLS, byte integrity, resolver fd isolation."""
import argparse
import os
from pathlib import Path
import shlex
import socket
import ssl
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]
COMPILER = shlex.split(os.environ.get("CC", "cc"))
BUILD_FLAGS = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror"] + \
    shlex.split(os.environ.get("CPPFLAGS", "")) + shlex.split(os.environ.get("CFLAGS", ""))
LINK_FLAGS = shlex.split(os.environ.get("LDFLAGS", ""))

PROBE = r'''
#define _POSIX_C_SOURCE 200809L
#include "net.h"
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    Net *n;
    char error[256], data[12345], reply[12345];
    size_t sent = 0, got = 0;
    int64_t end;
    int expected;
    if (argc != 6) return 2;
    signal(SIGPIPE, SIG_IGN);
    expected = atoi(argv[5]);
    n = net_open(argv[1], atoi(argv[2]), atoi(argv[3]), argv[4], error, sizeof error);
    if (!n) {
        fprintf(stderr, "%s\n", error);
        return expected ? 3 : 0;
    }
    if (expected == 2) {
        int result = net_step(n, 0, net_now_ms() + 31000);
        net_close(n);
        return result < 0 ? 0 : 6;
    }
    for (size_t i = 0; i < sizeof data; ++i) data[i] = (char)('a' + i % 26);
    end = net_now_ms() + 8000;
    while (net_state(n) != NET_FAILED && net_now_ms() < end) {
        struct pollfd p = {net_fd(n), net_events(n, sent < sizeof data), 0};
        int result = poll(&p, 1, net_pending(n) ? 0 : 50);
        if (result < 0) continue;
        net_step(n, p.revents, net_now_ms());
        if (net_state(n) != NET_READY) continue;
        if (!expected) { fprintf(stderr, "unexpected READY\n"); net_close(n); return 4; }
        if (sent < sizeof data) {
            ssize_t wrote = net_write(n, data + sent, sizeof data - sent);
            if (wrote > 0) sent += (size_t)wrote;
            else if (wrote == -1 || wrote == 0) break;
        }
        if (got < sizeof reply) {
            ssize_t read = net_read(n, reply + got, sizeof reply - got);
            if (read > 0) got += (size_t)read;
            else if (read == -1 || read == 0) break;
        }
        if (got == sizeof reply) {
            int result = memcmp(data, reply, sizeof data) == 0 && sent == sizeof data;
            net_close(n);
            return result ? 0 : 5;
        }
    }
    fprintf(stderr, "%s\n", net_error(n));
    int failed = net_state(n) == NET_FAILED;
    net_close(n);
    return expected ? 7 : (failed ? 0 : 8);
}
'''


RESOLVER_PROBE = r'''
#define _POSIX_C_SOURCE 200809L
#include "net.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

struct Shared { volatile sig_atomic_t entered, bad_fd; };
static struct Shared *shared;
static int high_fd;

/* Block only after recording whether the child retained anything except its
 * result pipe. No real DNS, sleeps, files, or extra descriptors are involved. */
int getaddrinfo(const char *host, const char *service,
                const struct addrinfo *hints, struct addrinfo **result)
{
    (void)host; (void)service; (void)hints; *result = NULL;
    struct stat info;
    if (fstat(3, &info) < 0 || !S_ISFIFO(info.st_mode) ||
        !(fcntl(3, F_GETFD) & FD_CLOEXEC)) shared->bad_fd = -1;
    for (int fd = 0; fd < 32; ++fd)
        if (fd != 3 && (fcntl(fd, F_GETFD) >= 0 || errno != EBADF)) shared->bad_fd = fd + 1;
    if (fcntl(high_fd, F_GETFD) >= 0 || errno != EBADF) shared->bad_fd = high_fd + 1;
    shared->entered = 1;
    for (;;) pause();
}

/* Compile-time symbol substitution exercises old Linux kernels and a system
 * without an fd-table directory, without a production-only testing switch. */
long resolver_test_syscall(long number, ...)
{
    (void)number; errno = ENOSYS; return -1;
}

DIR *resolver_test_opendir(const char *path)
{
    (void)path; errno = ENOENT; return NULL;
}

static int eof_now(int fd)
{
    struct pollfd ready = {fd, POLLIN, 0};
    char byte;
    return poll(&ready, 1, 500) > 0 && read(fd, &byte, 1) == 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    int mode = atoi(argv[1]);
    signal(SIGPIPE, SIG_IGN);
    FILE *backing = tmpfile();
    if (!backing || ftruncate(fileno(backing), (off_t)sizeof *shared) < 0) return 3;
    shared = mmap(NULL, sizeof *shared, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(backing), 0);
    fclose(backing);
    if (shared == MAP_FAILED) return 4;
    shared->entered = shared->bad_fd = 0;

    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) < 0) return 5;
    rlim_t wanted = 4097;
    if (limit.rlim_max != RLIM_INFINITY && wanted > limit.rlim_max) wanted = limit.rlim_max;
    if (wanted <= 64) return 6;
    if (limit.rlim_cur < wanted) {
        limit.rlim_cur = wanted;
        if (setrlimit(RLIMIT_NOFILE, &limit) < 0) return 7;
    }
    int sockets[2], channel[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0 || pipe(channel) < 0) return 8;
    int file = open("/dev/null", O_RDONLY);
    high_fd = fcntl(sockets[1], F_DUPFD, (int)wanted - 1);
    if (file < 0 || high_fd < 0 || fcntl(high_fd, F_SETFD, FD_CLOEXEC) < 0) return 9;
    limit.rlim_cur = limit.rlim_max = 32;
    if (setrlimit(RLIMIT_NOFILE, &limit) < 0) return 10;

    char error[256];
    Net *net = net_open("blocked.invalid", 9, 0, "", error, sizeof error);
    if (!net) { fprintf(stderr, "%s\n", error); return 11; }
    int result = 0;
    int64_t end = net_now_ms() + 2000;
    if (mode == 2) {
        while (net_state(net) != NET_FAILED && net_now_ms() < end) {
            struct pollfd ready = {net_fd(net), net_events(net, 0), 0};
            if (poll(&ready, 1, 10) >= 0) net_step(net, ready.revents, net_now_ms());
        }
        if (shared->entered || net_state(net) != NET_FAILED ||
            !strstr(net_error(net), "Cannot isolate resolver descriptors")) {
            fprintf(stderr, "isolation failure did not fail closed: %s\n", net_error(net)); result = 12;
        }
    } else {
        while (!shared->entered && net_now_ms() < end) (void)poll(NULL, 0, 5);
        if (!shared->entered || shared->bad_fd || waitpid(-1, NULL, WNOHANG) != 0) {
            fprintf(stderr, "resolver was not isolated and blocked: entered=%d bad_fd=%d\n",
                    (int)shared->entered, (int)shared->bad_fd); result = 13;
        }
    }
    close(sockets[1]); close(high_fd); close(channel[1]); close(file);
    if (!result && (!eof_now(sockets[0]) || !eof_now(channel[0]))) {
        fprintf(stderr, "blocked resolver retained an unrelated socket or pipe\n"); result = 14;
    }
    if (!result && mode == 1 && net_step(net, 0, net_now_ms() + 31000) >= 0) {
        fprintf(stderr, "resolver deadline did not fail\n"); result = 15;
    }
    net_close(net);
    if (waitpid(-1, NULL, WNOHANG) != -1 || errno != ECHILD) {
        fprintf(stderr, "resolver child was not reaped\n"); result = 16;
    }
    for (int fd = 0; fd < 3; ++fd)
        if (fcntl(fd, F_GETFD) < 0) result = 17;
    close(sockets[0]); close(channel[0]);
    munmap(shared, sizeof *shared);
    return result;
}
'''


def resolver_isolation(directory):
    source = directory / "resolver_probe.c"
    source.write_text(RESOLVER_PROBE, encoding="ascii")
    variants = [("native", [], (0, 1))]
    if sys.platform.startswith("linux"):
        variants.extend([
            ("enumeration", ["-Dsyscall=resolver_test_syscall"], (0, 1)),
            ("failure", ["-Dsyscall=resolver_test_syscall", "-Dopendir=resolver_test_opendir"], (2,)),
        ])
    for name, flags, modes in variants:
        probe = directory / ("resolver-" + name)
        subprocess.run([
            *COMPILER, *BUILD_FLAGS,
            *flags, "-I", str(ROOT / "src"), str(source), str(ROOT / "src/net.c"),
            *LINK_FLAGS, "-lssl", "-lcrypto", "-o", str(probe),
        ], check=True)
        for mode in modes:
            run = subprocess.run([str(probe), str(mode)], capture_output=True, text=True, timeout=5)
            assert run.returncode == 0, (name, mode, run.returncode, run.stdout, run.stderr)
    print("resolver: unrelated socket/pipe/file isolation, high fd above lowered limits, cancellation and deadline reaping passed")


def cert_pair(directory, name, san):
    key = directory / (name + ".key")
    cert = directory / (name + ".pem")
    subprocess.run([
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
        "-keyout", str(key), "-out", str(cert), "-days", "1",
        "-subj", "/CN=" + name, "-addext", "subjectAltName=" + san,
    ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return cert, key


def expired_pair(directory):
    """Sign a correctly named expired leaf with a currently trusted test CA."""
    ca = directory / "expiry-ca.pem"
    ca_key = directory / "expiry-ca.key"
    key = directory / "expired.key"
    request = directory / "expired.csr"
    cert = directory / "expired.pem"
    database = directory / "expiry-index"
    serial = directory / "expiry-serial"
    config = directory / "expiry-ca.cnf"
    database.write_text("", encoding="ascii")
    serial.write_text("01\n", encoding="ascii")
    config.write_text(
        "[ca]\ndefault_ca=fixture\n[fixture]\n"
        f"database={database}\nnew_certs_dir={directory}\nserial={serial}\n"
        "default_md=sha256\npolicy=subject\nx509_extensions=server\n"
        "[subject]\ncommonName=supplied\n[server]\n"
        "basicConstraints=critical,CA:FALSE\n"
        "keyUsage=critical,digitalSignature,keyEncipherment\n"
        "extendedKeyUsage=serverAuth\n"
        "subjectAltName=DNS:localhost,IP:127.0.0.1\n",
        encoding="ascii",
    )
    commands = [
        ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
         "-keyout", str(ca_key), "-out", str(ca), "-days", "1",
         "-subj", "/CN=Transport expiry test CA",
         "-addext", "basicConstraints=critical,CA:TRUE",
         "-addext", "keyUsage=critical,keyCertSign,cRLSign"],
        ["openssl", "req", "-new", "-newkey", "rsa:2048", "-nodes",
         "-keyout", str(key), "-out", str(request), "-subj", "/CN=localhost"],
        ["openssl", "ca", "-batch", "-notext", "-config", str(config),
         "-cert", str(ca), "-keyfile", str(ca_key), "-in", str(request),
         "-out", str(cert), "-startdate", "20000101000000Z",
         "-enddate", "20000102000000Z"],
    ]
    for command in commands:
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    return cert, key, ca


def exchange(probe, host, cert=None, key=None, trust="", expected=True):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(9)
    port = listener.getsockname()[1]
    exceptions = []

    def serve():
        try:
            connection, _ = listener.accept()
            connection.settimeout(9)
            with connection:
                if cert:
                    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                    context.minimum_version = ssl.TLSVersion.TLSv1_2
                    context.load_cert_chain(cert, key)
                    try:
                        connection = context.wrap_socket(connection, server_side=True)
                    except ssl.SSLError:
                        if not expected:
                            return
                        raise
                with connection:
                    while True:
                        data = connection.recv(997)
                        if not data:
                            return
                        connection.sendall(data)
        except (ConnectionResetError, BrokenPipeError, ssl.SSLEOFError):
            pass
        except Exception as error:
            exceptions.append(error)
        finally:
            listener.close()

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    run = subprocess.run([
        str(probe), host, str(port), "1" if cert else "0", str(trust),
        "1" if expected else "0",
    ], capture_output=True, text=True, timeout=12)
    thread.join(timeout=10)
    assert run.returncode == 0, (run.returncode, run.stdout, run.stderr)
    assert not thread.is_alive(), "server did not terminate"
    assert not exceptions, exceptions
    return run.stderr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--resolver-only", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="vipirc-net-") as name:
        directory = Path(name)
        resolver_isolation(directory)
        if args.resolver_only:
            return
        source = directory / "probe.c"
        probe = directory / "probe"
        source.write_text(PROBE, encoding="ascii")
        subprocess.run([
            *COMPILER, *BUILD_FLAGS,
            "-I", str(ROOT / "src"), str(source), str(ROOT / "src/net.c"),
            *LINK_FLAGS, "-lssl", "-lcrypto", "-o", str(probe),
        ], check=True)
        cert, key = cert_pair(directory, "localhost", "DNS:localhost,IP:127.0.0.1")
        wrong, wrong_key = cert_pair(directory, "wrong.example", "DNS:wrong.example")
        exchange(probe, "127.0.0.1")
        exchange(probe, "localhost", cert, key, cert)
        exchange(probe, "127.0.0.1", cert, key, cert)
        exchange(probe, "localhost", wrong, wrong_key, wrong, expected=False)
        exchange(probe, "127.0.0.1", cert, key, "", expected=False)
        expired, expired_key, expiry_ca = expired_pair(directory)
        error = exchange(probe, "localhost", expired, expired_key, expiry_ca, expected=False)
        assert "certificate has expired" in error, error
        subprocess.run([str(probe), "localhost", "9", "0", "", "2"], check=True, timeout=2)
    print("transport: plaintext, TLS DNS/IP verification, bad identity, untrusted CA, expired leaf, deadline and byte integrity passed")


if __name__ == "__main__":
    main()
