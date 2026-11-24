/* Compare retained-stream traversal with the original linear traversal, then
 * check compact formatting through retained and disk-backed viewport paths.
 * No terminal, session socket or IRC connection is opened. */
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif
#define _XOPEN_SOURCE 700
#include "idle.c"
#include <assert.h>
#include <time.h>

#ifdef IDLE_STREAM_ISOLATED
int background_detached(const Background *background)
{
    assert(!background);
    return 0;
}
#endif

static void sequential_output(Idle *idle)
{
    /* Alternate-screen interfaces own the display while they are active.
     * Incoming records remain retained and are emitted when WATCH resumes. */
    if (idle->mode != WATCH || background_detached(idle->background)) return;
    if (idle->notice_pending && *idle->notice) {
        char text[520];
        size_t length = display_text(idle, text, sizeof text - 2, idle->notice);
        text[length++] = '\r'; text[length++] = '\n';
        if (!output_add(idle, text, length)) return;
        idle->notice_pending = 0;
    }
    /* A single local transcript line needs neither a modal prompt nor space
     * in the input field. Operational notices retain priority. Count bytes
     * ahead of this line too; only a completed write consumes the reminder. */
    if (!idle->client->session_notice_shown && !idle->session_notice_remaining) {
        if (!output_literal(idle, CLIENT_SESSION_NOTICE "\r\n")) return;
        idle->session_notice_remaining = idle->output_length;
    }
    if (idle->count && idle->shown_sequence < idle->lines[idle->first].sequence) {
        char lost[128];
        (void)snprintf(lost, sizeof lost, "[stream: %llu earlier records omitted at the bounded history limit]\r\n",
                       (unsigned long long)(idle->lines[idle->first].sequence - idle->shown_sequence));
        if (!output_literal(idle, lost)) return;
        idle->shown_sequence = idle->lines[idle->first].sequence;
    }
    size_t emitted = 0;
    for (size_t n = 0; n < idle->count && emitted < 64; ++n) {
        StreamLine *line = &idle->lines[(idle->first + n) % STREAM_LINES];
        if (line->sequence < idle->shown_sequence) continue;
        if (record_matches(idle, line)) {
            char compact[APP_TEXT + 2048], rendered[APP_TEXT + 2048];
            size_t length = display_text(idle, rendered, sizeof rendered - 2,
                                         stream_text(idle, line, compact, sizeof compact));
            rendered[length++] = '\r'; rendered[length++] = '\n';
            if (!output_add(idle, rendered, length)) return;
            ++emitted;
        }
        idle->shown_sequence = line->sequence + 1;
    }
}


static Idle *fresh(uint64_t first)
{
    Idle *idle = calloc(1, sizeof *idle);
    assert(idle);
    idle->client = calloc(1, sizeof *idle->client);
    assert(idle->client);
    idle->client->slots[0].seed.history_lines = STREAM_LINES;
    idle->client->slots[0].seed.output_full = 1;
    idle->client->session_notice_shown = 1;
    idle->mode = WATCH;
    idle->next_sequence = idle->shown_sequence = first;
    idle->display_token = 1;
    idle->display_kind = B_CHANNEL;
    strcpy(idle->display_target, "#test");
    idle->tty.utf8 = 1;
    return idle;
}

static void dispose(Idle *idle)
{
    while (idle->count) stream_pop(idle);
    free(idle->client);
    free(idle);
}

static void add_lines(Idle *idle, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        char line[128];
        snprintf(line, sizeof line, "record %u sequence %llu", i,
                 (unsigned long long)idle->next_sequence);
        assert(stream_store(idle, i % 3u ? 1 : 2, B_CHANNEL,
                            i % 5u ? "#test" : "#else", !(i % 7u),
                            !(i % 11u), 0, line));
    }
}

static void same_step(Idle *idle)
{
    Idle *expected = malloc(sizeof *expected);
    Client *client = malloc(sizeof *client);
    assert(expected && client);
    memcpy(expected, idle, sizeof *idle);
    memcpy(client, idle->client, sizeof *client);
    expected->client = client;
    sequential_output(expected);
    stream_output(idle);
    assert(idle->output_start == expected->output_start);
    assert(idle->output_length == expected->output_length);
    assert(!memcmp(idle->output + idle->output_start,
                   expected->output + expected->output_start, idle->output_length));
    assert(idle->shown_sequence == expected->shown_sequence);
    assert(idle->notice_pending == expected->notice_pending);
    assert(idle->session_notice_remaining == expected->session_notice_remaining);
    assert(idle->client->session_notice_shown == expected->client->session_notice_shown);
    free(client); free(expected);
}

static void discard_output(Idle *idle)
{
    idle->output_start = idle->output_length = 0;
}

static void drain(Idle *idle)
{
    unsigned passes = 0;
    do {
        same_step(idle);
        discard_output(idle);
        assert(++passes <= STREAM_LINES + 1u);
    } while (idle->shown_sequence < idle->next_sequence);
    same_step(idle);
    assert(!idle->output_length);
}

static void traversal_cases(void)
{
    Idle *idle = fresh(1);
    same_step(idle);
    idle->client->session_notice_shown = 0;
    strcpy(idle->notice, "priority notice");
    idle->notice_pending = 1;
    same_step(idle);
    assert(idle->session_notice_remaining == idle->output_length);
    assert(!memcmp(idle->output, "priority notice\r\n", 17));
    dispose(idle);

    idle = fresh(1);
    add_lines(idle, 5000); /* Ring wrap and eviction. */
    assert(idle->count == STREAM_LINES && idle->first);
    drain(idle); /* Gap notice, filtered records, and 64-message output batches. */
    uint64_t oldest = idle->lines[idle->first].sequence;
    uint64_t offsets[] = {0, 1, 63, 64, 1000, STREAM_LINES - 1, STREAM_LINES, STREAM_LINES + 1};
    for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        idle->shown_sequence = oldest + offsets[i];
        same_step(idle);
        discard_output(idle);
    }
    /* Clearing retained text must not break positional sequence indexing. */
    for (size_t i = 0; i < idle->count; i += 13) {
        StreamLine *line = &idle->lines[(idle->first + i) % STREAM_LINES];
        idle->bytes -= line->bytes;
        free(line->text); line->text = NULL; line->bytes = 0;
    }
    idle->shown_sequence = oldest;
    drain(idle);
    idle->display_kind = B_MENTIONS;
    idle->shown_sequence = oldest;
    drain(idle);
    idle->display_kind = B_CHANNEL;
    strcpy(idle->display_target, "#else");
    idle->shown_sequence = oldest + 125;
    drain(idle);
    /* A stalled terminal retains the first record it cannot queue. */
    idle->shown_sequence = oldest;
    idle->output_start = 9;
    idle->output_length = sizeof idle->output - 40;
    memset(idle->output + idle->output_start, 'x', idle->output_length);
    same_step(idle);
    discard_output(idle);
    drain(idle);
    /* Reader/editor modes retain the same unshown cursor. */
    IdleMode modes[] = {SCROLLBACK, CATCHUP, COMPOSE, COMMAND, REVIEW, READING, SETUP, CONTEXT_HELP};
    idle->shown_sequence = oldest;
    for (size_t i = 0; i < sizeof modes / sizeof modes[0]; ++i) {
        idle->mode = modes[i];
        same_step(idle);
        assert(idle->shown_sequence == oldest && !idle->output_length);
    }
    idle->mode = WATCH;
    drain(idle);
    dispose(idle);

    idle = fresh(1);
    StreamLine *formatted = stream_store(idle, 1, B_CHANNEL, "#test", 1, 0, 0,
        "prefix: [2026-11-16 12:34:56] [#test] <Other> caf\xc3\xa9");
    assert(formatted);
    formatted->body_offset = 8;
    same_step(idle);
    discard_output(idle);
    idle->client->slots[0].seed.output_full = 0;
    idle->shown_sequence = 1;
    same_step(idle);
    assert(!memcmp(idle->output, "[12:34:56] <Other> caf", 21));
    discard_output(idle);
    idle->display_kind = B_MENTIONS;
    idle->shown_sequence = 1;
    same_step(idle);
    assert(!memcmp(idle->output, "[12:34:56] [#test] <Other>", 25));
    discard_output(idle);
    idle->tty.utf8 = 0;
    idle->shown_sequence = 1;
    same_step(idle);
    assert(idle->output_length >= 3 && idle->output[idle->output_length - 3] == '?');
    dispose(idle);

    idle = fresh(UINT64_MAX - 40u);
    add_lines(idle, 80);
    /* The fast path must fall back to existing traversal across wrap. This
     * checks equivalence; it does not certify arbitrary 2^64-record sessions. */
    same_step(idle);
    dispose(idle);

    idle = fresh(1); /* Fresh/frontend reset uses a new sequence space. */
    add_lines(idle, 90);
    drain(idle);
    dispose(idle);
    puts("idle stream traversal: empty/quiet, gap/eviction, ring wrap, cleared text, filtered views, 64-record batches, output pressure, mode/format/encoding changes and counter-wrap fallback passed");
}

static void compact_cases(void)
{
    static const struct {
        const char *record, *compact;
        BufferKind kind;
    } cases[] = {
        {"[2024-01-02 03:04:05] [#test] <alice> hello",
         "[03:04:05] <alice> hello", B_CHANNEL},
        {"[2024-01-02 03:04:05] [history] [#test] <alice> hello",
         "[03:04:05] [history] <alice> hello", B_CHANNEL},
        {"[2024-01-02 03:04:05 UTC] [#test] <alice> hello",
         "[03:04:05 UTC] <alice> hello", B_CHANNEL},
        {"[2024-01-02 03:04:05 UTC] [history] [#test] <alice> hello",
         "[03:04:05 UTC] [history] <alice> hello", B_CHANNEL},
        {"[2024-01-02 03:04:05 UTC] [history] [#test] * alice waves",
         "[03:04:05 UTC] [history] * alice waves", B_CHANNEL},
        {"[2024-01-02 03:04:05 UTC] [#test] -alice- notice",
         "[03:04:05 UTC] -alice- notice", B_CHANNEL},
        {"[2024-01-02 03:04:05 UTC] [history] [alice] <alice> private",
         "[03:04:05 UTC] [history] <alice> private", B_QUERY},
        {"[2024-01-02 03:04:05 UTC] server message",
         "[03:04:05 UTC] server message", B_SERVER}
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        Idle *idle = fresh(1);
        char record[256], out[256];
        snprintf(record, sizeof record, "origin: %s", cases[i].record);
        StreamLine *line = stream_store(idle, 1, cases[i].kind, "#test", 1, 0, 0, record);
        assert(line);
        line->body_offset = 8;
        idle->display_kind = idle->catchup_kind = cases[i].kind;
        assert(!strcmp(stream_text(idle, line, out, sizeof out), record));
        assert(!strcmp(catchup_display_text(idle, cases[i].record, out, sizeof out), cases[i].record));
        idle->client->slots[0].seed.output_full = 0;
        assert(!strcmp(stream_text(idle, line, out, sizeof out), cases[i].compact));
        assert(!strcmp(catchup_display_text(idle, cases[i].record, out, sizeof out), cases[i].compact));
        /* Actual viewport measuring/drawing must use the compact form too. */
        idle->tty.cols = 80; idle->tty.rows = 20;
        assert(scrollback_total_rows(idle) == 1);
        idle->redraw = 1; idle->mode = SCROLLBACK;
        draw_scrollback(idle);
        assert(idle->output_length < sizeof idle->output);
        idle->output[idle->output_length] = 0;
        assert(strstr(idle->output, cases[i].compact));
        assert(!strstr(idle->output, "2024-01-02") && !strstr(idle->output, "origin:"));
        dispose(idle);
    }
    Idle *idle = fresh(1);
    idle->client->slots[0].seed.output_full = 0;
    char record[256], out[256];
    snprintf(record, sizeof record, "origin: %s", cases[3].record);
    StreamLine *line = stream_store(idle, 1, B_CHANNEL, "#test", 1, 0, 0, record);
    assert(line); line->body_offset = 8;
    idle->display_kind = B_MENTIONS;
    assert(!strcmp(stream_text(idle, line, out, sizeof out),
                   "[03:04:05 UTC] [history] [#test] <alice> hello"));
    /* Read a real private log snapshot through the catch-up viewport path. */
    char path[] = "/tmp/irc-compact-catchup-XXXXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && !unlink(path));
    int length = snprintf(record, sizeof record, "[#test] %s\n", cases[3].record);
    assert(length > 0 && (size_t)length < sizeof record);
    assert(write(fd, record, (size_t)length) == length);
    idle->catchup_fd = fd;
    idle->catchup_start = 0; idle->catchup_end = length;
    idle->catchup_kind = B_CHANNEL;
    strcpy(idle->catchup_target, "#test");
    idle->tty.cols = 80; idle->tty.rows = 20;
    idle->catchup_total = catchup_scan(idle, 0, 0, NULL, 0);
    assert(idle->catchup_total == 1);
    Frame frame = {{0}, 0, 0};
    assert(catchup_scan(idle, 0, 5, &frame, 1) == 1);
    assert(!frame.overflow && frame.used < sizeof frame.data);
    frame.data[frame.used] = 0;
    assert(strstr(frame.data, cases[3].compact) && !strstr(frame.data, "2024-01-02"));
    catchup_close(idle);
    dispose(idle);

    /* Unknown or short prefixes are preserved, without speculative offsets. */
    static const char *const malformed[] = {
        "", "[2024", "[2024-01-02 03:04:05 UTC]", "[2024-01-02 03:04:05 UTCX] text",
        "[2024-01-02 03:04:x5 UTC] text", "[2024-01-02 03:04:05 GMT] text"
    };
    idle = fresh(1);
    idle->client->slots[0].seed.output_full = 0;
    for (size_t i = 0; i < sizeof malformed / sizeof malformed[0]; ++i) {
        snprintf(record, sizeof record, "origin: %s", malformed[i]);
        line = stream_store(idle, 1, B_CHANNEL, "#test", 0, 0, 0, record);
        assert(line); line->body_offset = 8;
        assert(!strcmp(stream_text(idle, line, out, sizeof out), record));
        assert(!strcmp(catchup_display_text(idle, malformed[i], out, sizeof out), malformed[i]));
    }
    dispose(idle);
    puts("compact records: local/UTC timestamps, history, messages/actions/notices/queries, mentions, retained viewport, log catch-up viewport and malformed fallback passed");
}

static double benchmark(Idle *idle, void (*paint)(Idle *))
{
    struct timespec start, end;
    void (*volatile call)(Idle *) = paint;
    assert(!clock_gettime(CLOCK_MONOTONIC, &start));
    for (unsigned i = 0; i < 100000; ++i) call(idle);
    assert(!clock_gettime(CLOCK_MONOTONIC, &end));
    assert(!idle->output_length);
    return (double)(end.tv_sec - start.tv_sec) * 1000.0 +
           (double)(end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main(void)
{
    traversal_cases();
    compact_cases();
    Idle *idle = fresh(1);
    add_lines(idle, STREAM_LINES);
    idle->shown_sequence = idle->next_sequence;
    double old_ms = benchmark(idle, sequential_output);
    double new_ms = benchmark(idle, stream_output);
    printf("quiet retained-stream fixture: 100000 calls, %u records; sequential_ms=%.3f production_ms=%.3f\n", STREAM_LINES, old_ms, new_ms);
    dispose(idle);
    return 0;
}
