#include "commands.h"
#include "editor.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef COMMANDS_TEST_ALLOC
static int fail_allocation;
void *commands_test_malloc(size_t size)
{
    return fail_allocation ? NULL : malloc(size);
}
#endif

static void match_is(const CommandHistory *history, const char *prefix,
                     size_t ordinal, const char *expected)
{
    const char *actual = commands_history_match(history, prefix, ordinal);
    assert(expected ? actual && !strcmp(actual, expected) : actual == NULL);
}

static void catalog_tests(void)
{
    static const char *const names[] = {
        "autoauth", "autocommands", "autoconnect", "autojoin", "away", "back", "background", "bind", "bindings", "cancel", "channel", "channels", "cl", "clear", "clearinput", "close",
        "connect", "dcc", "disconnect", "help", "ignore", "join", "keymap",
        "list", "ls", "manual", "me", "mentions", "mouse", "msg", "next", "nick", "panel", "part",
        "pm", "prev", "ql", "queries", "query", "quickstart", "quit", "save", "saveconf", "server", "servers", "set",
        "settings", "sl", "soju", "summary", "topic", "transfers", "unbind", "unignore", "unread", "users",
        "view", "views", "watch", "whois", "znc"
    };
    const CommandSpec *out[64];
    size_t count = commands_suggest("/", out, 64);
    assert(count == sizeof(names) / sizeof(names[0]));
    for (size_t i = 0; i < count; ++i) {
        char line[96];
        assert(!strcmp(out[i]->name, names[i]));
        assert(out[i]->usage[0] == '/' && *out[i]->summary);
        assert(!strncmp(out[i]->usage + 1, names[i], strlen(names[i])));
        assert(out[i]->usage[strlen(names[i]) + 1u] == 0 ||
               out[i]->usage[strlen(names[i]) + 1u] == ' ');
        snprintf(line, sizeof(line), "/%s anything", names[i]);
        assert(commands_find(line) == out[i]);
        for (char *p = line; *p && *p != ' '; ++p)
            if (*p >= 'a' && *p <= 'z') *p = (char)(*p - ('a' - 'A'));
        assert(commands_find(line) == out[i]);
    }
    assert(commands_suggest("", out, 64) == count);
    assert(commands_find("join") && !strcmp(commands_find("join")->name, "join"));
    assert(commands_find("/  JOIN #one") == commands_find("/join"));
    static const char *const unknown[] = {
        "", "/", "/ ", "/jo", "/joined", "/pms", "/lc", "/lq", "/raw PASS secret",
        "/quote PASS secret", "/oper name secret", "/authenticate secret",
        "//join", "/whois\nquit", "/whois\tNick", "/\xc3\xb6", "/123"
    };
    assert(!commands_find(NULL));
    for (size_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); ++i)
        assert(!commands_find(unknown[i]));
}

static void suggestion_tests(void)
{
    const CommandSpec *out[64];
    const CommandSpec *sentinel = commands_find("/join");
    assert(commands_suggest("/m", out, 64) == 5);
    assert(!strcmp(out[0]->name, "manual"));
    assert(!strcmp(out[1]->name, "me"));
    assert(!strcmp(out[2]->name, "mentions"));
    assert(!strcmp(out[3]->name, "mouse"));
    assert(!strcmp(out[4]->name, "msg"));
    assert(commands_find("/cl") && !strcmp(commands_find("/cl")->usage, "/cl"));
    assert(commands_suggest("/ql", out, 64) == 1 && !strcmp(out[0]->name, "ql"));
    assert(commands_suggest("/queries", out, 64) == 1 && !strcmp(out[0]->name, "queries"));
    assert(commands_suggest("/sl", out, 64) == 1 && !strcmp(out[0]->name, "sl"));
    assert(commands_suggest("/back", out, 64) == 2);
    assert(!strcmp(out[0]->name, "back") && !strcmp(out[1]->name, "background"));
    assert(commands_suggest("/background", out, 64) == 1);
    assert(!strcmp(out[0]->name, "background"));
    assert(commands_suggest("/Jo", out, 64) == 1 && out[0] == sentinel);
    assert(commands_suggest("/clear", out, 64) == 2);
    assert(!strcmp(out[0]->name, "clear") && !strcmp(out[1]->name, "clearinput"));
    assert(commands_suggest("/clearinput", out, 64) == 1);
    assert(!strcmp(out[0]->name, "clearinput"));
    assert(strcmp(out[0]->summary, commands_find("/clear")->summary));
    assert(commands_suggest("JO", out, 64) == 1 && out[0] == sentinel);
    assert(commands_suggest("/join #private TOP_SECRET", out, 64) == 1);
    assert(out[0] == sentinel && !strstr(out[0]->usage, "TOP_SECRET"));
    assert(commands_suggest("/msg NickServ identify TOP_SECRET", out, 64) == 1);
    assert(!strcmp(out[0]->name, "msg") && !strstr(out[0]->usage, "TOP_SECRET"));
    assert(commands_suggest("/jo ", out, 64) == 0);
    assert(commands_suggest("/missing", out, 64) == 0);
    assert(commands_suggest("/\xc3\xa9", out, 64) == 0);
    out[0] = out[1] = sentinel;
    assert(commands_suggest("", out, 1) == 1);
    assert(!strcmp(out[0]->name, "autoauth") && out[1] == sentinel);
    out[0] = sentinel;
    assert(commands_suggest("", out, 0) == 0 && out[0] == sentinel);
    assert(commands_suggest("", NULL, 5) == 0);
    assert(commands_suggest(NULL, out, 5) == 0);
}

static void paged_suggestion_tests(void)
{
    const CommandSpec *all[128], *page[130];
    const CommandSpec *sentinel = commands_find("/join");
    size_t count = commands_suggest("/", all, 128), total = SIZE_MAX;
    assert(count == 61);
    assert(!commands_suggest_page("/", NULL, 0, 0, &total) && total == count);
    static const size_t capacities[] = {1, 3, 7, 64, 128};
    for (size_t c = 0; c < sizeof capacities / sizeof capacities[0]; ++c) {
        size_t capacity = capacities[c];
        for (size_t offset = 0; offset <= count + 1; ++offset) {
            for (size_t i = 0; i < sizeof page / sizeof page[0]; ++i) page[i] = sentinel;
            size_t expected = offset < count ? count - offset : 0;
            if (expected > capacity) expected = capacity;
            size_t shown = commands_suggest_page("/", page, capacity, offset, &total);
            assert(shown == expected && total == count);
            for (size_t i = 0; i < shown; ++i) assert(page[i] == all[offset + i]);
            for (size_t i = shown; i < sizeof page / sizeof page[0]; ++i) assert(page[i] == sentinel);
        }
    }
    assert(!commands_suggest_page("/", page, 3, SIZE_MAX, &total) && total == count);
    assert(!commands_suggest_page("/", NULL, 3, 5, &total) && total == count);
    assert(!commands_suggest_page("/", page, 0, 5, &total) && total == count);
    assert(commands_suggest_page("/", page, 2, 1, NULL) == 2);
    assert(page[0] == all[1] && page[1] == all[2]);
    assert(commands_suggest_page("/M", page, 2, 2, &total) == 2 && total == 5);
    assert(!strcmp(page[0]->name, "mentions") && !strcmp(page[1]->name, "mouse"));
    assert(commands_suggest_page("/join #secret HIDDEN_KEY", page, 2, 0, &total) == 1 && total == 1);
    assert(page[0] == sentinel && !strstr(page[0]->usage, "HIDDEN_KEY"));
    assert(!commands_suggest_page("/join #secret HIDDEN_KEY", page, 2, 1, &total) && total == 1);
    static const char *const malformed[] = {NULL, "/missing", "/jo ", "//join", "/join\tchannel", "/\303\251"};
    for (size_t i = 0; i < sizeof malformed / sizeof malformed[0]; ++i) {
        total = SIZE_MAX; page[0] = sentinel;
        assert(!commands_suggest_page(malformed[i], page, 1, 0, &total));
        assert(!total && page[0] == sentinel);
    }
}

static void alias_tests(void)
{
    const CommandSpec *out[64];
    char expanded[256];
    assert(commands_find("/j") == commands_find("/join"));
    assert(commands_find("/PRIVMSG NickServ secret") == commands_find("/msg"));
    assert(!commands_find("/con") && !commands_find("/dis"));
    assert(commands_suggest("/con", out, 64) == 2);
    assert(!strcmp(out[0]->name, "connect") && !strcmp(out[1]->name, "panel"));
    assert(commands_suggest("/conn", out, 64) == 1 && !strcmp(out[0]->name, "connect"));
    assert(commands_suggest("/configure ", out, 64) == 1 && !strcmp(out[0]->name, "panel"));
    assert(commands_suggest("/nick", out, 64) == 2);
    assert(commands_expand_alias("/MESSAGE  Alice  caf\303\251 $(literal) `literal`", expanded, sizeof expanded) == 1);
    assert(!strcmp(expanded, "/msg  Alice  caf\303\251 $(literal) `literal`"));
    assert(commands_expand_alias("/j #one secret", expanded, 5) == -1);
    assert(!commands_expand_alias("/jo #one", expanded, sizeof expanded));
    assert(!commands_expand_alias("//j #one", expanded, sizeof expanded));
    assert(!commands_expand_alias("ordinary text", expanded, sizeof expanded));
    assert(commands_expand_alias(NULL, expanded, sizeof expanded) == -1);
}

static void history_tests(void)
{
    CommandHistory history = {0};
    match_is(&history, "", 0, NULL);
    assert(commands_history_add(&history, "/join #one") == 1);
    assert(commands_history_add(&history, "/join #two") == 1);
    size_t bytes = history.bytes;
    assert(commands_history_add(&history, "/join #two") == 0);
    assert(history.count == 2 && history.bytes == bytes);
    assert(commands_history_add(&history, "/JOIN #three") == 1);
    assert(commands_history_add(&history, "/watch add caf\xc3\xa9") == 1);
    assert(commands_history_add(&history, "/msg Alice Hello world") == 1);
    match_is(&history, "", 0, "/msg Alice Hello world");
    match_is(&history, "/", 1, "/watch add caf\xc3\xa9");
    match_is(&history, "/jo", 0, "/JOIN #three");
    match_is(&history, "JO", 1, "/join #two");
    match_is(&history, "/join", 2, "/join #one");
    match_is(&history, "/join", 3, NULL);
    match_is(&history, "/join", SIZE_MAX, NULL);
    match_is(&history, "/WATCH add c", 0, "/watch add caf\xc3\xa9");
    match_is(&history, "/WATCH ADD", 0, NULL);
    match_is(&history, "/msg Alice H", 0, "/msg Alice Hello world");
    match_is(&history, "/msg alice", 0, NULL);
    assert(commands_history_add(&history, "/join #one") == 1);
    match_is(&history, "/join", 0, "/join #one");
    match_is(&history, "/join", 3, "/join #one");
    assert(commands_history_add(&history, "/watch add e\xcc\x81 \xf0\x9f\x99\x82") == 1);
    match_is(&history, "/watch add e", 0, "/watch add e\xcc\x81 \xf0\x9f\x99\x82");
    commands_history_free(&history);
    assert(history.count == 0 && history.bytes == 0);
    for (size_t i = 0; i < COMMAND_HISTORY_MAX_ENTRIES; ++i)
        assert(!history.entries[i]);
    commands_history_free(&history);
    commands_history_free(NULL);
    commands_history_init(NULL);
    commands_history_init(&history);
    assert(!commands_history_add(NULL, "/quit"));
    assert(!commands_history_add(&history, NULL));
    assert(!commands_history_match(NULL, "", 0));
    assert(!commands_history_match(&history, NULL, 0));
}

static void privacy_and_invalid_tests(void)
{
    static const char *const rejected[] = {
        "plain chat", "nick name", "", "/", "//literal slash", "/unknown text",
        "/join #private KEY", "/JOIN   #private   KEY", "/join #private :KEY",
        "/join #private KEY more", "/msg NickServ identify PASSWORD",
        "/MSG nIcKsErV REGISTER PASSWORD user@example.invalid",
        "/msg NickServ help", "/msg NS identify PASSWORD", "/msg AuthServ auth USER PASSWORD",
        "/msg NickServ@services.example.invalid IDENTIFY PASSWORD",
        "/msg ChanServ SET #private KEY PASSWORD", "/msg OperServ OPER USER PASSWORD",
        "/msg UnknownBot LOGIN USER PASSWORD", "/msg Q AUTH USER PASSWORD",
        "/msg OtherBot AUTHENTICATE TOKEN", "/msg OtherBot PASS PASSWORD",
        "/msg OtherBot :IDENTIFY PASSWORD", "/msg OtherBot SET PASSWORD PASSWORD",
        "/msg OtherBot SASET someone PASSWORD PASSWORD",
        "/msg OtherBot sAsEt someone PaSs PASSWORD",
        "/msg OtherBot ::IDENTIFY PASSWORD", "/msg OtherBot : : IDENTIFY PASSWORD",
        "/msg OtherBot SET :PASSWORD PASSWORD", "/msg OtherBot SASET someone : PASS PASSWORD",
        "/raw PASS PASSWORD", "/quote AUTHENTICATE TOKEN", "/pass PASSWORD",
        "/authenticate TOKEN", "/auth USER PASSWORD", "/identify PASSWORD",
        "/autoauth status", "/autocommands list", "/autocommand add /msg bot secret",
        "/znc", "/znc SetPassword PASSWORD", "/SoJu user update -password PASSWORD",
        "/msg *status arbitrary PASSWORD", "/message *sasl Set PASSWORD",
        "/msg BouncerServ anything PASSWORD", "/privmsg bOuNcErSeRv@host anything PASSWORD",
        "/message NickServ identify PASSWORD", "/privmsg Q AUTH USER PASSWORD", "/j #private KEY", "/autoauth sasl account gpg /private/secret.gpg",
        "/autoauth sasl account plaintext PASSWORD",
        "/nickserv identify PASSWORD", "/ns identify PASSWORD", "/oper USER PASSWORD",
        "/watch add line\nbreak", "/watch add carriage\rreturn", "/watch add tab\tvalue",
        "/watch add escape\033[31m", "/watch add \177", "/watch add \002bold",
        "/watch add \302\233", "/watch add \342\200\256hidden",
        "/watch add \357\273\277", "/watch add \377", "/watch add \300\257",
        "/watch add \360\237\231"
    };
    CommandHistory history = {0};
    assert(commands_history_add(&history, "/join #public") == 1);
    size_t bytes = history.bytes;
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        int recorded = commands_history_add(&history, rejected[i]);
        if (recorded) fprintf(stderr, "Private/invalid command test case %zu was retained in history.\n", i);
        assert(recorded == 0);
        assert(history.count == 1 && history.bytes == bytes);
        match_is(&history, "", 0, "/join #public");
    }
    assert(commands_history_add(&history, "/join #public   ") == 1);
    assert(commands_history_add(&history, "/pm NickServ") == 1); /* No secret. */
    match_is(&history, "/msg", 0, NULL);
    commands_history_free(&history);
}

static void count_and_byte_bounds(void)
{
    CommandHistory history = {0};
    char command[64];
    for (unsigned i = 0; i < 70; ++i) {
        snprintf(command, sizeof(command), "/nick n%03u", i);
        assert(commands_history_add(&history, command) == 1);
        assert(history.count <= COMMAND_HISTORY_MAX_ENTRIES);
        assert(history.bytes <= COMMAND_HISTORY_MAX_BYTES);
    }
    assert(history.count == 64);
    match_is(&history, "", 0, "/nick n069");
    match_is(&history, "", 63, "/nick n006");
    match_is(&history, "", 64, NULL);
    match_is(&history, "/nick n005", 0, NULL);
    commands_history_free(&history);

    char *large = malloc(ED_MAX_BYTES + 2u);
    assert(large);
    memset(large, 'x', ED_MAX_BYTES + 1u);
    memcpy(large, "/msg Alice ", 11);
    large[ED_MAX_BYTES] = 0;
    for (unsigned i = 0; i < 5; ++i) {
        large[ED_MAX_BYTES - 1u] = (char)('a' + i);
        assert(commands_history_add(&history, large) == 1);
        assert(history.bytes <= COMMAND_HISTORY_MAX_BYTES);
    }
    assert(history.count == 3 && history.bytes == 3u * (ED_MAX_BYTES + 1u));
    assert(commands_history_match(&history, "", 0)[ED_MAX_BYTES - 1u] == 'e');
    assert(commands_history_match(&history, "", 2)[ED_MAX_BYTES - 1u] == 'c');
    match_is(&history, "", 3, NULL);
    size_t bytes = history.bytes;
    large[ED_MAX_BYTES] = 'x'; large[ED_MAX_BYTES + 1u] = 0;
    assert(commands_history_add(&history, large) == 0);
    assert(history.bytes == bytes && history.count == 3);
    assert(!commands_find(large));
    assert(!commands_suggest(large, (const CommandSpec *[1]){NULL}, 1));
    assert(!commands_history_match(&history, large, 0));
    commands_history_free(&history);
    free(large);

    for (unsigned i = 0; i < 10000; ++i) {
        snprintf(command, sizeof(command), "/nick Test%05u", i);
        assert(commands_history_add(&history, command) == 1);
        if (!(i % 7u)) assert(!commands_history_add(&history, "/join #private KEY"));
    }
    assert(history.count == 64 && history.bytes <= COMMAND_HISTORY_MAX_BYTES);
    match_is(&history, "/nick Test", 0, "/nick Test09999");
    match_is(&history, "/nick Test", 63, "/nick Test09936");
    commands_history_free(&history);
}

#ifdef COMMANDS_TEST_ALLOC
static void allocation_failure_is_transactional(void)
{
    CommandHistory history = {0};
    char line[64];
    for (unsigned i = 0; i < COMMAND_HISTORY_MAX_ENTRIES; ++i) {
        snprintf(line, sizeof(line), "/nick n%03u", i);
        assert(commands_history_add(&history, line) == 1);
    }
    size_t bytes = history.bytes;
    fail_allocation = 1;
    assert(commands_history_add(&history, "/nick NEW") == -1);
    assert(history.bytes == bytes && history.count == COMMAND_HISTORY_MAX_ENTRIES);
    match_is(&history, "", 0, "/nick n063");
    match_is(&history, "", 63, "/nick n000");
    fail_allocation = 0;
    assert(commands_history_add(&history, "/nick NEW") == 1);
    match_is(&history, "", 0, "/nick NEW");
    match_is(&history, "", 63, "/nick n001");
    commands_history_free(&history);
}
#endif

static void ranked_suggestions(void)
{
    Config config = {0};
    CommandHistory history = {0};
    CommandSuggestion rows[128], page[2];
    size_t count, total;
    count = commands_suggest_ranked("/", CONFIG_SUGGEST_CONTEXTUAL, COMMAND_CONTEXT_CLIENT,
                                    &history, &config, rows, 128, 0, &total);
    assert(count == 61 && total == count && !strcmp(rows[0].spec->name, "summary"));
    assert(!strcmp(rows[3].spec->name, "quit"));
    commands_suggest_ranked("/", CONFIG_SUGGEST_CONTEXTUAL, COMMAND_CONTEXT_SERVER,
                            &history, &config, rows, 128, 0, &total);
    assert(!strcmp(rows[0].spec->name, "join") && !strcmp(rows[1].spec->name, "list"));
    commands_suggest_ranked("/", CONFIG_SUGGEST_CONTEXTUAL, COMMAND_CONTEXT_CHANNEL,
                            &history, &config, rows, 128, 0, &total);
    assert(!strcmp(rows[0].spec->name, "me") && !strcmp(rows[1].spec->name, "topic"));
    commands_suggest_ranked("/", CONFIG_SUGGEST_CONTEXTUAL, COMMAND_CONTEXT_QUERY,
                            &history, &config, rows, 128, 0, &total);
    assert(!strcmp(rows[0].spec->name, "me") && !strcmp(rows[1].spec->name, "whois"));
    assert(commands_suggest_ranked("/quit", CONFIG_SUGGEST_CONTEXTUAL, COMMAND_CONTEXT_CHANNEL,
                                   &history, &config, rows, 128, 0, &total) == 1);
    assert(!strcmp(rows[0].spec->name, "quit"));
    commands_suggest_ranked("/", CONFIG_SUGGEST_COMMON, COMMAND_CONTEXT_DISCONNECTED,
                            &history, &config, rows, 128, 0, &total);
    assert(!strcmp(rows[0].spec->name, "connect"));
    commands_suggest_ranked("/", CONFIG_SUGGEST_ALPHABETICAL, COMMAND_CONTEXT_CHANNEL,
                            &history, &config, rows, 128, 0, &total);
    assert(!strcmp(rows[0].spec->name, "autoauth"));
    commands_usage_record(&config, "/topic a title");
    commands_usage_record(&config, "/j #test SECRET");
    commands_usage_record(&config, "/join #test");
    commands_usage_record(&config, "/raw FORBIDDEN");
    assert(config.command_usage_count == 2);
    assert(!strcmp(config.command_usage[1].name, "join") && config.command_usage[1].count == 2);
    assert(!memchr(config.command_usage, '#', sizeof config.command_usage));
    commands_suggest_ranked("/", CONFIG_SUGGEST_FREQUENT, COMMAND_CONTEXT_CLIENT,
                            &history, &config, rows, 128, 0, &total);
    assert(!strcmp(rows[0].spec->name, "join") && !strcmp(rows[1].spec->name, "topic"));
    assert(commands_usage_reset(&config, "join"));
    commands_suggest_ranked("/", CONFIG_SUGGEST_FREQUENT, COMMAND_CONTEXT_CLIENT,
                            &history, &config, rows, 128, 0, &total);
    assert(!strcmp(rows[0].spec->name, "topic"));
    config.command_usage[0].count = CONFIG_COMMAND_USAGE_LIMIT;
    commands_usage_record(&config, "/topic");
    assert(config.command_usage[0].count == CONFIG_COMMAND_USAGE_LIMIT);
    assert(commands_history_add(&history, "/join #one") == 1);
    assert(commands_history_add(&history, "/whois Alice") == 1);
    assert(commands_history_add(&history, "/join #two") == 1);
    assert(commands_suggest_ranked("/jo", CONFIG_SUGGEST_HISTORY, COMMAND_CONTEXT_CLIENT,
                                   &history, &config, rows, 128, 0, &total) == 2 && total == 2);
    assert(!strcmp(rows[0].text, "/join #two") && rows[0].history_index == 0);
    assert(!strcmp(rows[1].text, "/join #one") && rows[1].history_index == 2);
    assert(commands_suggest_ranked("/jo", CONFIG_SUGGEST_HISTORY, COMMAND_CONTEXT_CLIENT,
                                   &history, &config, page, 1, 1, &total) == 1 && total == 2);
    assert(!strcmp(page[0].text, "/join #one"));
    assert(commands_history_remove(&history, rows[0].history_index));
    assert(!commands_history_remove(&history, SIZE_MAX));
    assert(commands_suggest_ranked("/jo", CONFIG_SUGGEST_HISTORY, COMMAND_CONTEXT_CLIENT,
                                   &history, &config, rows, 128, 0, &total) == 1);
    assert(!strcmp(rows[0].text, "/join #one"));
    commands_history_free(&history);
}

int main(void)
{
    catalog_tests();
    suggestion_tests();
    paged_suggestion_tests();
    alias_tests();
    history_tests();
    privacy_and_invalid_tests();
    count_and_byte_bounds();
    ranked_suggestions();
#ifdef COMMANDS_TEST_ALLOC
    allocation_failure_is_transactional();
#endif
    puts("commands: complete catalog, five suggestion modes, context ranking, personal count reset, history deletion, bounded pagination and private history passed");
    return 0;
}
