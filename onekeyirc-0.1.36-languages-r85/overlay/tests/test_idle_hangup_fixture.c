/* Native terminal-loss fixture. IRC, PTYs, read/write failures, signals and
 * managed detach are real. Only initial managed-state setup and reattachment
 * replace the AF_UNIX handshake, so this is not an IPC authentication test.
 * Link main.c and the other standalone sources, excluding idle/background.c. */
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

static ssize_t fixture_terminal_read(int, void *, size_t);
#define read fixture_terminal_read
#ifndef IDLE_SOURCE
#define IDLE_SOURCE "idle.c"
#endif
#include IDLE_SOURCE
#undef read

/* The two production translation units have one private helper in common. */
#define notice background_notice
#define background_create fixture_real_create
#define background_detach fixture_real_detach
#define background_process fixture_real_process
/* Include the generated localization lookup for this single-file IPC fixture.
 * The production executable instead defines it in idle_setup.c. */
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#undef IRC_L10N_IMPLEMENTATION
#include "../src/background.c"
#undef background_process
#undef background_detach
#undef background_create
#undef notice

static int fixture_fd(const char *name)
{
    const char *text = getenv(name);
    if (!text || !*text) return -1;
    char *end;
    long fd = strtol(text, &end, 10);
    assert(!*end && fd >= 0 && fd <= INT_MAX);
    return (int)fd;
}

static void fixture_report(char code)
{
    int fd = fixture_fd("VIPIRC_TEST_REPORT_FD");
    if (fd >= 0) assert(write(fd, &code, 1) == 1);
}

Background *background_create(void)
{
    Background *b = fixture_real_create();
    if (!b || !getenv("VIPIRC_TEST_MANAGED")) return b;
    b->listener = fixture_fd("VIPIRC_TEST_REQUEST_FD");
    b->controller = fixture_fd("VIPIRC_TEST_CONTROLLER_FD");
    assert(b->listener > STDERR_FILENO && b->controller > STDERR_FILENO);
    assert(fd_flags(b->listener, 1) && fd_flags(b->controller, 1));
    b->managed = 1;
    b->detached = b->suspended = 0;
    return b;
}

int background_detach(Background *b, const volatile sig_atomic_t *cancel,
                      char *error, size_t capacity)
{
    int managed = background_managed(b), old_controller = b ? b->controller : -1;
    int okay = fixture_real_detach(b, cancel, error, capacity);
    if (managed && okay) {
        struct stat quiet, current;
        assert(stat("/dev/null", &quiet) == 0);
        for (int fd = 0; fd < 3; ++fd)
            assert(fstat(fd, &current) == 0 && current.st_dev == quiet.st_dev &&
                   current.st_ino == quiet.st_ino);
        assert(background_detached(b) && b->controller == -1);
        if (old_controller >= 0)
            assert(fcntl(old_controller, F_GETFD) < 0 && errno == EBADF);
        fixture_report('D');
    }
    return okay;
}

unsigned background_process(Background *b, const struct pollfd *fds, size_t count)
{
    unsigned events = fixture_real_process(b, fds, count);
    if (!b || !getenv("VIPIRC_TEST_MANAGED") || !background_detached(b)) return events;
    struct pollfd request = {b->listener, POLLIN, 0};
    if (poll(&request, 1, 0) != 1 || !(request.revents & POLLIN)) return events;
    char command;
    assert(read(request.fd, &command, 1) == 1 && command == 'A');
    int terminal = fixture_fd("VIPIRC_TEST_REATTACH_FD");
    assert(terminal > STDERR_FILENO && isatty(terminal));
    for (int fd = 0; fd < 3; ++fd) assert(dup2(terminal, fd) == fd);
    close(terminal);
    clearerr(stdin); clearerr(stdout); clearerr(stderr);
    b->detached = b->suspended = 0;
    b->frontend = BACKGROUND_FRONTEND_IDLE;
    fixture_report('A');
    return events | BACKGROUND_ATTACH;
}

static ssize_t fixture_terminal_read(int fd, void *data, size_t capacity)
{
    ssize_t got = read(fd, data, capacity);
    const char *mode = getenv("VIPIRC_TEST_READ_FAILURE");
    /* An explicit local test byte produces the boundary cases not guaranteed
     * by every PTY driver. Real master closure is tested independently. */
    if (fd == STDIN_FILENO && got == 1 && *(unsigned char *)data == 29 && mode) {
        fixture_report('I');
        if (!strcmp(mode, "eof")) return 0;
        assert(!strcmp(mode, "eio"));
        errno = EIO;
        return -1;
    }
    return got;
}
