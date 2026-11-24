#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#define main legacy_idle_commands_main
#include "test_idle_commands.c"
#undef main

static void expect(const char *key, const IrcDisplayField *fields, size_t n)
{
    char rendered[2048];
    assert(irc_display_fields_valid(irc_tr(key), fields, n));
    assert(irc_display_render(rendered, sizeof rendered, key, fields, n));
    if (!strstr(output, rendered)) fprintf(stderr, "Expected: %s\nActual: %s\n", rendered, output);
    assert(strstr(output, rendered));
}

static void readers(int translated)
{
    Fixture f; init(&f, 1); App *a = app(&f);
    f.commands.translate = translated ? irc_tr : NULL;
    const char *(*tr)(const char *) = translated ? irc_tr : NULL;
    size_t channel = app_buffer(a, "#literal%{channel}", B_CHANNEL, 1);
    size_t query = app_buffer(a, "literal-peer", B_QUERY, 1);
    assert(channel != SIZE_MAX && query != SIZE_MAX);
    a->buffers[channel].unread = 17; a->buffers[channel].unread_mentions = 3;
    a->buffers[channel].joined = 0;
    a->buffers[channel].names_in_progress = 1; a->buffers[channel].names_truncated = 1;
    a->buffers[query].unread = 19; a->buffers[query].unread_mentions = 4;
    a->current = channel;
    assert(run(&f, "/channels") == IDLE_COMMAND_DONE);
    assert(strstr(output, "#literal%{channel}"));
    assert(strstr(output, tr ? tr(" (selected)") : " (selected)"));
    assert(strstr(output, tr ? tr("parted") : "parted"));
    if (translated) expect("{unread} unread, {mentions} mentions", (IrcDisplayField[]){{"unread", "17"}, {"mentions", "3"}}, 2);
    else assert(strstr(output, "17 unread, 3 mentions"));
    assert(run(&f, "/queries") == IDLE_COMMAND_DONE && strstr(output, "literal-peer"));
    if (translated) expect("{unread} unread, {mentions} mentions", (IrcDisplayField[]){{"unread", "19"}, {"mentions", "4"}}, 2);
    assert(run(&f, "/unread") == IDLE_COMMAND_DONE && strstr(output, "#literal%{channel}") && strstr(output, "literal-peer"));
    assert(run(&f, "/users") == IDLE_COMMAND_DONE && strstr(output, "#literal%{channel}"));
    assert(strstr(output, tr ? tr(" (NAMES incomplete)") : " (NAMES incomplete)"));
    assert(strstr(output, tr ? tr(" (roster limit reached)") : " (roster limit reached)"));
    const char *staged = "Automatic commands staged for the next connection; /save persists them. /cancel stops this connection's pending commands.";
    assert(run(&f, "/autocommands add /msg NickServ IDENTIFY TEST-SECRET") == IDLE_COMMAND_DONE);
    assert(strstr(output, tr ? tr(staged) : staged));
    assert(run(&f, "/autocommands add /join #private JOIN-SECRET") == IDLE_COMMAND_DONE);
    assert(run(&f, "/autocommands list") == IDLE_COMMAND_DONE);
    assert(strstr(output, "/msg NickServ") && strstr(output, "/join #private"));
    assert(strstr(output, tr ? tr(" [text hidden]") : " [text hidden]"));
    assert(!strstr(output, "TEST-SECRET") && !strstr(output, "JOIN-SECRET"));
    Config cfg; app_config_effective(a, &cfg);
    char count[64], delay[32], interval[32];
    snprintf(count, sizeof count, "%zu/%d", cfg.autocommands.count, CONFIG_AUTOCOMMAND_MAX);
    snprintf(delay, sizeof delay, "%d", cfg.autocommands.delay_seconds);
    snprintf(interval, sizeof interval, "%d", cfg.autocommands.interval_seconds);
    if (translated) expect("Automatic commands: {state} | {count} | delay {delay} seconds | interval {interval} seconds", (IrcDisplayField[]){{"state", tr(cfg.autocommands.enabled ? "on" : "off")}, {"count", count}, {"delay", delay}, {"interval", interval}}, 4);
    config_wipe(&cfg, sizeof cfg);
    const char *bad[][2] = {
        {"/autocommands add /msg NickServ", "Automatic /msg requires one nickname or channel and nonempty text."},
        {"/autocommands add /join #private key extra", "Automatic /join requires one channel and at most one printable ASCII key."},
        {"/autocommands add /nick one two", "Automatic /nick and /whois require exactly one nickname."},
        {"/autocommands add /back extra", "Automatic /back takes no arguments."},
        {"/autocommands add /raw PASS TEST-SECRET", "Automatic commands support /msg, /join, /nick, /away, /back and /whois only."},
        {"/autocommands add nonsense", "Automatic command must be a single line of at most 511 UTF-8 bytes beginning with '/'."},
        {"/autocommands remove 0", "That automatic command number does not exist. Use /autocommands list."},
        {"/autocommands interval 1", "Automatic command interval must be from 2 to 60 seconds."}
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        assert(run(&f, bad[i][0]) == IDLE_COMMAND_ERROR);
        assert(strstr(output, tr ? tr(bad[i][1]) : bad[i][1]));
        assert(!strstr(output, "TEST-SECRET") && !strstr(output, "JOIN-SECRET"));
    }
    app_config_effective(a, &cfg);
    assert(cfg.autocommands.count == 2);
    config_wipe(&cfg, sizeof cfg);
    assert(run(&f, "/autoconnect on") == IDLE_COMMAND_DONE);
    const char *auto_msg = "Auto-connect changed for next startup. /save persists it; /connect acts now.";
    assert(strstr(output, tr ? tr(auto_msg) : auto_msg));
    cleanup(&f);
}
int main(void)
{
    readers(1); readers(0);
    puts("Conversation and automatic command readers: PASS");
    return 0;
}
