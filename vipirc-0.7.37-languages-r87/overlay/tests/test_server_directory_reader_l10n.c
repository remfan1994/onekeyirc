#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#define main legacy_idle_commands_main
#include "test_idle_commands.c"
#undef main

static void expect(int translated, const char *key, const IrcDisplayField *fields, size_t n)
{
    char rendered[2048];
    const char *(*tr)(const char *) = translated ? irc_tr : NULL;
    assert(irc_display_fields_valid(tr ? tr(key) : key, fields, n));
    assert(irc_display_render_lookup(rendered, sizeof rendered, key, fields, n, tr));
    if (!strstr(output, rendered)) fprintf(stderr, "Expected: %s\nActual: %s\n", rendered, output);
    assert(strstr(output, rendered));
}
static void readers(int translated)
{
    Fixture f; init(&f, 1); App *a = app(&f);
    const char *(*tr)(const char *) = translated ? irc_tr : NULL;
    f.commands.translate = tr;
    assert(run(&f, "/servers") == IDLE_COMMAND_DONE);
    assert(strstr(output, "fixture.invalid:") && strstr(output, "tester"));
    assert(strstr(output, tr ? tr(" (selected)") : " (selected)"));
    assert(run(&f, "/connect") == IDLE_COMMAND_DONE); pump(a);
    assert(a->registered);
    assert(run(&f, "/autojoin") == IDLE_COMMAND_DONE);
    const char *join_msg = "Join requests use the running list after registration. Channels still not joined may be pending or rejected; see this server's replies.";
    assert(strstr(output, tr ? tr(join_msg) : join_msg));
    assert(run(&f, "/autojoin add #next") == IDLE_COMMAND_DONE);
    assert(run(&f, "/autojoin list") == IDLE_COMMAND_DONE);
    const char *pending = "Pending channels do not change this connection or automatic retries. /disconnect then /connect applies them; /save persists them.";
    assert(strstr(output, tr ? tr(pending) : pending) && strstr(output, "#next"));
    assert(run(&f, "/list --limit 1") == IDLE_COMMAND_DONE);
    assert(a->list.active);
    free(a->list.entries);
    a->list.entries = calloc(2, sizeof *a->list.entries); assert(a->list.entries);
    a->list.count = a->list.capacity = 2; a->list.matched = 4; a->list.received = 7;
    a->list.truncated = 1; ++a->list.revision;
    strcpy(a->list.entries[0].channel, "#literal");
    strcpy(a->list.entries[0].topic, "topic %n {literal}");
    a->list.entries[0].users = 987654321ULL;
    strcpy(a->list.entries[1].channel, "#empty");
    a->list.entries[1].users = 42;
    assert(run(&f, "/list show --sort users --desc --limit 1") == IDLE_COMMAND_DONE);
    expect(translated, "showing {visible}, {cached} cached, {matched} matched, {received} received", (IrcDisplayField[]){{"visible","1"},{"cached","2"},{"matched","4"},{"received","7"}}, 4);
    expect(translated, "  {channel} | {count} users | {topic}", (IrcDisplayField[]){{"channel","#literal"},{"count","987654321"},{"topic","topic %n {literal}"}}, 3);
    expect(translated, "Display limited to {visible} of {cached} cached matches.", (IrcDisplayField[]){{"visible","1"},{"cached","2"}}, 2);
    char limit[32]; snprintf(limit, sizeof limit, "%u", (unsigned)APP_LIST_MAX);
    expect(translated, "Channel cache reached its hard safety limit of {limit} entries.", (IrcDisplayField[]){{"limit",limit}}, 1);
    assert(strstr(output, tr ? tr(" | still receiving") : " | still receiving"));
    assert(run(&f, "/list show --limit 2") == IDLE_COMMAND_DONE);
    assert(strstr(output, tr ? tr("(no topic)") : "(no topic)"));
    assert(run(&f, "/saveconf") == IDLE_COMMAND_ERROR);
    const char *off = "OFFLINE TEST MODE: /saveconf does not write configuration.";
    assert(strstr(output, tr ? tr(off) : off) && access(f.path,F_OK) != 0);
    cleanup(&f);

    init(&f,0); f.commands.translate = tr; a=app(&f);
    strcpy(a->cfg.password,"NOT-READER-TEXT");
    assert(run(&f,"/saveconf") == IDLE_COMMAND_DONE);
    expect(translated,"Saved configuration for {count} server(s). /connect will reuse the selected connection next time.",(IrcDisplayField[]){{"count","1"}},1);
    assert(!strstr(output,"NOT-READER-TEXT"));
    f.client->slots[0].load_failed=1;
    assert(run(&f,"/save all") == IDLE_COMMAND_ERROR);
    expect(translated,"Server {number} was not saved: profile could not be loaded.",(IrcDisplayField[]){{"number","0"}},1);
    expect(translated,"Saved {done} profile(s); {failed} failed. Each successful file remains saved.",(IrcDisplayField[]){{"done","0"},{"failed","1"}},2);
    f.client->slots[0].load_failed=0;
    cleanup(&f);
}
int main(void)
{
    readers(1); readers(0);
    puts("Server, directory, autojoin and save readers: PASS"); return 0;
}
