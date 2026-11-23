/* Deterministic mutation checks at the untrusted DCC offer boundary.
 * No connections or files are opened. Exact-sized inputs let ASan check
 * end-of-string reads; failed parses must leave the caller's output intact.
 * Successful offers must survive canonical serialization without changing
 * their meaning. This is a bounded corpus check, not coverage-guided fuzzing.
 */
#include "dcc.h"
#include "protocol.h"
#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t state = UINT32_C(0x76513bad);
static size_t accepted, rejected;

static uint32_t random_word(void)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static void check(const char *text, size_t length)
{
    char *input = malloc(length + 1u);
    assert(input);
    memcpy(input, text, length);
    input[length] = 0;
    struct { unsigned char left[16]; DccInfo value; unsigned char right[16]; } out;
    unsigned char before[sizeof out];
    memset(&out, 0xa5, sizeof out);
    memcpy(before, &out, sizeof out);
    unsigned char error[257];
    memset(error, 0x5a, sizeof error);
    size_t cap = random_word() % sizeof error;
    int ok = dcc_parse_send(input, &out.value, (char *)error, cap);
    assert(ok == 0 || ok == 1);
    assert(!memcmp(input, text, length) && !input[length]);
    assert(!memcmp(out.left, before, sizeof out.left));
    assert(!memcmp(out.right, before + sizeof out - sizeof out.right, sizeof out.right));
    for (size_t i = cap; i < sizeof error; ++i) assert(error[i] == 0x5a);
    if (cap) assert(memchr(error, 0, cap));
    if (!ok) {
        assert(!memcmp(&out, before, sizeof out));
        ++rejected;
    } else {
        const DccInfo *info = &out.value;
        struct in_addr address;
        char body[IRC_BODY_MAX + 1u];
        DccInfo repeated;
        assert(info->state == DCC_OFFERED && !info->id && !info->server_token);
        assert(!info->outgoing && !info->transferred && !info->acknowledged);
        assert(!info->nick[0] && !info->path[0] && !info->error[0]);
        assert(info->port && strlen(info->filename) <= DCC_NAME_MAX);
        assert(inet_pton(AF_INET, info->address, &address) == 1);
        int n = snprintf(body, sizeof body, "DCC SEND \"%s\" %" PRIu32 " %u %" PRIu32,
                         info->filename, ntohl(address.s_addr), (unsigned)info->port, info->size);
        assert(n > 0 && (size_t)n < sizeof body);
        assert(dcc_parse_send(body, &repeated, NULL, 0));
        assert(!strcmp(info->filename, repeated.filename));
        assert(!strcmp(info->address, repeated.address));
        assert(info->port == repeated.port && info->size == repeated.size);
        ++accepted;
    }
    free(input);
}

int main(void)
{
    static const char *const seeds[] = {
        "DCC SEND file 2130706433 9000 0",
        "DCC SEND file 2130706433 65535 4294967295",
        "DCC SEND \"file with spaces\" 167772161 1 17",
        "DCC SEND caf\xc3\xa9.txt 3232235777 42 1024",
        "DCC SEND file 0 0 0",
        "DCC SEND file 4294967296 65536 4294967296",
        "DCC SEND ../escape 2130706433 9000 1",
        "DCC SEND \"unterminated 2130706433 9000 1",
        "DCC SEND file 2130706433 9000 1 token",
        "DCC CHAT chat 2130706433 9000",
        "DCC SEND file 00000000000002130706433 009000 000001",
        "DCC SEND \"bad\033[201~name\" 2130706433 9000 1"
    };
    const size_t count = sizeof seeds / sizeof seeds[0];
    char input[IRC_BODY_MAX + 34u];
    for (size_t s = 0; s < count; ++s) {
        size_t length = strlen(seeds[s]);
        for (size_t i = 0; i <= length; ++i) check(seeds[s], i);
        for (size_t i = 0; i < length; ++i)
            for (unsigned byte = 1; byte <= 255; ++byte) {
                memcpy(input, seeds[s], length + 1u);
                input[i] = (char)byte;
                check(input, length);
            }
    }
    for (size_t round = 0; round < 250000u; ++round) {
        size_t length;
        if (round % 4u) {
            const char *seed = seeds[random_word() % count];
            length = strlen(seed);
            memcpy(input, seed, length + 1u);
            size_t mutations = 1u + random_word() % 7u;
            for (size_t i = 0; i < mutations; ++i) {
                size_t at = random_word() % (length + 1u);
                unsigned mode = random_word() % 3u;
                if (mode == 0 && length < sizeof input - 1u) {
                    memmove(input + at + 1u, input + at, length - at + 1u);
                    input[at] = (char)(1u + random_word() % 255u);
                    ++length;
                } else if (mode == 1 && at < length) {
                    memmove(input + at, input + at + 1u, length - at);
                    --length;
                } else if (at < length) input[at] = (char)(1u + random_word() % 255u);
            }
        } else {
            length = random_word() % sizeof input;
            for (size_t i = 0; i < length; ++i) input[i] = (char)(1u + random_word() % 255u);
            if (length >= 9u) memcpy(input, "DCC SEND ", 9u);
            input[length] = 0;
        }
        check(input, length);
    }
    assert(accepted && rejected);
    printf("DCC parser properties: %zu accepted, %zu rejected; input preservation, atomic failure, bounded diagnostics, and semantic roundtrip passed\n",
           accepted, rejected);
    return 0;
}
