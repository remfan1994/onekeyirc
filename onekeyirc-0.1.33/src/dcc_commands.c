#define _POSIX_C_SOURCE 200809L
#include "dcc_commands.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct Word { const char *text, *rest; size_t length; } Word;

static Word word(const char *text)
{
    Word result;
    while (*text == ' ') ++text;
    result.text = text;
    while (*text && *text != ' ') ++text;
    result.length = (size_t)(text - result.text);
    while (*text == ' ') ++text;
    result.rest = text;
    return result;
}

static int equal(Word value, const char *text)
{
    return strlen(text) == value.length && !strncasecmp(value.text, text, value.length);
}

static int copy_word(char *out, size_t capacity, Word value)
{
    if (!value.length || value.length >= capacity) return 0;
    memcpy(out, value.text, value.length); out[value.length] = 0;
    return 1;
}

static void emit(DccPrint print, void *context, const char *format, ...)
{
    char line[DCC_PATH_MAX + 512], safe[DCC_PATH_MAX + 512];
    va_list args;
    va_start(args, format); (void)vsnprintf(line, sizeof line, format, args); va_end(args);
    irc_sanitize(safe, sizeof safe, line);
    if (print) print(context, safe);
}

static int usage(DccPrint print, void *context)
{
    emit(print, context, "Usage: /dcc NICK PATH | /dcc send [-address IPV4] [-port PORT] NICK PATH");
    emit(print, context, "/dcc list | /dcc accept NUMBER [DIRECTORY] | /dcc reject NUMBER | /dcc cancel NUMBER");
    emit(print, context, "Paths are literal, including spaces, relative to the directory where this process was launched.");
    return 0;
}

static int number(Word value, uint64_t maximum, uint64_t *out)
{
    uint64_t n = 0;
    if (!value.length) return 0;
    for (size_t i = 0; i < value.length; ++i) {
        unsigned char c = (unsigned char)value.text[i];
        if (c < '0' || c > '9' || n > (maximum - (uint64_t)(c - '0')) / 10u) return 0;
        n = n * 10u + (uint64_t)(c - '0');
    }
    *out = n; return 1;
}

static ClientSlot *origin(Client *client, uint64_t token)
{
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (client->slots[i].used && client->slots[i].token == token) return &client->slots[i];
    return NULL;
}

static int local_address(App *app, char out[16])
{
    struct sockaddr_storage storage;
    socklen_t length = sizeof storage;
    memset(&storage, 0, sizeof storage);
    if (getsockname(app_fd(app), (struct sockaddr *)&storage, &length) < 0 || storage.ss_family != AF_INET)
        return 0;
    return inet_ntop(AF_INET, &((struct sockaddr_in *)&storage)->sin_addr, out, 16) != NULL;
}

int dcc_command(DccManager **manager, Client *client, App *app,
                const char *arguments, DccPrint print, void *context)
{
    char error[512];
    if (!manager || !client || !app || !arguments || strlen(arguments) > DCC_PATH_MAX + 256u)
        return usage(print, context);
    for (const unsigned char *p = (const unsigned char *)arguments; *p; ++p)
        if (*p < 32 || *p == 127) return usage(print, context);
    Word action = word(arguments);
    if (!action.length || equal(action, "list")) {
        if (*action.rest) return usage(print, context);
        emit(print, context, "File transfers");
        emit(print, context, "--------------");
        if (!*manager || !dcc_count(*manager)) emit(print, context, "No file transfers.");
        for (size_t i = 0; *manager && i < dcc_count(*manager); ++i) {
            const DccInfo *info = dcc_at(*manager, i);
            ClientSlot *slot = origin(client, info->server_token);
            emit(print, context, "#%llu %s %s | %s | %u/%u bytes | %s",
                 (unsigned long long)info->id, info->outgoing ? "to" : "from", info->nick,
                 dcc_state_name(info->state), info->transferred, info->size, info->filename);
            emit(print, context, "  Server %zu | peer %s:%u%s%s", slot ? slot->index + 1u : 0u,
                 info->address, (unsigned)info->port, *info->error ? " | " : "", info->error);
        }
        if (*manager && dcc_count(*manager))
            emit(print, context, "DCC connects directly to the peer without encryption. Accepting reveals your IP address.");
        return 1;
    }
    if (equal(action, "accept") || equal(action, "reject") || equal(action, "cancel")) {
        Word id_word = word(action.rest);
        uint64_t id;
        int accept = equal(action, "accept"), reject = equal(action, "reject");
        if (!number(id_word, UINT64_MAX, &id) || !id || ((!accept) && *id_word.rest)) return usage(print, context);
        const DccInfo *info = *manager ? dcc_get(*manager, id) : NULL;
        if (!info) { emit(print, context, "No file transfer has number %llu. Use /dcc list.", (unsigned long long)id); return 0; }
        int success = accept ? dcc_accept(*manager, id, *id_word.rest ? id_word.rest : ".", NULL, error, sizeof error) :
                      reject ? dcc_reject(*manager, id, error, sizeof error) : dcc_cancel(*manager, id, error, sizeof error);
        if (!success) { emit(print, context, "%s", error); return 0; }
        if (!accept && info->outgoing) {
            ClientSlot *slot = origin(client, info->server_token);
            if (slot && slot->app) (void)app_cancel_dcc_offer(slot->app, id);
        }
        emit(print, context, "File transfer #%llu %s.", (unsigned long long)id,
             accept ? "accepted" : reject ? "rejected" : "cancelled");
        return 1;
    }
    const char *rest = equal(action, "send") ? action.rest : arguments;
    char address[16] = "", nick[64];
    uint16_t port = 0;
    int have_address = 0, have_port = 0;
    Word target = word(rest);
    while (target.length && target.text[0] == '-') {
        Word value = word(target.rest);
        if (equal(target, "-address") || equal(target, "--address")) {
            struct in_addr ip;
            if (have_address || !copy_word(address, sizeof address, value) || inet_pton(AF_INET, address, &ip) != 1)
                return usage(print, context);
            have_address = 1;
        } else if (equal(target, "-port") || equal(target, "--port")) {
            uint64_t parsed;
            if (have_port || !number(value, 65535, &parsed)) return usage(print, context);
            port = (uint16_t)parsed; have_port = 1;
        } else return usage(print, context);
        target = word(value.rest);
    }
    if (!copy_word(nick, sizeof nick, target) || !irc_valid_nick(nick) || !*target.rest || strlen(target.rest) >= DCC_PATH_MAX)
        return usage(print, context);
    if (client->test_mode || app->test_mode) {
        emit(print, context, "File transfers are disabled in offline test mode; no file was opened."); return 0;
    }
    if (!app->registered) { emit(print, context, "Connect to a server before offering a file."); return 0; }
    ClientSlot *slot = NULL;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (client->slots[i].used && client->slots[i].app == app) { slot = &client->slots[i]; break; }
    if (!slot) { emit(print, context, "This server is no longer available."); return 0; }
    if (!have_address && !local_address(app, address)) {
        emit(print, context, "This connection has no local IPv4 address. Use /dcc send -address IPV4 NICK PATH."); return 0;
    }
    if (!*manager) *manager = dcc_create(client->test_mode);
    if (!*manager) { emit(print, context, "Cannot allocate file-transfer state."); return 0; }
    char body[1024]; uint64_t id;
    if (!dcc_send(*manager, slot->token, nick, target.rest, address, port, &id, body, sizeof body, error, sizeof error)) {
        emit(print, context, "%s", error); return 0;
    }
    if (!app_queue_dcc_offer(app, id, nick, body)) {
        (void)dcc_cancel(*manager, id, error, sizeof error);
        emit(print, context, "The file offer could not be queued: %s", app->status); return 0;
    }
    const DccInfo *info = dcc_get(*manager, id);
    emit(print, context, "File offer #%llu to %s: %s (%u bytes).", (unsigned long long)id,
         nick, info ? info->filename : "file", info ? info->size : 0);
    emit(print, context, "Unencrypted peer transfer; advertised address %s:%u. The peer must be able to reach it.",
         address, info ? (unsigned)info->port : 0u);
    return 1;
}
