/* Membership/order regressions through actual IRC application handlers. */
#define main legacy_app_test_main
#include "test_app.c"
#undef main

static void roster_is(const Buffer *b, const char *expected)
{
    char copy[1024], *save, *nick;
    size_t at = 0;
    assert(strlen(expected) < sizeof copy);
    strcpy(copy, expected);
    for (nick = strtok_r(copy, " ", &save); nick; nick = strtok_r(NULL, " ", &save)) {
        assert(at < b->user_count && !strcmp(b->users[at].nick, nick));
        ++at;
    }
    assert(at == b->user_count);
}

static void ordering_and_modes(void)
{
    App *a = fresh(0);
    register_plain(a);
    feed(a, ":tester!u@h JOIN #test\r\n");
    Buffer *b = &a->buffers[channel(a)];
    feed(a, ":server 353 tester = #test :@Zulu +alice tester @+Bob [zed ^caret _under \\pipe ]end ~tilde `tick\r\n");
    roster_is(b, "^caret ~tilde _under `tick alice Bob tester Zulu [zed \\pipe ]end");
    feed(a, ":server 353 tester = #test :@ALICE\r\n:server 366 tester #test :end\r\n");
    roster_is(b, "^caret ~tilde _under `tick alice Bob tester Zulu [zed \\pipe ]end");
    assert(b->users[user_index(b, "alice")].modes == 3);
    assert(b->users[user_index(b, "Bob")].modes == 3);
    feed(a, ":ALICE!u@h JOIN #test\r\n:Aaron!u@h JOIN #test\r\n");
    roster_is(b, "^caret ~tilde _under `tick Aaron alice Bob tester Zulu [zed \\pipe ]end");
    assert(b->users[user_index(b, "alice")].modes == 3);
    feed(a, ":Bob!u@h NICK :zzNew\r\n:zzNew!u@h NICK :ZZNEW\r\n");
    roster_is(b, "^caret ~tilde _under `tick Aaron alice tester Zulu ZZNEW [zed \\pipe ]end");
    assert(b->users[user_index(b, "ZZNEW")].modes == 3);

    feed(a, ":server 005 tester CASEMAPPING=ascii :supported\r\n");
    roster_is(b, "[zed \\pipe ]end ^caret _under `tick Aaron alice tester Zulu ZZNEW ~tilde");
    feed(a, ":server MODE #test -o ZZNEW\r\n:[zed!u@h PART #test :leaving\r\n");
    assert(b->users[user_index(b, "ZZNEW")].modes == 2);
    roster_is(b, "\\pipe ]end ^caret _under `tick Aaron alice tester Zulu ZZNEW ~tilde");

    feed(a, ":server 005 tester CASEMAPPING=strict-rfc1459 :supported\r\n");
    roster_is(b, "^caret _under `tick Aaron alice tester Zulu ZZNEW \\pipe ]end ~tilde");
    feed(a, ":server KICK #test ^caret :gone\r\n:\\pipe!u@h QUIT :gone\r\n");
    roster_is(b, "_under `tick Aaron alice tester Zulu ZZNEW ]end ~tilde");

    feed(a, ":server 005 tester CASEMAPPING=rfc1459 :supported\r\n");
    roster_is(b, "~tilde _under `tick Aaron alice tester Zulu ZZNEW ]end");
    feed(a, ":^TILDE!u@h NICK :First\r\n");
    roster_is(b, "_under `tick Aaron alice First tester Zulu ZZNEW ]end");
    destroy(a);
}

static void full_roster(void)
{
    App *a = fresh(0);
    register_plain(a); feed(a, ":tester!u@h JOIN #test\r\n");
    Buffer *b = &a->buffers[channel(a)];
    for (size_t start = 0; start < APP_USERS; start += 10) {
        char wire[512];
        size_t used = (size_t)snprintf(wire, sizeof wire, ":server 353 tester = #test :");
        for (size_t i = start; i < start + 10 && i < APP_USERS; ++i)
            used += (size_t)snprintf(wire + used, sizeof wire - used, "%s%sn%04zu",
                                     i == start ? "" : " ", i % 2 ? "+" : "@", APP_USERS - i - 1);
        assert(used + 3 < sizeof wire); memcpy(wire + used, "\r\n", 3); feed(a, wire);
    }
    feed(a, ":server 366 tester #test :end\r\n");
    assert(b->user_count == APP_USERS && b->user_cap == APP_USERS);
    for (size_t i = 0; i < APP_USERS; ++i) {
        char expected[64]; snprintf(expected, sizeof expected, "n%04zu", i);
        assert(!strcmp(b->users[i].nick, expected));
        assert(b->users[i].modes == ((APP_USERS - i - 1) % 2 ? 2u : 1u));
    }
    feed(a, ":overflow!u@h JOIN #test\r\n");
    assert(b->user_count == APP_USERS && b->names_truncated);
    feed(a, ":n0000!u@h NICK :zzz\r\n");
    assert(b->user_count == APP_USERS && !strcmp(b->users[APP_USERS - 1].nick, "zzz"));
    assert(b->users[APP_USERS - 1].modes == 2);
    feed(a, ":zzz!u@h NICK :n0000\r\n:n2048!u@h PART #test :gone\r\n:replacement!u@h JOIN #test\r\n");
    assert(b->user_count == APP_USERS && user_index(b, "n2048") == SIZE_MAX);
    assert(!strcmp(b->users[0].nick, "n0000") && b->users[0].modes == 2);
    assert(!strcmp(b->users[APP_USERS - 1].nick, "replacement"));
    destroy(a);
}

int main(void)
{
    ordering_and_modes();
    full_roster();
    puts("roster: incremental NAMES/JOIN order, duplicate/prefix preservation, nick repositioning, all three CASEMAPPING values, removals and the unchanged 4096-user cap passed");
    return 0;
}
