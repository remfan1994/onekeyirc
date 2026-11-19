/* Actual pipe writes with an explicit short-write/EAGAIN budget. A dropped
 * terminal queue must not consume a reminder that never finished writing. */
#include <unistd.h>
static ssize_t notice_write(int, const void *, size_t);
#define write notice_write
#include "idle.c"
#undef write
#include <assert.h>

static ssize_t write_budget = -1;

static ssize_t notice_write(int fd, const void *bytes, size_t length)
{
    if (fd == STDOUT_FILENO && write_budget >= 0) {
        if (!write_budget) { errno = EAGAIN; return -1; }
        if (length > (size_t)write_budget) length = (size_t)write_budget;
        ssize_t count = write(fd, bytes, length);
        if (count > 0) write_budget -= count;
        return count;
    }
    return write(fd, bytes, length);
}

int main(void)
{
    Idle *idle = calloc(1, sizeof *idle);
    Client *client = calloc(1, sizeof *client);
    assert(idle && client);
    idle->client = client;
    idle->mode = WATCH;
    int saved = dup(STDOUT_FILENO), fds[2];
    assert(saved >= 0 && pipe(fds) == 0 && dup2(fds[1], STDOUT_FILENO) >= 0);
    assert(close(fds[1]) == 0);
    assert(output_literal(idle, "older presentation\r\n"));
    stream_output(idle);
    assert(!client->session_notice_shown && idle->session_notice_remaining == idle->output_length);
    size_t pending = idle->output_length;
    stream_output(idle);
    assert(idle->output_length == pending); /* queued only once */
    write_budget = 5;
    assert(output_flush(idle));
    assert(!client->session_notice_shown && idle->session_notice_remaining == pending - 5);
    release_terminal(idle);
    assert(!client->session_notice_shown && !idle->session_notice_remaining && !idle->output_length);
    char bytes[256];
    assert(read(fds[0], bytes, sizeof bytes) == 5 && !memcmp(bytes, "older", 5));
    assert(close(fds[0]) == 0);
    assert(pipe(fds) == 0 && dup2(fds[1], STDOUT_FILENO) >= 0 && close(fds[1]) == 0);

    /* Returning from an input view defers the reminder until ordinary WATCH. */
    idle->mode = COMPOSE;
    stream_output(idle);
    assert(!idle->output_length && !client->session_notice_shown);
    idle->mode = WATCH;
    strcpy(idle->notice, "Operational error first");
    idle->notice_pending = 1;
    idle->tty.utf8 = 1;
    stream_output(idle);
    const char expected[] = "Operational error first\r\n" CLIENT_SESSION_NOTICE "\r\n";
    assert(idle->output_length == sizeof expected - 1);
    assert(!memcmp(idle->output, expected, sizeof expected - 1));
    write_budget = (ssize_t)sizeof expected - 2;
    assert(output_flush(idle));
    assert(idle->session_notice_remaining == 1 && !client->session_notice_shown);
    write_budget = -1;
    assert(output_flush(idle));
    assert(!idle->session_notice_remaining && client->session_notice_shown);
    assert(read(fds[0], bytes, sizeof bytes) == (ssize_t)sizeof expected - 1);
    assert(!memcmp(bytes, expected, sizeof expected - 1));
    release_terminal(idle);
    stream_output(idle);
    assert(!idle->output_length); /* a completed reminder stays consumed */
    assert(dup2(saved, STDOUT_FILENO) >= 0 && close(saved) == 0 && close(fds[0]) == 0);
    free(client); free(idle);
    puts("notice: partial writes and EAGAIN do not consume pending output; discarded queues retry, completed notices stay once, errors retain priority");
    return 0;
}
