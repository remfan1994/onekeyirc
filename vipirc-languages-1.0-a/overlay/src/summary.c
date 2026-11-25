/* Shared local session overview. Only explicit public state is displayed;
 * credentials, queued payloads and startup command arguments are never read.
 * summary_print walks existing Client/App/DCC state without opening lazy Apps.
 * The print callback consumes a scratch line synchronously. Log-based catch-up
 * belongs to idle.c; this module neither opens logs nor summarizes remotely. */
#include "summary.h"
#include "l10n.h"
#include "display_l10n.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct SummaryOutput { SummaryPrint print; void *context; } SummaryOutput;
typedef struct SummaryTotals {
    size_t servers, registered, channels, joined, queries, unread, mentions;
    size_t chat, control, inflight, startup;
} SummaryTotals;

static size_t add(size_t left, size_t right)
{
    return right > SIZE_MAX - left ? SIZE_MAX : left + right;
}

static void emit(SummaryOutput *out, const char *format, ...)
{
    char raw[SUMMARY_LINE_MAX], safe[SUMMARY_LINE_MAX];
    va_list ap;
    va_start(ap, format);
    (void)vsnprintf(raw, sizeof raw, format, ap);
    va_end(ap);
    irc_sanitize(safe, sizeof safe, raw);
    out->print(out->context, safe);
}

/* Translate complete display templates, with raw fields kept as data.
 * Never pass a translated string to a printf-style formatter. */
static const char *summary_number(char *buf, uintmax_t value)
{
    (void)snprintf(buf, 32, "%" PRIuMAX, value);
    return buf;
}
#define N(value) summary_number((char[32]){0}, (uintmax_t)(value))
#define F(name, value) {name, value}
#define T(value) irc_tr(value)
static void emit_fields(SummaryOutput *out, const char *indent, const char *key,
                        const IrcDisplayField *fields, size_t count)
{
    char line[SUMMARY_LINE_MAX];
    if (irc_display_render(line, sizeof line, key, fields, count))
        emit(out, "%s%s", indent, line);
    else
        emit(out, "%s%s", indent, T("Summary line too long."));
}
#define SHOW(out, indent, key, ...) do { \
    const IrcDisplayField fields[] = {__VA_ARGS__}; \
    emit_fields(out, indent, key, fields, sizeof fields / sizeof fields[0]); \
} while (0)

static size_t views(const App *a)
{
    return a->buffer_count < APP_BUFFERS ? a->buffer_count : APP_BUFFERS;
}

static size_t startup_remaining(const App *a)
{
    size_t count = a->cfg.autocommands.count;
    if (count > CONFIG_AUTOCOMMAND_MAX) count = CONFIG_AUTOCOMMAND_MAX;
    size_t remaining = a->autocommand_active && a->autocommand_next < count ?
                       count - a->autocommand_next : 0;
    return add(remaining, a->autocommand_waiting ? 1u : 0u);
}

static void total_app(SummaryTotals *total, const App *a)
{
    total->registered += a->registered != 0;
    total->chat = add(total->chat, a->chat_count);
    total->control = add(total->control, a->control_count);
    total->inflight = add(total->inflight, a->inflight ? 1u : 0u);
    total->startup = add(total->startup, startup_remaining(a));
    for (size_t i = 0; i < views(a); ++i) {
        const Buffer *b = &a->buffers[i];
        if (b->kind == B_HELP || b->kind == B_MENTIONS) continue;
        total->unread = add(total->unread, b->unread > 0 ? (size_t)b->unread : 0u);
        total->mentions = add(total->mentions, b->unread_mentions > 0 ? (size_t)b->unread_mentions : 0u);
        if (b->kind == B_CHANNEL) {
            ++total->channels;
            total->joined += b->joined != 0;
        } else if (b->kind == B_QUERY) ++total->queries;
    }
}

static const char *selected_view(const Client *client, size_t server,
                                 const App *a, size_t view)
{
    return client->current == server && a->current == view ? T(" (selected)") : "";
}

static void server_summary(SummaryOutput *out, const Client *client,
                            size_t index, unsigned server_base)
{
    const ClientSlot *slot = &client->slots[index];
    const App *a = slot->app;
    /* cfg describes the current/last connection. Staged connection edits
     * must not rename an active server in the session summary. */
    const Config *cfg = a ? &a->cfg : &slot->seed;
    size_t label = index + (server_base ? 1u : 0u);
    emit(out, "");
    SHOW(out, "", "Server {number}: {host}:{port}{selected} | {saved}",
         F("number", N(label)), F("host", *cfg->server ? cfg->server : T("(not configured)")),
         F("port", N(cfg->port)), F("selected", client->current == index ? T(" (selected)") : ""),
         F("saved", T(slot->saved ? "saved" : "unsaved")));
    SHOW(out, "  ", "State: {state} | nick: {nick}",
         F("state", a ? app_connection_label(a) : T(slot->load_failed ? "profile unavailable" : "disconnected")),
         F("nick", a && *a->nick ? a->nick : *cfg->nick ? cfg->nick : T("(default)")));
    if (*slot->error) SHOW(out, "  ", "Profile status: {detail}", F("detail", slot->error));
    if (!a) {
        if (cfg->autocommands.count)
            SHOW(out, "  ", "Startup commands: {count} configured for the next connection ({state}).",
                 F("count", N(cfg->autocommands.count)), F("state", T(cfg->autocommands.enabled ? "enabled" : "disabled")));
        return;
    }
    SHOW(out, "  ", "Away: {away} | SASL: {sasl} | watch strings: {watch} | ignore masks: {ignore}",
         F("away", T(a->away ? "yes" : "no")), F("sasl", T(a->authenticated ? "authenticated" :
         a->auth_started ? "in progress" : !strcmp(a->cfg.auth, "sasl") ? "not authenticated" : "off")),
         F("watch", N(a->cfg.word_count)), F("ignore", N(a->cfg.ignore_count)));
    if (a->config_pending_mask) emit(out, "  %s", T("Connection settings are staged for the next connection."));
    if (a->chat_count || a->control_count || a->inflight)
        SHOW(out, "  ", "Queues: {chat} chat, {control} control, {flight} in flight.",
             F("chat", N(a->chat_count)), F("control", N(a->control_count)), F("flight", N(a->inflight ? 1u : 0u)));
    size_t remaining = startup_remaining(a);
    if (remaining)
        SHOW(out, "  ", "Startup commands: {count} remaining on this connection{detail}.", F("count", N(remaining)),
             F("detail", a->autocommand_waiting ? T(" (one queued or sending)") :
             a->autocommand_active ? T(" (waiting for the next scheduled command)") : ""));
    if (!a->registered && !remaining && cfg->autocommands.count)
        SHOW(out, "  ", "Startup plan: {count} configured commands ({state}); none are running.",
             F("count", N(cfg->autocommands.count)), F("state", T(cfg->autocommands.enabled ? "enabled" : "disabled")));
    if (*a->status && a->status_kind != APP_STATUS_QUEUE_NOTICE)
        SHOW(out, "  ", "Latest status: {detail}", F("detail", a->status));
    size_t channel = 0, query = 0;
    for (size_t i = 0; i < views(a); ++i) {
        const Buffer *b = &a->buffers[i];
        int unread = b->unread > 0 ? b->unread : 0;
        int mentions = b->unread_mentions > 0 ? b->unread_mentions : 0;
        const char *selected = selected_view(client, index, a, i);
        if (b->kind == B_SERVER)
            SHOW(out, "  ", "Channel 0: server messages{selected} | {unread} unread, {mentions} mentions",
                 F("selected", selected), F("unread", N(unread)), F("mentions", N(mentions)));
        else if (b->kind == B_CHANNEL)
            SHOW(out, "  ", "Channel {number}: {name}{selected} | {state} | {users} users{roster} | {unread} unread, {mentions} mentions",
                 F("number", N(++channel)), F("name", b->name), F("selected", selected),
                 F("state", T(b->joined ? "joined" : b->background_join ? "join queued" : "not joined")),
                 F("users", N(b->user_count)), F("roster", b->names_in_progress ? T(" (roster loading)") :
                 b->names_truncated ? T(" (roster limited)") : ""), F("unread", N(unread)), F("mentions", N(mentions)));
        else if (b->kind == B_QUERY)
            SHOW(out, "  ", "Query {number}: {name}{selected} | {unread} unread, {mentions} mentions",
                 F("number", N(++query)), F("name", b->name), F("selected", selected), F("unread", N(unread)), F("mentions", N(mentions)));
        else if (b->kind == B_MENTIONS && b->line_count)
            SHOW(out, "  ", "Mentions feed: {count} retained copies{selected} (excluded from totals).",
                 F("count", N(b->line_count)), F("selected", selected));
    }
}

static size_t transfer_server(const Client *client, uint64_t token)
{
    if (client)
        for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
            if (client->slots[i].used && client->slots[i].token == token) return i;
    return SIZE_MAX;
}

static void transfer_summary(SummaryOutput *out, const Client *client,
                              const DccManager *manager, unsigned server_base)
{
    size_t count = dcc_count(manager), offered = 0, active = 0, finished = 0;
    if (!count) return;
    for (size_t i = 0; i < count; ++i) {
        const DccInfo *info = dcc_at(manager, i);
        if (!info) continue;
        switch (info->state) {
        case DCC_OFFERED:
            ++offered;
            break;
        case DCC_LISTENING: case DCC_CONNECTING: case DCC_SENDING: case DCC_RECEIVING:
            ++active;
            break;
        default:
            ++finished;
            break;
        }
    }
    emit(out, "");
    SHOW(out, "", "File transfers: {offers} offers, {active} active, {finished} finished records.",
         F("offers", N(offered)), F("active", N(active)), F("finished", N(finished)));
    for (size_t i = 0; i < count; ++i) {
        const DccInfo *info = dcc_at(manager, i);
        if (!info) continue;
        size_t server = transfer_server(client, info->server_token);
        char origin[256], detail[164];
        if (server == SIZE_MAX) (void)snprintf(origin, sizeof origin, "%s", T("closed server"));
        else {
            const IrcDisplayField field[] = {F("number", N(server + (server_base ? 1u : 0u)))};
            if (!irc_display_render(origin, sizeof origin, "server {number}", field, 1))
                (void)snprintf(origin, sizeof origin, "server %zu", server + (server_base ? 1u : 0u));
        }
        SHOW(out, "  ", "Transfer #{id} | {origin} | {direction} {nick} | {state} | {done}/{size} bytes",
             F("id", N(info->id)), F("origin", origin), F("direction", T(info->outgoing ? "to" : "from")),
             F("nick", info->nick), F("state", T(dcc_state_name(info->state))), F("done", N(info->transferred)), F("size", N(info->size)));
        (void)snprintf(detail, sizeof detail, "%s%.159s", *info->error ? " | " : "", info->error);
        SHOW(out, "    ", "File: {name}{detail}", F("name", info->filename), F("detail", detail));
    }
}

void summary_print(const Client *client, const DccManager *manager, unsigned server_base,
                   SummaryPrint print, void *context)
{
    if (!print) return;
    SummaryOutput out = {print, context};
    SummaryTotals total = {0};
    if (client) for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        if (!client->slots[i].used) continue;
        ++total.servers;
        if (client->slots[i].app) total_app(&total, client->slots[i].app);
    }
    emit(&out, "%s", T("Session summary"));
    emit(&out, "%s", T("Local snapshot across all servers. Run /summary again to refresh."));
    SHOW(&out, "", "Servers: {servers} | registered: {registered} | channel views: {channels} ({joined} joined) | queries: {queries}",
         F("servers", N(total.servers)), F("registered", N(total.registered)), F("channels", N(total.channels)), F("joined", N(total.joined)), F("queries", N(total.queries)));
    SHOW(&out, "", "Unread: {unread} | mentions: {mentions} (source views; copies are not counted twice)", F("unread", N(total.unread)), F("mentions", N(total.mentions)));
    SHOW(&out, "", "Queues: {chat} chat, {control} control, {flight} in flight | startup commands remaining: {startup}",
         F("chat", N(total.chat)), F("control", N(total.control)), F("flight", N(total.inflight)), F("startup", N(total.startup)));
    if (client && client->test_mode) emit(&out, "%s", T("Offline test mode is active."));
    if (client && *client->catalog_error) SHOW(&out, "", "Server catalog: {detail}", F("detail", client->catalog_error));
    if (client && client->default_unavailable) emit(&out, "%s", T("The saved default server is unavailable; choose an available server explicitly."));
    if (!total.servers) emit(&out, "%s", T("No server sessions."));
    if (client) for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (client->slots[i].used) server_summary(&out, client, i, server_base);
    transfer_summary(&out, client, manager, server_base);
}
