#ifndef IRC_ARABIC_VISUAL_H
#define IRC_ARABIC_VISUAL_H
/* Optional visual-only shaping for simple LTR terminal emulators.
 * The original logical UTF-8 is retained everywhere except the output frame.
 * A system FriBidi library is loaded on demand; no headers, link-time library
 * or persistent text transformation are required.  If unavailable, output
 * remains unchanged.  Call only on sanitized, bounded terminal lines.
 * This does not implement right-to-left editing or the ncurses frontend. */
#ifndef IRC_NO_ARABIC_VISUAL
#include <dlfcn.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Match FriBidiChar, FriBidiStrIndex, FriBidiParType and FriBidiLevel from
 * the public 1.x ABI. FriBidiLevel is signed char, including the return. */
typedef signed char (*IrcFribidiLog2Vis)(const uint32_t *, int32_t,
                                           uint32_t *, uint32_t *,
                                           int32_t *, int32_t *, signed char *);

static int irc_arabic_visual_selected(void)
{
    const char *enabled;
    const char *lang;
#ifdef ONEKEY_ONLY
    enabled = getenv("ONEKEYIRC_ARABIC_VISUAL");
    if (!enabled || !*enabled) enabled = getenv("VIPIRC_ARABIC_VISUAL");
    lang = getenv("ONEKEYIRC_LANG");
    if (!lang || !*lang) lang = getenv("VIPIRC_LANG");
#else
    enabled = getenv("VIPIRC_ARABIC_VISUAL");
    if (!enabled || !*enabled) enabled = getenv("ONEKEYIRC_ARABIC_VISUAL");
    lang = getenv("VIPIRC_LANG");
    if (!lang || !*lang) lang = getenv("ONEKEYIRC_LANG");
#endif
    if (!lang || !*lang) lang = getenv("LC_ALL");
    if (!lang || !*lang) lang = getenv("LC_MESSAGES");
    if (!lang || !*lang) lang = getenv("LANG");
    if (!lang || strncasecmp(lang, "ar", 2) ||
        (lang[2] && lang[2] != '_' && lang[2] != '-' && lang[2] != '.' && lang[2] != '@'))
        return 0;
    return enabled && (!strcmp(enabled, "1") || !strcasecmp(enabled, "visual"));
}

#ifdef IRC_NO_ARABIC_VISUAL
static IrcFribidiLog2Vis irc_arabic_fribidi(void) { return NULL; }
#else
static IrcFribidiLog2Vis irc_arabic_fribidi(void)
{
    static int tried;
    static IrcFribidiLog2Vis function;
    /* The handle is intentionally retained for the process lifetime. */
    static void *handle;
    static const char *const names[] = {
        "libfribidi.so.0", "libfribidi.so", "libfribidi.so.0.0",
        "libfribidi.so.1", "libfribidi.so.0.4.0"
    };
    if (!tried) {
        tried = 1;
        for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
            void *candidate = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
            void *symbol;
            if (!candidate) continue;
            symbol = dlsym(candidate, "fribidi_log2vis");
            if (symbol && sizeof function == sizeof symbol) {
                memcpy(&function, &symbol, sizeof function);
                handle = candidate;
                break;
            }
            (void)dlclose(candidate);
        }
    }
    (void)handle;
    return function;
}
#endif /* IRC_NO_ARABIC_VISUAL */

static size_t irc_arabic_encode(uint32_t cp, char result[4])
{
    if (cp <= 0x7fu) { result[0] = (char)cp; return 1; }
    if (cp <= 0x7ffu) {
        result[0] = (char)(0xc0u | (cp >> 6));
        result[1] = (char)(0x80u | (cp & 63u)); return 2;
    }
    if (cp <= 0xffffu) {
        result[0] = (char)(0xe0u | (cp >> 12));
        result[1] = (char)(0x80u | ((cp >> 6) & 63u));
        result[2] = (char)(0x80u | (cp & 63u)); return 3;
    }
    result[0] = (char)(0xf0u | (cp >> 18));
    result[1] = (char)(0x80u | ((cp >> 12) & 63u));
    result[2] = (char)(0x80u | ((cp >> 6) & 63u));
    result[3] = (char)(0x80u | (cp & 63u));
    return 4;
}

static int irc_arabic_character(uint32_t cp)
{
    return (cp >= 0x0600u && cp <= 0x08ffu) ||
           (cp >= 0xfb50u && cp <= 0xfdffu) ||
           (cp >= 0xfe70u && cp <= 0xfeffu);
}

/* Decode one bounded scalar. Callers normally pass sanitized text, but the
 * optional converter must also reject stray continuation and invalid leading
 * bytes rather than turning malformed input into different valid characters. */
static size_t irc_arabic_decode(const char *src, size_t bytes, uint32_t *value)
{
    unsigned char b;
    uint32_t cp;
    size_t n;
    if (!bytes) return 0;
    b = (unsigned char)src[0];
    if (b < 0x80u) { *value = b; return 1; }
    if (b >= 0xc2u && b <= 0xdfu) n = 2;
    else if (b >= 0xe0u && b <= 0xefu) n = 3;
    else if (b >= 0xf0u && b <= 0xf4u) n = 4;
    else return 0;
    if (n > bytes) return 0;
    cp = b & (n == 2 ? 31u : n == 3 ? 15u : 7u);
    for (size_t j = 1; j < n; ++j) {
        unsigned char next = (unsigned char)src[j];
        if ((next & 0xc0u) != 0x80u) return 0;
        cp = (cp << 6) | (next & 63u);
    }
    if (cp < (n == 2 ? 0x80u : n == 3 ? 0x800u : 0x10000u) ||
        cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu)) return 0;
    *value = cp;
    return n;
}

/* Keep ordinary Latin WATCH output byte-for-byte unchanged. This bounded
 * preflight is also used before explicitly wrapping an Arabic live record. */
static int irc_arabic_has_text(const char *src, size_t bytes)
{
    int found = 0;
    for (size_t at = 0; at < bytes;) {
        uint32_t cp;
        size_t n = irc_arabic_decode(src + at, bytes - at, &cp);
        if (!n) return 0;
        if (irc_arabic_character(cp)) found = 1;
        at += n;
    }
    return found;
}

/* Output is the visual presentation form of one already-wrapped *physical*
 * terminal row.  Never apply this to the stored text or to input cursors.
 * Returns zero on failure or for non-Arabic input, leaving the caller's normal
 * logical rendering untouched.  Inserts LRM around ASCII command tokens only
 * inside the temporary buffer so /help and --options keep their exact spelling.
 */
static size_t irc_arabic_visual_row(const char *src, size_t bytes, char *dst, size_t cap)
{
    enum { LIMIT = 2048, INPUT_LIMIT = 4096 };
    uint32_t logical[INPUT_LIMIT], visual[INPUT_LIMIT];
    size_t len = 0, count = 0, seen_arabic = 0;
    IrcFribidiLog2Vis log2vis;
    uint32_t base = 0x111u; /* FriBidi's public FRIBIDI_PAR_RTL value. */
    if (!irc_arabic_visual_selected() || !src || !dst || !cap || bytes > LIMIT) return 0;
    log2vis = irc_arabic_fribidi();
    if (!log2vis) return 0;
    while (len < bytes && count < LIMIT) {
        uint32_t cp;
        size_t n = irc_arabic_decode(src + len, bytes - len, &cp);
        if (!n) return 0;
        if (irc_arabic_character(cp)) seen_arabic = 1;
        logical[count++] = cp;
        len += n;
    }
    if (len != bytes || !seen_arabic || count >= LIMIT) return 0;
    /* Wrap ASCII runs with a temporary left-to-right mark to keep /help,
     * --resume, 0600, and other literal syntax from reversing. */
    uint32_t marked[INPUT_LIMIT];
    size_t used = 0;
    for (size_t i = 0; i < count;) {
        if (logical[i] >= 33 && logical[i] <= 126) {
            size_t end = i, letters = 0;
            while (end < count && logical[end] >= 33 && logical[end] <= 126) {
                uint32_t cp = logical[end];
                if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
                    (cp >= '0' && cp <= '9')) letters = 1;
                ++end;
            }
            if (end - i + (letters ? 2u : 0u) > INPUT_LIMIT - used) return 0;
            if (letters) marked[used++] = 0x200eu;
            while (i < end) marked[used++] = logical[i++];
            if (letters) marked[used++] = 0x200eu;
        } else {
            if (used >= INPUT_LIMIT) return 0;
            marked[used++] = logical[i++];
        }
    }
    if (used >= INPUT_LIMIT || !log2vis(marked, (int32_t)used, &base, visual,
                                         NULL, NULL, NULL)) return 0;
    len = 0;
    for (size_t i = 0; i < used; ++i) {
        uint32_t cp = visual[i];
        char encoded[4];
        size_t n;
        /* FriBidi uses FEFF as a filler for a shaped lam-alef ligature. */
        if (cp == 0xfeffu || cp == 0x200eu || cp == 0x200fu || cp == 0x2066u ||
            cp == 0x2067u || cp == 0x2068u || cp == 0x2069u) continue;
        if (cp == 0 || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu)) return 0;
        n = irc_arabic_encode(cp, encoded);
        if (n >= cap - len) return 0;
        memcpy(dst + len, encoded, n); len += n;
    }
    dst[len] = '\0';
    return len;
}
#endif /* IRC_ARABIC_VISUAL_H */
