/* Run the actual transport entry point with local certificate fixtures.
 * Compile-time wrappers change a pathname or file length at the read boundary;
 * there are no production test switches and no remote network connection. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *fixture_path, *replacement_path, *fixture_mode;
static int fixture_fd = -1, changed, safe_flags;
static int ca_fixture_open(const char *, int, ...);
static ssize_t ca_fixture_read(int, void *, size_t);

#define open ca_fixture_open
#define read ca_fixture_read
#include "net.c"
#undef open
#undef read

static int ca_fixture_open(const char *path, int flags, ...)
{
    int fd;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode_t mode = (mode_t)va_arg(args, int);
        va_end(args);
        fd = open(path, flags, mode);
    } else fd = open(path, flags);
    if (fd >= 0 && !strcmp(path, fixture_path)) {
        fixture_fd = fd;
        safe_flags = (flags & O_NONBLOCK) && (fcntl(fd, F_GETFD) & FD_CLOEXEC);
        if (!strcmp(fixture_mode, "swap")) {
            assert(replacement_path && rename(replacement_path, path) == 0);
            changed = 1;
        }
    }
    return fd;
}

static ssize_t ca_fixture_read(int fd, void *bytes, size_t size)
{
    if (fd == fixture_fd && !changed) {
        changed = 1;
        if (!strcmp(fixture_mode, "eintr")) { errno = EINTR; return -1; }
        if (!strcmp(fixture_mode, "shrink") || !strcmp(fixture_mode, "grow")) {
            int writer = open(fixture_path, O_WRONLY | O_APPEND);
            assert(writer >= 0);
            if (!strcmp(fixture_mode, "shrink")) assert(ftruncate(writer, 0) == 0);
            else assert(write(writer, "x", 1) == 1);
            assert(close(writer) == 0);
        }
    }
    return read(fd, bytes, size);
}

int main(int argc, char **argv)
{
    if (argc != 7 && argc != 8) return 2;
    fixture_path = argv[1];
    int expected = atoi(argv[2]), expected_certificates = atoi(argv[3]);
    int expected_crls = atoi(argv[4]), auxiliary_trust = atoi(argv[5]);
    fixture_mode = argv[6];
    if (argc == 8) replacement_path = argv[7];
    char error[256];
    Net *net = net_open("127.0.0.1", 9, 1, fixture_path, error, sizeof error);
    if (!net) {
        if (!expected) { printf("rejected: %s\n", error); return 0; }
        fprintf(stderr, "unexpected CA rejection: %s\n", error);
        return 3;
    }
    /* net_step is deliberately never called, so no socket is connected. */
    int certificates = 0, crls = 0, trust_okay = !auxiliary_trust;
    STACK_OF(X509_OBJECT) *objects = X509_STORE_get0_objects(SSL_CTX_get_cert_store(net->ctx));
    for (int i = 0; i < sk_X509_OBJECT_num(objects); ++i) {
        X509_OBJECT *object = sk_X509_OBJECT_value(objects, i);
        if (X509_OBJECT_get_type(object) == X509_LU_X509) {
            ++certificates;
            if (auxiliary_trust) {
                X509 *certificate = X509_OBJECT_get0_X509(object);
                trust_okay = X509_check_trust(certificate, X509_TRUST_SSL_SERVER, 0) == X509_TRUST_TRUSTED &&
                             X509_check_trust(certificate, X509_TRUST_SSL_CLIENT, 0) == X509_TRUST_REJECTED;
            }
        } else if (X509_OBJECT_get_type(object) == X509_LU_CRL) ++crls;
    }
    int okay = expected && certificates == expected_certificates &&
               crls == expected_crls && trust_okay && safe_flags;
    if (!strcmp(fixture_mode, "swap") || !strcmp(fixture_mode, "eintr")) okay = okay && changed;
    net_close(net);
    if (!okay) {
        fprintf(stderr, "CA result: expected=%d certificates=%d CRLs=%d trust=%d flags=%d\n",
                expected, certificates, crls, trust_okay, safe_flags);
        return 4;
    }
    puts("accepted pinned regular CA file; trust objects preserved");
    return 0;
}
