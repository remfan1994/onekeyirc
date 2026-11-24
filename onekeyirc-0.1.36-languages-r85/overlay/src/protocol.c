/* Bounded IRC text operations, with no socket, file or allocation ownership.
 * Read in order: in-place framing/tags; case mapping and ignore masks;
 * UTF-8 validation/display sanitization; matching and outbound byte budgets.
 * Parsing accepts protocol syntax; presentation and application authorization
 * remain separate boundaries. Protocol references are in README/the manual.
 */
#include "protocol.h"

#include <stdint.h>
#include <string.h>

static int ascii_alpha(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int ascii_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static size_t bounded_length(const char *s, size_t maximum)
{
    size_t n = 0;
    if (!s)
        return 0;
    while (n <= maximum && s[n])
        ++n;
    return n;
}

static unsigned char folded(unsigned char c, IrcCase mapping)
{
    if (c >= 'A' && c <= 'Z')
        return (unsigned char)(c + ('a' - 'A'));
    if (mapping != IRC_CASE_ASCII) {
        if (c == '[') return '{';
        if (c == ']') return '}';
        if (c == '\\') return '|';
        if (mapping == IRC_CASE_RFC1459 && c == '~') return '^';
    }
    return c;
}

static int nick_character(unsigned char c)
{
    return ascii_alpha(c) || ascii_digit(c) || c == '-' ||
           c == '[' || c == ']' || c == '\\' || c == '`' ||
           c == '_' || c == '^' || c == '{' || c == '|' ||
           c == '}' || c == '~' || c >= 0x80;
}

static int field_safe(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!p || !*p)
        return 0;
    while (*p) {
        if (*p <= 0x20 || *p == 0x7f)
            return 0;
        ++p;
    }
    return 1;
}

/* Parse into a temporary view; publish borrowed fields only after success. */
int irc_parse(char *line, IrcMessage *out)
{
    IrcMessage parsed;
    char *p, *end;
    size_t n, i;
    int numeric = 1;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    memset(&parsed, 0, sizeof(parsed));
    if (!line)
        return 0;
    n = bounded_length(line, IRC_WIRE_MAX);
    if (n == 0 || n > IRC_WIRE_MAX)
        return 0;
    for (i = 0; i < n; ++i)
        if (line[i] == '\r' || line[i] == '\n')
            return 0;
    p = line;
    if (*p == '@') {
        parsed.tags = ++p;
        end = strchr(p, ' ');
        if (!end || end == p)
            return 0;
        /* IRCv3 maximum includes the initial '@' and separating space. */
        if ((size_t)(end - line) + 1 > 8191u)
            return 0;
        *end = '\0';
        if (!field_safe(p))
            return 0;
        p = end + 1;
        if (strlen(p) > IRC_BODY_MAX)
            return 0;
        while (*p == ' ')
            ++p;
    }
    if (strlen(p) > IRC_BODY_MAX)
        return 0;
    if (*p == ':') {
        parsed.prefix = ++p;
        end = strchr(p, ' ');
        if (!end || end == p)
            return 0;
        *end = '\0';
        if (!field_safe(p))
            return 0;
        p = end + 1;
        while (*p == ' ')
            ++p;
    }
    if (!*p || *p == ' ')
        return 0;
    parsed.command = p;
    end = strchr(p, ' ');
    if (end)
        *end = '\0';
    n = strlen(p);
    for (i = 0; i < n; ++i)
        if (!ascii_digit((unsigned char)p[i]))
            numeric = 0;
    if (numeric) {
        if (n != 3)
            return 0;
    } else {
        for (i = 0; i < n; ++i) {
            unsigned char c = (unsigned char)p[i];
            if (!ascii_alpha(c))
                return 0;
            if (c >= 'a' && c <= 'z')
                p[i] = (char)(c - ('a' - 'A'));
        }
    }
    if (end) {
        p = end + 1;
        while (*p) {
            while (*p == ' ')
                ++p;
            if (!*p)
                break;
            if (parsed.nparams == 15)
                return 0;
            if (*p == ':') {
                parsed.params[parsed.nparams++] = p + 1;
                break;
            }
            parsed.params[parsed.nparams++] = p;
            end = strchr(p, ' ');
            if (end)
                *end = '\0';
            if (!field_safe(p))
                return 0;
            if (!end)
                break;
            p = end + 1;
        }
    }
    *out = parsed;
    return 1;
}

/* IRCv3 tag decoding and validated display timestamps. */
int irc_tag_value(const char *tags, const char *key, char *out, size_t cap)
{
    const char *value = NULL, *value_end = NULL;
    size_t key_len, used = 0;
    if (!out || !cap) return -1;
    out[0] = 0;
    if (!tags || !key || !*key) return 0;
    key_len = strlen(key);
    for (const char *p = tags; *p;) {
        const char *end = strchr(p, ';');
        if (!end) end = p + strlen(p);
        const char *equal = memchr(p, '=', (size_t)(end - p));
        size_t n = (size_t)((equal ? equal : end) - p);
        if (n == key_len && !memcmp(p, key, n)) {
            value = equal ? equal + 1 : end;
            value_end = end;
        }
        p = *end ? end + 1 : end;
    }
    if (!value) return 0;
    while (value < value_end) {
        unsigned char c = (unsigned char)*value++;
        if (c == '\\') {
            if (value == value_end) break;
            c = (unsigned char)*value++;
            if (c == ':') c = ';';
            else if (c == 's') c = ' ';
            else if (c == 'r') c = '\r';
            else if (c == 'n') c = '\n';
        }
        if (used + 1 >= cap) {
            out[0] = 0;
            return -1;
        }
        out[used++] = (char)c;
    }
    out[used] = 0;
    return 1;
}

int irc_server_time(const char *tags, char *out, size_t cap)
{
    char value[40];
    static const unsigned char days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (!out || !cap) return 0;
    out[0] = 0;
    if (irc_tag_value(tags, "time", value, sizeof value) != 1) return 0;
    size_t n = strlen(value);
    /* Standard server-time uses three fractional digits. Accept other
     * bounded precisions and whole seconds without converting through time_t. */
    if (n < 20 || n > 30 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':' || value[n - 1] != 'Z') return 0;
    for (size_t i = 0; i < 19; ++i) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
        if (!ascii_digit((unsigned char)value[i])) return 0;
    }
    if (n != 20) {
        if (n < 22 || value[19] != '.') return 0;
        for (size_t i = 20; i + 1 < n; ++i)
            if (!ascii_digit((unsigned char)value[i])) return 0;
    }
    unsigned year = (unsigned)(value[0]-'0') * 1000u + (unsigned)(value[1]-'0') * 100u +
                    (unsigned)(value[2]-'0') * 10u + (unsigned)(value[3]-'0');
    unsigned month = (unsigned)(value[5]-'0') * 10u + (unsigned)(value[6]-'0');
    unsigned day = (unsigned)(value[8]-'0') * 10u + (unsigned)(value[9]-'0');
    unsigned hour = (unsigned)(value[11]-'0') * 10u + (unsigned)(value[12]-'0');
    unsigned minute = (unsigned)(value[14]-'0') * 10u + (unsigned)(value[15]-'0');
    unsigned second = (unsigned)(value[17]-'0') * 10u + (unsigned)(value[18]-'0');
    if (!year || month < 1 || month > 12 || !day || hour > 23 || minute > 59 || second > 60) return 0;
    unsigned max_day = days[month - 1];
    if (month == 2 && !(year % 4u) && ((year % 100u) || !(year % 400u))) ++max_day;
    if (day > max_day) return 0;
    if (cap < 24) return 0;
    memcpy(out, value, 10);
    out[10] = ' ';
    memcpy(out + 11, value + 11, 8);
    memcpy(out + 19, " UTC", 5);
    return 1;
}

/* Network case mapping and bounded nickname/ignore-mask validation. */
int irc_equal(const char *a, const char *b, IrcCase mapping)
{
    if (!a || !b)
        return 0;
    while (*a && *b) {
        if (folded((unsigned char)*a, mapping) !=
            folded((unsigned char)*b, mapping))
            return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

int irc_valid_nick(const char *nick)
{
    size_t n, i;
    unsigned char c;
    if (!nick)
        return 0;
    n = bounded_length(nick, IRC_NICK_MAX);
    if (n == 0 || n > IRC_NICK_MAX)
        return 0;
    c = (unsigned char)nick[0];
    if (ascii_digit(c) || c == '-' || c >= 0x80 || !nick_character(c))
        return 0;
    for (i = 1; i < n; ++i) {
        c = (unsigned char)nick[i];
        if (c >= 0x80 || !nick_character(c))
            return 0;
    }
    return 1;
}

int irc_valid_ignore_mask(const char *mask)
{
    size_t length = bounded_length(mask, IRC_IGNORE_MASK_MAX);
    size_t bang = SIZE_MAX, at = SIZE_MAX;
    if (!length || length > IRC_IGNORE_MASK_MAX)
        return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)mask[i];
        if (c <= 32 || c >= 127)
            return 0;
        if (c == '!') {
            if (bang != SIZE_MAX) return 0;
            bang = i;
        } else if (c == '@') {
            if (at != SIZE_MAX) return 0;
            at = i;
        }
    }
    if (bang == SIZE_MAX && at == SIZE_MAX)
        return irc_valid_nick(mask);
    if (bang == SIZE_MAX || at == SIZE_MAX || !bang ||
        at <= bang + 1 || at + 1 == length)
        return 0;
    for (size_t i = 0; i < bang; ++i) {
        unsigned char c = (unsigned char)mask[i];
        if (c == '*' || c == '?') continue;
        if (!nick_character(c) || (!i && (ascii_digit(c) || c == '-')))
            return 0;
    }
    return 1;
}

static size_t ignore_source_length(const char *text, size_t maximum)
{
    size_t length = bounded_length(text, maximum);
    if (!length || length > maximum) return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c <= 32 || c >= 127 || c == '!' || c == '@') return 0;
    }
    return length;
}

static int ignore_component_match(const char *pattern, size_t pattern_length,
                                   const char *text, size_t text_length, IrcCase mapping)
{
    /* One DP row. Each byte advances each pattern position exactly once.
     * Consecutive stars and hostile near-matches cannot cause recursive or
     * exponential searches, and no state crosses a prefix delimiter. */
    unsigned char matches[IRC_IGNORE_MASK_MAX + 1] = { 0 };
    matches[0] = 1;
    for (size_t j = 1; j <= pattern_length; ++j)
        matches[j] = (unsigned char)(pattern[j - 1] == '*' && matches[j - 1]);
    for (size_t i = 0; i < text_length; ++i) {
        unsigned char previous = matches[0];
        matches[0] = 0;
        for (size_t j = 1; j <= pattern_length; ++j) {
            unsigned char old = matches[j];
            unsigned char c = (unsigned char)pattern[j - 1];
            if (c == '*') matches[j] = (unsigned char)(matches[j - 1] || old);
            else matches[j] = (unsigned char)(previous && (c == '?' ||
                folded(c, mapping) == folded((unsigned char)text[i], mapping)));
            previous = old;
        }
    }
    return matches[pattern_length] != 0;
}

/* Matching a local ignore mask is a display
 * decision, not authentication or a server-side ban. Use the caller
 * verified user prefix and current IRC case mapping. */
int irc_ignore_match(const char *mask, const char *nick, const char *user,
                     const char *host, IrcCase mapping)
{
    const char *bang, *at;
    size_t user_length, host_length;
    if (!irc_valid_ignore_mask(mask) || !irc_valid_nick(nick)) return 0;
    bang = strchr(mask, '!');
    if (!bang) return irc_equal(mask, nick, mapping);
    at = strchr(bang + 1, '@'); /* The validator established both delimiters. */
    user_length = ignore_source_length(user, 63);
    host_length = ignore_source_length(host, 255);
    if (!user_length || !host_length) return 0;
    return ignore_component_match(mask, (size_t)(bang - mask), nick, strlen(nick), mapping) &&
           ignore_component_match(bang + 1, (size_t)(at - bang - 1), user, user_length, IRC_CASE_ASCII) &&
           ignore_component_match(at + 1, strlen(at + 1), host, host_length, IRC_CASE_ASCII);
}

/* Decode exactly one scalar value, without reading beyond the NUL.
 * Non-shortest encodings, surrogate values, and values above U+10FFFF
 * are invalid. Invalid returns zero and consumes nothing. */
/* Scalar decoding is shared by validation, display and message splitting. */
static size_t utf8_decode(const unsigned char *s, uint32_t *value)
{
    uint32_t c;
    size_t n, i;
    if (*s < 0x80) {
        *value = *s;
        return 1;
    }
    if (*s >= 0xc2 && *s <= 0xdf) {
        n = 2;
        c = *s & 0x1fu;
    } else if (*s >= 0xe0 && *s <= 0xef) {
        n = 3;
        c = *s & 0x0fu;
    } else if (*s >= 0xf0 && *s <= 0xf4) {
        n = 4;
        c = *s & 0x07u;
    } else {
        return 0;
    }
    for (i = 1; i < n; ++i) {
        if (s[i] < 0x80 || s[i] > 0xbf)
            return 0;
        c = (c << 6) | (s[i] & 0x3fu);
    }
    if ((n == 2 && c < 0x80) || (n == 3 && c < 0x800) ||
        (n == 4 && c < 0x10000) || c > 0x10ffff ||
        (c >= 0xd800 && c <= 0xdfff))
        return 0;
    *value = c;
    return n;
}

static int unsafe_scalar(uint32_t c)
{
    return c < 0x20 || (c >= 0x7f && c <= 0x9f) ||
           c == 0x061c || c == 0x200e || c == 0x200f ||
           (c >= 0x202a && c <= 0x202e) ||
           (c >= 0x2066 && c <= 0x206f) ||
           c == 0x180e || (c >= 0xfff9 && c <= 0xfffb) ||
           c == 0x2028 || c == 0x2029 || c == 0xfeff;
}

int irc_utf8_valid(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    uint32_t value;
    size_t n;
    if (!p)
        return 0;
    while (*p) {
        n = utf8_decode(p, &value);
        if (!n)
            return 0;
        p += n;
    }
    return 1;
}

int irc_valid_channel(const char *channel)
{
    const unsigned char *p;
    size_t n, k;
    uint32_t value;
    if (!channel)
        return 0;
    n = bounded_length(channel, IRC_TARGET_MAX);
    if (n < 2 || n > IRC_TARGET_MAX ||
        !strchr("#&+!", (unsigned char)channel[0]))
        return 0;
    p = (const unsigned char *)channel + 1;
    while (*p) {
        k = utf8_decode(p, &value);
        if (!k || unsafe_scalar(value) || value == ' ' || value == ',' ||
            value == ':')
            return 0;
        p += k;
    }
    return 1;
}

int irc_valid_target(const char *target)
{
    return irc_valid_nick(target) || irc_valid_channel(target);
}

static int ascii_hex(unsigned char c)
{
    return ascii_digit(c) || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int six_hex(const unsigned char *p)
{
    size_t i;
    for (i = 0; i < 6; ++i)
        if (!p[i] || !ascii_hex(p[i]))
            return 0;
    return 1;
}

/* Terminal-safe text, preserving only complete valid UTF-8 scalars. */
size_t irc_sanitize(char *dst, size_t cap, const char *src)
{
    const unsigned char *p = (const unsigned char *)src;
    size_t used = 0, n, digits;
    uint32_t value;
    char replacement;
    if (!dst || !cap)
        return 0;
    if (!p) {
        dst[0] = '\0';
        return 0;
    }
    while (*p) {
        /* Consume IRC styling, including its color parameters, so styling
         * cannot break an otherwise literal nick/keyword match. */
        if (*p == 0x03) {
            ++p;
            for (digits = 0; digits < 2 && ascii_digit(*p); ++digits)
                ++p;
            if (*p == ',' && ascii_digit(p[1])) {
                ++p;
                for (digits = 0; digits < 2 && ascii_digit(*p); ++digits)
                    ++p;
            }
            continue;
        }
        if (*p == 0x04) {
            ++p;
            if (six_hex(p)) {
                p += 6;
                if (*p == ',' && six_hex(p + 1))
                    p += 7;
            }
            continue;
        }
        if (*p == 0x02 || *p == 0x0f || *p == 0x11 || *p == 0x16 ||
            *p == 0x1d || *p == 0x1e || *p == 0x1f) {
            ++p;
            continue;
        }
        n = utf8_decode(p, &value);
        replacement = 0;
        if (!n) {
            n = 1;
            replacement = '?';
        } else if (unsafe_scalar(value)) {
            replacement = (value == '\r' || value == '\n' || value == '\t' ||
                           value == 0x2028 || value == 0x2029) ? ' ' : '?';
        }
        if (replacement) {
            if (used >= cap - 1)
                break;
            dst[used++] = replacement;
        } else {
            if (n > cap - 1 - used)
                break;
            memmove(dst + used, p, n);
            used += n;
        }
        p += n;
    }
    dst[used] = '\0';
    return used;
}

static int match_at(const unsigned char *text, const unsigned char *needle,
                    IrcCase mapping)
{
    while (*needle) {
        if (!*text || folded(*text, mapping) != folded(*needle, mapping))
            return 0;
        ++text;
        ++needle;
    }
    return 1;
}

int irc_contains(const char *text, const char *needle, IrcCase mapping)
{
    const unsigned char *p = (const unsigned char *)text;
    if (!p || !needle || !*needle)
        return 0;
    while (*p) {
        /* A start inside a UTF-8 continuation byte cannot match a valid
         * scalar sequence, but skipping it also makes the intent clear. */
        if ((*p & 0xc0) != 0x80 &&
            match_at(p, (const unsigned char *)needle, mapping))
            return 1;
        ++p;
    }
    return 0;
}

int irc_mentions(const char *text, const char *nick, IrcCase mapping)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t n;
    if (!p || !nick || !*nick)
        return 0;
    n = strlen(nick);
    while (*p) {
        if ((p == (const unsigned char *)text || !nick_character(p[-1])) &&
            match_at(p, (const unsigned char *)nick, mapping) &&
            !nick_character(p[n]))
            return 1;
        ++p;
    }
    return 0;
}

size_t irc_payload_budget(size_t wire_limit, const char *command,
                          const char *target, const char *nick,
                          const char *user, const char *host)
{
    size_t c, t, n, u, h, overhead, i;
    if (!command || !irc_valid_target(target))
        return 0;
    c = bounded_length(command, 32);
    if (!c || c > 32)
        return 0;
    for (i = 0; i < c; ++i)
        if (!ascii_alpha((unsigned char)command[i]))
            return 0;
    if (!wire_limit || wire_limit > IRC_BODY_MAX + 2u)
        wire_limit = IRC_BODY_MAX + 2u;
    t = strlen(target);
    n = nick && *nick ? bounded_length(nick, IRC_WIRE_MAX) : IRC_NICK_MAX;
    u = user && *user ? bounded_length(user, IRC_WIRE_MAX) : 32;
    h = host && *host ? bounded_length(host, IRC_WIRE_MAX) : 255;
    if (n > IRC_WIRE_MAX || u > IRC_WIRE_MAX || h > IRC_WIRE_MAX ||
        (nick && *nick && !field_safe(nick)) ||
        (user && *user && !field_safe(user)) ||
        (host && *host && !field_safe(host)))
        return 0;
    /* :nick!user@host COMMAND target :text\r\n */
    overhead = n + u + h + c + t + 9;
    return overhead < wire_limit ? wire_limit - overhead : 0;
}

/* Return ranges into the original UTF-8 text; callers
 * must keep that text alive for review and revalidate before sending.
 * Splitting never allocates messages or writes to a transport. */
int irc_split(const char *text, size_t budget, IrcChunk *chunks,
              size_t cap, size_t *count)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t offset = 0, start = 0, used = 0, n, total = 0;
    uint32_t value;
    if (!count)
        return 0;
    *count = 0;
    if (!p || (!chunks && cap))
        return 0;
    if (!*p)
        return 1;
    if (!budget || !cap)
        return 0;
    while (*p) {
        n = utf8_decode(p, &value);
        if (!n || n > budget || value == '\r' || value == '\n')
            return 0;
        if (used && n > budget - used) {
            if (total >= cap)
                return 0;
            chunks[total].offset = start;
            chunks[total++].length = used;
            start = offset;
            used = 0;
        }
        used += n;
        offset += n;
        p += n;
    }
    if (used) {
        if (total >= cap)
            return 0;
        chunks[total].offset = start;
        chunks[total++].length = used;
    }
    /* Until this commit, callers must ignore any tentative chunk entries. */
    *count = total;
    return 1;
}
