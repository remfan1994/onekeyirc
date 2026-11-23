/* Public runtime fixtures: summary must be a read-only, bounded local view. */
#include "summary.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char output[262144];
static size_t used, lines;

static void capture(void *context, const char *line)
{
    assert(context == &used);
    size_t n = strlen(line);
    assert(n < SUMMARY_LINE_MAX && used + n + 2 < sizeof output);
    for (size_t i = 0; i < n; ++i)
        assert((unsigned char)line[i] >= 32 && (unsigned char)line[i] != 127);
    memcpy(output + used, line, n); used += n;
    output[used++] = '\n'; output[used] = 0; ++lines;
}

static void render(const Client *client, const DccManager *dcc, unsigned base)
{
    used = lines = 0; output[0] = 0;
    summary_print(client, dcc, base, capture, &used);
    assert(lines && strstr(output, "Session summary"));
}

static void buffer(App *a, size_t index, BufferKind kind, const char *name,
                   int unread, int mentions, int joined)
{
    Buffer *b = &a->buffers[index];
    b->kind = kind; b->id = index + 1;
    (void)snprintf(b->name, sizeof b->name, "%s", name);
    b->unread = unread; b->unread_mentions = mentions; b->joined = joined;
    b->scroll = 7; b->user_scroll = 3;
    if (a->buffer_count <= index) a->buffer_count = index + 1;
}

static void session(Client *client, size_t index, App *a, uint64_t token, const char *host)
{
    ClientSlot *slot = &client->slots[index];
    slot->used = slot->saved = 1; slot->index = index; slot->token = token; slot->app = a;
    config_defaults(&slot->seed);
    strcpy(slot->seed.server, "old-profile.invalid");
    config_defaults(&a->cfg);
    (void)snprintf(a->cfg.server, sizeof a->cfg.server, "%s", host);
    strcpy(a->nick, "runtime_nick"); strcpy(a->cfg.nick, "configured_nick");
    a->test_mode = 1; a->dirty = 3; a->visible = 0;
    strcpy(a->cfg.password, "SASL-SECRET-DO-NOT-PRINT");
    strcpy(a->cfg.server_password, "SERVER-SECRET-DO-NOT-PRINT");
    strcpy(a->cfg.password_file, "/private/SECRET-PATH-DO-NOT-PRINT");
    strcpy(a->cfg.server_password_gpg_file, "/private/SECRET-GPG-DO-NOT-PRINT");
    strcpy(a->cfg.autocommands.lines[0], "/msg NickServ IDENTIFY STARTUP-SECRET-DO-NOT-PRINT");
    strcpy(a->config_pending.password, "PENDING-SECRET-DO-NOT-PRINT");
    strcpy(a->config_pending.server, "future-server.invalid");
    a->config_pending_mask = 1;
    buffer(a, 0, B_SERVER, "server", 0, 0, 0);
    buffer(a, 1, B_MENTIONS, "mentions", 999, 999, 0);
    buffer(a, 2, B_HELP, "manual", 888, 888, 0);
    a->buffers[1].line_count = 10;
}

static void empty_state(void)
{
    render(NULL, NULL, 1);
    assert(strstr(output, "No server sessions.") && !strstr(output, "File transfers:"));
    assert(strstr(output, "Unread: 0 | mentions: 0"));
    Client *client = calloc(1, sizeof *client); assert(client);
    client->current = SIZE_MAX;
    render(client, NULL, 0);
    assert(strstr(output, "Servers: 0") && !client->slots[0].app);
    summary_print(client, NULL, 0, NULL, NULL);
    App *a = calloc(1, sizeof *a); assert(a);
    session(client, 0, a, 1, "empty.invalid");
    a->buffers[1].line_count = 0;
    render(client, NULL, 0);
    assert(strstr(output, "Channel 0: server messages"));
    assert(!strstr(output, "  Queues:") && !strstr(output, "  Startup commands:"));
    assert(!strstr(output, "No channel views.") && !strstr(output, "No open queries."));
    assert(!strstr(output, "File transfers:") && !strstr(output, "Mentions feed:"));
    free(a);
    free(client);
}

static void all_sessions_and_no_mutation(void)
{
    Client *client = calloc(1, sizeof *client), *before = malloc(sizeof *before);
    App *first = calloc(1, sizeof *first), *second = calloc(1, sizeof *second);
    App *first_before = malloc(sizeof *first_before), *second_before = malloc(sizeof *second_before);
    assert(client && before && first && second && first_before && second_before);
    session(client, 0, first, 100, "first.invalid");
    session(client, 3, second, 200, "second.invalid");
    client->current = 3; client->test_mode = 1;
    first->buffers[0].unread = 2; first->buffers[0].unread_mentions = 1;
    buffer(first, 3, B_CHANNEL, "#same", 5, 2, 1);
    buffer(first, 4, B_CHANNEL, "#left", 3, 0, 0);
    buffer(first, 5, B_QUERY, "Alice", 7, 1, 0);
    first->buffers[3].user_count = 23; first->buffers[3].names_in_progress = 1;
    first->current = 3; first->away = 1; first->authenticated = 1;
    first->chat_count = 4; first->control_count = 2;
    first->cfg.autocommands.count = 4; first->autocommand_active = 1;
    first->autocommand_next = 2; first->autocommand_waiting = 1;
    /* The formatter may test this borrowed pointer, never dereference it. */
    first->inflight = (OutLine *)(void *)first_before;
    strcpy(first->status, "Connection status\033[2J\ncontinued safely");
    second->buffers[0].unread = 1;
    buffer(second, 3, B_CHANNEL, "#same", 11, 3, 1);
    buffer(second, 4, B_QUERY, "Alice", 13, 2, 0);
    second->current = 4; second->chat_count = 3; second->control_count = 5;
    client->slots[5].used = 1; client->slots[5].load_failed = 1;
    strcpy(client->slots[5].seed.server, "unavailable.invalid");
    strcpy(client->slots[5].error, "Saved server profile is missing.");
    strcpy(client->slots[5].seed.password, "UNOPENED-SECRET-DO-NOT-PRINT");
    client->default_unavailable = 1;
    strcpy(client->catalog_error, "One saved profile could not be loaded.");

    DccManager *dcc = dcc_create(1); assert(dcc);
    char error[256]; uint64_t ids[3]; DccInfo transfer_before[3];
    assert(dcc_offer(dcc, 100, "sender", "DCC SEND first.txt 2130706433 9000 4294967295", &ids[0], error, sizeof error));
    assert(dcc_offer(dcc, 200, "sender", "DCC SEND second.txt 2130706433 9001 4", &ids[1], error, sizeof error));
    assert(dcc_reject(dcc, ids[1], error, sizeof error));
    assert(dcc_offer(dcc, 999, "orphan", "DCC SEND orphan.txt 2130706433 9002 0", &ids[2], error, sizeof error));
    for (size_t i = 0; i < 3; ++i) transfer_before[i] = *dcc_at(dcc, i);
    memcpy(before, client, sizeof *client);
    memcpy(first_before, first, sizeof *first); memcpy(second_before, second, sizeof *second);

    render(client, dcc, 1);
    assert(strstr(output, "Servers: 3") && strstr(output, "channel views: 3 (2 joined) | queries: 2"));
    assert(strstr(output, "Unread: 42 | mentions: 9"));
    assert(strstr(output, "Queues: 7 chat, 7 control, 1 in flight | startup commands remaining: 3"));
    assert(strstr(output, "Server 1: first.invalid:6697") && strstr(output, "Server 4: second.invalid:6697 (selected)"));
    assert(strstr(output, "Channel 0: server messages") && strstr(output, "Channel 2: #left | not joined"));
    assert(strstr(output, "Query 1: Alice (selected) | 13 unread, 2 mentions"));
    assert(strstr(output, "Away: yes | SASL: authenticated") && strstr(output, "Connection settings are staged"));
    assert(strstr(output, "Mentions feed: 10 retained copies") && strstr(output, "roster loading"));
    assert(strstr(output, "profile unavailable") && strstr(output, "default server is unavailable"));
    assert(strstr(output, "File transfers: 2 offers, 0 active, 1 finished records."));
    assert(strstr(output, "Transfer #1 | server 1 | from sender | offered | 0/4294967295 bytes"));
    assert(strstr(output, "Transfer #2 | server 4 | from sender | rejected"));
    assert(strstr(output, "closed server | from orphan") && strstr(output, "orphan.txt"));
    assert(!strstr(output, "SECRET") && !strstr(output, "old-profile.invalid") && !strstr(output, "future-server.invalid"));
    assert(strstr(output, "runtime_nick") && !strstr(output, "configured_nick"));
    assert(!memcmp(client, before, sizeof *client));
    assert(!memcmp(first, first_before, sizeof *first) && !memcmp(second, second_before, sizeof *second));
    for (size_t i = 0; i < 3; ++i) assert(!memcmp(dcc_at(dcc, i), &transfer_before[i], sizeof(DccInfo)));
    assert(dcc_changed(dcc)); /* Summary did not consume the existing dirty flag. */
    render(client, dcc, 0);
    assert(strstr(output, "Server 0: first.invalid") && strstr(output, "Transfer #2 | server 3"));
    assert(!dcc_changed(dcc)); /* Summary did not set it, either. */

    first->chat_count = SIZE_MAX;
    first->buffers[3].unread = INT_MAX; first->buffers[4].unread = -1;
    first->buffers[4].unread_mentions = -1;
    render(client, dcc, 0);
    char expected[128]; (void)snprintf(expected, sizeof expected, "Queues: %zu chat, 7 control", SIZE_MAX);
    assert(strstr(output, expected));
    assert(!strstr(output, "-1 unread") && !strstr(output, "-1 mentions"));
    assert(!strstr(output, "  Queues: 0 chat, 0 control, 0 in flight."));
    assert(!strstr(output, "  Startup commands: 0 remaining"));
    assert(!strstr(output, "No channel views.") && !strstr(output, "No open queries."));
    render(client, NULL, 0);
    assert(!strstr(output, "File transfers:") && !strstr(output, "No file transfers."));
    dcc_free(dcc);
    free(first_before); free(second_before); free(first); free(second); free(before); free(client);
}

int main(void)
{
    empty_state(); all_sessions_and_no_mutation();
    puts("summary: all sessions, duplicate names, channel 0, unread/mention deduplication, queues, startup state, transfers, no mutation, secret exclusion and bounded sanitized output passed");
    return 0;
}
