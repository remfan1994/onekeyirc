/* DCC/editor strings end at a protected page, including truncated encodings.
 * No connection, credential, existing user file, or terminal is touched.
 * Build: cc -std=c11 -O2 -Isrc tests/test_runtime_guard_pages.c
 *        src/dcc.c src/editor.c src/protocol.c -o build/test_runtime_guard_pages
 */
#define _DEFAULT_SOURCE 1
#define _XOPEN_SOURCE 700
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif
#include "dcc.h"
#include "editor.h"
#include "protocol.h"

#include <sys/mman.h>
#include <arpa/inet.h>
#include <assert.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

typedef struct Guard {
    unsigned char *mapping, *end;
    size_t total, capacity;
} Guard;

static Guard input_guard, output_guard;
static size_t dcc_cases, editor_cases, accepted_dcc, accepted_editor;

static Guard allocate_guard(void)
{
    long page = sysconf(_SC_PAGESIZE);
    assert(page > 0);
    size_t size = (size_t)page;
    size_t capacity = ((ED_MAX_BYTES + 1024u + size - 1u) / size) * size;
    size_t total = capacity + 2u * size;
    unsigned char *mapping = mmap(NULL, total, PROT_NONE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(mapping != MAP_FAILED);
    assert(mprotect(mapping + size, capacity, PROT_READ | PROT_WRITE) == 0);
    return (Guard){mapping, mapping + size + capacity, total, capacity};
}

static char *guard_buffer(Guard *guard, size_t length)
{
    assert(length <= guard->capacity);
    return (char *)(guard->end - length);
}

static const char *guard_string(const unsigned char *text, size_t length)
{
    char *guarded = guard_buffer(&input_guard, length + 1u);
    if (length) memcpy(guarded, text, length);
    guarded[length] = 0;
    return guarded;
}

static void dcc_case(const unsigned char *text, size_t length)
{
    const char *input = guard_string(text, length);
    static const size_t capacities[] = {0, 1, 2, 16, 160};
    size_t capacity = capacities[dcc_cases % (sizeof capacities / sizeof capacities[0])];
    char *error = guard_buffer(&output_guard, capacity);
    if (capacity) memset(error, 0xa5, capacity);
    DccInfo out, original;
    memset(&out, 0xa5, sizeof out);
    original = out;
    int accepted = dcc_parse_send(input, &out, error, capacity);
    ++dcc_cases;
    if (capacity) assert(memchr(error, 0, capacity));
    if (!accepted) {
        assert(!memcmp(&out, &original, sizeof out));
        return;
    }
    ++accepted_dcc;
    size_t n = strlen(out.filename);
    assert(n && n <= DCC_NAME_MAX && out.port);
    assert(out.state == DCC_OFFERED && !out.id && !out.server_token);
    assert(!out.outgoing && !out.transferred && !out.acknowledged);
    assert(out.filename[0] != '.' && out.filename[0] != ' ' && out.filename[n - 1] != ' ');
    assert(!strchr(out.filename, '/') && !strchr(out.filename, '\\') && !strchr(out.filename, '"'));
    assert(irc_utf8_valid(out.filename));
    char safe[DCC_NAME_MAX + 1];
    assert(irc_sanitize(safe, sizeof safe, out.filename) == n && !strcmp(safe, out.filename));
    struct in_addr address;
    assert(inet_pton(AF_INET, out.address, &address) == 1);
    assert(ntohl(address.s_addr) && (ntohl(address.s_addr) >> 24) < 224u);
    if (capacity) assert(!*error);
}

static void editor_case(const unsigned char *text, size_t length)
{
    const char *input = guard_string(text, length);
    Editor editor, original;
    ed_init(&editor);
    assert(ed_from_utf8(&editor, "preserve this draft"));
    ed_select_all(&editor);
    original = editor;
    int accepted = ed_from_utf8(&editor, input);
    ++editor_cases;
    if (!accepted) {
        assert(!memcmp(&editor, &original, sizeof editor));
        return;
    }
    ++accepted_editor;
    assert(editor.len <= ED_MAX_CHARS && editor.bytes <= ED_MAX_BYTES);
    assert(editor.bytes == length && editor.pos == editor.len);
    assert(!editor.data[editor.len] && !editor.selection_all);
    char *output = guard_buffer(&output_guard, editor.bytes);
    if (editor.bytes) memset(output, 0xa5, editor.bytes);
    assert(ed_to_utf8(&editor, output, editor.bytes) == SIZE_MAX);
    output = guard_buffer(&output_guard, editor.bytes + 1u);
    assert(ed_to_utf8(&editor, output, editor.bytes + 1u) == length);
    assert(!memcmp(output, input, length + 1u));
    int row, col, rows;
    ed_layout(&editor, 1, &rows, &row, &col);
    assert(rows >= 1 && rows <= ED_MAX_CHARS + 1 && row >= 0 && row < rows && col == 0);
    ed_move_vertical(&editor, INT_MIN, 1);
    ed_move_vertical(&editor, INT_MAX, 1);
    ed_set_cell(&editor, INT_MAX, INT_MIN, 1);
    assert(editor.pos <= editor.len);
}

static void prefixes(void)
{
    static const char *const corpus[] = {
        "DCC SEND report.txt 2130706433 65535 4294967295",
        "DCC SEND \"two words.txt\" 2130706433 1 0",
        "DCC SEND caf\xc3\xa9.txt 2130706433 9000 15",
        "DCC SEND file 2130706433 9000 4294967296",
        "DCC SEND file 4294967296 9000 1",
        "DCC SEND file 2130706433 65536 1",
        "DCC SEND file 2130706433 9000 1 1234",
        "DCC SEND ../file 2130706433 9000 1",
        "DCC SEND \"\" 2130706433 9000 1",
        "DCC SEND \"file 2130706433 9000 1",
        "DCC SEND \033]52;c;secret\007 2130706433 9000 1",
        "DCC SEND \00312,4file 2130706433 9000 1",
        "DCC SEND \004ffffff,000000file 2130706433 9000 1",
        "DCC SEND \xe2\x80\xae" "file 2130706433 9000 1",
        "\xf0\x9f\x98\x80\xe4\xb8\xad" "A\xcc\x81\nlast line",
        "\xf4\x90\x80\x80", "\xed\xa0\x80", "\xc0\xaf", "\xe0\x80\xaf",
        "\xe2\x80\xa8", "\xc2\x9b", "\x1b[201~\n/quit"
    };
    for (size_t i = 0; i < sizeof corpus / sizeof corpus[0]; ++i)
        for (size_t n = 0; n <= strlen(corpus[i]); ++n) {
            dcc_case((const unsigned char *)corpus[i], n);
            editor_case((const unsigned char *)corpus[i], n);
        }
}

static void byte_pairs(void)
{
    unsigned char filename[128];
    const char prefix[] = "DCC SEND ", suffix[] = " 2130706433 9000 1";
    memcpy(filename, prefix, sizeof prefix - 1u);
    for (unsigned a = 1; a <= 255; ++a) {
        unsigned char bytes[2] = {(unsigned char)a, 0};
        editor_case(bytes, 1);
        filename[sizeof prefix - 1u] = bytes[0];
        memcpy(filename + sizeof prefix, suffix, sizeof suffix - 1u);
        dcc_case(filename, sizeof prefix + sizeof suffix - 1u);
        for (unsigned b = 1; b <= 255; ++b) {
            bytes[1] = (unsigned char)b;
            editor_case(bytes, 2);
            filename[sizeof prefix] = bytes[1];
            memcpy(filename + sizeof prefix + 1u, suffix, sizeof suffix - 1u);
            dcc_case(filename, sizeof prefix + sizeof suffix);
        }
    }
}

static void long_boundaries(void)
{
    unsigned char text[ED_MAX_BYTES + 2u];
    memset(text, 'a', sizeof text);
    static const size_t lengths[] = {0, 1, 254, 255, 256, 509, 510, 511,
        ED_MAX_CHARS - 1u, ED_MAX_CHARS, ED_MAX_CHARS + 1u,
        ED_MAX_BYTES - 1u, ED_MAX_BYTES, ED_MAX_BYTES + 1u};
    for (size_t i = 0; i < sizeof lengths / sizeof lengths[0]; ++i) {
        editor_case(text, lengths[i]);
        dcc_case(text, lengths[i]);
    }
    for (size_t n = 254; n <= 256; ++n) {
        memcpy(text, "DCC SEND ", 9);
        memset(text + 9, 'f', n);
        const char suffix[] = " 2130706433 9000 1";
        memcpy(text + 9 + n, suffix, sizeof suffix - 1u);
        dcc_case(text, 9 + n + sizeof suffix - 1u);
    }
    for (size_t i = 0; i < ED_MAX_BYTES; i += 4u)
        memcpy(text + i, "\xf0\x9f\x98\x80", 4);
    editor_case(text, ED_MAX_BYTES - 1u);
    editor_case(text, ED_MAX_BYTES);
    text[ED_MAX_BYTES] = 'x';
    editor_case(text, ED_MAX_BYTES + 1u);
}

int main(void)
{
    alarm(30);
    assert(setlocale(LC_CTYPE, "C.UTF-8") || setlocale(LC_CTYPE, "en_US.UTF-8"));
    input_guard = allocate_guard();
    output_guard = allocate_guard();
    prefixes();
    byte_pairs();
    long_boundaries();
    assert(munmap(input_guard.mapping, input_guard.total) == 0);
    assert(munmap(output_guard.mapping, output_guard.total) == 0);
    printf("runtime guard pages: DCC cases=%zu accepted=%zu; editor cases=%zu accepted=%zu; protected NUL/capacity boundaries and transactional rejection passed\n",
           dcc_cases, accepted_dcc, editor_cases, accepted_editor);
    return 0;
}
