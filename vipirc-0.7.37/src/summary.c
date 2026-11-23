/* Shared local session overview. Only explicit public state is displayed;
 * credentials, queued payloads and startup command arguments are never read.
 * summary_print walks existing Client/App/DCC state without opening lazy Apps.
 * The print callback consumes a scratch line synchronously. Log-based catch-up
 * belongs to idle.c; this module neither opens logs nor summarizes remotely. */
#include "summary.h"
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
    return client->current == server && a->current == view ? " (selected)" : "";
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
    emit(out, "Server %zu: %.255s:%d%s | %s", label,
         *cfg->server ? cfg->server : "(not configured)", cfg->port,
         client->current == index ? " (selected)" : "", slot->saved ? "saved" : "unsaved");
    emit(out, "  State: %s | nick: %.63s", a ? app_connection_label(a) :
         slot->load_failed ? "profile unavailable" : "disconnected",
         a && *a->nick ? a->nick : *cfg->nick ? cfg->nick : "(default)");
    if (*slot->error) emit(out, "  Profile status: %.511s", slot->error);
    if (!a) {
        if (cfg->autocommands.count)
            emit(out, "  Startup commands: %zu configured for the next connection (%s).",
                 cfg->autocommands.count, cfg->autocommands.enabled ? "enabled" : "disabled");
        return;
    }
    emit(out, "  Away: %s | SASL: %s | watch strings: %zu | ignore masks: %zu",
         a->away ? "yes" : "no", a->authenticated ? "authenticated" :
         a->auth_started ? "in progress" : !strcmp(a->cfg.auth, "sasl") ? "not authenticated" : "off",
         a->cfg.word_count, a->cfg.ignore_count);
    if (a->config_pending_mask) emit(out, "  Connection settings are staged for the next connection.");
    if (a->chat_count || a->control_count || a->inflight)
        emit(out, "  Queues: %zu chat, %zu control, %u in flight.",
             a->chat_count, a->control_count, a->inflight ? 1u : 0u);
    size_t remaining = startup_remaining(a);
    if (remaining)
        emit(out, "  Startup commands: %zu remaining on this connection%s.", remaining,
             a->autocommand_waiting ? " (one queued or sending)" :
             a->autocommand_active ? " (waiting for the next scheduled command)" : "");
    if (!a->registered && !remaining && cfg->autocommands.count)
        emit(out, "  Startup plan: %zu configured commands (%s); none are running.",
             cfg->autocommands.count, cfg->autocommands.enabled ? "enabled" : "disabled");
    if (*a->status && a->status_kind != APP_STATUS_QUEUE_NOTICE)
        emit(out, "  Latest status: %.511s", a->status);
    size_t channel = 0, query = 0;
    for (size_t i = 0; i < views(a); ++i) {
        const Buffer *b = &a->buffers[i];
        int unread = b->unread > 0 ? b->unread : 0;
        int mentions = b->unread_mentions > 0 ? b->unread_mentions : 0;
        if (b->kind == B_SERVER)
            emit(out, "  Channel 0: server messages%s | %d unread, %d mentions",
                 selected_view(client, index, a, i), unread, mentions);
        else if (b->kind == B_CHANNEL)
            emit(out, "  Channel %zu: %.200s%s | %s | %zu users%s | %d unread, %d mentions",
                 ++channel, b->name, selected_view(client, index, a, i),
                 b->joined ? "joined" : b->background_join ? "join queued" : "not joined",
                 b->user_count, b->names_in_progress ? " (roster loading)" :
                 b->names_truncated ? " (roster limited)" : "", unread, mentions);
        else if (b->kind == B_QUERY)
            emit(out, "  Query %zu: %.200s%s | %d unread, %d mentions",
                 ++query, b->name, selected_view(client, index, a, i), unread, mentions);
        else if (b->kind == B_MENTIONS && b->line_count)
            emit(out, "  Mentions feed: %zu retained copies%s (excluded from totals).", b->line_count,
                 selected_view(client, index, a, i));
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
    emit(out, "File transfers: %zu offers, %zu active, %zu finished records.", offered, active, finished);
    for (size_t i = 0; i < count; ++i) {
        const DccInfo *info = dcc_at(manager, i);
        if (!info) continue;
        size_t server = transfer_server(client, info->server_token);
        char origin[64];
        if (server == SIZE_MAX) (void)snprintf(origin, sizeof origin, "closed server");
        else (void)snprintf(origin, sizeof origin, "server %zu", server + (server_base ? 1u : 0u));
        emit(out, "  Transfer #%" PRIu64 " | %s | %s %.63s | %s | %" PRIu32 "/%" PRIu32 " bytes",
             info->id, origin, info->outgoing ? "to" : "from", info->nick,
             dcc_state_name(info->state), info->transferred, info->size);
        emit(out, "    File: %.255s%s%.159s", info->filename, *info->error ? " | " : "", info->error);
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
    emit(&out, "Session summary");
    emit(&out, "Local snapshot across all servers. Run /summary again to refresh.");
    emit(&out, "Servers: %zu | registered: %zu | channel views: %zu (%zu joined) | queries: %zu",
         total.servers, total.registered, total.channels, total.joined, total.queries);
    emit(&out, "Unread: %zu | mentions: %zu (source views; copies are not counted twice)", total.unread, total.mentions);
    emit(&out, "Queues: %zu chat, %zu control, %zu in flight | startup commands remaining: %zu",
         total.chat, total.control, total.inflight, total.startup);
    if (client && client->test_mode) emit(&out, "Offline test mode is active.");
    if (client && *client->catalog_error) emit(&out, "Server catalog: %.511s", client->catalog_error);
    if (client && client->default_unavailable) emit(&out, "The saved default server is unavailable; choose an available server explicitly.");
    if (!total.servers) emit(&out, "No server sessions.");
    if (client) for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (client->slots[i].used) server_summary(&out, client, i, server_base);
    transfer_summary(&out, client, manager, server_base);
}
