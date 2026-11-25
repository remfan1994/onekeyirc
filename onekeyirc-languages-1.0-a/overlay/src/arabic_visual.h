#ifndef IRC_ARABIC_VISUAL_H
#define IRC_ARABIC_VISUAL_H
/* Optional visual-only shaping for simple LTR terminal emulators.
 * The original logical UTF-8 is retained everywhere except the output frame.
 * A system FriBidi library is loaded on demand; no headers, link-time library
 * or persistent text transformation are required.  If unavailable, output
 * remains unchanged. The paragraph layout supplies display-only source maps
 * for both frontends, while editing and transmitted text stay logical. */
#ifndef IRC_NO_ARABIC_VISUAL
#include <dlfcn.h>
#endif
#include <limits.h>
#include <langinfo.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>

/* POSIX terminal-cell API; also declare it for minimal header consumers whose
 * feature-test macros do not expose the declaration from wchar.h. */
extern int wcwidth(wchar_t);

/* Public display-layout API. Every position refers to the unchanged logical
 * source. Arrays belong to the returned object and are freed together.
 *
 * Glyphs are ordered by visual row/column. A base and its following combining
 * marks are consecutive, base first, with a common source span and row/column;
 * only the base advances cells. Read-only lam-alef uses a merged source span.
 * Line cells excludes alignment padding. The final physical line of a source
 * paragraph includes its terminating newline in past/byte_past, if present.
 *
 * Carets are sorted by row/column, with directional ties inside a cell. A
 * source boundary can have two visual affinities at a direction change or
 * wrap. logical_row/col
 * give the canonical leading edge of the following cluster, or the preceding
 * cluster's trailing edge at paragraph end. boundary marks legal cluster
 * stops. SINGLE_LINE leaves unrendered logical_row/col entries at -1. */
enum { IRC_ARABIC_EDIT = 1u, IRC_ARABIC_SINGLE_LINE = 2u };
enum { IRC_ARABIC_MAX_SCALARS = 8192, IRC_ARABIC_MAX_BYTES = 16384 };
typedef struct IrcArabicGlyph {
    uint32_t codepoint;
    size_t first, past, byte_first, byte_past;
    int row, col, cells;
} IrcArabicGlyph;
typedef struct IrcArabicCaret {
    size_t index;
    int row, col;
    unsigned char level;
} IrcArabicCaret;
typedef struct IrcArabicLine {
    size_t first, past, byte_first, byte_past;
    size_t glyph_first, glyph_past;
    int cells, rtl;
} IrcArabicLine;
typedef struct IrcArabicLayout {
    size_t count, glyph_count, caret_count;
    int rows, width;
    IrcArabicGlyph *glyphs;
    IrcArabicCaret *carets;
    IrcArabicLine *lines;
    int *logical_row, *logical_col;
    unsigned char *boundary;
    size_t *byte_offsets;
} IrcArabicLayout;

/* Factories return NULL for the normal logical-rendering fallback: opt-out,
 * no Arabic, unavailable library/UTF-8 widths, invalid input, or allocation
 * failure. SINGLE_LINE fits the first logical prefix inside width. EDIT
 * reserves one caret cell when width > 1 and does not collapse lam-alef.
 * Every caret column is less than width, including at width == 1. */
static inline IrcArabicLayout *irc_arabic_layout_utf8(const char *src, size_t bytes,
                                                     int width, unsigned flags);
static inline IrcArabicLayout *irc_arabic_layout_wide(const wchar_t *src, size_t count,
                                                     int width, unsigned flags);
static inline void irc_arabic_layout_free(IrcArabicLayout *layout);

/* Public FriBidi 1.x ABI (fribidi-types.h, fribidi-bidi.h and friends).
 * StrIndex is int, Level is signed char, flags/types/characters use 32-bit ABI
 * values, and joining/Arabic properties use 8-bit ABI values. No development
 * headers are needed by the application. Tests check ABI widths/constants and
 * behavior against the official headers/library. */
typedef signed char (*IrcFribidiLog2Vis)(const uint32_t *, int,
                                      uint32_t *, uint32_t *,
                                      int *, int *, signed char *);
typedef struct IrcArabicApi {
    IrcFribidiLog2Vis log2vis;
    void (*get_types)(const uint32_t *, int, uint32_t *);
    uint32_t (*get_direction)(const uint32_t *, int);
    void (*get_brackets)(const uint32_t *, int, const uint32_t *, uint32_t *);
    signed char (*get_levels)(const uint32_t *, const uint32_t *, int,
                             uint32_t *, signed char *);
    void (*get_joining)(const uint32_t *, int, uint8_t *);
    void (*join)(const uint32_t *, int, const signed char *, uint8_t *);
    void (*shape)(uint32_t, const signed char *, int, uint8_t *, uint32_t *);
    signed char (*reorder)(uint32_t, const uint32_t *, int, int, uint32_t,
                          signed char *, uint32_t *, int *);
    int layout_ready;
} IrcArabicApi;

static inline int irc_arabic_visual_selected(void)
{
    const char *enabled;
#ifdef ONEKEY_ONLY
    enabled = getenv("ONEKEYIRC_ARABIC_VISUAL");
    if (!enabled || !*enabled) enabled = getenv("VIPIRC_ARABIC_VISUAL");
#else
    enabled = getenv("VIPIRC_ARABIC_VISUAL");
    if (!enabled || !*enabled) enabled = getenv("ONEKEYIRC_ARABIC_VISUAL");
#endif
    /* Rendering preference is independent of the menu translation. An explicit
     * primary value, including 0, takes precedence over the sibling fallback. */
    return enabled && (!strcmp(enabled, "1") || !strcasecmp(enabled, "visual"));
}

static inline int irc_arabic_utf8_locale(void)
{
    const char *codeset = nl_langinfo(CODESET);
    return codeset && (!strcasecmp(codeset, "UTF-8") || !strcasecmp(codeset, "UTF8"));
}

#ifdef IRC_NO_ARABIC_VISUAL
static inline const IrcArabicApi *irc_arabic_api(void) { return NULL; }
#else
static inline int irc_arabic_symbol(void *handle, const char *name,
                                    void *destination, size_t bytes)
{
    void *symbol = dlsym(handle, name);
    if (!symbol || bytes != sizeof symbol) return 0;
    memcpy(destination, &symbol, bytes);
    return 1;
}

static inline const IrcArabicApi *irc_arabic_api(void)
{
    static int tried;
    static IrcArabicApi api;
    /* The handle is intentionally retained for the process lifetime. */
    static void *handle;
    static const char *const names[] = {
#ifdef __OpenBSD__
        /* OpenBSD dlopen(3) resolves an unversioned library name through its
         * normal major/minor library search and ldconfig hints. The package's
         * version suffix need not be guessed or scanned by the application.
         * https://man.openbsd.org/OpenBSD-7.8/dlopen.3 */
        "libfribidi.so"
#else
        "libfribidi.so.0", "libfribidi.so", "libfribidi.so.0.0",
        "libfribidi.so.1", "libfribidi.so.0.4.0"
#endif
    };
    if (!tried) {
        tried = 1;
        for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
            void *candidate = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
            if (!candidate) continue;
            if (!irc_arabic_symbol(candidate, "fribidi_log2vis",
                                   &api.log2vis, sizeof api.log2vis)) {
                (void)dlclose(candidate);
                continue;
            }
            handle = candidate;
#define IRC_ARABIC_LOAD(member, name) \
    irc_arabic_symbol(candidate, name, &api.member, sizeof api.member)
            api.layout_ready =
                IRC_ARABIC_LOAD(get_types, "fribidi_get_bidi_types") &&
                IRC_ARABIC_LOAD(get_direction, "fribidi_get_par_direction") &&
                IRC_ARABIC_LOAD(get_brackets, "fribidi_get_bracket_types") &&
                IRC_ARABIC_LOAD(get_levels, "fribidi_get_par_embedding_levels_ex") &&
                IRC_ARABIC_LOAD(get_joining, "fribidi_get_joining_types") &&
                IRC_ARABIC_LOAD(join, "fribidi_join_arabic") &&
                IRC_ARABIC_LOAD(shape, "fribidi_shape") &&
                IRC_ARABIC_LOAD(reorder, "fribidi_reorder_line");
#undef IRC_ARABIC_LOAD
            break;
        }
    }
    (void)handle;
    return api.log2vis ? &api : NULL;
}
#endif /* IRC_NO_ARABIC_VISUAL */

static inline IrcFribidiLog2Vis irc_arabic_fribidi(void)
{
    const IrcArabicApi *api = irc_arabic_api();
    return api ? api->log2vis : NULL;
}

static inline size_t irc_arabic_encode(uint32_t cp, char result[4])
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

static inline int irc_arabic_character(uint32_t cp)
{
    return (cp >= 0x0600u && cp <= 0x08ffu) ||
           (cp >= 0xfb50u && cp <= 0xfdffu) ||
           (cp >= 0xfe70u && cp <= 0xfeffu);
}

/* Decode one bounded scalar. Callers normally pass sanitized text, but the
 * optional converter must also reject stray continuation and invalid leading
 * bytes rather than turning malformed input into different valid characters. */
static inline size_t irc_arabic_decode(const char *src, size_t bytes, uint32_t *value)
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
static inline int irc_arabic_has_text(const char *src, size_t bytes)
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
static inline size_t irc_arabic_visual_row(const char *src, size_t bytes, char *dst, size_t cap)
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
        /* A row renderer must not pass terminal controls or Unicode
         * formatting controls through as raw output. The normal caller
         * sanitizes, but this defensive boundary also protects reuse. */
        if (cp < 0x20u || (cp >= 0x7fu && cp <= 0x9fu) ||
            cp == 0x061cu || cp == 0x180eu ||
            cp == 0x200eu || cp == 0x200fu ||
            (cp >= 0x2028u && cp <= 0x202eu) ||
            (cp >= 0x2066u && cp <= 0x206fu) ||
            (cp >= 0xfff9u && cp <= 0xfffbu) || cp == 0xfeffu) return 0;
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
    if (used >= INPUT_LIMIT || !log2vis(marked, (int)used, &base, visual,
                                         NULL, NULL, NULL)) return 0;
    len = 0;
    for (size_t i = 0; i < used; ++i) {
        uint32_t cp = visual[i];
        char encoded[4];
        size_t n;
        /* FriBidi uses FEFF as a filler for a shaped lam-alef ligature. */
        if (cp == 0xfeffu || cp == 0x200eu || cp == 0x200fu || cp == 0x2066u ||
            (cp >= 0x2066u && cp <= 0x206fu)) continue;
        if (cp == 0 || cp > 0x10ffffu || (cp >= 0xd800u && cp <= 0xdfffu)) return 0;
        n = irc_arabic_encode(cp, encoded);
        if (n >= cap - len) return 0;
        memcpy(dst + len, encoded, n); len += n;
    }
    dst[len] = '\0';
    return len;
}

/* Paragraph layout follows the FriBidi public API: resolve levels once for
 * the paragraph, measure shaped clusters, then shape/join and reorder each
 * physical line. Wrapping prefers spaces, with complete-cluster emergency
 * breaks; it is deliberately not a full Unicode line-break implementation. */
typedef struct IrcArabicWork {
    uint32_t *source, *marked, *types, *brackets, *shaped, *visual;
    int *origin, *at, *cut, *map, *cells;
    size_t *group, *past;
    signed char *levels;
    uint8_t *joining, *emitted;
} IrcArabicWork;

static inline void irc_arabic_work_free(IrcArabicWork *work)
{
    if (!work) return;
    free(work->source); free(work->marked); free(work->types);
    free(work->brackets); free(work->shaped); free(work->visual);
    free(work->origin); free(work->at); free(work->cut); free(work->map);
    free(work->cells); free(work->group); free(work->past);
    free(work->levels); free(work->joining); free(work->emitted);
    free(work);
}

static inline IrcArabicWork *irc_arabic_work_new(size_t count)
{
    IrcArabicWork *work = calloc(1, sizeof *work);
    size_t capacity = count * 3u + 2u;
    if (!work) return NULL;
#define IRC_ARABIC_ALLOC(member, length) \
    ((work->member = calloc((length), sizeof *work->member)) != NULL)
    if (!IRC_ARABIC_ALLOC(source, count + 1u) ||
        !IRC_ARABIC_ALLOC(marked, capacity) ||
        !IRC_ARABIC_ALLOC(types, capacity) ||
        !IRC_ARABIC_ALLOC(brackets, capacity) ||
        !IRC_ARABIC_ALLOC(shaped, capacity) ||
        !IRC_ARABIC_ALLOC(visual, capacity) ||
        !IRC_ARABIC_ALLOC(origin, capacity) ||
        !IRC_ARABIC_ALLOC(at, count + 1u) ||
        !IRC_ARABIC_ALLOC(cut, count + 1u) ||
        !IRC_ARABIC_ALLOC(map, capacity) ||
        !IRC_ARABIC_ALLOC(cells, count + 1u) ||
        !IRC_ARABIC_ALLOC(group, count + 1u) ||
        !IRC_ARABIC_ALLOC(past, count + 1u) ||
        !IRC_ARABIC_ALLOC(levels, capacity) ||
        !IRC_ARABIC_ALLOC(joining, capacity) ||
        !IRC_ARABIC_ALLOC(emitted, count + 1u)) {
        irc_arabic_work_free(work);
        return NULL;
    }
#undef IRC_ARABIC_ALLOC
    return work;
}

static inline void irc_arabic_layout_free(IrcArabicLayout *layout)
{
    if (!layout) return;
    free(layout->glyphs); free(layout->carets); free(layout->lines);
    free(layout->logical_row); free(layout->logical_col);
    free(layout->boundary); free(layout->byte_offsets);
    free(layout);
}

static inline IrcArabicLayout *irc_arabic_layout_new(size_t count, int width)
{
    IrcArabicLayout *layout = calloc(1, sizeof *layout);
    if (!layout) return NULL;
    layout->count = count;
    layout->width = width;
#define IRC_ARABIC_ALLOC(member, length) \
    ((layout->member = calloc((length), sizeof *layout->member)) != NULL)
    if (!IRC_ARABIC_ALLOC(glyphs, count * 2u + 1u) ||
        !IRC_ARABIC_ALLOC(carets, count * 2u + 2u) ||
        !IRC_ARABIC_ALLOC(lines, count + 1u) ||
        !IRC_ARABIC_ALLOC(logical_row, count + 1u) ||
        !IRC_ARABIC_ALLOC(logical_col, count + 1u) ||
        !IRC_ARABIC_ALLOC(boundary, count + 1u) ||
        !IRC_ARABIC_ALLOC(byte_offsets, count + 1u)) {
        irc_arabic_layout_free(layout);
        return NULL;
    }
#undef IRC_ARABIC_ALLOC
    for (size_t i = 0; i <= count; ++i) {
        layout->logical_row[i] = -1;
        layout->logical_col[i] = -1;
    }
    return layout;
}

/* Callers sanitize IRC data before painting. Reject controls here as well,
 * so accidental raw input cannot introduce a terminal operation or invisible
 * bidi overrides. Join controls are allowed and consumed by the shaper. */
static inline int irc_arabic_layout_scalar(uint32_t cp)
{
    if (cp == '\n') return 1;
    if (cp < 0x20u || (cp >= 0x7fu && cp <= 0x9fu) ||
        cp > 0x10ffffu || cp > (uint32_t)WCHAR_MAX ||
        (cp >= 0xd800u && cp <= 0xdfffu) || cp == 0x061cu ||
        cp == 0x180eu || cp == 0x200eu || cp == 0x200fu ||
        (cp >= 0x2028u && cp <= 0x202eu) ||
        (cp >= 0x2066u && cp <= 0x206fu) ||
        (cp >= 0xfff9u && cp <= 0xfffbu) || cp == 0xfeffu) return 0;
    return wcwidth((wchar_t)cp) >= 0;
}

static inline int irc_arabic_ascii_word(uint32_t cp)
{
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
           (cp >= '0' && cp <= '9');
}

static inline int irc_arabic_break_space(uint32_t cp)
{
    return cp == 0x20u || (cp >= 0x2000u && cp <= 0x200au && cp != 0x2007u) ||
           cp == 0x205fu || cp == 0x3000u;
}

static inline int irc_arabic_hidden_join(uint32_t cp)
{
    return cp == 0x200cu || cp == 0x200du || cp == 0xfeffu;
}

static inline uint32_t irc_arabic_shape_flags(unsigned flags)
{
    /* FRIBIDI_FLAG_SHAPE_MIRRORING | FRIBIDI_FLAG_SHAPE_ARAB_PRES,
     * plus FRIBIDI_FLAG_SHAPE_ARAB_LIGA for read-only display. */
    return 0x00000101u | ((flags & IRC_ARABIC_EDIT) ? 0u : 0x00000200u);
}

static inline void irc_arabic_shape_range(const IrcArabicApi *api,
                                          IrcArabicWork *work, int first,
                                          int past, unsigned flags)
{
    int length = past - first;
    memcpy(work->shaped + first, work->marked + first,
           (size_t)length * sizeof *work->shaped);
    api->get_joining(work->marked + first, length, work->joining + first);
    api->join(work->types + first, length, work->levels + first,
              work->joining + first);
    api->shape(irc_arabic_shape_flags(flags), work->levels + first, length,
               work->joining + first, work->shaped + first);
}

static inline int irc_arabic_prepare_paragraph(const IrcArabicApi *api,
                                               IrcArabicWork *work,
                                               size_t first, size_t past,
                                               unsigned flags, uint32_t *base)
{
    int used = 0;
    *base = 0x111u; /* FRIBIDI_PAR_RTL for a paragraph without a strong letter. */
    if (first == past) { work->cut[first] = 0; return 1; }
    /* Resolve the base from the original text, before adding temporary marks.
     * Thus a Latin timestamp/nickname prefix remains a left-to-right paragraph. */
    api->get_types(work->source + first, (int)(past - first), work->types);
    *base = api->get_direction(work->types, (int)(past - first));
    if (*base != 0x110u && *base != 0x111u) *base = 0x111u;
    for (size_t i = first; i < past;) {
        size_t end = i + 1u;
        int protect = 0;
        work->cut[i] = used;
        if (work->source[i] >= 33u && work->source[i] <= 126u) {
            end = i;
            while (end < past && work->source[end] >= 33u &&
                   work->source[end] <= 126u) {
                if (irc_arabic_ascii_word(work->source[end])) protect = 1;
                ++end;
            }
        }
        if (protect) {
            work->marked[used] = 0x200eu;
            work->origin[used++] = -1;
        }
        for (size_t j = i; j < end; ++j) {
            if (j != i) work->cut[j] = used;
            work->at[j] = used;
            work->marked[used] = work->source[j];
            work->origin[used++] = (int)j;
        }
        if (protect) {
            work->marked[used] = 0x200eu;
            work->origin[used++] = -1;
        }
        work->cut[end] = used;
        i = end;
    }
    api->get_types(work->marked, used, work->types);
    api->get_brackets(work->marked, used, work->types, work->brackets);
    if (!api->get_levels(work->types, work->brackets, used, base, work->levels))
        return 0;
    irc_arabic_shape_range(api, work, 0, used, flags);
    /* FriBidi emits FEFF at the consumed lam and a presentation ligature at
     * the following alef. Merge their source spans before choosing line breaks.
     * A mark between the letters prevents this library ligature naturally. */
    for (size_t i = first; i + 1u < past; ++i) {
        uint32_t next = work->shaped[work->at[i + 1u]];
        if (work->shaped[work->at[i]] == 0xfeffu &&
            next >= 0xfef5u && next <= 0xfefcu) {
            size_t start = work->group[i];
            size_t end = work->past[i + 1u];
            for (size_t j = start; j < end; ++j) {
                work->group[j] = start;
                work->past[j] = end;
            }
        }
    }
    for (size_t i = first; i < past; i = work->past[i]) {
        int cells = 0, visible = 0;
        for (size_t j = i; j < work->past[i]; ++j) {
            uint32_t cp = work->shaped[work->at[j]];
            int span;
            if (irc_arabic_hidden_join(cp)) continue;
            span = wcwidth((wchar_t)cp);
            if (span < 0) return 0;
            cells += span;
            visible = 1;
        }
        /* A leading combining group gets a visible base in the output frame. */
        work->cells[i] = cells ? cells : visible;
    }
    return 1;
}

static inline void irc_arabic_add_caret(IrcArabicLayout *layout, size_t index,
                                        int row, int col, unsigned char level)
{
    IrcArabicCaret *caret = &layout->carets[layout->caret_count++];
    caret->index = index;
    caret->row = row;
    caret->col = col < layout->width ? col : layout->width - 1;
    if (caret->col < 0) caret->col = 0;
    caret->level = level;
    layout->boundary[index] = 1;
}

static inline void irc_arabic_add_glyph(IrcArabicLayout *layout, uint32_t cp,
                                        size_t first, size_t past,
                                        int row, int col, int cells)
{
    IrcArabicGlyph *glyph = &layout->glyphs[layout->glyph_count++];
    glyph->codepoint = cp;
    glyph->first = first; glyph->past = past;
    glyph->byte_first = layout->byte_offsets[first];
    glyph->byte_past = layout->byte_offsets[past];
    glyph->row = row; glyph->col = col; glyph->cells = cells;
}

static inline int irc_arabic_emit_line(const IrcArabicApi *api,
                                      IrcArabicWork *work,
                                      IrcArabicLayout *layout, size_t first,
                                      size_t past, size_t paragraph_past,
                                      uint32_t base, unsigned flags)
{
    int row = layout->rows++;
    int capacity = layout->width - ((flags & IRC_ARABIC_EDIT) && layout->width > 1);
    int begin = work->cut[first], end = work->cut[past];
    int cells = 0, col;
    IrcArabicLine *line = &layout->lines[row];
    for (size_t i = first; i < past; i = work->past[i])
        cells += work->cells[i] > capacity ? 1 : work->cells[i];
    if (cells > capacity) return 0;
    line->first = first;
    line->past = past + (past == paragraph_past && past < layout->count);
    line->byte_first = layout->byte_offsets[first];
    line->byte_past = layout->byte_offsets[line->past];
    line->glyph_first = layout->glyph_count;
    line->cells = cells;
    line->rtl = base == 0x111u;
    col = line->rtl ? capacity - cells : 0;
    if (first == past) {
        int caret_col = col < layout->width ? col : layout->width - 1;
        layout->logical_row[first] = row;
        layout->logical_col[first] = caret_col;
        irc_arabic_add_caret(layout, first, row, caret_col,
                             (unsigned char)line->rtl);
        line->glyph_past = layout->glyph_count;
        return 1;
    }
    /* Reset joining at this physical line's boundaries. Paragraph levels are
     * retained; reorder_line applies the line-specific trailing-space L1 rule. */
    irc_arabic_shape_range(api, work, begin, end, flags);
    memcpy(work->visual + begin, work->shaped + begin,
           (size_t)(end - begin) * sizeof *work->visual);
    for (int i = begin; i < end; ++i) work->map[i] = i;
    if (!api->reorder(0x00000002u, work->types + begin, end - begin, 0,
                      base, work->levels + begin, work->visual + begin,
                      work->map + begin)) return 0;
    for (int at = begin; at < end; ++at) {
        int original = work->origin[work->map[at]];
        size_t start, finish;
        int span, leading, trailing, visible = 0, positive = 0;
        unsigned char level;
        if (original < 0) continue; /* Our temporary token-direction mark. */
        start = work->group[(size_t)original];
        if (work->emitted[start]) continue;
        work->emitted[start] = 1;
        finish = work->past[start];
        span = work->cells[start] > capacity ? 1 : work->cells[start];
        level = (unsigned char)work->levels[work->at[start]];
        leading = (level & 1u) ? col + span : col;
        trailing = (level & 1u) ? col : col + span;
        if (leading >= layout->width) leading = layout->width - 1;
        if (trailing >= layout->width) trailing = layout->width - 1;
        for (size_t i = start; i < finish; ++i) {
            layout->logical_row[i] = row;
            layout->logical_col[i] = leading;
        }
        if (finish == past) {
            layout->logical_row[finish] = row;
            layout->logical_col[finish] = trailing;
        }
        irc_arabic_add_caret(layout, start, row, leading, level);
        irc_arabic_add_caret(layout, finish, row, trailing, level);
        if (work->cells[start] > capacity) {
            /* A two-cell glyph cannot fit a one-cell viewport. Its source span
             * remains intact; only this frame uses a one-cell replacement. */
            irc_arabic_add_glyph(layout, 0xfffdu, start, finish, row, col, 1);
        } else {
            for (size_t i = start; i < finish; ++i) {
                uint32_t cp = work->shaped[work->at[i]];
                if (!irc_arabic_hidden_join(cp)) {
                    visible = 1;
                    if (wcwidth((wchar_t)cp) > 0) positive = 1;
                }
            }
            if (visible && !positive)
                irc_arabic_add_glyph(layout, 0x25ccu, start, finish, row, col, 1);
            /* Reconstruct each cluster in logical scalar order after ordering
             * the clusters visually. This explicitly keeps every base before
             * its combining marks, including marks adjacent to join controls. */
            for (size_t i = start; i < finish; ++i) {
                uint32_t cp = work->shaped[work->at[i]];
                if (!irc_arabic_hidden_join(cp))
                    irc_arabic_add_glyph(layout, cp, start, finish, row, col,
                                         wcwidth((wchar_t)cp));
            }
        }
        col += span;
    }
    line->glyph_past = layout->glyph_count;
    return 1;
}

static inline int irc_arabic_caret_unique_compare(const void *left, const void *right)
{
    const IrcArabicCaret *a = left, *b = right;
    if (a->row != b->row) return a->row < b->row ? -1 : 1;
    if (a->col != b->col) return a->col < b->col ? -1 : 1;
    if (a->index != b->index) return a->index < b->index ? -1 : 1;
    return a->level < b->level ? -1 : a->level != b->level;
}

static inline int irc_arabic_caret_compare(const void *left, const void *right)
{
    const IrcArabicCaret *a = left, *b = right;
    if (a->row != b->row) return a->row < b->row ? -1 : 1;
    if (a->col != b->col) return a->col < b->col ? -1 : 1;
    /* A one-cell viewport cannot reserve a separate caret cell. Keep its two
     * coincident endpoints in the run's visual order, so an RTL Left crosses
     * that character before wrapping. Sort levels before indices to keep the
     * comparison transitive when different directional runs meet in a cell. */
    if (a->level != b->level) return a->level < b->level ? -1 : 1;
    if (a->index == b->index) return 0;
    if (a->level & 1u) return a->index > b->index ? -1 : 1;
    return a->index < b->index ? -1 : 1;
}

static inline IrcArabicLayout *irc_arabic_layout_utf8(const char *src, size_t bytes,
                                                     int width, unsigned flags)
{
    const IrcArabicApi *api;
    IrcArabicWork *work = NULL;
    IrcArabicLayout *layout = NULL;
    size_t count = 0;
    int seen_arabic = 0;
    int capacity;
    if (!src || !bytes || bytes > IRC_ARABIC_MAX_BYTES || width < 1 ||
        (flags & ~(IRC_ARABIC_EDIT | IRC_ARABIC_SINGLE_LINE)) ||
        !irc_arabic_visual_selected() || !irc_arabic_utf8_locale() ||
        wcwidth((wchar_t)0xfe91u) != 1 ||
        wcwidth((wchar_t)0x25ccu) != 1 || wcwidth((wchar_t)0xfffdu) != 1)
        return NULL;
    api = irc_arabic_api();
    if (!api || !api->layout_ready) return NULL;
    for (size_t at = 0; at < bytes;) {
        uint32_t cp;
        size_t length = irc_arabic_decode(src + at, bytes - at, &cp);
        if (!length || !irc_arabic_layout_scalar(cp) ||
            ++count > IRC_ARABIC_MAX_SCALARS) return NULL;
        if (irc_arabic_character(cp)) seen_arabic = 1;
        at += length;
    }
    if (!seen_arabic) return NULL;
    layout = irc_arabic_layout_new(count, width);
    work = irc_arabic_work_new(count);
    if (!layout || !work) goto failed;
    for (size_t i = 0, at = 0; i < count; ++i) {
        size_t length;
        layout->byte_offsets[i] = at;
        length = irc_arabic_decode(src + at, bytes - at, &work->source[i]);
        at += length;
    }
    layout->byte_offsets[count] = bytes;
    /* The editor uses the same base-plus-following-zero-width cluster model.
     * A newline is always its own boundary and never adopts following marks. */
    for (size_t i = 0; i < count;) {
        size_t end = i + 1u;
        if (work->source[i] != '\n')
            while (end < count && work->source[end] != '\n' &&
                   wcwidth((wchar_t)work->source[end]) == 0) ++end;
        for (size_t j = i; j < end; ++j) {
            work->group[j] = i;
            work->past[j] = end;
        }
        i = end;
    }
    capacity = width - ((flags & IRC_ARABIC_EDIT) && width > 1);
    for (size_t paragraph = 0;;) {
        size_t paragraph_past = paragraph;
        size_t first;
        uint32_t base;
        while (paragraph_past < count && work->source[paragraph_past] != '\n')
            ++paragraph_past;
        if (!irc_arabic_prepare_paragraph(api, work, paragraph, paragraph_past,
                                          flags, &base)) goto failed;
        first = paragraph;
        do {
            size_t past = first, last_space = first;
            int cells = 0;
            while (past < paragraph_past) {
                int span = work->cells[past] > capacity ? 1 : work->cells[past];
                if (span > capacity - cells) break;
                cells += span;
                if (irc_arabic_break_space(work->source[past]))
                    last_space = work->past[past];
                past = work->past[past];
            }
            if (past < paragraph_past && last_space > first &&
                !(flags & IRC_ARABIC_SINGLE_LINE)) past = last_space;
            if (past == first && first < paragraph_past) goto failed;
            if (!irc_arabic_emit_line(api, work, layout, first, past,
                                      paragraph_past, base, flags)) goto failed;
            if (flags & IRC_ARABIC_SINGLE_LINE) goto finished;
            first = past;
        } while (first < paragraph_past);
        if (paragraph_past == count) break;
        paragraph = paragraph_past + 1u;
    }
finished:
    qsort(layout->carets, layout->caret_count, sizeof *layout->carets,
          irc_arabic_caret_unique_compare);
    {
        size_t used = 0;
        for (size_t i = 0; i < layout->caret_count; ++i) {
            const IrcArabicCaret *caret = &layout->carets[i];
            if (used && layout->carets[used - 1u].index == caret->index &&
                layout->carets[used - 1u].row == caret->row &&
                layout->carets[used - 1u].col == caret->col) continue;
            layout->carets[used++] = *caret;
        }
        layout->caret_count = used;
    }
    qsort(layout->carets, layout->caret_count, sizeof *layout->carets,
          irc_arabic_caret_compare);
    irc_arabic_work_free(work);
    return layout;
failed:
    irc_arabic_work_free(work);
    irc_arabic_layout_free(layout);
    return NULL;
}

static inline IrcArabicLayout *irc_arabic_layout_wide(const wchar_t *src, size_t count,
                                                     int width, unsigned flags)
{
    const IrcArabicApi *api;
    char *encoded;
    size_t bytes = 0;
    int seen_arabic = 0;
    IrcArabicLayout *layout;
    if (!src || !count || count > IRC_ARABIC_MAX_SCALARS || width < 1 ||
        (flags & ~(IRC_ARABIC_EDIT | IRC_ARABIC_SINGLE_LINE)) ||
        !irc_arabic_visual_selected() || !irc_arabic_utf8_locale()) return NULL;
    /* Ordinary drafts need neither character-width work nor a library load.
     * For Arabic drafts, reject unavailable conversion before validating and
     * allocating a UTF-8 copy; the logical editor is the frequent fallback. */
    for (size_t i = 0; i < count; ++i)
        if (irc_arabic_character((uint32_t)src[i])) { seen_arabic = 1; break; }
    if (!seen_arabic) return NULL;
    api = irc_arabic_api();
    if (!api || !api->layout_ready || wcwidth((wchar_t)0xfe91u) != 1 ||
        wcwidth((wchar_t)0x25ccu) != 1 || wcwidth((wchar_t)0xfffdu) != 1)
        return NULL;
    for (size_t i = 0; i < count; ++i) {
        char scalar[4];
        uint32_t cp = (uint32_t)src[i];
        if (!irc_arabic_layout_scalar(cp)) return NULL;
        bytes += irc_arabic_encode(cp, scalar);
        if (bytes > IRC_ARABIC_MAX_BYTES) return NULL;
    }
    encoded = malloc(bytes + 1u);
    if (!encoded) return NULL;
    bytes = 0;
    for (size_t i = 0; i < count; ++i)
        bytes += irc_arabic_encode((uint32_t)src[i], encoded + bytes);
    encoded[bytes] = '\0';
    layout = irc_arabic_layout_utf8(encoded, bytes, width, flags);
    free(encoded);
    return layout;
}

#endif /* IRC_ARABIC_VISUAL_H */
