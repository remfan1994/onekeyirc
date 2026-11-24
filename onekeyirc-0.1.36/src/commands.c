/* Local command metadata and ephemeral history. Nothing here executes input. */
#include "commands.h"
#include "editor.h"
#include "protocol.h"

#include <stdlib.h>
#include <string.h>

/* Keep this table aligned with app_command() and the UI-only commands in
 * submit(). Aliases describe the same operation; no unsupported IRC command
 * or third-party command syntax is suggested. */
static const CommandSpec catalog[] = {
    {"autoauth", "/autoauth [status|off|sasl USER file PATH|sasl USER gpg PATH]", "Inspect credential locations or prepare authentication"},
    {"autocommands", "/autocommands [list|add COMMAND|remove NUMBER|clear|on|off|delay SECONDS|interval SECONDS]", "Prepare delayed commands for the next connection"},
    {"autoconnect", "/autoconnect [on|off]", "Show or change automatic startup connection"},
    {"autojoin", "/autojoin [list|add #CHANNEL|remove #CHANNEL|clear]", "Inspect running joins or prepare the next explicit connection"},
    {"away", "/away [MESSAGE]", "Set or clear away status"},
    {"back", "/back", "Clear away status"},
    {"background", "/background", "Keep running and release this terminal; resume with vipirc --resume"},
    {"bind", "/bind KEY ACTION", "Assign a user-selected key to a local function"},
    {"bindings", "/bindings [actions|keys]", "Edit custom key bindings or list available names"},
    {"cancel", "/cancel", "Stop unsent chat and startup commands on this server"},
    {"channel", "/channel NUMBER|#CHANNEL", "Switch channel; 0 is the server"},
    {"channels", "/channels", "Open this server's channel list"},
    {"cl", "/cl", "Open this server's channel list (alias of /channels)"},
    {"clear", "/clear", "Clear the current view's stored lines"},
    {"clearinput", "/clearinput", "Clear this conversation's draft"},
    {"close", "/close [NAME|NUMBER]", "Close a channel or query view"},
    {"connect", "/connect [-tls|-plain] [-nick NICK] HOST [PORT] | /connect", "Connect directly, or reuse the selected saved server"},
    {"dcc", "/dcc [NICK PATH|list|send|accept|reject|cancel ...]", "Open file transfers, or manage a transfer directly"},
    {"disconnect", "/disconnect [SERVER_NUMBER]", "Disconnect the selected or numbered server"},
    {"help", "/help [TEXT]", "Open the manual; TEXT searches its contents"},
    {"ignore", "/ignore [list|clear|[add|remove] MASK]", "Manage local ignored senders"},
    {"join", "/join #CHANNEL [KEY]", "Join one channel"},
    {"keymap", "/keymap [original|vim]", "Explain ordinary navigation and legacy keymap names"},
    {"list", "/list [MASK] [--min-users N] [--max-users N] [--name TEXT] [--topic TEXT] [--sort channel|users|topic] [--asc|--desc] [--limit N] | /list show [--sort channel|users|topic] [--asc|--desc] [--limit N] | /list clear", "Open and filter this network channel directory"},
    {"ls", "/ls [servers|channels|queries|unread]", "Open a local list; channels by default"},
    {"manual", "/manual", "Open the application manual"},
    {"me", "/me TEXT", "Send an action to this conversation"},
    {"mentions", "/mentions [clear]", "View this server's mentions, or clear retained mentions across servers"},
    {"mouse", "/mouse [on|off]", "Toggle or set mouse controls"},
    {"msg", "/msg TARGET TEXT", "Send text to one nickname or channel"},
    {"next", "/next", "Switch to the next view"},
    {"nick", "/nick NICK", "Set or request a nickname"},
    {"panel", "/panel", "Open the application configuration panel"},
    {"part", "/part [#CHANNEL] [MESSAGE]", "Leave a channel with an optional reason and retain its history"},
    {"pm", "/pm NICK|NUMBER", "Open or select a private conversation"},
    {"prev", "/prev", "Switch to the previous view"},
    {"ql", "/ql", "Open this server's query list"},
    {"queries", "/queries", "Open this server's query list (alias of /ql)"},
    {"query", "/query NICK|NUMBER", "Open or select a private conversation without sending"},
    {"quickstart", "/quickstart", "Open the selected server's connection settings"},
    {"quit", "/quit [MESSAGE]", "Exit all sessions with an optional quit reason"},
    {"save", "/save [input|output|all]", "Save this server, primary preferences, or all profiles"},
    {"saveconf", "/saveconf", "Save configuration and remember the selected connection"},
    {"server", "/server [-tls|-plain] [-nick NICK] HOST [PORT] | /server [NUMBER|add HOST [PORT] [NICK]|connect NUMBER|disconnect NUMBER|close NUMBER]", "Connect or select a server; no argument opens its MOTD"},
    {"servers", "/servers", "Open the server connection list"},
    {"set", "/set send_mode direct|review | composer_mode line|fullscreen | output_format compact|full | queue_notices on|off | program_messages on|off | persistent_logging on|off | single_session on|off | default_interface idle|ncurses | command_suggestions common|history|frequent|alphabetical|contextual | part_message [TEXT] | quit_message [TEXT] | background_away on|off | background_away_message TEXT | autoaway_minutes 0..1440 | autoaway_message TEXT", "Set application preferences"},
    {"settings", "/settings", "Open the configuration panel"},
    {"sl", "/sl", "Open the server connection list (alias of /servers)"},
    {"soju", "/soju [COMMAND]", "Send a private control command to this soju bouncer; help by default"},
    {"summary", "/summary", "Review all servers, conversations, activity, queues and transfers"},
    {"topic", "/topic [TEXT]", "Show or change the current channel topic"},
    {"transfers", "/transfers", "Open file transfers"},
    {"unbind", "/unbind KEY|all", "Remove one or all custom key bindings"},
    {"unignore", "/unignore MASK", "Remove one local ignore mask"},
    {"unread", "/unread", "Open grouped unread conversations and mentions across servers"},
    {"users", "/users [on|off|toggle|up|down]", "Show, hide or scroll the user list"},
    {"view", "/view NAME|NUMBER", "Switch to a named view or numbered channel"},
    {"views", "/views", "Open this server's channel list"},
    {"watch", "/watch [list|add TEXT|remove TEXT]", "Manage additional mention strings"},
    {"whois", "/whois NICK", "Request information about a nickname"},
    {"znc", "/znc [COMMAND]", "Send a private control command to this ZNC bouncer; Help by default"}
};

typedef struct Token {
    const char *start;
    const char *rest;
    size_t size;
    int separated;
} Token;

static size_t bounded_length(const char *s)
{
    size_t n = 0;
    if (!s) return ED_MAX_BYTES + 1u;
    while (n <= ED_MAX_BYTES && s[n]) ++n;
    return n;
}

static unsigned char fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

typedef struct CommandAlias { const char *alias, *canonical; } CommandAlias;
static const CommandAlias aliases[] = {
    {"autocommand", "autocommands"}, {"channellist", "cl"},
    {"configure", "panel"}, {"conn", "connect"}, {"detach", "background"},
    {"disc", "disconnect"}, {"exit", "quit"}, {"j", "join"},
    {"leave", "part"}, {"message", "msg"}, {"names", "users"},
    {"nickname", "nick"}, {"nicks", "users"}, {"preferences", "panel"},
    {"privmsg", "msg"}, {"querylist", "ql"}, {"saveconfig", "saveconf"},
    {"serverlist", "servers"}, {"unaway", "back"}
};

static int name_matches(const char *name, const char *prefix, size_t length, int exact)
{
    size_t n = strlen(name);
    if (!prefix || length > n || (exact && length != n)) return 0;
    for (size_t i = 0; i < length; ++i)
        if (fold((unsigned char)prefix[i]) != (unsigned char)name[i]) return 0;
    return 1;
}

const char *commands_alias_name(const char *name, size_t length)
{
    for (size_t i = 0; i < sizeof aliases / sizeof aliases[0]; ++i)
        if (name_matches(aliases[i].alias, name, length, 1)) return aliases[i].canonical;
    return NULL;
}

int commands_name_matches(const char *canonical, const char *prefix, size_t length, int exact)
{
    if (!canonical) return 0;
    if (name_matches(canonical, prefix, length, exact)) return 1;
    for (size_t i = 0; i < sizeof aliases / sizeof aliases[0]; ++i)
        if (!strcmp(aliases[i].canonical, canonical) &&
            name_matches(aliases[i].alias, prefix, length, exact)) return 1;
    return 0;
}

int commands_expand_alias(const char *line, char *out, size_t capacity)
{
    size_t length = bounded_length(line), n = 0;
    if (length > ED_MAX_BYTES) return -1;
    if (!length || line[0] != '/') return 0;
    while (line[n + 1] && line[n + 1] != ' ') ++n;
    const char *canonical = commands_alias_name(line + 1, n);
    if (!canonical) return 0;
    size_t replacement = strlen(canonical), tail = length - n - 1u;
    if (!out || replacement + tail + 2u > capacity) return -1;
    /* Callers supply a separate buffer so argument bytes can be copied once. */
    out[0] = '/';
    memcpy(out + 1, canonical, replacement);
    memcpy(out + 1 + replacement, line + 1 + n, tail + 1u);
    return 1;
}

static Token word(const char *s)
{
    Token t;
    while (*s == ' ') ++s;
    t.start = s;
    while (*s && *s != ' ') ++s;
    t.size = (size_t)(s - t.start);
    t.separated = *s == ' ';
    while (*s == ' ') ++s;
    t.rest = s;
    return t;
}

static int token_equal(Token t, const char *name)
{
    size_t n = strlen(name);
    if (t.size != n) return 0;
    for (size_t i = 0; i < n; ++i)
        if (fold((unsigned char)t.start[i]) != (unsigned char)name[i]) return 0;
    return 1;
}

static int command_token(const char *line, Token *t)
{
    if (bounded_length(line) > ED_MAX_BYTES) return 0;
    if (*line == '/') ++line;
    *t = word(line);
    for (size_t i = 0; i < t->size; ++i) {
        unsigned char c = fold((unsigned char)t->start[i]);
        if (c < 'a' || c > 'z') return 0;
    }
    return 1;
}

const CommandSpec *commands_find(const char *line)
{
    Token t;
    if (!command_token(line, &t) || !t.size) return NULL;
    for (size_t i = 0; i < sizeof(catalog) / sizeof(catalog[0]); ++i)
        if (commands_name_matches(catalog[i].name, t.start, t.size, 1)) return &catalog[i];
    return NULL;
}

size_t commands_suggest_page(const char *prefix, const CommandSpec **out,
                             size_t cap, size_t offset, size_t *total)
{
    Token t;
    size_t count = 0, written = 0;
    if (total) *total = 0;
    if (!command_token(prefix, &t)) return 0;
    for (size_t i = 0; i < sizeof(catalog) / sizeof(catalog[0]); ++i) {
        if (!commands_name_matches(catalog[i].name, t.start, t.size, t.separated)) continue;
        if (out && count >= offset && written < cap) out[written++] = &catalog[i];
        ++count;
    }
    if (total) *total = count;
    return written;
}

size_t commands_suggest(const char *prefix, const CommandSpec **out, size_t cap)
{
    if (!out || !cap) return 0;
    return commands_suggest_page(prefix, out, cap, 0, NULL);
}

static void wipe(void *ptr, size_t size)
{
    volatile unsigned char *p = ptr;
    while (size--) *p++ = 0;
}

static int safe_text(const char *text, size_t size)
{
    char clean[ED_MAX_BYTES + 1];
    if (!irc_utf8_valid(text)) return 0;
    /* Use the same terminal-safety policy as received/displayed text, rather
     * than maintaining a second list of unsafe Unicode format characters. */
    size_t cleaned = irc_sanitize(clean, sizeof(clean), text);
    int safe = cleaned == size && !memcmp(clean, text, size);
    wipe(clean, cleaned + 1u);
    return safe;
}

static int service_target(Token target)
{
    static const char *const names[] = {
        "nickserv", "authserv", "chanserv", "operserv", "hostserv",
        "memoserv", "groupserv", "bouncerserv", "ns", "as", "cs", "os"
    };
    if (target.size && target.start[0] == '*') return 1;
    /* Service@server forms are also excluded, even though the current /msg
     * target validator does not accept them. */
    for (size_t i = 0; i < target.size; ++i)
        if (target.start[i] == '@') { target.size = i; break; }
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (token_equal(target, names[i])) return 1;
    return 0;
}

static Token credential_word(Token token)
{
    for (;;) {
        while (token.size && token.start[0] == ':') {
            ++token.start;
            --token.size;
        }
        if (token.size || !*token.rest) return token;
        token = word(token.rest);
    }
}

static int credential_text(Token first)
{
    static const char *const verbs[] = {
        "identify", "id", "auth", "authenticate", "login", "password",
        "pass", "register", "ghost", "recover", "release", "group",
        "drop", "resetpass", "setpass", "changepass", "verify", "oper"
    };
    first = credential_word(first);
    for (size_t i = 0; i < sizeof(verbs) / sizeof(verbs[0]); ++i)
        if (token_equal(first, verbs[i])) return 1;
    if (token_equal(first, "set") || token_equal(first, "saset")) {
        Token next = credential_word(word(first.rest));
        if (token_equal(next, "password") || token_equal(next, "pass")) return 1;
        /* Services may place an account between SASET and PASSWORD. */
        if (token_equal(first, "saset")) {
            next = credential_word(word(next.rest));
            if (token_equal(next, "password") || token_equal(next, "pass")) return 1;
        }
    }
    return 0;
}

static int sensitive_command(const CommandSpec *spec, Token name)
{
    /* Authentication source paths and account choices are private too. */
    if (!strcmp(spec->name, "autoauth") || !strcmp(spec->name, "autocommands") ||
        !strcmp(spec->name, "znc") || !strcmp(spec->name, "soju")) return 1;
    if (!strcmp(spec->name, "join")) {
        Token channel = word(name.rest);
        return *channel.rest != 0; /* Any supplied key, even malformed. */
    }
    if (!strcmp(spec->name, "msg")) {
        Token target = word(name.rest);
        return service_target(target) || credential_text(word(target.rest));
    }
    return 0;
}

void commands_history_init(CommandHistory *history)
{
    if (history) memset(history, 0, sizeof(*history));
}

static void discard_oldest(CommandHistory *history)
{
    if (!history->count) return;
    char *text = history->entries[--history->count];
    size_t size = strlen(text) + 1u;
    history->bytes -= size;
    wipe(text, size);
    free(text);
    history->entries[history->count] = NULL;
}

void commands_history_free(CommandHistory *history)
{
    if (!history) return;
    while (history->count) discard_oldest(history);
    memset(history, 0, sizeof(*history));
}

int commands_history_add(CommandHistory *history, const char *line)
{
    Token name;
    size_t size = bounded_length(line);
    if (!history || !size || size > ED_MAX_BYTES ||
        size >= COMMAND_HISTORY_MAX_BYTES || line[0] != '/') return 0;
    const CommandSpec *spec = commands_find(line);
    if (!spec || !command_token(line, &name) || sensitive_command(spec, name) ||
        !safe_text(line, size)) return 0;
    if (history->count && !strcmp(history->entries[0], line)) return 0;
    char *copy = malloc(size + 1u);
    if (!copy) return -1;
    memcpy(copy, line, size + 1u);
    /* Allocate before eviction so ENOMEM cannot discard existing history.
     * Transient allocation adds at most one ED_MAX_BYTES+1 byte string. */
    while (history->count >= COMMAND_HISTORY_MAX_ENTRIES ||
           history->bytes > COMMAND_HISTORY_MAX_BYTES - (size + 1u))
        discard_oldest(history);
    if (history->count)
        memmove(history->entries + 1, history->entries,
                history->count * sizeof(history->entries[0]));
    history->entries[0] = copy;
    ++history->count;
    history->bytes += size + 1u;
    return 1;
}

static int history_prefix(const char *line, const char *prefix)
{
    int name = 1;
    if (*line == '/') ++line;
    if (*prefix == '/') ++prefix;
    while (*line == ' ') ++line;
    while (*prefix == ' ') ++prefix;
    while (*prefix) {
        unsigned char a = (unsigned char)*line, b = (unsigned char)*prefix;
        if (!a) return 0;
        if (name && b == ' ') name = 0;
        if ((name ? fold(a) : a) != (name ? fold(b) : b)) return 0;
        ++line; ++prefix;
    }
    return 1;
}

const char *commands_history_match(const CommandHistory *history,
                                   const char *prefix, size_t ordinal)
{
    if (!history || bounded_length(prefix) > ED_MAX_BYTES) return NULL;
    for (size_t i = 0; i < history->count; ++i)
        if (history_prefix(history->entries[i], prefix)) {
            if (!ordinal) return history->entries[i];
            --ordinal;
        }
    return NULL;
}

int commands_history_remove(CommandHistory *history, size_t index)
{
    if (!history || index >= history->count) return 0;
    char *text = history->entries[index];
    size_t bytes = strlen(text) + 1u;
    wipe(text, bytes);
    free(text);
    history->bytes -= bytes;
    --history->count;
    if (index < history->count)
        memmove(history->entries + index, history->entries + index + 1u,
                (history->count - index) * sizeof history->entries[0]);
    history->entries[history->count] = NULL;
    return 1;
}

const char *commands_suggestion_mode_name(int mode)
{
    static const char *const names[] = {
        "alphabetical", "common", "history", "frequent", "contextual"
    };
    return names[mode >= CONFIG_SUGGEST_ALPHABETICAL && mode <= CONFIG_SUGGEST_CONTEXTUAL ? mode : 0];
}

const char *commands_suggestion_mode_label(int mode)
{
    static const char *const names[] = {
        "Alphabetical", "Commonly used", "Recent history", "Most used with this configuration", "Contextual"
    };
    return names[mode >= CONFIG_SUGGEST_ALPHABETICAL && mode <= CONFIG_SUGGEST_CONTEXTUAL ? mode : 0];
}

void commands_usage_record(Config *config, const char *line)
{
    if (!config || !line || line[0] != '/') return;
    const CommandSpec *spec = commands_find(line);
    if (!spec || strlen(spec->name) >= CONFIG_COMMAND_NAME_SIZE ||
        config->command_usage_count > CONFIG_COMMAND_USAGE_MAX) return;
    size_t i;
    for (i = 0; i < config->command_usage_count; ++i)
        if (!strcmp(config->command_usage[i].name, spec->name)) break;
    if (i == config->command_usage_count) {
        if (i == CONFIG_COMMAND_USAGE_MAX) return;
        memcpy(config->command_usage[i].name, spec->name, strlen(spec->name) + 1u);
        config->command_usage[i].count = 0;
        ++config->command_usage_count;
    }
    if (config->command_usage[i].count < CONFIG_COMMAND_USAGE_LIMIT)
        ++config->command_usage[i].count;
}

int commands_usage_reset(Config *config, const char *name)
{
    const CommandSpec *spec = commands_find(name);
    if (!config || !spec || config->command_usage_count > CONFIG_COMMAND_USAGE_MAX) return 0;
    for (size_t i = 0; i < config->command_usage_count; ++i)
        if (!strcmp(config->command_usage[i].name, spec->name)) {
            unsigned previous = config->command_usage[i].count;
            config->command_usage[i].count = 0;
            return previous != 0;
        }
    return 0;
}

static unsigned priority(const char *name, const char *const *names, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(name, names[i])) return (unsigned)(count - i);
    return 0;
}

unsigned commands_suggestion_score(const char *name, ConfigSuggestMode mode,
                                   CommandContext context, const Config *config)
{
    /* Curated common tasks, not telemetry or a claim of measured popularity. */
    static const char *const common[] = {
        "join", "msg", "me", "query", "nick", "part", "whois", "summary",
        "servers", "channels", "ql", "unread", "help", "connect", "quit"
    };
    static const char *const disconnected[] = {
        "connect", "server", "servers", "settings", "help", "summary", "quit",
        "quickstart", "autoconnect", "save", "panel"
    };
    static const char *const conversation[] = {
        "me", "msg", "query", "whois", "part", "topic", "users", "summary",
        "join", "nick", "away", "back", "close", "help", "quit"
    };
    static const char *const client[] = {
        "summary", "servers", "settings", "quit", "help", "connect",
        "unread", "mentions", "transfers", "panel", "save", "background"
    };
    static const char *const server[] = {
        "join", "list", "channels", "nick", "connect", "disconnect", "autojoin",
        "autoauth", "autoconnect", "autocommands", "summary", "server", "query", "whois"
    };
    static const char *const channel[] = {
        "me", "topic", "users", "part", "msg", "query", "whois", "summary",
        "close", "channel", "channels", "ignore", "clear", "away", "back"
    };
    static const char *const query[] = {
        "me", "whois", "msg", "close", "dcc", "query", "ql", "summary",
        "ignore", "clear", "away", "back"
    };
    if (!name) return 0;
    if (mode == CONFIG_SUGGEST_FREQUENT) {
        if (!config || config->command_usage_count > CONFIG_COMMAND_USAGE_MAX) return 0;
        for (size_t i = 0; i < config->command_usage_count; ++i)
            if (!strcmp(config->command_usage[i].name, name)) return config->command_usage[i].count;
        return 0;
    }
#define PRIORITY(list) priority(name, list, sizeof list / sizeof list[0])
    if (mode == CONFIG_SUGGEST_COMMON) {
        if (context == COMMAND_CONTEXT_DISCONNECTED) return PRIORITY(disconnected);
        if (context == COMMAND_CONTEXT_SERVER) return PRIORITY(server);
        if (context == COMMAND_CONTEXT_CHANNEL || context == COMMAND_CONTEXT_QUERY) return PRIORITY(conversation);
        return PRIORITY(common);
    }
    if (mode == CONFIG_SUGGEST_CONTEXTUAL) {
        unsigned preferred;
        if (context == COMMAND_CONTEXT_SERVER) preferred = PRIORITY(server);
        else if (context == COMMAND_CONTEXT_CHANNEL) preferred = PRIORITY(channel);
        else if (context == COMMAND_CONTEXT_QUERY) preferred = PRIORITY(query);
        else preferred = PRIORITY(client);
        return preferred ? 1000u + preferred : PRIORITY(common);
    }
#undef PRIORITY
    return 0;
}

size_t commands_suggest_ranked(const char *prefix, ConfigSuggestMode mode,
                               CommandContext context, const CommandHistory *history,
                               const Config *config, CommandSuggestion *out,
                               size_t cap, size_t offset, size_t *total)
{
    Token token;
    size_t count = 0, written = 0;
    if (total) *total = 0;
    if (!command_token(prefix, &token)) return 0;
    if (mode == CONFIG_SUGGEST_HISTORY) {
        if (!history) return 0;
        for (size_t i = 0; i < history->count; ++i) {
            if (!history_prefix(history->entries[i], prefix)) continue;
            if (out && count >= offset && written < cap) {
                out[written].spec = commands_find(history->entries[i]);
                out[written].text = history->entries[i];
                out[written++].history_index = i;
            }
            ++count;
        }
    } else {
        const CommandSpec *matches[sizeof catalog / sizeof catalog[0]];
        unsigned scores[sizeof catalog / sizeof catalog[0]];
        for (size_t i = 0; i < sizeof catalog / sizeof catalog[0]; ++i) {
            if (!commands_name_matches(catalog[i].name, token.start, token.size, token.separated)) continue;
            unsigned score = commands_suggestion_score(catalog[i].name, mode, context, config);
            size_t at = count;
            /* The catalog is alphabetical, so equal scores stay alphabetical. */
            while (at && scores[at - 1u] < score) {
                matches[at] = matches[at - 1u]; scores[at] = scores[at - 1u]; --at;
            }
            matches[at] = &catalog[i]; scores[at] = score; ++count;
        }
        if (offset < count)
            for (size_t i = offset; i < count && out && written < cap; ++i) {
                out[written].spec = matches[i];
                out[written].text = NULL;
                out[written++].history_index = SIZE_MAX;
            }
    }
    if (total) *total = count;
    return written;
}
