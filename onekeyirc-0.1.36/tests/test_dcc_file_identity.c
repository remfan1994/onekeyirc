/* Local, deterministic pathname replacement regression. No external traffic. */
#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int replace_source;
static int audit_openat(int directory, const char *name, int flags, ...);
#define openat audit_openat
#include "dcc.c"
#undef openat

static int audit_openat(int directory, const char *name, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    if (replace_source && !strcmp(name, "source.bin") && (flags & O_ACCMODE) == O_RDONLY) {
        replace_source = 0;
        if (renameat(directory, "source.bin", directory, "original.bin") < 0 ||
            renameat(directory, "replacement.bin", directory, "source.bin") < 0) {
            perror("test rename"); exit(2);
        }
    }
    return openat(directory, name, flags, mode);
}

static int create_file(const char *directory, const char *name, const char *contents)
{
    char path[DCC_PATH_MAX];
    int n = snprintf(path, sizeof path, "%s/%s", directory, name);
    if (n < 0 || (size_t)n >= sizeof path) return 0;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return 0;
    ssize_t written = write(fd, contents, strlen(contents));
    int closed = close(fd);
    return written == (ssize_t)strlen(contents) && closed == 0;
}

int main(void)
{
    char root[] = "/tmp/vipirc-dcc-identity-XXXXXX";
    char source[DCC_PATH_MAX], body[IRC_BODY_MAX + 1], error[256];
    uint64_t id = 0;
    if (!mkdtemp(root) || !create_file(root, "source.bin", "chosen file") ||
        !create_file(root, "replacement.bin", "not the chosen file")) return 2;
    (void)snprintf(source, sizeof source, "%s/source.bin", root);
    DccManager *manager = dcc_create(0);
    if (!manager) return 2;
    replace_source = 1;
    int accepted = dcc_send(manager, 1, "peer", source, "127.0.0.1", 0,
                            &id, body, sizeof body, error, sizeof error);
    int failed = accepted || replace_source || id || body[0] || dcc_count(manager);
    if (failed) fputs("FAIL: a replaced DCC source was accepted for transfer.\n", stderr);
    else if (!strstr(error, "changed")) failed = 1;
    dcc_free(manager);
    int directory = open(root, O_RDONLY | O_DIRECTORY);
    if (directory < 0 || unlinkat(directory, "source.bin", 0) < 0 ||
        unlinkat(directory, "original.bin", 0) < 0 || close(directory) < 0 || rmdir(root) < 0) return 2;
    if (failed) return 1;
    puts("dcc: substituted source inode rejected before listener creation");
    return 0;
}
