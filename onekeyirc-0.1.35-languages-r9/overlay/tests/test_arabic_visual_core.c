/* Arabic display regressions. All input is local; no terminal or network is
 * opened. Compile like test_idle_stream. Also build with IRC_NO_ARABIC_VISUAL
 * to verify the unavailable-loader path independently of the host libraries. */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "idle.c"
#include <assert.h>
#include <locale.h>

static const char beh_pair[] = "\xd8\xa8\xd8\xa8";
/* Two joined behs: final beh at the left, initial beh at the right. */
static const char beh_visual[] = "\xef\xba\x90\xef\xba\x91";

static void selected(int on)
{
    assert(setenv("ONEKEYIRC_LANG", "ar", 1) == 0);
    assert(setenv("VIPIRC_LANG", "ar", 1) == 0);
    assert(setenv("ONEKEYIRC_ARABIC_VISUAL", on ? "1" : "0", 1) == 0);
    assert(setenv("VIPIRC_ARABIC_VISUAL", on ? "1" : "0", 1) == 0);
}

static void decoder_cases(void)
{
    static const unsigned char invalid[][5] = {
        {0x80,0x80,0,0,0}, {0xbf,0xbf,0,0,0},
        {0xc0,0x80,0,0,0}, {0xc1,0xbf,0,0,0},
        {0xe0,0x80,0x80,0,0}, {0xed,0xa0,0x80,0,0},
        {0xf4,0x90,0x80,0x80,0}, {0xf5,0x80,0x80,0x80,0},
        {0xf8,0x90,0x80,0x80,0}, {0xff,0x80,0x80,0x80,0},
        {0xe2,0x28,0xa1,0,0}
    };
    uint32_t cp;
    char joined[32], output[64];
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        size_t n = strlen((const char *)invalid[i]);
        assert(!irc_arabic_decode((const char *)invalid[i], n, &cp));
        memcpy(joined, beh_pair, sizeof beh_pair - 1);
        memcpy(joined + sizeof beh_pair - 1, invalid[i], n);
        assert(!irc_arabic_visual_row(joined, sizeof beh_pair - 1 + n,
                                      output, sizeof output));
    }
    const char valid[] = "\xf0\x9f\x98\x80";
    for (size_t n = 1; n < 4; ++n)
        assert(!irc_arabic_decode(valid, n, &cp));
    assert(irc_arabic_decode(valid, 4, &cp) == 4 && cp == 0x1f600u);
    assert(!irc_arabic_decode(valid, 0, &cp));
    assert(irc_arabic_decode("A", 1, &cp) == 1 && cp == 'A');
    assert(irc_arabic_has_text(beh_pair, sizeof beh_pair - 1));
    assert(!irc_arabic_has_text("/help keys", 10));
}

static void shaping_cases(void)
{
    char output[8192] = {0}, source[2049];
    size_t n = irc_arabic_visual_row(beh_pair, sizeof beh_pair - 1,
                                    output, sizeof output);
    assert(n == sizeof beh_visual - 1 && !strcmp(output, beh_visual));
    const char mixed[] = "\xd9\x85\xd8\xab\xd9\x84 /help keys --resume 7 /away "
                         "\xd8\xa8\xd9\x84\xd8\xa7 \xd8\xb1\xd8\xb3\xd8\xa7\xd9\x84\xd8\xa9";
    n = irc_arabic_visual_row(mixed, sizeof mixed - 1, output, sizeof output);
    assert(n && strstr(output, "/help keys --resume 7 /away"));
    assert(!strstr(output, "\xe2\x80\x8e") && !strstr(output, "\xef\xbb\xbf"));
    assert(!irc_arabic_visual_row("/help keys", 10, output, sizeof output));
    assert(!irc_arabic_visual_row(NULL, 0, output, sizeof output));
    assert(!irc_arabic_visual_row(beh_pair, sizeof beh_pair - 1, NULL, 0));
    unsigned char guarded[16];
    memset(guarded, 0xa5, sizeof guarded);
    assert(!irc_arabic_visual_row(beh_pair, sizeof beh_pair - 1,
                                 (char *)guarded + 1, sizeof beh_visual - 1));
    assert(guarded[0] == 0xa5 && guarded[sizeof beh_visual] == 0xa5);
    n = irc_arabic_visual_row(beh_pair, sizeof beh_pair - 1,
                             (char *)guarded + 1, sizeof beh_visual);
    assert(n == sizeof beh_visual - 1 && guarded[sizeof beh_visual + 1] == 0xa5);
    memset(source, 'a', sizeof source);
    memcpy(source, beh_pair, sizeof beh_pair - 1);
    assert(!irc_arabic_visual_row(source, sizeof source, output, sizeof output));
    /* Maximum token/mark pressure stays bounded and cannot lose a token. */
    for (size_t i = 4; i + 1 < 2048; i += 2) { source[i] = 'a'; source[i + 1] = ' '; }
    n = irc_arabic_visual_row(source, 2048, output, sizeof output);
    assert(n);
    size_t letters = 0;
    for (size_t i = 0; i < n; ++i) letters += output[i] == 'a';
    assert(letters == 1022);
}

static Idle *fresh(void)
{
    Idle *idle = calloc(1, sizeof *idle);
    assert(idle);
    idle->client = calloc(1, sizeof *idle->client);
    assert(idle->client);
    idle->client->slots[0].seed.output_full = 1;
    idle->client->slots[0].seed.history_lines = STREAM_LINES;
    idle->client->session_notice_shown = 1;
    idle->mode = WATCH;
    idle->next_sequence = idle->shown_sequence = 1;
    idle->display_token = 1;
    idle->display_kind = B_CHANNEL;
    strcpy(idle->display_target, "#test");
    idle->tty.utf8 = 1;
    idle->tty.cols = 100;
    return idle;
}

static void dispose(Idle *idle)
{
    while (idle->count) stream_pop(idle);
    free(idle->client);
    free(idle);
}

static void check_rows(const Idle *idle, int width)
{
    int cells = 0;
    for (size_t at = 0; at < idle->output_length;) {
        if (idle->output[at] == '\r') {
            assert(at + 1 < idle->output_length && idle->output[at + 1] == '\n');
            assert(cells <= width);
            cells = 0; at += 2;
        } else {
            uint32_t cp;
            size_t n = irc_arabic_decode(idle->output + at, idle->output_length - at, &cp);
            assert(n && cp != 0 && cp != 0x1b);
            int span = wcwidth((wchar_t)cp);
            assert(span >= 0);
            cells += span; at += n;
        }
    }
    assert(!cells);
}

static void stream_cases(int available)
{
    Idle *idle = fresh();
    assert(stream_display_output(idle, "/help keys"));
    assert(idle->output_length == 12 && !memcmp(idle->output, "/help keys\r\n", 12));
    idle->output_length = 0;
    assert(stream_display_output(idle, beh_pair));
    const char *expected = available ? beh_visual : beh_pair;
    size_t length = strlen(expected);
    assert(idle->output_length == length + 2 && !memcmp(idle->output, expected, length));
    selected(0);
    idle->output_length = 0;
    assert(stream_display_output(idle, beh_pair));
    assert(idle->output_length == sizeof beh_pair + 1 &&
           !memcmp(idle->output, beh_pair, sizeof beh_pair - 1));
    selected(1);
    idle->output_length = 0;
    StreamLine *line = stream_store(idle, 1, B_CHANNEL, "#test", 0, 0, 0, beh_pair);
    assert(line && !strcmp(line->text, beh_pair));
    /* Force backpressure before the record, then retry with an empty queue. */
    idle->output_length = OUTPUT_BYTES - 1;
    memset(idle->output, 'x', idle->output_length);
    stream_output(idle);
    assert(idle->shown_sequence == 1 && idle->output_length == OUTPUT_BYTES - 1);
    idle->output_length = 0;
    stream_output(idle);
    assert(idle->shown_sequence == 2 && !strcmp(line->text, beh_pair));
    assert(idle->output_length == length + 2 && !memcmp(idle->output, expected, length));
    size_t previous = idle->output_length;
    stream_output(idle);
    assert(idle->output_length == previous);
    if (available) {
        char long_text[APP_TEXT];
        for (size_t i = 0; i < sizeof long_text - 5; ++i) long_text[i] = 'a';
        memcpy(long_text + sizeof long_text - 5, beh_pair, sizeof beh_pair);
        const int widths[] = {2, 12, 40, 100, 400};
        for (size_t i = 0; i < sizeof widths / sizeof widths[0]; ++i) {
            idle->output_length = 0;
            idle->tty.cols = widths[i];
            assert(stream_display_output(idle, long_text));
            check_rows(idle, widths[i] - 1);
            /* All original ASCII content survives very narrow wraps. */
            size_t letters = 0;
            for (size_t j = 0; j < idle->output_length; ++j) letters += idle->output[j] == 'a';
            assert(letters == sizeof long_text - 5);
        }
    }
    dispose(idle);
}

int main(void)
{
    if (!setlocale(LC_CTYPE, "C.UTF-8") && !setlocale(LC_CTYPE, "")) return 77;
    if (wcwidth((wchar_t)0xfe91u) != 1) {
        puts("Arabic visual core: UNAVAILABLE (UTF-8 character widths)");
        return 77;
    }
    selected(1);
    decoder_cases();
    int available = irc_arabic_fribidi() != NULL;
    if (available) shaping_cases();
    else {
        char output[64];
        assert(!irc_arabic_visual_row(beh_pair, sizeof beh_pair - 1, output, sizeof output));
    }
    stream_cases(available);
    puts(available ? "Arabic visual core: PASS (shaping, tokens, bounds, wrapped WATCH, backpressure, retained logical text)" :
                     "Arabic visual core: PASS (unavailable-loader logical fallback)");
    return 0;
}
