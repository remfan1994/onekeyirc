#include "protocol.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_text(const char *s, IrcMessage *msg, char *buf)
{
    assert(strlen(s) <= IRC_WIRE_MAX + 1);
    strcpy(buf, s);
    return irc_parse(buf, msg);
}

static void parser_tests(void)
{
    char buf[IRC_WIRE_MAX + 2], big[IRC_WIRE_MAX + 2];
    IrcMessage m;
    size_t i;
    assert(parse_text(":n!u@h pRiVmSg #chan :hello  world", &m, buf));
    assert(!m.tags && !strcmp(m.prefix, "n!u@h"));
    assert(!strcmp(m.command, "PRIVMSG") && m.nparams == 2);
    assert(!strcmp(m.params[0], "#chan"));
    assert(!strcmp(m.params[1], "hello  world"));
    assert(parse_text("@time=2026-10-24T21:30:00Z;x=hi\\sthere :s 372 n :- MOTD", &m, buf));
    assert(!strcmp(m.tags, "time=2026-10-24T21:30:00Z;x=hi\\sthere"));
    assert(!strcmp(m.command, "372") && m.nparams == 2);
    assert(parse_text("CAP   *  LS  :multi-prefix sasl", &m, buf));
    assert(m.nparams == 3 && !strcmp(m.params[2], "multi-prefix sasl"));
    assert(parse_text("PING :", &m, buf) && m.nparams == 1 && !*m.params[0]);
    assert(parse_text("QUIT", &m, buf) && m.nparams == 0);
    assert(parse_text("PRIVMSG #c :\002bold\002\00304red\017", &m, buf));
    assert(parse_text("CMD a b c d e f g h i j k l m n :the fifteenth", &m, buf));
    assert(m.nparams == 15 && !strcmp(m.params[14], "the fifteenth"));
    assert(parse_text("CMD a b c d e f g h i j k l m n o", &m, buf));
    assert(m.nparams == 15);
    assert(!parse_text("CMD a b c d e f g h i j k l m n o p", &m, buf));
    assert(!m.command && m.nparams == 0);
    assert(!parse_text("", &m, buf));
    assert(!parse_text(" :a PING :x", &m, buf));
    assert(!parse_text("@bad", &m, buf));
    assert(!parse_text("@ PING :x", &m, buf));
    assert(!parse_text("@a=\033x PING :x", &m, buf));
    assert(!parse_text(": PING :x", &m, buf));
    assert(!parse_text(":bad\177 PING :x", &m, buf));
    assert(!parse_text("12 n :x", &m, buf));
    assert(!parse_text("1234 n :x", &m, buf));
    assert(!parse_text("1a3 n :x", &m, buf));
    assert(!parse_text("PRIVMSG #c\t :x", &m, buf));
    assert(!parse_text("PRIVMSG #c :hi\r\nJOIN #other", &m, buf));
    assert(!parse_text("PRIVMSG #c :hi\nJOIN #other", &m, buf));
    assert(!irc_parse(NULL, &m) && !m.command);
    assert(!irc_parse(buf, NULL));
    strcpy(big, "NOTICE n :");
    memset(big + 10, 'x', 500);
    big[510] = 0;
    assert(parse_text(big, &m, buf));
    big[510] = 'x'; big[511] = 0;
    assert(!parse_text(big, &m, buf));
    /* Maximum tags do not consume the independent 510-byte body. */
    big[0] = '@';
    memset(big + 1, 'x', 8189);
    big[8190] = ' ';
    strcpy(big + 8191, "NOTICE n :");
    memset(big + 8201, 'y', 500);
    big[8701] = 0;
    assert(parse_text(big, &m, buf));
    big[8190] = 'x'; big[8191] = ' ';
    strcpy(big + 8192, "PING :x");
    assert(!parse_text(big, &m, buf));
    strcpy(big, "@a=b ");
    memset(big + 5, ' ', 511);
    strcpy(big + 516, "PING :x");
    assert(!parse_text(big, &m, buf));
    for (i = 0; i < IRC_WIRE_MAX + 1; ++i)
        big[i] = 'x';
    big[IRC_WIRE_MAX + 1] = 0;
    assert(!parse_text(big, &m, buf));
}

static void identity_tests(void)
{
    char name[202];
    assert(irc_equal("AlICE", "alice", IRC_CASE_ASCII));
    assert(irc_equal("A[\\]^", "a{|}~", IRC_CASE_RFC1459));
    assert(!irc_equal("^", "~", IRC_CASE_STRICT));
    assert(irc_equal("[x]", "{X}", IRC_CASE_STRICT));
    assert(!irc_equal("[x]", "{X}", IRC_CASE_ASCII));
    assert(!irc_equal("alice", "alice_", IRC_CASE_RFC1459));
    assert(!irc_equal(NULL, "x", IRC_CASE_ASCII));
    assert(irc_valid_nick("tutankh-aten"));
    assert(irc_valid_nick("[_test`^]{|}\\~"));
    assert(!irc_valid_nick(""));
    assert(!irc_valid_nick("1test"));
    assert(!irc_valid_nick("-test"));
    assert(!irc_valid_nick("te st"));
    assert(!irc_valid_nick("test\r\nOPER"));
    assert(!irc_valid_nick("a,b"));
    assert(!irc_valid_nick("a:b"));
    assert(!irc_valid_nick("\303\251"));
    memset(name, 'a', 63); name[63] = 0;
    assert(irc_valid_nick(name));
    name[63] = 'a'; name[64] = 0;
    assert(!irc_valid_nick(name));
    assert(irc_valid_channel("#ircnow"));
    assert(irc_valid_channel("&local"));
    assert(irc_valid_channel("#caf\303\251"));
    assert(!irc_valid_channel("#"));
    assert(!irc_valid_channel("ircnow"));
    assert(!irc_valid_channel("#a,#b"));
    assert(!irc_valid_channel("#a b"));
    assert(!irc_valid_channel("#x\033"));
    assert(!irc_valid_channel("#x\302\233"));
    assert(!irc_valid_channel("#x\342\200\256"));
    assert(!irc_valid_channel("#\377"));
    name[0] = '#'; memset(name + 1, 'a', 199); name[200] = 0;
    assert(irc_valid_channel(name));
    name[200] = 'a'; name[201] = 0;
    assert(!irc_valid_channel(name));
    assert(irc_valid_target("alice") && irc_valid_target("#ircnow"));
    assert(!irc_valid_target("alice,bob") && !irc_valid_target("$*.host"));
}

/* Test-only exhaustive oracle; callers generate at most four pattern/text
 * bytes. Production matching has no recursion or exponential search. */
static int tiny_glob_oracle(const char *pattern, const char *text)
{
    if (!*pattern) return !*text;
    if (*pattern == '*')
        return tiny_glob_oracle(pattern + 1, text) ||
               (*text && tiny_glob_oracle(pattern, text + 1));
    return *text && (*pattern == '?' || *pattern == *text) &&
           tiny_glob_oracle(pattern + 1, text + 1);
}

static void ignore_tests(void)
{
    static const char *const valid[] = {
        "Alice", "[Nick]", "nick!*@*", "*!*@*", "A?i*e!~U?ER@*.Example.COM",
        "*![ab]@[XY]", "*!*@2001:db8::*", "*!*@cloak/#room;tag=$value"
    };
    static const char *const invalid[] = {
        "", "*", "n?", "nick@host", "nick!user", "!u@h", "n!@h", "n!u@",
        "n!u@h@x", "n!!u@h", "n@h!u", "a b!u@h", "1nick!u@h", "-nick!u@h",
        "n!u ser@h", "n!u@h\nother", "n!u@h\r", "n!u@h\t", "n!u@\177", "n!u@\303\251"
    };
    char maximum[IRC_IGNORE_MASK_MAX + 2], nick[65], user[65], host[257];
    char pattern[5], text[5], mask[16];
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
        assert(irc_valid_ignore_mask(valid[i]));
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!irc_valid_ignore_mask(invalid[i]));
        assert(!irc_ignore_match(invalid[i], "nick", "user", "host", IRC_CASE_RFC1459));
    }
    assert(!irc_valid_ignore_mask(NULL));
    assert(irc_ignore_match("Alice", "ALICE", NULL, NULL, IRC_CASE_ASCII));
    assert(!irc_ignore_match("Alice", "Alice_", "u", "h", IRC_CASE_ASCII));
    assert(!irc_ignore_match("Alice", NULL, "u", "h", IRC_CASE_ASCII));
    assert(irc_ignore_match("A?i*e!~U?ER@*.Example.COM", "alice", "~user", "irc.example.com", IRC_CASE_ASCII));
    assert(!irc_ignore_match("A?i*e!~U?ER@*.Example.COM", "alice", "~user", "example.com", IRC_CASE_ASCII));
    assert(irc_ignore_match("a*c!u@h", "ac", "u", "h", IRC_CASE_ASCII));
    assert(irc_ignore_match("a**?*!**@**", "ab", "u", "h", IRC_CASE_ASCII));
    assert(!irc_ignore_match("a?c!u@h", "ac", "u", "h", IRC_CASE_ASCII));
    assert(!irc_ignore_match("a?c!u@h", "abbc", "u", "h", IRC_CASE_ASCII));
    assert(irc_ignore_match("[N]*!U^@H[", "{n}ick", "u^", "h[", IRC_CASE_RFC1459));
    assert(!irc_ignore_match("[N]*!U^@H[", "{n}ick", "u^", "h{", IRC_CASE_RFC1459));
    assert(!irc_ignore_match("[N]*!U^@H[", "{n}ick", "u~", "h[", IRC_CASE_RFC1459));
    assert(!irc_ignore_match("[N]*!U^@H[", "{n}ick", "u^", "h[", IRC_CASE_ASCII));
    assert(irc_ignore_match("nick^!u@h", "nick~", "u", "h", IRC_CASE_RFC1459));
    assert(!irc_ignore_match("nick^!u@h", "nick~", "u", "h", IRC_CASE_STRICT));
    assert(irc_ignore_match("*!*@x[ab]", "n", "u", "x[ab]", IRC_CASE_ASCII));
    assert(!irc_ignore_match("*!*@x[ab]", "n", "u", "xa", IRC_CASE_ASCII));
    assert(irc_ignore_match("*!*@foo\\*", "n", "u", "foo\\bar", IRC_CASE_ASCII));
    assert(!irc_ignore_match("*!*@foo\\*", "n", "u", "foo*", IRC_CASE_ASCII));
    assert(!irc_ignore_match("*!*@*", "nick", NULL, "h", IRC_CASE_ASCII));
    assert(!irc_ignore_match("*!*@*", "nick", "u", "", IRC_CASE_ASCII));
    assert(!irc_ignore_match("*!*@*", "nick", "u@x", "h", IRC_CASE_ASCII));
    assert(!irc_ignore_match("*!*@*", "nick", "u", "h!x", IRC_CASE_ASCII));
    assert(!irc_ignore_match("*!*@*", "nick", "u", "h\033x", IRC_CASE_ASCII));
    assert(!irc_ignore_match("*!*@*", "nick", "\303\251", "h", IRC_CASE_ASCII));

    strcpy(maximum, "*!*@");
    memset(maximum + 4, 'a', IRC_IGNORE_MASK_MAX - 4);
    maximum[IRC_IGNORE_MASK_MAX] = 0;
    assert(irc_valid_ignore_mask(maximum));
    assert(irc_ignore_match(maximum, "n", "u", maximum + 4, IRC_CASE_ASCII));
    maximum[IRC_IGNORE_MASK_MAX] = 'a'; maximum[IRC_IGNORE_MASK_MAX + 1] = 0;
    assert(!irc_valid_ignore_mask(maximum));
    memset(nick, 'n', 63); nick[63] = 0;
    memset(user, 'u', 63); user[63] = 0;
    memset(host, 'h', 255); host[255] = 0;
    assert(irc_ignore_match("*!*@*", nick, user, host, IRC_CASE_RFC1459));
    nick[63] = 'n'; nick[64] = 0;
    assert(!irc_ignore_match("*!*@*", nick, user, host, IRC_CASE_RFC1459));
    nick[63] = 0; user[63] = 'u'; user[64] = 0;
    assert(!irc_ignore_match("*!*@*", nick, user, host, IRC_CASE_RFC1459));
    user[63] = 0; host[255] = 'h'; host[256] = 0;
    assert(!irc_ignore_match("*!*@*", nick, user, host, IRC_CASE_RFC1459));

    /* A near-match with many stars explodes under naive backtracking. */
    strcpy(maximum, "*!*@");
    for (size_t i = 0; i < 60; ++i) { maximum[4 + i * 2] = '*'; maximum[5 + i * 2] = 'a'; }
    maximum[124] = 'b'; maximum[125] = 0;
    memset(host, 'a', 255); host[255] = 0;
    assert(!irc_ignore_match(maximum, "n", "u", host, IRC_CASE_ASCII));
    host[254] = 'b';
    assert(irc_ignore_match(maximum, "n", "u", host, IRC_CASE_ASCII));

    /* Exhaust all short wildcard patterns against an independent oracle. */
    for (size_t plen = 1, possibilities = 4; plen <= 4; ++plen, possibilities *= 4) {
        for (size_t p = 0; p < possibilities; ++p) {
            size_t value = p;
            for (size_t i = 0; i < plen; ++i) { pattern[i] = "ab*?"[value % 4]; value /= 4; }
            pattern[plen] = 0;
            (void)snprintf(mask, sizeof(mask), "*!*@%s", pattern);
            for (size_t tlen = 1; tlen <= 4; ++tlen) {
                for (size_t t = 0; t < ((size_t)1 << tlen); ++t) {
                    for (size_t i = 0; i < tlen; ++i) text[i] = "ab"[(t >> i) & 1];
                    text[tlen] = 0;
                    assert(irc_ignore_match(mask, "n", "u", text, IRC_CASE_ASCII) == tiny_glob_oracle(pattern, text));
                }
            }
        }
    }
}

static void utf8_sanitize_tests(void)
{
    static const char *bad[] = {
        "\200", "\300\200", "\301\277", "\302", "\340\200\200",
        "\355\240\200", "\360\200\200\200", "\364\220\200\200",
        "\365\200\200\200", "\377", "\342\202", "\342x"
    };
    char result[256], inplace[256], small[8];
    size_t i, n;
    assert(irc_utf8_valid("ASCII"));
    assert(irc_utf8_valid("\302\200\340\240\200\357\277\277\360\220\200\200\364\217\277\277"));
    assert(irc_utf8_valid("\303\251\346\227\245\360\237\214\261"));
    assert(!irc_utf8_valid(NULL));
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(!irc_utf8_valid(bad[i]));
        n = irc_sanitize(result, sizeof(result), bad[i]);
        assert(n == strlen(result) && irc_utf8_valid(result));
    }
    irc_sanitize(result, sizeof(result), "\033]52;c;ZGFuZ2Vy\007\302\2332J\342\200\256hello\342\200\254");
    assert(!strcmp(result, "?]52;c;ZGFuZ2Vy??2J?hello?"));
    irc_sanitize(result, sizeof(result), "a\tB\r\nC\342\200\250D\342\200\251E");
    assert(!strcmp(result, "a B  C D E"));
    irc_sanitize(result, sizeof(result), "al\002i\00304,12c\017e\035!\035");
    assert(!strcmp(result, "alice!"));
    irc_sanitize(result, sizeof(result), "\004FF0000red\004FFFFFF,000000white\017");
    assert(!strcmp(result, "redwhite"));
    irc_sanitize(result, sizeof(result), "a\003,b\00302,c\004FFd");
    assert(!strcmp(result, "a,b,cFFd"));
    strcpy(inplace, "al\002i\00304,12c\017e \303\251");
    irc_sanitize(inplace, sizeof(inplace), inplace);
    assert(!strcmp(inplace, "alice \303\251"));
    for (i = 0; i < sizeof(small); ++i) {
        memset(small, 'z', sizeof(small));
        n = irc_sanitize(small, i, "x\346\227\245\303\251y");
        assert(n < i || i == 0);
        if (i) {
            assert(small[n] == 0 && irc_utf8_valid(small));
            assert(n == strlen(small));
        }
        assert(small[i] == 'z');
    }
    assert(irc_sanitize(NULL, 10, "x") == 0);
    assert(irc_sanitize(result, sizeof(result), NULL) == 0 && !*result);
}

static void mention_tests(void)
{
    assert(irc_mentions("hello, ALICE!", "alice", IRC_CASE_ASCII));
    assert(irc_mentions("alice: hi", "alice", IRC_CASE_ASCII));
    assert(irc_mentions("(alice)", "alice", IRC_CASE_ASCII));
    assert(irc_mentions("~alice", "^Alice", IRC_CASE_RFC1459));
    assert(!irc_mentions("malice", "alice", IRC_CASE_ASCII));
    assert(!irc_mentions("alice_", "alice", IRC_CASE_ASCII));
    assert(!irc_mentions("alice-bob", "alice", IRC_CASE_ASCII));
    assert(!irc_mentions("\303\251alice", "alice", IRC_CASE_ASCII));
    assert(!irc_mentions("alice\303\251", "alice", IRC_CASE_ASCII));
    assert(!irc_mentions("ali", "alice", IRC_CASE_ASCII));
    assert(!irc_mentions("x", "", IRC_CASE_ASCII));
    assert(!irc_mentions(NULL, "alice", IRC_CASE_ASCII));
    assert(irc_contains("An ecology message", "ECOLOGY", IRC_CASE_ASCII));
    assert(irc_contains("caf\303\251", "\303\251", IRC_CASE_ASCII));
    assert(!irc_contains("caf\303\211", "\303\251", IRC_CASE_ASCII));
    assert(!irc_contains("text", "", IRC_CASE_ASCII));
    assert(!irc_contains("text", "text-longer", IRC_CASE_ASCII));
}

static void budget_split_tests(void)
{
    char ascii[8193], restored[8193], one[1024];
    IrcChunk chunks[8192];
    const char *unicode = "ab \303\251\346\227\245\360\237\214\261  c";
    size_t count, i, k, budget;
    /* ':' + n!u@h + ' PRIVMSG #c :' + text + CRLF. */
    budget = irc_payload_budget(512, "PRIVMSG", "#c", "n", "u", "h");
    assert(budget == 491);
    assert(irc_payload_budget(4096, "PRIVMSG", "#c", "n", "u", "h") == budget);
    assert(irc_payload_budget(0, "PRIVMSG", "#c", "n", "u", "h") == budget);
    assert(irc_payload_budget(256, "PRIVMSG", "#c", "n", "u", "h") == 235);
    assert(irc_payload_budget(512, "PRIVMSG", "#c", NULL, NULL, NULL) == 144);
    assert(!irc_payload_budget(10, "PRIVMSG", "#c", "n", "u", "h"));
    assert(!irc_payload_budget(512, "PRIVMSG\nJOIN", "#c", "n", "u", "h"));
    assert(!irc_payload_budget(512, "PRIVMSG", "#c,#d", "n", "u", "h"));
    assert(!irc_payload_budget(512, "PRIVMSG", "#c", "n", "u\nx", "h"));
    memset(ascii, 'x', sizeof(ascii) - 1); ascii[sizeof(ascii) - 1] = 0;
    assert(irc_split(ascii, budget, chunks, 8192, &count));
    assert(count == 17);
    k = 0;
    for (i = 0; i < count; ++i) {
        int len;
        assert(chunks[i].offset == k && chunks[i].length <= budget);
        memcpy(restored + k, ascii + k, chunks[i].length);
        len = snprintf(one, sizeof(one), ":n!u@h PRIVMSG #c :%.*s\r\n",
                       (int)chunks[i].length, ascii + chunks[i].offset);
        assert(len <= 512);
        k += chunks[i].length;
    }
    restored[k] = 0;
    assert(!strcmp(ascii, restored));
    for (budget = 4; budget <= 32; ++budget) {
        assert(irc_split(unicode, budget, chunks, 8192, &count));
        k = 0;
        for (i = 0; i < count; ++i) {
            assert(chunks[i].offset == k && chunks[i].length <= budget);
            memcpy(one, unicode + k, chunks[i].length);
            one[chunks[i].length] = 0;
            assert(irc_utf8_valid(one));
            memcpy(restored + k, one, chunks[i].length);
            k += chunks[i].length;
        }
        restored[k] = 0;
        assert(!strcmp(unicode, restored));
    }
    assert(!irc_split(unicode, 3, chunks, 8192, &count) && count == 0);
    assert(!irc_split("hello", 2, chunks, 1, &count) && count == 0);
    assert(!irc_split("hello", 0, chunks, 8192, &count) && count == 0);
    assert(!irc_split("a\r\nJOIN #x", 20, chunks, 8192, &count));
    assert(!irc_split("\377", 20, chunks, 8192, &count));
    assert(!irc_split("hello", 20, NULL, 1, &count));
    assert(!irc_split("hello", 20, chunks, 1, NULL));
    assert(irc_split("", 0, NULL, 0, &count) && count == 0);
}

static uint32_t random_next(uint32_t *s)
{
    *s = *s * UINT32_C(1664525) + UINT32_C(1013904223);
    return *s;
}

static void bounded_fuzz_tests(void)
{
    char data[1025], parsed[1025], clean[1025];
    IrcMessage msg;
    IrcChunk chunks[1024];
    uint32_t rng = UINT32_C(0x91bacc02);
    size_t i, j, n, count;
    for (i = 0; i < 25000; ++i) {
        n = random_next(&rng) % 1024;
        for (j = 0; j < n; ++j) {
            unsigned char v = (unsigned char)(random_next(&rng) >> 24);
            data[j] = (char)(v ? v : 1);
        }
        data[n] = 0;
        memcpy(parsed, data, n + 1);
        if (irc_parse(parsed, &msg)) {
            assert(msg.command >= parsed && msg.command <= parsed + n);
            assert(msg.nparams <= 15);
            for (j = 0; j < msg.nparams; ++j)
                assert(msg.params[j] >= parsed && msg.params[j] <= parsed + n);
        }
        irc_sanitize(clean, sizeof(clean), data);
        assert(irc_utf8_valid(clean));
        for (j = 0; clean[j]; ++j)
            assert((unsigned char)clean[j] >= 32 && (unsigned char)clean[j] != 127);
        if (irc_split(clean, 32, chunks, 1024, &count)) {
            size_t offset = 0;
            for (j = 0; j < count; ++j) {
                assert(chunks[j].offset == offset && chunks[j].length <= 32);
                offset += chunks[j].length;
            }
            assert(offset == strlen(clean));
        } else {
            assert(!"sanitized text must split");
        }
        (void)irc_mentions(clean, "alice", IRC_CASE_RFC1459);
    }
}

/* Allocation boundaries make overreads past a truncated UTF-8, color or
 * parser token visible to memory sanitizers. The older randomized fixture
 * deliberately keeps a larger backing array, so these cover a different
 * boundary rather than merely adding more random input. */
static void exact_allocation_case(const unsigned char *bytes, size_t length,
                                  size_t capacity)
{
    char *input = malloc(length + 1u), *parsed = malloc(length + 1u);
    char *full = malloc(length + 1u), *inplace = malloc(length + 1u);
    char *limited = malloc(capacity ? capacity : 1u);
    IrcMessage message;
    size_t used;
    assert(input && parsed && full && inplace && limited);
    memcpy(input, bytes, length); input[length] = 0;
    memcpy(parsed, input, length + 1u);
    if (irc_parse(parsed, &message)) {
        assert(message.command >= parsed && message.command <= parsed + length);
        assert(message.nparams <= 15u);
        for (size_t j = 0; j < message.nparams; ++j)
            assert(message.params[j] >= parsed && message.params[j] <= parsed + length);
    } else {
        assert(!message.command && !message.tags && !message.prefix && !message.nparams);
    }
    used = irc_sanitize(full, length + 1u, input);
    assert(used <= length && used == strlen(full) && irc_utf8_valid(full));
    for (size_t j = 0; j < used; ++j)
        assert((unsigned char)full[j] >= 32u && (unsigned char)full[j] != 127u);
    memcpy(inplace, input, length + 1u);
    assert(irc_sanitize(inplace, length + 1u, inplace) == used);
    assert(!strcmp(full, inplace));
    limited[0] = 'z';
    used = irc_sanitize(limited, capacity, input);
    if (capacity) {
        assert(used < capacity && used == strlen(limited) && irc_utf8_valid(limited));
        assert(!strncmp(full, limited, used));
        /* Re-sanitizing cannot uncover terminal controls or change content. */
        assert(irc_sanitize(limited, capacity, limited) == used);
    } else assert(!used && limited[0] == 'z');
    (void)irc_valid_channel(input);
    (void)irc_valid_nick(input);
    (void)irc_mentions(input, "alice", IRC_CASE_RFC1459);
    (void)irc_contains(input, "NickServ", IRC_CASE_ASCII);
    free(limited); free(inplace); free(full); free(parsed); free(input);
}

static void exact_allocation_tests(void)
{
    unsigned char bytes[IRC_WIRE_MAX + 1u];
    static const char *const seeds[] = {
        "@time=2026-11-08T00:00:00Z :alice!user@host PRIVMSG #room :hello",
        "\033]52;c;ZGFuZ2Vy\007\302\2332J\342\200\256text",
        "\004FF0000,FFFFFFtext\00312,03more",
        "\360\237\222\251\303\251\342\200\250",
        ":server.example CAP * LS * :multi-prefix sasl=PLAIN,EXTERNAL",
        "CMD a b c d e f g h i j k l m n :last"
    };
    uint32_t rng = UINT32_C(0xa55317c9);
    size_t cases = 0;
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        size_t length = strlen(seeds[i]);
        for (size_t cut = 0; cut <= length; ++cut)
            for (size_t capacity = 0; capacity <= cut + 1u; ++capacity) {
                exact_allocation_case((const unsigned char *)seeds[i], cut, capacity);
                ++cases;
            }
    }
    for (size_t first = 1; first < 256u; ++first) {
        bytes[0] = (unsigned char)first;
        exact_allocation_case(bytes, 1u, first % 3u); ++cases;
        for (size_t second = 1; second < 256u; ++second) {
            bytes[1] = (unsigned char)second;
            exact_allocation_case(bytes, 2u, second % 4u); ++cases;
        }
    }
    for (size_t i = 0; i < 20000u; ++i) {
        size_t length = i % 100u ? random_next(&rng) % 256u :
                        random_next(&rng) % sizeof(bytes);
        for (size_t j = 0; j < length; ++j) {
            unsigned char c = (unsigned char)(random_next(&rng) >> 24);
            bytes[j] = c ? c : 1u;
        }
        exact_allocation_case(bytes, length, random_next(&rng) % (length + 2u));
        ++cases;
    }
    printf("protocol: %zu exact-allocation/truncation security cases passed\n", cases);
}

int main(void)
{
    parser_tests();
    identity_tests();
    ignore_tests();
    utf8_sanitize_tests();
    mention_tests();
    budget_split_tests();
    bounded_fuzz_tests();
    exact_allocation_tests();
    puts("protocol: parser, limits, UTF-8, sanitizer, mentions, bounded ignore masks, splitter and 25000 randomized cases passed");
    return 0;
}
