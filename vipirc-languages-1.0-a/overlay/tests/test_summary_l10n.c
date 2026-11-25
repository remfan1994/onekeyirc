/* Exercise real shared /summary output in each supported locale. */
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include "display_l10n.h"
#include "summary.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char report[65536];
static size_t used;
static void capture(void *context, const char *line)
{
    (void)context;
    size_t len = strlen(line);
    assert(used + len + 2 < sizeof report);
    memcpy(report + used, line, len); used += len;
    report[used++] = '\n'; report[used] = 0;
}
static void expect(const char *key, const IrcDisplayField *fields, size_t count)
{
    char line[2048];
    assert(irc_display_render(line, sizeof line, key, fields, count));
    assert(strstr(report, line));
}
int main(void)
{
    Client *client = calloc(1, sizeof *client);
    App *a = calloc(1, sizeof *a);
    assert(client && a);
    client->slots[0].used = client->slots[0].saved = 1;
    client->slots[0].token = 1;
    client->slots[0].app = a;
    client->current = 0;
    config_defaults(&a->cfg);
    strcpy(a->cfg.server, "irc.example.invalid");
    strcpy(a->cfg.password, "MUST-NOT-LEAK-PRIVATE-CREDENTIAL");
    strcpy(a->nick, "raw%nick{field}");
    a->registered = 1;
    a->buffer_count = 4; a->current = 3;
    a->buffers[0].kind = B_SERVER;
    a->buffers[1].kind = B_MENTIONS;
    a->buffers[2].kind = B_HELP;
    a->buffers[3].kind = B_CHANNEL;
    strcpy(a->buffers[3].name, "#raw%name{field}");
    a->buffers[3].joined = 1; a->buffers[3].user_count = 23;
    a->buffers[3].unread = 7; a->buffers[3].unread_mentions = 2;
    a->buffers[3].names_in_progress = 1;
    DccManager *dcc = dcc_create(1); assert(dcc);
    uint64_t id; char error[256];
    assert(dcc_offer(dcc, 999, "rawpeer", "DCC SEND raw%file.txt 2130706433 9000 4294967295", &id, error, sizeof error));
    summary_print(client, dcc, 1, capture, NULL);
    assert(strstr(report, irc_tr("Session summary")));
    assert(strstr(report, "irc.example.invalid") && strstr(report, "raw%nick{field}"));
    assert(strstr(report, "raw%file.txt") && strstr(report, "4294967295"));
    assert(!strstr(report, "MUST-NOT-LEAK") && !strstr(report, irc_tr("Summary line too long.")));
    const IrcDisplayField totals[] = {{"servers","1"},{"registered","1"},{"channels","1"},{"joined","1"},{"queries","0"}};
    expect("Servers: {servers} | registered: {registered} | channel views: {channels} ({joined} joined) | queries: {queries}", totals, 5);
    const IrcDisplayField channel[] = {{"number","1"},{"name","#raw%name{field}"},{"selected",irc_tr(" (selected)")},{"state",irc_tr("joined")},{"users","23"},{"roster",irc_tr(" (roster loading)")},{"unread","7"},{"mentions","2"}};
    expect("Channel {number}: {name}{selected} | {state} | {users} users{roster} | {unread} unread, {mentions} mentions", channel, 8);
    const IrcDisplayField duplicate[] = {{"x","1"},{"x","2"}};
    char small[4];
    assert(!irc_display_render(small, sizeof small, "{x}{x}", duplicate, 2));
    assert(!irc_display_render(small, sizeof small, "{name}", channel, 8));
    const IrcDisplayField transport[] = {{"server","example.invalid"},{"port","6697"},{"detail",irc_tr("The response looks like a TLS record; select verified TLS for a TLS endpoint.")}};
    char line[512];
    assert(irc_display_render(line, sizeof line, "Server sent a NUL byte on the unencrypted IRC connection to {server}:{port} (TLS disabled). {detail} Check Transport and Port, then /connect", transport, 3));
    assert(strstr(line,"example.invalid") && strstr(line,"6697") && strstr(line,"/connect"));
    dcc_free(dcc); free(a); free(client);
    puts("summary and transport localization: PASS");
    return 0;
}
