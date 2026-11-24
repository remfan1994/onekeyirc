/* Security boundaries of optional localization + Arabic RTL source patch.
 * This program has no network, terminal or filesystem side effects. */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "protocol.h"
#include "arabic_visual.h"

static void check_blocked(uint32_t cp)
{
    char text[40] = "A", encoded[4], display[64], row[128];
    size_t size = irc_arabic_encode(cp, encoded);
    memcpy(text+1, encoded, size);
    text[1+size] = 'B'; text[2+size] = 0;
    assert(irc_utf8_valid(text));
    assert(irc_sanitize(display, sizeof display, text) == 3);
    assert(!strcmp(display, cp == 0x2028 || cp == 0x2029 ? "A B" : "A?B"));
    assert(!irc_arabic_layout_scalar(cp));
    assert(irc_arabic_visual_row(text, strlen(text), row, sizeof row) == 0);
    {
        char channel[40] = "#A";
        memcpy(channel+2, encoded, size);
        channel[2+size] = 'B'; channel[3+size] = 0;
        assert(!irc_valid_channel(channel));
    }
}

int main(void)
{
    static const uint32_t bad[] = {
        0x061c, 0x180e, 0x200e, 0x200f, 0x2028, 0x2029,
        0x202a, 0x202b, 0x202c, 0x202d, 0x202e,
        0x2066, 0x2067, 0x2068, 0x2069,
        0x206a, 0x206b, 0x206c, 0x206d, 0x206e, 0x206f,
        0xfeff, 0xfff9, 0xfffa, 0xfffb
    };
    char text[64], safe[128];
    (void)setlocale(LC_ALL, "C.UTF-8");
    (void)setenv("VIPIRC_ARABIC_VISUAL", "1", 1);
    (void)setenv("ONEKEYIRC_ARABIC_VISUAL", "1", 1);
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i)
        check_blocked(bad[i]);
    /* Logical Arabic text and legitimate join controls are preserved. */
    strcpy(text, "\xd8\xa8\xe2\x80\x8d\xd8\xa8");
    assert(irc_sanitize(safe, sizeof safe, text) == strlen(text));
    assert(!strcmp(text, safe));
    /* Raw C0/C1 controls never reach the terminal unescaped. */
    strcpy(text, "A\033" "B\xc2\x9b" "C");
    assert(irc_sanitize(safe, sizeof safe, text) == 5);
    assert(!strcmp(safe, "A?B?C"));
    printf("RTL localization security: %zu controls rejected; logical text retained PASS\n",
           sizeof bad / sizeof bad[0]);
    return 0;
}
