/* Direct local-IPC security regressions. The production implementation is
 * used with only resolver/credential readiness supplied by deterministic
 * stubs; no IRC server or real credential helper is contacted. */
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <signal.h>
#include <unistd.h>
static pid_t boundary_fork(void);
#define fork boundary_fork
#include "../src/background.c"
#undef fork
#include <assert.h>
#include <dirent.h>
#include <time.h>

static int helpers_pending;
static int cancel_at_fork;
static pid_t fork_child;
static volatile sig_atomic_t cancellation;
static pid_t boundary_fork(void)
{
    pid_t child = fork();
    if (child > 0) {
        fork_child = child;
        if (cancel_at_fork) cancellation = SIGINT;
    }
    return child;
}
int net_detach_ready(Net *n) { (void)n; return !helpers_pending; }
void credential_reap(void) { }
int credential_cleanup_pending(void) { return 0; }
int64_t net_now_ms(void)
{
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static size_t fd_count(void)
{
    DIR *d = opendir("/proc/self/fd");
    if (!d) d = opendir("/dev/fd");
    if (!d) return 0;
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL)
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) ++count;
    assert(closedir(d) == 0);
    return count;
}

static void process_one(Background *b)
{
    struct pollfd descriptors[BACKGROUND_POLL_CAP];
    size_t count = background_pollfds(b, descriptors, BACKGROUND_POLL_CAP);
    assert(count && poll(descriptors, (nfds_t)count, 200) >= 0);
    assert(background_process(b, descriptors, count) == 0);
}

static int connect_one(Background *b)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un address;
    memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, background_path(b), strlen(background_path(b)) + 1);
    assert(fd >= 0 && connect(fd, (struct sockaddr *)&address, sizeof address) == 0);
    process_one(b);
    assert(b->candidate >= 0);
    return fd;
}

static void reject_request(Background *b, int count, int bad_magic)
{
    int fd = connect_one(b);
    AttachRequest request;
    memset(&request, 0, sizeof request);
    memcpy(request.magic, bad_magic ? "BADMAGIC" : "VIPIRCB1", 8);
    strcpy(request.term, "xterm-256color"); strcpy(request.locale, "C");
    union { struct cmsghdr align; unsigned char bytes[CMSG_SPACE(sizeof(int) * 16)]; } ancillary;
    memset(&ancillary, 0, sizeof ancillary);
    struct iovec vector = {&request, sizeof request};
    struct msghdr message;
    memset(&message, 0, sizeof message);
    message.msg_iov = &vector; message.msg_iovlen = 1;
    int quiet = open("/dev/null", O_RDWR);
    assert(quiet >= 0);
    if (count) {
        message.msg_control = ancillary.bytes;
        message.msg_controllen = CMSG_SPACE(sizeof(int) * (size_t)count);
        struct cmsghdr *c = CMSG_FIRSTHDR(&message);
        c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int) * (size_t)count);
        for (int i = 0; i < count; ++i)
            memcpy((unsigned char *)CMSG_DATA(c) + (size_t)i * sizeof(int), &quiet, sizeof quiet);
    }
    assert(sendmsg(fd, &message, 0) == (ssize_t)sizeof request);
    close(quiet);
    process_one(b);
    unsigned char reply[2];
    assert(recv(fd, reply, sizeof reply, 0) == 2 && reply[0] == 'E' && reply[1] == 2);
    assert(b->candidate < 0 && b->controller < 0 && background_detached(b));
    close(fd);
}

static SessionReply session_query(Background *b)
{
    int fd = connect_one(b);
    assert(write(fd, "VIPIRCQ1", 8) == 8);
    process_one(b);
    SessionReply reply;
    assert(recv(fd, &reply, sizeof reply, MSG_WAITALL) == (ssize_t)sizeof reply);
    const char expected[8] = "1keyirc";
    const char expected_full[8] = "vipirc";
    assert(!memcmp(reply.magic, "VIPIRCS1", 8));
    assert(!memcmp(reply.program, !strcmp(b->program, "onekeyirc") ? expected : expected_full,
                   sizeof reply.program));
    assert(reply.job == b->job && !memcmp(reply.token, b->token, sizeof b->token));
    assert(b->candidate < 0 && b->controller < 0 && background_detached(b));
    close(fd);
    return reply;
}

static size_t bounded_receive(int fd, void *data, size_t length)
{
    assert(fd_flags(fd, 1));
    size_t used = 0;
    int64_t deadline = net_now_ms() + BG_HANDSHAKE_MS;
    while (used < length && net_now_ms() < deadline) {
        ssize_t got = recv(fd, (char *)data + used, length - used, 0);
        if (got > 0) used += (size_t)got;
        else if (got < 0 && errno == EINTR) continue;
        else if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!wait_socket(fd, POLLIN, deadline)) break;
        } else break;
    }
    return used;
}

static void status_query_v2(Background *b, int attached)
{
    int controller = b->controller;
    int fd = connect_one(b);
    AttachRequest request;
    memset(&request, 0, sizeof request);
    memcpy(request.magic, "VIPIRCQ2", 8);
    assert(write(fd, &request, sizeof request) == (ssize_t)sizeof request);
    process_one(b);
    SessionReply reply;
    assert(bounded_receive(fd, &reply, sizeof reply) == sizeof reply);
    assert(!memcmp(reply.magic, attached ? "VIPIRCA2" : "VIPIRCS2", 8));
    assert(reply.job == b->job && !memcmp(reply.token, b->token, sizeof b->token));
    assert(b->controller == controller && b->candidate < 0);
    close(fd);
}

/* The spelling grew from seven to nine characters. Exercise both display
 * names in the same shared runtime, including the exact established wire
 * layout. A build with ONEKEY_ONLY also validates the standalone reader. */
static void program_names(Client *client)
{
    const char *names[] = {"vipirc", "onekeyirc"};
    assert(offsetof(SessionReply, program) == 8 && offsetof(SessionReply, job) == 16);
    assert(offsetof(SessionReply, token) == 20 && offsetof(SessionReply, label) == 36);
    assert(sizeof(SessionReply) == 228);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
        Background *b = background_create();
        char error[256], suffix[96];
        assert(b && !strcmp(b->program, BG_PROGRAM));
        background_set_program(b, names[i]);
        assert(!strcmp(b->program, names[i]) && strlen(b->program) == strlen(names[i]));
        assert(background_prepare(b, client, error, sizeof error));
        int n = snprintf(suffix, sizeof suffix, "/%s-jobs-%lu/job-%u/session",
                         names[i], (unsigned long)getuid(), b->job);
        assert(n > 0 && (size_t)n < sizeof suffix && strlen(b->path) >= (size_t)n);
        assert(!strcmp(b->path + strlen(b->path) - (size_t)n, suffix));
        b->managed = b->detached = 1;
        background_set_program(b, names[1 - i]);
        assert(!strcmp(b->program, names[i]));
        SessionJob job;
        memset(&job, 0, sizeof job);
        job.number = b->job;
        job.reply = session_query(b);
        assert(reply_valid(&job) == !strcmp(names[i], BG_PROGRAM));
        /* No truncation of the new display name can become a valid v1 ID. */
        memcpy(job.reply.program, "onekeyir", sizeof job.reply.program);
        assert(!reply_valid(&job));
        char endpoint[BG_PATH_SIZE];
        strcpy(endpoint, b->path);
        background_free(b);
        assert(access(endpoint, F_OK) < 0 && errno == ENOENT);
    }
}


static void accept_frontend_request(Background *b, int frontend)
{
    int saved[3] = {dup(STDIN_FILENO), dup(STDOUT_FILENO), dup(STDERR_FILENO)};
    assert(saved[0] >= 0 && saved[1] >= 0 && saved[2] >= 0);
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master >= 0 && fd_flags(master, 0) &&
           grantpt(master) == 0 && unlockpt(master) == 0);
    const char *name = ptsname(master); assert(name);
    int slave = open(name, O_RDWR | O_NOCTTY | O_CLOEXEC); assert(slave >= 0);
    int fd = connect_one(b);
    AttachRequest request; memset(&request, 0, sizeof request);
    memcpy(request.magic, "VIPIRCB3", 8);
    strcpy(request.term, "xterm-256color"); strcpy(request.locale, "C");
    memcpy(request.token, b->token, sizeof request.token);
    request.frontend = (unsigned char)frontend;
    union { struct cmsghdr align; unsigned char bytes[CMSG_SPACE(sizeof(int) * 3)]; } ancillary;
    memset(&ancillary, 0, sizeof ancillary);
    struct iovec vector = {&request, sizeof request};
    struct msghdr message; memset(&message, 0, sizeof message);
    message.msg_iov = &vector; message.msg_iovlen = 1;
    message.msg_control = ancillary.bytes; message.msg_controllen = sizeof ancillary.bytes;
    struct cmsghdr *c = CMSG_FIRSTHDR(&message);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(sizeof(int) * 3);
    int terminals[3] = {slave, slave, slave}; memcpy(CMSG_DATA(c), terminals, sizeof terminals);
    assert(sendmsg(fd, &message, 0) == (ssize_t)sizeof request);
    struct pollfd descriptors[BACKGROUND_POLL_CAP]; size_t count = background_pollfds(b, descriptors, BACKGROUND_POLL_CAP);
    assert(count && poll(descriptors, (nfds_t)count, 200) >= 0);
    unsigned events = background_process(b, descriptors, count);
    unsigned char reply[2]; assert(recv(fd, reply, sizeof reply, MSG_WAITALL) == 2);
    assert(reply[0] == 'A' && reply[1] == 0 && (events & BACKGROUND_ATTACH));
    assert(background_frontend(b) == frontend && b->controller >= 0 && !background_detached(b));
    int controller = b->controller;
    status_query_v2(b, 1);
    int second = connect_one(b);
    assert(send_attach(second, &request, net_now_ms() + BG_HANDSHAKE_MS));
    process_one(b);
    assert(bounded_receive(second, reply, sizeof reply) == sizeof reply && reply[0] == 'E' && reply[1] == 1);
    assert(b->controller == controller && !background_detached(b));
    close(second);
    /* The old query remains wire-compatible for existing CLI clients. */
    second = connect_one(b);
    assert(write(second, "VIPIRCQ1", 8) == 8);
    process_one(b);
    assert(bounded_receive(second, reply, sizeof reply) == sizeof reply && reply[0] == 'E' && reply[1] == 1);
    close(second);
    for (int i = 0; i < 3; ++i) { assert(dup2(saved[i], i) >= 0); close(saved[i]); }
    clearerr(stdin); clearerr(stdout); clearerr(stderr);
    close(fd); close(slave); close(master);
    close(b->controller); b->controller = -1; b->detached = 1;
}

static void reject_reused_job(Background *b)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master >= 0 && fd_flags(master, 0) &&
           grantpt(master) == 0 && unlockpt(master) == 0);
    const char *name = ptsname(master);
    assert(name);
    int slave = open(name, O_RDWR | O_NOCTTY | O_CLOEXEC);
    assert(slave >= 0);
    int fd = connect_one(b);
    AttachRequest request;
    memset(&request, 0, sizeof request);
    memcpy(request.magic, "VIPIRCB2", 8);
    strcpy(request.term, "xterm-256color"); strcpy(request.locale, "C");
    memcpy(request.token, b->token, sizeof request.token);
    request.token[0] ^= 1; /* The slot number alone never authorizes handoff. */
    union { struct cmsghdr align; unsigned char bytes[CMSG_SPACE(sizeof(int) * 3)]; } ancillary;
    memset(&ancillary, 0, sizeof ancillary);
    struct iovec vector = {&request, sizeof request};
    struct msghdr message;
    memset(&message, 0, sizeof message);
    message.msg_iov = &vector; message.msg_iovlen = 1;
    message.msg_control = ancillary.bytes; message.msg_controllen = sizeof ancillary.bytes;
    struct cmsghdr *c = CMSG_FIRSTHDR(&message);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(3 * sizeof(int));
    int descriptors[3] = {slave, slave, slave};
    memcpy(CMSG_DATA(c), descriptors, sizeof descriptors);
    assert(sendmsg(fd, &message, 0) == (ssize_t)sizeof request);
    process_one(b);
    unsigned char reply[2];
    assert(recv(fd, reply, 2, MSG_WAITALL) == 2 && reply[0] == 'E' && reply[1] == 2);
    assert(b->controller < 0 && background_detached(b));
    close(fd); close(slave); close(master);
}

/* A real worker owns the original lease across fork. Its parent closes every
 * inherited lease/listener reference, then uses the public no-terminal API.
 * Only these directly created test children are ever signalled on failure. */
static void cooperative_close_native(Client *client, int attached)
{
    Background *b = background_create();
    char error[256], number[24], endpoint[BG_PATH_SIZE];
    assert(b && background_prepare(b, client, error, sizeof error));
    b->managed = 1; b->detached = !attached;
    int controller[2] = {-1, -1};
    if (attached) {
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, controller) == 0);
        assert(fd_flags(controller[0], 1) && fd_flags(controller[1], 1));
        b->controller = controller[0];
    }
    snprintf(number, sizeof number, "%u", b->job);
    strcpy(endpoint, b->path);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        if (controller[1] >= 0) close(controller[1]);
        alarm(12);
        for (;;) {
            struct pollfd fds[BACKGROUND_POLL_CAP];
            size_t count = background_pollfds(b, fds, BACKGROUND_POLL_CAP);
            assert(count && poll(fds, (nfds_t)count, background_timeout(b, 100)) >= 0);
            unsigned events = background_process(b, fds, count);
            if (events & BACKGROUND_STOP) {
                background_finish(b, 0);
                _exit(0);
            }
            if (events & BACKGROUND_DETACH)
                assert(background_detach(b, NULL, error, sizeof error));
        }
    }
    /* Do not run destructors in this parent: the endpoint belongs to child. */
    close(b->listener); close(b->directory_fd); close(b->registry_fd); close(b->lease);
    if (b->controller >= 0) close(b->controller);
    free(b);
    int result = background_close_session(number);
    if (result) (void)kill(child, SIGKILL);
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(result == 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(access(endpoint, F_OK) < 0 && errno == ENOENT);
    if (controller[1] >= 0) {
        unsigned char reply[2];
        assert(bounded_receive(controller[1], reply, sizeof reply) == sizeof reply);
        assert(reply[0] == 'Q' && reply[1] == 0);
        close(controller[1]);
    }
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    size_t initial = fd_count();
    Client *client = calloc(1, sizeof *client);
    App *app = calloc(1, sizeof *app);
    Background *b = background_create();
    assert(client && app && b);
    client->slots[0].app = app;
    program_names(client);
    cooperative_close_native(client, 0);
    cooperative_close_native(client, 1);
    char error[256];
    app->cfg.single_session = 1;
    Background *single_one = background_create(), *single_two = background_create();
    assert(single_one && single_two && background_prepare(single_one, client, error, sizeof error));
    single_one->managed = single_one->detached = 1;
    assert(!background_prepare(single_two, client, error, sizeof error) && strstr(error, "Single-session mode"));
    background_free(single_one);
    assert(background_prepare(single_two, client, error, sizeof error));
    background_free(single_two);
    app->cfg.single_session = 0;
    struct pollfd pollfds[BACKGROUND_POLL_CAP];
    assert(background_pollfds(b, pollfds, BACKGROUND_POLL_CAP) == 0);
    assert(!background_managed(b) && !background_detached(b) && !*background_path(b));
    app->credential = (Credential *)app;
    assert(!background_prepare(b, client, error, sizeof error) && !*background_path(b));
    app->credential = NULL;
    helpers_pending = 1;
    assert(!background_prepare(b, client, error, sizeof error) && !*background_path(b));
    helpers_pending = 0;
    assert(background_prepare(b, client, error, sizeof error));
    assert(endpoint_valid(background_path(b)));
    helpers_pending = 1;
    assert(!background_prepare(b, client, error, sizeof error));
    helpers_pending = 0;
    assert(background_prepare(b, client, error, sizeof error));
    char endpoint[sizeof b->path], directory[sizeof b->directory];
    strcpy(endpoint, b->path); strcpy(directory, b->directory);
    assert(chmod(directory, 0755) == 0 && !endpoint_valid(endpoint));
    assert(chmod(directory, 0700) == 0 && endpoint_valid(endpoint));
    assert(chmod(endpoint, 0666) == 0 && !endpoint_valid(endpoint));
    assert(chmod(endpoint, 0600) == 0 && endpoint_valid(endpoint));
    b->managed = b->detached = 1;
    status_query_v2(b, 0);
    accept_frontend_request(b, BACKGROUND_FRONTEND_IDLE);
#ifndef ONEKEY_ONLY
    accept_frontend_request(b, BACKGROUND_FRONTEND_FULL);
#endif
    size_t protected_count = fd_count();
    assert(b->job >= 1 && b->job <= BG_JOB_MAX);
    assert(!stale_job(b->registry_fd, b->job_name));
    session_query(b);
    reject_reused_job(b);
    if (protected_count) assert(fd_count() == protected_count);
    Background *dead = background_create();
    assert(dead && background_prepare(dead, client, error, sizeof error));
    assert(dead->job != b->job && !stale_job(dead->registry_fd, dead->job_name));
    char dead_path[BG_PATH_SIZE];
    strcpy(dead_path, dead->path);
    close(dead->lease); dead->lease = -1;
    close(dead->listener); dead->listener = -1;
    assert(stale_job(dead->registry_fd, dead->job_name));
    assert(access(dead_path, F_OK) < 0 && errno == ENOENT);
    background_free(dead);
    if (protected_count) assert(fd_count() == protected_count);
    for (int i = 0; i < 30; ++i) {
        reject_request(b, i % 4 == 0 ? 0 : i % 4 == 1 ? 2 : i % 4 == 2 ? 3 : 16, i % 3 == 0);
        if (protected_count) assert(fd_count() == protected_count);
    }
    int partial = connect_one(b);
    assert(write(partial, "VIP", 3) == 3);
    process_one(b);
    assert(b->candidate >= 0);
    b->candidate_deadline = net_now_ms() - 1;
    assert(background_timeout(b, 1000) == 0);
    assert(background_process(b, NULL, 0) == 0 && b->candidate < 0);
    unsigned char reply[2];
    assert(read(partial, reply, 2) == 2 && reply[0] == 'E');
    close(partial);
    int blocked[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, blocked) == 0);
    assert(fd_flags(blocked[0], 1));
    char fill[4096] = {0};
    ssize_t sent = 0;
    for (size_t i = 0; i < 1024; ++i) {
        sent = send(blocked[0], fill, sizeof fill, 0);
        if (sent < 0) break;
    }
    assert(sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
    assert(!notice(blocked[0], 'S', 0));
    close(blocked[0]); close(blocked[1]);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, blocked) == 0);
    assert(fd_flags(blocked[0], 1));
    assert(notice(blocked[1], 'E', 1));
    close(blocked[1]);
    AttachRequest rejected_request;
    memset(&rejected_request, 0, sizeof rejected_request);
    memcpy(rejected_request.magic, "VIPIRCB1", 8);
    int64_t deadline = net_now_ms() + 1000;
    assert(!send_attach(blocked[0], &rejected_request, deadline));
    assert(initial_reply(blocked[0], reply, deadline) && reply[0] == 'E' && reply[1] == 1);
    close(blocked[0]);
    background_finish(b, 0);
    assert(access(endpoint, F_OK) < 0 && errno == ENOENT);
    assert(access(directory, F_OK) < 0 && errno == ENOENT);
    background_free(b);
    b = background_create();
    assert(b && background_prepare(b, client, error, sizeof error));
    cancel_at_fork = 1;
    int detached = background_detach(b, &cancellation, error, sizeof error);
    if (detached) _exit(0); /* The real child never executes parent checks. */
    assert(!detached && cancellation == SIGINT && fork_child > 0);
    assert(waitpid(fork_child, NULL, WNOHANG) < 0 && errno == ECHILD);
    assert(!background_managed(b));
    background_free(b);
    free(app); free(client);
    if (initial) assert(fd_count() == initial);
    puts("background IPC: complete display names and namespaces, stable v1 wire layout/identifiers, cross-program and truncated-ID rejection, no foreground endpoint, child-ownership refusal, private-directory/socket modes, stable numbered jobs, single-session collision refusal, requested-frontend handoff, live-lease protection/stale cleanup, identity query, reused-job token rejection with valid terminals, 30 malformed/nonterminal/extra-descriptor rejections, timeout, bounded full-buffer acknowledgment failure, early rejection before request write, fork-boundary interrupt kills/reaps child, descriptor stability and cleanup passed");
    return 0;
}
