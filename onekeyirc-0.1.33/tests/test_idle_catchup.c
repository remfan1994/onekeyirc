/* Exercise the production disk reader against a sequential snapshot oracle.
 * All files are temporary; no terminal, network connection or session endpoint
 * is needed. Counting fgets calls also guards against full rescans on paging. */
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif
#define _XOPEN_SOURCE 700
#include <stdio.h>
static char *counted_fgets(char *, int, FILE *);
#define fgets counted_fgets
#include "idle.c"
#undef fgets
#include <assert.h>

static size_t reader_calls, reader_bytes;

static char *counted_fgets(char *buffer, int capacity, FILE *file)
{
    ++reader_calls;
    char *result = fgets(buffer, capacity, file);
    if (result) reader_bytes += strlen(buffer);
    return result;
}

static void write_bytes(int fd, const char *bytes, size_t size)
{
    while (size) {
        ssize_t count = write(fd, bytes, size);
        assert(count > 0);
        bytes += count; size -= (size_t)count;
    }
}

static int temporary_file(void)
{
    char path[] = "/tmp/onekey-catchup-XXXXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && unlink(path) == 0);
    return fd;
}

static Idle *new_reader(int fd, int64_t start, int64_t end)
{
    Idle *idle = calloc(1, sizeof *idle);
    assert(idle);
    idle->client = calloc(1, sizeof *idle->client);
    assert(idle->client);
    idle->catchup_fd = dup(fd);
    assert(idle->catchup_fd >= 0);
    idle->catchup_start = start; idle->catchup_end = end;
    idle->catchup_kind = B_CHANNEL;
    memcpy(idle->catchup_target, "#test", sizeof "#test");
    idle->tty.rows = 20; idle->tty.cols = 80; idle->tty.utf8 = 1;
    idle->mode = CATCHUP;
    return idle;
}

static void free_reader(Idle *idle)
{
    catchup_close(idle);
    free(idle->client); free(idle);
}

/* Intentionally independent file traversal: read the fixed byte interval,
 * split whole physical records, and always walk from the anchor. Reuse the
 * production sanitizer/wrapper so this oracle isolates indexing correctness. */
static size_t sequential(Idle *idle, size_t first, size_t room, Frame *frame)
{
    if (idle->catchup_end <= idle->catchup_start) return 0;
    size_t size = (size_t)(idle->catchup_end - idle->catchup_start), used = 0;
    char *bytes = malloc(size + 1);
    assert(bytes);
    while (used < size) {
        ssize_t count = pread(idle->catchup_fd, bytes + used, size - used,
                              (off_t)(idle->catchup_start + (int64_t)used));
        assert(count >= 0);
        if (!count) break;
        used += (size_t)count;
    }
    bytes[used] = 0;
    char prefix[256], shown[APP_TEXT + 2048];
    int prefix_size = snprintf(prefix, sizeof prefix, "[%s] ", idle->catchup_target);
    assert(prefix_size > 0 && (size_t)prefix_size < sizeof prefix);
    size_t rows = 0, rendered = 0;
    for (size_t at = 0; at < used;) {
        char *newline = memchr(bytes + at, '\n', used - at);
        size_t end = newline ? (size_t)(newline - bytes) : used;
        size_t length = end - at;
        char *record = bytes + at;
        at = end + (newline ? 1u : 0u);
        if (length + (newline ? 1u : 0u) >= APP_TEXT + 4096u) continue;
        bytes[end] = 0;
        while (length && record[length - 1] == '\r') record[--length] = 0;
        if (strncmp(record, prefix, (size_t)prefix_size)) continue;
        const char *text = catchup_display_text(idle, record + prefix_size, shown, sizeof shown);
        size_t text_size = strlen(text), cursor = 0;
        if (!text_size) {
            if (rows >= first && rendered < room) ++rendered;
            ++rows;
        }
        while (cursor < text_size) {
            int visible = frame && rows >= first && rendered < room;
            size_t next = text_row(idle, text, text_size, cursor, visible ? frame : NULL,
                                   2 + (int)rendered);
            if (visible) ++rendered;
            ++rows;
            assert(next > cursor);
            cursor = next;
        }
    }
    free(bytes);
    return rows;
}

static void compare_page(Idle *idle, size_t first, size_t room)
{
    Frame actual = {{0}, 0, 0}, expected = {{0}, 0, 0};
    size_t total = catchup_prepare(idle);
    size_t expected_total = sequential(idle, first, room, &expected);
    assert(total == expected_total);
    assert(catchup_scan(idle, first, room, &actual, 2) != SIZE_MAX);
    assert(!actual.overflow && !expected.overflow && actual.used == expected.used);
    assert(!memcmp(actual.data, expected.data, actual.used));
    assert(actual.used < sizeof actual.data);
    actual.data[actual.used] = 0;
    assert(!strstr(actual.data, "\033]52;") && !strstr(actual.data, "\033[2J"));
    assert(!strchr(actual.data, '\a') && !strstr(actual.data, "FAKE-RECORD-IN-LONG-LINE"));
}

static void assert_points(Idle *idle)
{
    assert(idle->catchup_point_count <= CATCHUP_POINTS);
    for (size_t i = 0; i < idle->catchup_point_count; ++i) {
        const CatchupPoint *point = &idle->catchup_points[i];
        assert(point->offset >= idle->catchup_start && point->offset < idle->catchup_end);
        if (i) {
            char previous;
            assert(point->offset > idle->catchup_points[i - 1].offset);
            assert(point->row >= idle->catchup_points[i - 1].row);
            assert(pread(idle->catchup_fd, &previous, 1, (off_t)point->offset - 1) == 1);
            assert(previous == '\n');
        }
    }
}

static void make_mixed_file(int fd, int64_t *start, int64_t *end)
{
    FILE *file = fdopen(dup(fd), "w");
    assert(file);
    assert(fputs("[#other] ignored before anchor\n", file) >= 0);
    *start = (int64_t)ftello(file);
    assert(fputs("[#test] [2026-11-15 12:00:00] <me> ANCHOR\n", file) >= 0);
    for (unsigned i = 0; i < 2048; ++i) {
        if (!(i % 17)) {
            assert(fputs("[#other] other channel\n[#testing] prefix must not match\n[alice] private query\n", file) >= 0);
        }
        if (!(i % 31)) assert(fputs("[#test] \n", file) >= 0);
        if (i == 777) {
            size_t size = 3 * (APP_TEXT + 4096u - 1u) + 70u;
            char *long_line = malloc(size);
            assert(long_line);
            memset(long_line, 'z', size);
            memcpy(long_line, "[#test] ", 8);
            const char fake[] = "[#test] FAKE-RECORD-IN-LONG-LINE";
            memcpy(long_line + APP_TEXT + 4096u - 1u, fake, sizeof fake - 1u);
            memcpy(long_line + 2 * (APP_TEXT + 4096u - 1u), fake, sizeof fake - 1u);
            long_line[size - 1] = '\n';
            assert(fwrite(long_line, 1, size, file) == size);
            free(long_line);
        }
        assert(fprintf(file, "[#test] [2026-11-15 12:00:00] [origin] <alice> ROW-%04u ", i) > 0);
        for (unsigned j = 0; j < 50; ++j)
            assert(fputs(i % 3 ? "abcdefgh" : "\347\225\214e\314\201w", file) >= 0);
        if (!(i % 43)) assert(fputs("\033]52;c;BAD\a\033[2J\00304red\017", file) >= 0);
        assert(fputs(i % 2 ? "\n" : "\r\n", file) >= 0);
    }
    assert(fputs("[#test] partial snapshot ending", file) >= 0);
    *end = (int64_t)ftello(file);
    assert(*start > 0 && *end > *start && *end < APP_PERSISTENT_LOG_MAX);
    assert(fclose(file) == 0);
}

static void mixed_navigation_and_invalidation(const char *utf8)
{
    int fd = temporary_file();
    int64_t start, end;
    make_mixed_file(fd, &start, &end);
    Idle *idle = new_reader(fd, start, end);
    reader_calls = reader_bytes = 0;
    size_t total = catchup_prepare(idle);
    assert(total > 10000 && reader_calls > 2000 && idle->catchup_point_count > 20);
    assert_points(idle);
    size_t starts[] = {0, 1, 2, total / 3, total / 2, total - 20, total - 1, total, SIZE_MAX};
    for (size_t i = 0; i < sizeof starts / sizeof starts[0]; ++i)
        compare_page(idle, starts[i], i % 3 ? 18 : 1);
    for (size_t i = 1; i < idle->catchup_point_count; i += 5) {
        size_t row = idle->catchup_points[i].row;
        compare_page(idle, row, 18);
        if (row) compare_page(idle, row - 1, 18);
    }

    reader_calls = reader_bytes = 0;
    assert(catchup_prepare(idle) == total && reader_calls == 0);
    key_event(idle, IK_END);
    assert(idle->catchup_row == total - catchup_page_rows(idle) && reader_calls == 0);
    key_event(idle, IK_DOWN); key_event(idle, IK_PAGE_DOWN);
    assert(idle->catchup_row == total - catchup_page_rows(idle) && reader_calls == 0);
    Frame newest = {{0}, 0, 0};
    assert(catchup_scan(idle, idle->catchup_row, 18, &newest, 2) != SIZE_MAX);
    assert(reader_bytes < 128 * 1024u);
    key_event(idle, IK_HOME); key_event(idle, IK_UP); key_event(idle, IK_PAGE_UP);
    assert(idle->catchup_row == 0);
    key_event(idle, IK_PAGE_DOWN);
    assert(idle->catchup_row == catchup_page_rows(idle));

    reader_calls = 0;
    idle->tty.rows = 15;
    assert(catchup_prepare(idle) == total && reader_calls == 0);
    idle->tty.cols = 23;
    assert(catchup_prepare(idle) > total && reader_calls > 0);
    compare_page(idle, idle->catchup_total / 2, 13);
    idle->tty.cols = 80;
    reader_calls = 0;
    compare_page(idle, 0, 13);
    assert(reader_calls > 0);

    reader_calls = 0;
    idle->client->slots[0].seed.output_full = 1;
    compare_page(idle, 0, 13);
    assert(reader_calls > 2000);
    idle->client->slots[0].seed.output_full = 0;
    compare_page(idle, 0, 13);
    reader_calls = 0;
    assert(setlocale(LC_CTYPE, "C"));
    compare_page(idle, 0, 13);
    assert(reader_calls > 2000);
    assert(setlocale(LC_CTYPE, utf8));
    compare_page(idle, 0, 13);
    reader_calls = 0;
    idle->tty.utf8 = 0;
    compare_page(idle, 0, 13);
    assert(reader_calls > 2000);
    idle->tty.utf8 = 1;
    total = catchup_prepare(idle);

    Frame before = {{0}, 0, 0}, after = {{0}, 0, 0};
    assert(catchup_scan(idle, total - 13, 13, &before, 2) != SIZE_MAX);
    assert(lseek(fd, 0, SEEK_END) == (off_t)end);
    const char appended[] = " MODIFIED-TAIL\n[#test] APPENDED-AFTER-SNAPSHOT\n";
    write_bytes(fd, appended, sizeof appended - 1);
    reader_calls = 0;
    assert(catchup_prepare(idle) == total && reader_calls == 0 && idle->catchup_end == end);
    assert(catchup_scan(idle, total - 13, 13, &after, 2) != SIZE_MAX);
    assert(before.used == after.used && !memcmp(before.data, after.data, before.used));
    compare_page(idle, total - 13, 13);

    /* A known same-size rewrite must refresh the row map, including when
     * the filesystem exposes only coarse timestamp precision. */
    struct stat st;
    assert(fstat(fd, &st) == 0);
    assert(pwrite(fd, "#skip", 5, (off_t)start + 1) == 5);
    struct timespec times[2] = {{0, UTIME_OMIT}, {st.st_mtim.tv_sec + 1, st.st_mtim.tv_nsec}};
    assert(futimens(fd, times) == 0);
    reader_calls = 0;
    assert(catchup_prepare(idle) < total && reader_calls > 2000);
    compare_page(idle, 0, 13);

    int64_t shorter = idle->catchup_points[idle->catchup_point_count / 3].offset;
    assert(shorter > start && shorter < end && ftruncate(fd, (off_t)shorter) == 0);
    reader_calls = 0;
    total = catchup_prepare(idle);
    assert(reader_calls > 0 && idle->catchup_end == shorter);
    compare_page(idle, total - 13, 13);
    idle->catchup_row = SIZE_MAX; idle->redraw = 1;
    draw_catchup(idle);
    assert(idle->catchup_row == total - catchup_page_rows(idle));
    idle->output_length = idle->output_start = 0;
    assert(lseek(fd, 0, SEEK_END) == (off_t)shorter);
    write_bytes(fd, appended, sizeof appended - 1);
    reader_calls = 0;
    assert(catchup_prepare(idle) == total && reader_calls == 0 && idle->catchup_end == shorter);
    compare_page(idle, total - 13, 13);

    catchup_close(idle);
    assert(!idle->catchup_index_valid && !idle->catchup_point_count && !idle->catchup_total);
    assert(!catchup_prepare(idle));
    int replacement = temporary_file();
    const char fresh[] = "[#test] NEW-SNAPSHOT\n";
    write_bytes(replacement, fresh, sizeof fresh - 1);
    idle->catchup_fd = dup(replacement);
    idle->catchup_start = 0; idle->catchup_end = sizeof fresh - 1;
    memcpy(idle->catchup_target, "#test", sizeof "#test");
    idle->catchup_kind = B_CHANNEL;
    reader_calls = 0;
    assert(catchup_prepare(idle) == 1 && reader_calls > 0);
    compare_page(idle, 0, 13);
    assert(ftruncate(replacement, 0) == 0);
    assert(!catchup_prepare(idle) && !idle->catchup_end);
    key_event(idle, IK_END); key_event(idle, IK_DOWN); key_event(idle, IK_PAGE_DOWN);
    assert(!idle->catchup_row);
    free_reader(idle);
    close(replacement); close(fd);
}

static void maximum_snapshot_bound(void)
{
    int fd = temporary_file();
    char record[1024];
    memset(record, 'x', sizeof record);
    memcpy(record, "[#other] ", 9); record[sizeof record - 1] = '\n';
    for (size_t i = 0; i < APP_PERSISTENT_LOG_MAX / sizeof record; ++i)
        write_bytes(fd, record, sizeof record);
    Idle *idle = new_reader(fd, 0, APP_PERSISTENT_LOG_MAX);
    assert(!catchup_prepare(idle));
    assert_points(idle);
    assert(idle->catchup_point_count > 400);
    assert(sizeof idle->catchup_points <= 8192);
    reader_calls = 0;
    compare_page(idle, 0, 18);
    assert(reader_calls == 0);
    free_reader(idle); close(fd);
}

static void durable_anchor_identity(void)
{
    char directory[] = "/tmp/onekey-anchor-XXXXXX";
    char config[CONFIG_PATH_MAX], path[CONFIG_PATH_MAX], archive[CONFIG_PATH_MAX];
    assert(mkdtemp(directory));
    assert(snprintf(config, sizeof config, "%s/config.ini", directory) > 0);
    assert(snprintf(path, sizeof path, "%s/config.ini.log", directory) > 0);
    assert(snprintf(archive, sizeof archive, "%s/archived.log", directory) > 0);
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    assert(fd >= 0);
    const char before[] = "[#other] before the anchor\n";
    const char prefix[] = "[#test] [2026-11-16 00:00:00] [#test] <me> ";
    const char after[] = "[#test] later incoming record\n";
    const char added[] = "[#test] another incoming append\n";
    char record[2048];
    size_t record_length = sizeof prefix - 1u;
    memcpy(record, prefix, record_length);
    memset(record + record_length, 'A', 1800); record_length += 1800;
    memcpy(record + record_length, " SENT-ANCHOR\n", sizeof " SENT-ANCHOR\n");
    record_length += sizeof " SENT-ANCHOR\n" - 1u;
    assert(record_length == 1856);
    write_bytes(fd, before, sizeof before - 1u);
    write_bytes(fd, record, record_length);
    write_bytes(fd, after, sizeof after - 1u);
    struct stat original;
    assert(fstat(fd, &original) == 0);

    Idle *idle = new_reader(fd, 0, (int64_t)original.st_size);
    catchup_close(idle); idle->mode = COMMAND;
    App *app = calloc(1, sizeof *app); assert(app);
    idle->client->slots[0].used = 1; idle->client->slots[0].token = 1;
    idle->client->slots[0].app = app;
    idle->compose_token = 1; idle->compose_kind = B_CHANNEL;
    memcpy(idle->compose_target, "#test", sizeof "#test");
    app->cfg.persistent_logging = 1; app->cfg.history_lines = 100;
    memcpy(app->config_path, config, strlen(config) + 1u);
    app->buffer_count = 1;
    Buffer *anchor = &app->buffers[0];
    anchor->kind = B_CHANNEL; memcpy(anchor->name, "#test", sizeof "#test");
    anchor->last_own_log_valid = 1;
    anchor->last_own_log_offset = sizeof before - 1u;
    anchor->last_own_log_device = original.st_dev;
    anchor->last_own_log_inode = original.st_ino;
    anchor->last_own_log_length = record_length;
    /* Independent known FNV-1a value of the exact record above. Production
     * logger capture is exercised by test_app's real sent-composition case. */
    anchor->last_own_log_checksum = UINT64_C(4494413781395486186);
    assert(app_catchup_log_end(anchor, fd) == (int64_t)original.st_size);
    assert(begin_catchup(idle) && idle->mode == CATCHUP);
    assert(idle->catchup_start == anchor->last_own_log_offset &&
           idle->catchup_end == (int64_t)original.st_size);
    catchup_close(idle); idle->mode = COMMAND;

    /* Ordinary append must preserve both the sent anchor and a new end. */
    write_bytes(fd, added, sizeof added - 1u);
    int64_t appended_end = (int64_t)original.st_size + (int64_t)sizeof added - 1;
    assert(begin_catchup(idle) && idle->catchup_end == appended_end);
    catchup_close(idle); idle->mode = COMMAND;

    /* Even identical bytes in a replacement inode are not the original log. */
    assert(rename(path, archive) == 0);
    int replacement = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    assert(replacement >= 0);
    write_bytes(replacement, before, sizeof before - 1u);
    write_bytes(replacement, record, record_length);
    write_bytes(replacement, after, sizeof after - 1u);
    write_bytes(replacement, added, sizeof added - 1u);
    struct stat replaced; assert(fstat(replacement, &replaced) == 0);
    assert(replaced.st_dev != original.st_dev || replaced.st_ino != original.st_ino);
    assert(app_catchup_log_end(anchor, replacement) < 0);
    assert(!begin_catchup(idle) && idle->mode == COMMAND && idle->catchup_fd < 0);
    assert(strstr(idle->notice, "durable sent-message anchor is unavailable"));

    /* An unrelated same-channel replacement falls back to the retained
     * original own record, not to the replacement's matching channel prefix. */
    assert(ftruncate(replacement, 0) == 0 && lseek(replacement, 0, SEEK_SET) == 0);
    const char substituted[] = "[#test] <other> REPLACEMENT-LOG-WITHOUT-SENT-ANCHOR\n";
    write_bytes(replacement, substituted, sizeof substituted - 1u);
    assert(close(replacement) == 0);
    record[record_length - 1u] = 0;
    assert(stream_store(idle, 1, B_CHANNEL, "#test", 0, 0, 1, record + 8));
    record[record_length - 1u] = '\n';
    assert(stream_store(idle, 1, B_CHANNEL, "#test", 0, 0, 0, "later retained incoming record"));
    anchor->last_own_retained = 1;
    idle->tty.rows = 40; idle->tty.cols = 100;
    assert(begin_catchup(idle) && idle->mode == SCROLLBACK && idle->catchup_fd < 0);
    assert(idle->display_token == 1 && idle->display_kind == B_CHANNEL && !strcmp(idle->display_target, "#test"));
    draw_scrollback(idle);
    assert(idle->output_length < sizeof idle->output);
    idle->output[idle->output_length] = 0;
    assert(strstr(idle->output, "SENT-ANCHOR") && strstr(idle->output, "later retained incoming record"));
    assert(!strstr(idle->output, "REPLACEMENT-LOG"));
    while (idle->count) stream_pop(idle);
    anchor->last_own_retained = 0;
    idle->output_start = idle->output_length = 0;
    idle->mode = COMMAND;
    assert(unlink(path) == 0 && rename(archive, path) == 0);

    /* Same inode, same prefix and same size, but a changed record tail past
     * the checksum reader's first block: old byte positions must not pass. */
    assert(ftruncate(fd, 0) == 0 && lseek(fd, 0, SEEK_SET) == 0);
    record[1500] = 'B';
    write_bytes(fd, before, sizeof before - 1u);
    write_bytes(fd, record, record_length);
    write_bytes(fd, after, sizeof after - 1u);
    write_bytes(fd, added, sizeof added - 1u);
    assert(fstat(fd, &replaced) == 0 && replaced.st_dev == original.st_dev &&
           replaced.st_ino == original.st_ino && (int64_t)replaced.st_size == appended_end);
    assert(!begin_catchup(idle) && idle->mode == COMMAND && idle->catchup_fd < 0);
    write_bytes(fd, added, sizeof added - 1u);
    assert(!begin_catchup(idle)); /* Growth cannot make the changed anchor valid. */
    assert(ftruncate(fd, (off_t)anchor->last_own_log_offset + 10) == 0);
    assert(!begin_catchup(idle)); /* A partially truncated anchor is refused. */

    assert(close(fd) == 0 && unlink(path) == 0 && rmdir(directory) == 0);
    free(app); free_reader(idle);
}

int main(void)
{
    const char *chosen = setlocale(LC_CTYPE, "C.UTF-8");
    if (!chosen) chosen = setlocale(LC_CTYPE, "en_US.UTF-8");
    if (!chosen) chosen = setlocale(LC_CTYPE, "UTF-8");
    assert(chosen);
    char utf8[128];
    assert(strlen(chosen) < sizeof utf8);
    memcpy(utf8, chosen, strlen(chosen) + 1);
    mixed_navigation_and_invalidation(utf8);
    maximum_snapshot_bound();
    durable_anchor_identity();
    puts("idle catch-up: indexed/sequential pages, bounded work, filtering, long records, Unicode, layout/locale/format changes, append/rewrite/truncate, snapshot reset, durable file/record identity and original memory fallback passed");
    return 0;
}
