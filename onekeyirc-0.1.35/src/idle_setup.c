/* Guided setup model shared by both terminal frontends.
 *
 * Source order: credential destinations/cleanup; profile selection and question
 * navigation; bounded view construction; answer parsing; then merge/commit/poll.
 * The original/work pair records user edits while live Client/App state can
 * change. idle_setup_commit() merges into a fresh effective configuration.
 * Presentation timing and key ownership belong to idle.c/guided.c. */
#define IRC_L10N_IMPLEMENTATION
#include "idle_setup.h"
#include "l10n.h"
#include "credentials.h"
#include "commands.h"

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum Field {
    PROFILE = -1, SERVER, TLS, PORT, NICK, BOUNCER, BOUNCER_USER,
    BOUNCER_NETWORK, BOUNCER_CLIENT, BOUNCER_AWAY, USERNAME, REALNAME,
    AUTOCONNECT, CHANNELS, AUTH, AUTH_USER, AUTH_SOURCE, AUTH_GPG_MODE, AUTH_VALUE,
    AUTH_GPG_KEY, AUTH_GPG_SECRET, AUTH_GPG_CONFIRM,
    SERVER_SOURCE, SERVER_GPG_MODE, SERVER_VALUE, SERVER_GPG_KEY,
    SERVER_GPG_SECRET, SERVER_GPG_CONFIRM, CA_FILE, HISTORY, SEND_MODE, COMPOSER_MODE, OUTPUT_FORMAT, QUEUE_NOTICES,
    PROGRAM_MESSAGES, PERSISTENT_LOGGING, SINGLE_SESSION, COMMAND_SUGGESTIONS, DEFAULT_INTERFACE, WATCH_WORD, IGNORE_MASK, FINISH
};
enum Source { SOURCE_NONE, SOURCE_FILE, SOURCE_GPG, SOURCE_PLAIN };
typedef struct Creation {
    int create, confirmed, ready, managed_default, published;
    char fingerprint[CREDENTIAL_FINGERPRINT_MAX + 1];
    char secret[CONFIG_SECRET_MAX + 1];
    CredentialCreate *job;
} Creation;
/* Owns both credential jobs and draft copies; Client is borrowed. index is
 * a stable slot, and token prevents committing into a replacement profile. */
struct IdleSetup {
    Client *client;
    size_t index, item;
    uint64_t token;
    Config original, work;
    int quick, application, field, source, server_source, action, cleared, confirmed, pending;
    Creation creation[2];
};

static int fail(char *out, size_t cap, const char *format, ...)
{
    if (out && cap) {
        va_list args;
        va_start(args, format);
        (void)vsnprintf(out, cap, format, args);
        va_end(args);
    }
    return 0;
}

static int text_set(char *, size_t, const char *, int, char *, size_t);

static int source_of(const Config *cfg, int server)
{
    if (server ? cfg->server_password_file[0] : cfg->password_file[0]) return SOURCE_FILE;
    if (server ? cfg->server_password_gpg_file[0] : cfg->password_gpg_file[0]) return SOURCE_GPG;
    if (server ? cfg->server_password[0] : cfg->password[0]) return SOURCE_PLAIN;
    return SOURCE_NONE;
}

static const char *source_name(int source)
{
    static const char *const names[] = {"none", "file", "gpg", "plain"};
    return source >= SOURCE_NONE && source <= SOURCE_PLAIN ? names[source] : "none";
}

static char *source_value(Config *cfg, int server, int source, size_t *cap)
{
    if (source == SOURCE_PLAIN) {
        *cap = server ? sizeof cfg->server_password : sizeof cfg->password;
        return server ? cfg->server_password : cfg->password;
    }
    *cap = CONFIG_PATH_MAX;
    if (source == SOURCE_GPG) return server ? cfg->server_password_gpg_file : cfg->password_gpg_file;
    return server ? cfg->server_password_file : cfg->password_file;
}

static const char *setup_config_path(const IdleSetup *s)
{
    const ClientSlot *slot = &s->client->slots[s->index];
    return *slot->config_path ? slot->config_path : s->client->primary_path;
}

/* Offering a destination only inspects metadata. Its private directory is
 * prepared after the user accepts this specific path, never while drawing. */
static void offer_credential_path(IdleSetup *s, size_t which)
{
    Creation *creation = &s->creation[which];
    char *path = which ? s->work.server_password_gpg_file : s->work.password_gpg_file;
    char detail[512];
    creation->managed_default = 0;
    if (!s->client->test_mode &&
        credential_default_path(setup_config_path(s), (int)which, path,
                                CONFIG_PATH_MAX, detail, sizeof detail) > 0)
        creation->managed_default = 1;
    else path[0] = 0;
    config_wipe(detail, sizeof detail);
}

/* Compare a not-yet-existing leaf by its containing directory, also catching
 * aliases in configuration paths without relaxing credential path rules. */
static int same_destination(const char *first, const char *second)
{
    if (!first || !second || !*first || !*second) return 0;
    if (!strcmp(first, second)) return 1;
    const char *one = strrchr(first, '/'), *two = strrchr(second, '/');
    if (strcmp(one ? one + 1 : first, two ? two + 1 : second)) return 0;
    char a[CONFIG_PATH_MAX], b[CONFIG_PATH_MAX];
    size_t an = one ? (size_t)(one - first) : 0, bn = two ? (size_t)(two - second) : 0;
    if (an >= sizeof a || bn >= sizeof b) return 0;
    if (an) { memcpy(a, first, an); a[an] = 0; }
    else strcpy(a, one ? "/" : ".");
    if (bn) { memcpy(b, second, bn); b[bn] = 0; }
    else strcpy(b, two ? "/" : ".");
    struct stat ast, bst;
    return !stat(a, &ast) && !stat(b, &bst) && S_ISDIR(ast.st_mode) &&
           S_ISDIR(bst.st_mode) && ast.st_dev == bst.st_dev && ast.st_ino == bst.st_ino;
}

static int configuration_destination(const char *candidate, const char *config, int primary)
{
    if (!config || !*config) return 0;
    if (same_destination(candidate, config)) return 1;
    static const char *const suffixes[] = {".commands", ".servers", ".servers.lock", ".d"};
    for (size_t i = 0; i < (primary ? 4u : 1u); ++i) {
        char sidecar[CONFIG_PATH_MAX];
        int n = snprintf(sidecar, sizeof sidecar, "%s%s", config, suffixes[i]);
        if (n >= 0 && (size_t)n < sizeof sidecar && same_destination(candidate, sidecar)) return 1;
    }
    return 0;
}

static int distinct_credential_destination(const IdleSetup *s, const Config *next,
                                           int server, const char *path,
                                           char *error, size_t cap)
{
    if (configuration_destination(path, s->client->primary_path, 1))
        return fail(error, cap, "The new credential file must differ from configuration files and their sidecars; choose the offered credentials directory.");
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (s->client->slots[i].used && configuration_destination(path, s->client->slots[i].config_path, !i))
            return fail(error, cap, "The new credential file matches a server configuration file; choose a different destination.");
    const char *other = server ? next->password_gpg_file : next->server_password_gpg_file;
    if (s->creation[!server].create && same_destination(path, other))
        return fail(error, cap, "Choose different new files for the authentication and server passwords.");
    return 1;
}

static int credential_path_answer(IdleSetup *s, Config *next, int server,
                                 int source, const char *answer,
                                 char *error, size_t cap)
{
    size_t size;
    char *value = source_value(next, server, source, &size);
    if (!text_set(value, size, answer, 0, error, cap)) return 0;
    Creation *creation = &s->creation[server];
    int create = source == SOURCE_GPG && creation->create;
    if (create && s->client->test_mode)
        return fail(error, cap, "GPG file creation is disabled in --test-mode; no helper, keyring, or credential path was accessed.");
    if (source == SOURCE_PLAIN)
        return *value ? 1 : fail(error, cap, "Enter the password, or go back to change its source.");
    if (!*value)
        return fail(error, cap, "Enter the credential file path, or go back and select Create new to offer a private default location.");
    char absolute[CONFIG_PATH_MAX];
    if (create) {
        if (credential_absolute_path(value, absolute, sizeof absolute, error, cap) < 0) return 0;
        if (!distinct_credential_destination(s, next, server, absolute, error, cap)) return 0;
        const char *offered = server ? s->work.server_password_gpg_file : s->work.password_gpg_file;
        if (!strcmp(absolute, offered) && credential_create_published(creation->job)) {
            /* Retrying after a later save/publication failure may revisit a
             * file created by this job. Recheck its inode and complete bytes. */
            if (credential_create_publish(creation->job, error, cap) < 0) return 0;
        } else {
            if (creation->managed_default && !strcmp(absolute, offered) &&
                credential_default_prepare(setup_config_path(s), absolute, error, cap) < 0) return 0;
            if (credential_create_check(absolute, error, cap) < 0) return 0;
        }
    } else if (*value == '/') {
        memcpy(absolute, value, strlen(value) + 1u);
    } else {
        /* Anchor existing sources without changing their established read
         * semantics for parent components. Validation remains at connection. */
        char current[CONFIG_PATH_MAX];
        if (!getcwd(current, sizeof current))
            return fail(error, cap, "Cannot determine the current directory; enter an absolute credential path.");
        int n = snprintf(absolute, sizeof absolute, "%s/%s", current, value);
        if (n < 0 || (size_t)n >= sizeof absolute)
            return fail(error, cap, "The absolute credential path is too long; use a shorter path.");
    }
    memcpy(value, absolute, strlen(absolute) + 1u);
    return 1;
}

static void clear_source(Config *cfg, int server)
{
    if (server) {
        config_wipe(cfg->server_password, sizeof cfg->server_password);
        config_wipe(cfg->server_password_file, sizeof cfg->server_password_file);
        config_wipe(cfg->server_password_gpg_file, sizeof cfg->server_password_gpg_file);
    } else {
        config_wipe(cfg->password, sizeof cfg->password);
        config_wipe(cfg->password_file, sizeof cfg->password_file);
        config_wipe(cfg->password_gpg_file, sizeof cfg->password_gpg_file);
    }
}

static void clear_auth(Config *cfg)
{
    clear_source(cfg, 0); clear_source(cfg, 1);
    config_wipe(cfg->auth_user, sizeof cfg->auth_user);
    (void)snprintf(cfg->auth, sizeof cfg->auth, "none");
}

static void clear_connection_credentials(Config *cfg)
{
    clear_auth(cfg);
    if (cfg->bouncer_type == CONFIG_BOUNCER_SOJU)
        (void)snprintf(cfg->auth, sizeof cfg->auth, "sasl");
    /* Startup messages may themselves contain account or channel secrets. */
    config_wipe(cfg->autocommands.lines, sizeof cfg->autocommands.lines);
    cfg->autocommands.count = 0;
}

static const char *bouncer_name(int type)
{
    return type == CONFIG_BOUNCER_ZNC ? "znc" :
           type == CONFIG_BOUNCER_SOJU ? "soju" : "direct";
}

static int connection_identity_changed(const Config *old, const Config *work)
{
#define DIFFERENT(member) (strcmp(old->member, work->member) != 0)
    return old->bouncer_type != work->bouncer_type || old->port != work->port || old->tls != work->tls ||
        DIFFERENT(server) || DIFFERENT(ca_file) || DIFFERENT(bouncer_user) || DIFFERENT(bouncer_network) ||
        DIFFERENT(bouncer_client) || DIFFERENT(auth) || DIFFERENT(auth_user);
#undef DIFFERENT
}

static int connection_credentials_edited(const Config *old, const Config *work)
{
#define DIFFERENT(member) (strcmp(old->member, work->member) != 0)
    return connection_identity_changed(old, work) ||
        DIFFERENT(password) || DIFFERENT(password_file) || DIFFERENT(password_gpg_file) ||
        DIFFERENT(server_password) || DIFFERENT(server_password_file) || DIFFERENT(server_password_gpg_file);
#undef DIFFERENT
}

static void reset_creation(Creation *creation)
{
    credential_create_cancel(creation->job);
    config_wipe(creation, sizeof *creation);
}

static void cancel_creation_jobs(IdleSetup *s)
{
    for (size_t i = 0; i < 2; ++i) {
        if (s->creation[i].job) s->creation[i].published = 0;
        credential_create_cancel(s->creation[i].job);
        s->creation[i].job = NULL;
        s->creation[i].ready = 0;
    }
    s->pending = 0;
}

/* Draft lifetime and stable profile selection. */
static int select_profile(IdleSetup *s, size_t index, char *error, size_t cap)
{
    if (index >= CLIENT_MAX_SERVERS || !s->client->slots[index].used)
        return fail(error, cap, "Choose one of the displayed panel numbers.");
    ClientSlot *slot = &s->client->slots[index];
    if (slot->load_failed)
        return fail(error, cap, "That profile could not be loaded; correct its configuration file first.");
    for (size_t i = 0; i < 2; ++i) reset_creation(&s->creation[i]);
    config_wipe(&s->original, sizeof s->original);
    config_wipe(&s->work, sizeof s->work);
    if (slot->app) app_config_effective(slot->app, &s->original);
    else s->original = slot->seed;
    s->work = s->original;
    s->index = index; s->token = slot->token; s->item = 0; s->cleared = 0;
    s->application = 0; s->action = s->quick ? 3 : 2;
    s->source = source_of(&s->work, 0); s->server_source = source_of(&s->work, 1);
    return 1;
}

static int select_application(IdleSetup *s, char *error, size_t cap)
{
    if (!select_profile(s, 0, error, cap)) return 0;
    s->application = 1; s->action = 1;
    return 1;
}

IdleSetup *idle_setup_new(Client *client, int quick, char *error, size_t cap)
{
    if (!client) { fail(error, cap, "No configuration is available."); return NULL; }
    IdleSetup *s = calloc(1, sizeof *s);
    if (!s) { fail(error, cap, "Cannot allocate the configuration draft; nothing changed."); return NULL; }
    s->client = client; s->quick = !!quick;
    s->field = quick ? SERVER : PROFILE;
    int selected = quick ? select_profile(s, client->current, error, cap) :
                           select_application(s, error, cap);
    if (!selected) { idle_setup_free(s); return NULL; }
    if (quick && s->work.bouncer_type != CONFIG_BOUNCER_NONE) {
        fail(error, cap, "Use /panel to edit this bouncer profile, including its network and password source.");
        idle_setup_free(s); return NULL;
    }
    if (error && cap) error[0] = 0;
    return s;
}

void idle_setup_free(IdleSetup *s)
{
    if (!s) return;
    cancel_creation_jobs(s);
    config_wipe(s, sizeof *s);
    free(s);
}

static int active(const IdleSetup *s, int field)
{
    if (field == FINISH || field == PROFILE) return 1;
    if (s->quick) return field >= SERVER && field <= NICK;
    if (field == DEFAULT_INTERFACE) return s->application && !s->work.minimal;
    if (s->application) return field == HISTORY || field == SEND_MODE || field == COMPOSER_MODE || field == OUTPUT_FORMAT || field == QUEUE_NOTICES || field == PROGRAM_MESSAGES || field == PERSISTENT_LOGGING || field == SINGLE_SESSION || field == COMMAND_SUGGESTIONS || field == DEFAULT_INTERFACE;
    if (field >= BOUNCER_USER && field <= BOUNCER_AWAY) return s->work.bouncer_type != CONFIG_BOUNCER_NONE;
    if (field == CHANNELS || field == AUTH_USER || field == AUTH)
        return s->work.bouncer_type == CONFIG_BOUNCER_NONE &&
               (field == CHANNELS || (s->work.tls &&
                (field == AUTH || !strcmp(s->work.auth, "sasl"))));
    if (field == SERVER_SOURCE) return s->work.tls && s->work.bouncer_type != CONFIG_BOUNCER_SOJU;
    if (field == CA_FILE) return s->work.tls;
    if (field == AUTH_SOURCE) return s->work.tls && !strcmp(s->work.auth, "sasl");
    if (field == AUTH_VALUE) return s->work.tls && !strcmp(s->work.auth, "sasl") && s->source != SOURCE_NONE;
    if (field == SERVER_VALUE) return s->work.tls && s->server_source != SOURCE_NONE;
    if (field == AUTH_GPG_MODE || (field >= AUTH_GPG_KEY && field <= AUTH_GPG_CONFIRM))
        return s->work.tls && !strcmp(s->work.auth, "sasl") && s->source == SOURCE_GPG &&
               (field == AUTH_GPG_MODE || s->creation[0].create);
    if (field == SERVER_GPG_MODE || (field >= SERVER_GPG_KEY && field <= SERVER_GPG_CONFIRM))
        return s->work.tls && s->server_source == SOURCE_GPG &&
               (field == SERVER_GPG_MODE || s->creation[1].create);
    if (field == HISTORY || field == SEND_MODE || field == COMPOSER_MODE || field == OUTPUT_FORMAT || field == QUEUE_NOTICES || field == PROGRAM_MESSAGES || field == PERSISTENT_LOGGING || field == SINGLE_SESSION || field == COMMAND_SUGGESTIONS || field == DEFAULT_INTERFACE) return 0;
    return field >= PROFILE && field < FINISH;
}

static void advance(IdleSetup *s)
{
    do { ++s->field; } while (s->field < FINISH && !active(s, s->field));
    s->item = 0;
}

int idle_setup_back(IdleSetup *s)
{
    if (s) {
        s->confirmed = 0;
        if (s->pending) cancel_creation_jobs(s);
    }
    if (!s || s->field == PROFILE || (s->quick && s->field == SERVER)) return 0;
    if ((s->field == WATCH_WORD || s->field == IGNORE_MASK) && s->item) {
        --s->item; return 1;
    }
    do { --s->field; } while (s->field > PROFILE && !active(s, s->field));
    s->item = 0;
    if (s->field == WATCH_WORD) s->item = s->work.word_count ? s->work.word_count - 1u : 0;
    if (s->field == IGNORE_MASK) s->item = s->work.ignore_count ? s->work.ignore_count - 1u : 0;
    return 1;
}

static const char *const questions[] = {
    "Server", "Use TLS", "Port", "Nickname", "Connection type", "Bouncer username",
    "Bouncer network", "Bouncer client name", "Apply automatic away", "IRC username", "Real name",
    "Auto-connect at startup", "Auto-join channels", "Auto-authentication",
    "Authentication account", "Authentication password source", "Authentication GPG file", "Authentication password",
    "Authentication GPG key fingerprint", "Authentication password to encrypt", "Confirm authentication password",
    "Server password source", "Server GPG file", "Server password", "Server GPG key fingerprint",
    "Server password to encrypt", "Confirm server password", "CA certificate file", "History lines",
    "Send mode", "Composer mode", "Output format", "Queue notices", "Program messages", "Persistent logging", "Single-session mode", "Command suggestions", "Default interface", "Mention text", "Ignored sender", "Finish"
};

static void merge(const IdleSetup *, Config *);

/* Read-only presentation snapshot; descriptions must not expose secrets. */
void idle_setup_view(const IdleSetup *s, IdleSetupView *v)
{
    memset(v, 0, sizeof *v);
    if (!s) return;
    const Config *c = &s->work;
    if (s->field == PROFILE) (void)snprintf(v->title, sizeof v->title, "%s", irc_tr("Settings panel"));
    else if (s->application)
        (void)snprintf(v->title, sizeof v->title, "%s | %s", irc_tr("Settings panel"), irc_tr("Application preferences"));
    else (void)snprintf(v->title, sizeof v->title, "%s | Server %zu",
                       irc_tr(s->quick ? "Quickstart" : "Settings panel"), s->index + 1u);
    (void)snprintf(v->question, sizeof v->question, "%s",
                   irc_tr(s->field == PROFILE ? "Choose a panel" : questions[s->field]));
    v->step = 1;
    for (int i = s->quick ? SERVER : PROFILE; i < s->field; ++i) if (active(s, i)) ++v->step;
    if (s->pending) {
        (void)snprintf(v->question, sizeof v->question, "%s", irc_tr("Creating encrypted credentials"));
        (void)snprintf(v->current, sizeof v->current, "%s", irc_tr("Working"));
        (void)snprintf(v->detail, sizeof v->detail, "GPG is encrypting with the selected local key. Settings have not been applied.");
        v->final = 1;
        return;
    }
    const char *value = "";
    switch (s->field) {
    case PROFILE: {
        (void)snprintf(v->current, sizeof v->current, "%zu", s->application ? 0u : s->index + 1u);
        (void)snprintf(v->detail, sizeof v->detail, "Application preferences apply to every server. Server panels edit one connection profile.");
        size_t used = (size_t)snprintf(v->summary, sizeof v->summary, "0: Application preferences\n");
        for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
            const ClientSlot *slot = &s->client->slots[i];
            if (!slot->used) continue;
            const char *host = slot->app ? slot->app->cfg.server : slot->seed.server;
            int n = snprintf(v->summary + used, sizeof v->summary - used, "%zu: %.180s%s\n",
                             i + 1u, *host ? host : "(not configured)", slot->load_failed ? " (unavailable)" : "");
            if (n < 0 || (size_t)n >= sizeof v->summary - used) break;
            used += (size_t)n;
        }
        return;
    }
    case SERVER: value = c->server; (void)snprintf(v->detail, sizeof v->detail, "%s", s->quick ?
        "Enter the IRC server hostname or IP address, without a URL or port. For ZNC or soju setup use /panel." :
        "Enter the IRC server or bouncer hostname or IP address, without a URL or port. Bouncers connect at their provider's address."); break;
    case TLS: value = c->tls ? "yes" : "no"; (void)snprintf(v->detail, sizeof v->detail, "yes = verified encrypted connection; no = unencrypted IRC."); break;
    case PORT: (void)snprintf(v->current, sizeof v->current, "%d", c->port); return;
    case NICK: value = c->nick; (void)snprintf(v->detail, sizeof v->detail, "Your nickname on IRC."); break;
    case BOUNCER:
        value = bouncer_name(c->bouncer_type);
        (void)snprintf(v->detail, sizeof v->detail, "direct = ordinary IRC; znc or soju = one bounced network. Changing type clears this draft's previous credentials, startup commands and bouncer identity. Bouncers require verified TLS; the provider's port stays unchanged."); break;
    case BOUNCER_USER: case BOUNCER_NETWORK: case BOUNCER_CLIENT:
        value = s->field == BOUNCER_USER ? c->bouncer_user :
                s->field == BOUNCER_NETWORK ? c->bouncer_network : c->bouncer_client;
        (void)snprintf(v->detail, sizeof v->detail, "%s Up to 63 ASCII letters, digits, '.', '_' or '-'; do not include '/' or '@'. Changing identity clears this draft's password source and startup commands.",
            s->field == BOUNCER_USER ? "Your bouncer account, not the upstream NickServ account." :
            s->field == BOUNCER_NETWORK ? "The network name already configured on your bouncer. The bouncer restores its joined channels." :
            "Optional stable name for this device, such as laptop. '-' clears it."); break;
    case BOUNCER_AWAY:
        value = c->bouncer_away ? "yes" : "no";
        (void)snprintf(v->detail, sizeof v->detail, "no lets the bouncer manage automatic away. yes applies this client's existing idle/background-away rules to this profile; other connected devices may still be active. Manual /away and /back always work."); break;
    case USERNAME: value = c->username; break;
    case REALNAME: value = c->realname; (void)snprintf(v->detail, sizeof v->detail, "A public description; it need not be your legal name. '-' clears it."); break;
    case AUTOCONNECT: value = c->autoconnect ? "yes" : "no"; (void)snprintf(v->detail, sizeof v->detail, "Connect this profile automatically on future starts: yes or no."); break;
    case CHANNELS: value = c->channels; (void)snprintf(v->detail, sizeof v->detail, "Comma-separated channels, for example #chat,#friends. '-' clears saved joins."); break;
    case AUTH: value = c->auth; (void)snprintf(v->detail, sizeof v->detail, "none = no automatic account login; sasl = log in during connection. Verified TLS is required."); break;
    case AUTH_USER: value = c->auth_user; break;
    case AUTH_SOURCE: case SERVER_SOURCE: {
        int source = s->field == SERVER_SOURCE ? s->server_source : s->source;
        value = source_name(source);
        if (c->bouncer_type != CONFIG_BOUNCER_NONE) {
            (void)snprintf(v->question, sizeof v->question, "%s", irc_tr("Bouncer password source"));
            (void)snprintf(v->detail, sizeof v->detail, "file (private plaintext file), gpg (create or use an encrypted file), or plain (hidden entry saved in config). Supply the bouncer PASSWORD ONLY; the client adds the account and network. A source is required.");
        } else (void)snprintf(v->detail, sizeof v->detail, "none, file (private plaintext file), gpg (create or use an encrypted file), or plain (hidden entry saved in config).");
        break;
    }
    case AUTH_GPG_MODE: case SERVER_GPG_MODE: {
        const Creation *creation = &s->creation[s->field == SERVER_GPG_MODE];
        value = creation->create ? "create" : "existing";
        if (c->bouncer_type != CONFIG_BOUNCER_NONE)
            (void)snprintf(v->question, sizeof v->question, "%s", irc_tr("Bouncer GPG file"));
        (void)snprintf(v->detail, sizeof v->detail, "create = encrypt a password into a new file; existing = use a file you already encrypted. Create offers a private file location beside this configuration and uses an existing local GPG key.");
        break;
    }
    case AUTH_VALUE: case SERVER_VALUE: {
        int server = s->field == SERVER_VALUE, source = server ? s->server_source : s->source;
        if (c->bouncer_type != CONFIG_BOUNCER_NONE)
            (void)snprintf(v->question, sizeof v->question, "%s", irc_tr("Bouncer password"));
        if (source == SOURCE_PLAIN) {
            v->secret = 1;
            value = (server ? c->server_password[0] : c->password[0]) ? "(set; hidden)" : "(empty)";
            (void)snprintf(v->detail, sizeof v->detail, "Stored directly in the private configuration file. Password input is hidden.");
        } else {
            value = source == SOURCE_GPG ? (server ? c->server_password_gpg_file : c->password_gpg_file) :
                                         (server ? c->server_password_file : c->password_file);
            (void)snprintf(v->question, sizeof v->question, "%s password file",
                c->bouncer_type != CONFIG_BOUNCER_NONE ? "Bouncer" : server ? "Server" : "Authentication");
            if (source == SOURCE_GPG && s->creation[server].create) {
                if (s->client->test_mode)
                    (void)snprintf(v->detail, sizeof v->detail, "GPG credential creation is unavailable in --test-mode. Go back to choose an existing file; no credential path is inspected here.");
                else (void)snprintf(v->detail, sizeof v->detail,
                    "%s The destination is checked before the key and password questions. The encrypted file is created only at Finish; existing files are never replaced. Paths are literal, with no shell expansion.",
                    s->creation[server].managed_default ?
                    "Enter accepts this offered absolute path and prepares its private directory (0700). You may enter a different path in an existing private directory." :
                    "Enter a new file in a directory you own with mode 0700.");
            }
            else (void)snprintf(v->detail, sizeof v->detail, "%s path; no shell expansion. The file is read only when connecting.",
                               source == SOURCE_GPG ? "GPG-encrypted" : "Private plaintext");
            if (*value) (void)snprintf(v->summary, sizeof v->summary, "%s credential file:\n%s\n",
                s->creation[server].published ? "Created" :
                source == SOURCE_GPG && s->creation[server].create ? "Proposed (not created)" : "Configured", value);
        }
        break;
    }
    case AUTH_GPG_KEY: case SERVER_GPG_KEY: {
        const Creation *creation = &s->creation[s->field == SERVER_GPG_KEY];
        value = creation->fingerprint;
        (void)snprintf(v->detail, sizeof v->detail, "Full 40- or 64-digit hexadecimal fingerprint of your local encryption key, without spaces. Verify it with gpg --fingerprint. No key is downloaded.");
        break;
    }
    case AUTH_GPG_SECRET: case SERVER_GPG_SECRET: {
        const Creation *creation = &s->creation[s->field == SERVER_GPG_SECRET];
        v->secret = 1;
        value = *creation->secret ? "(set; hidden)" : "(empty)";
        (void)snprintf(v->detail, sizeof v->detail, "Password to encrypt. Input is hidden; no plaintext password file is created.");
        break;
    }
    case AUTH_GPG_CONFIRM: case SERVER_GPG_CONFIRM:
        v->secret = 1; value = "(hidden)";
        (void)snprintf(v->detail, sizeof v->detail, "Type the same password again to confirm it.");
        break;
    case CA_FILE: value = c->ca_file; (void)snprintf(v->detail, sizeof v->detail, "Optional CA file. Empty uses the system trust store; '-' clears. Server identity is always verified."); break;
    case HISTORY: (void)snprintf(v->current, sizeof v->current, "%d", c->history_lines); (void)snprintf(v->detail, sizeof v->detail, "50 to 5000 retained lines. Native output also has hard record and byte bounds."); return;
    case SEND_MODE: value = c->send_review ? "review" : "direct"; (void)snprintf(v->detail, sizeof v->detail, "direct = one Enter to send; review = review the log and confirm with another Enter. Applies to all servers."); break;
    case COMPOSER_MODE: value = c->composer_fullscreen ? "fullscreen" : "line"; (void)snprintf(v->detail, sizeof v->detail, "line = one input row below the live tail; fullscreen = a multi-line editor with a smaller live-tail pane. Applies to vipirc -i and onekeyirc across all servers."); break;
    case OUTPUT_FORMAT: value = c->output_full ? "full" : "compact"; (void)snprintf(v->detail, sizeof v->detail, "compact = time, sender, text; full = also date, server, channel. Applies to all servers."); break;
    case QUEUE_NOTICES: value = c->queue_notices ? "yes" : "no"; (void)snprintf(v->detail, sizeof v->detail, "Show routine message-queue and pacing notifications. Errors and multi-message warnings remain visible. Applies to all servers."); break;
    case PROGRAM_MESSAGES: value = c->program_messages ? "yes" : "no"; (void)snprintf(v->detail, sizeof v->detail, "Show low-priority local feedback such as draft cancellation, input-cleared, and preserved-draft notices. Errors and confirmations stay visible. Applies to all servers."); break;
    case PERSISTENT_LOGGING: value = c->persistent_logging ? "yes" : "no"; (void)snprintf(v->detail, sizeof v->detail, "Append chat to a private log in each saved configuration directory. Each log stops at 16 MiB; archive it and toggle off/on to resume. Off keeps history memory-only. Applies to all servers."); break;
    case SINGLE_SESSION: value = c->single_session ? "yes" : "no"; (void)snprintf(v->detail, sizeof v->detail, "When yes, only one background job may exist in this client namespace, making bare --resume unambiguous. Multiple-server connections inside that session are unaffected."); break;
    case COMMAND_SUGGESTIONS:
        value = commands_suggestion_mode_name(c->command_suggestions);
        (void)snprintf(v->detail, sizeof v->detail,
            "common = useful commands first; history = recent accepted commands; frequent = your most-used commands; alphabetical = names in order; contextual = commands for the screen that opened command entry. Down selects a suggestion; Enter inserts it. Applies to all servers.");
        break;
    case DEFAULT_INTERFACE:
        value = c->default_interface == CONFIG_INTERFACE_IDLE ? "idle" : "ncurses";
        (void)snprintf(v->detail, sizeof v->detail,
            "Next fresh launch: ncurses is vipirc's full interface; idle is minimalist. Explicit -i or -N overrides the saved choice for that launch. onekeyirc always uses idle. The current interface stays open.");
        break;
    case WATCH_WORD: case IGNORE_MASK: {
        int watch = s->field == WATCH_WORD;
        size_t count = watch ? c->word_count : c->ignore_count;
        value = s->item < count ? (watch ? c->words[s->item] : c->ignores[s->item]) : "";
        (void)snprintf(v->question, sizeof v->question, "%s %zu%s", irc_tr(watch ? "Mention text" : "Ignored sender"),
                       s->item + 1u, s->item < count ? "" : " (optional new entry)");
        (void)snprintf(v->detail, sizeof v->detail, "%s A blank new entry ends the list; '-' removes the shown entry.",
                       watch ? "Match this text as a mention." : "A nickname or nick!user@host mask.");
        break;
    }
    case FINISH: {
        const ClientSlot *slot = &s->client->slots[s->index];
        Config review = s->work;
        if (slot->used && slot->token == s->token && !slot->load_failed) {
            if (slot->app) app_config_effective(slot->app, &review);
            else review = slot->seed;
            merge(s, &review);
        }
        c = &review;
        v->final = 1;
        (void)snprintf(v->current, sizeof v->current, "%d", s->action);
        if (s->application) {
            (void)snprintf(v->detail, sizeof v->detail, "1 Save | 2 Apply without saving");
            (void)snprintf(v->summary, sizeof v->summary,
                "History lines: %d\nSend mode: %s\nComposer mode: %s\nOutput format: %s\nQueue notices: %s\nProgram messages: %s\nPersistent logging: %s\nSingle-session mode: %s\nCommand suggestions: %s\nDefault interface: %s\nConfiguration: %.1100s\n",
                c->history_lines, c->send_review ? "review" : "direct", c->composer_fullscreen ? "fullscreen" : "line", c->output_full ? "full" : "compact",
                c->queue_notices ? "yes" : "no", c->program_messages ? "yes" : "no",
                c->persistent_logging ? "yes" : "no", c->single_session ? "yes" : "no",
                commands_suggestion_mode_label(c->command_suggestions),
                c->minimal ? "idle (fixed in onekeyirc)" :
                c->default_interface == CONFIG_INTERFACE_IDLE ? "idle (next fresh launch)" : "ncurses (next fresh launch)",
                s->client->primary_path);
            config_wipe(&review, sizeof review);
            return;
        }
        (void)snprintf(v->detail, sizeof v->detail, "1 Save and connect | 2 Save only | 3 Connect once | 4 Apply without saving");
        (void)snprintf(v->summary, sizeof v->summary,
            "Server: %.255s:%d (%s)\nNickname: %.63s\n%sConfiguration: %.1100s\n%s%s",
            c->server, c->port, c->tls ? "verified TLS" : "unencrypted", c->nick,
            s->quick ? "" : "Connection changes apply on the next connection.\n",
            slot->config_path[0] ? slot->config_path : "new saved profile",
            (s->cleared & 1) ? "Previous server's startup/authentication settings were cleared in this draft.\n" :
            (s->cleared & (2 | 4)) ? "Previous authentication settings and startup commands in this draft were cleared; supply the current account's password.\n" : "",
            slot->app && (slot->app->net || slot->app->sim) ? "This server is already active; choose Save only or Apply without saving.\n" : "");
        size_t used = strlen(v->summary);
        if (c->bouncer_type != CONFIG_BOUNCER_NONE) {
            char identity[192];
            int ready = config_bouncer_identity(c, identity, sizeof identity) == 1;
            int n = snprintf(v->summary + used, sizeof v->summary - used,
                "Bouncer: %s\nLogin identity: %s\nChannels: restored by the bouncer\nAutomatic away: %s\n",
                bouncer_name(c->bouncer_type), ready ? identity : "(incomplete)",
                c->bouncer_away ? "client rules enabled" : "managed by bouncer");
            if (n > 0 && (size_t)n < sizeof v->summary - used) used += (size_t)n;
            config_wipe(identity, sizeof identity);
        }
        for (size_t i = 0; i < 2 && used < sizeof v->summary; ++i) if (s->creation[i].create || s->creation[i].published) {
            const char *path = i ? c->server_password_gpg_file : c->password_gpg_file;
            int n = snprintf(v->summary + used, sizeof v->summary - used,
                             "%s encrypted file (%s):\n%s\n%s%s%s", i ? "Server" : "Authentication",
                             s->creation[i].published ? "created" : "not created; created at Finish",
                             path, *s->creation[i].fingerprint ? "Local key: " : "",
                             s->creation[i].fingerprint, *s->creation[i].fingerprint ? "\n" : "");
            if (n < 0 || (size_t)n >= sizeof v->summary - used) break;
            used += (size_t)n;
        }
        config_wipe(&review, sizeof review);
        return;
    }
    }
    (void)snprintf(v->current, sizeof v->current, "%s", *value ? value : "(empty)");
}

static int numeric(const char *text, unsigned low, unsigned high, int *out)
{
    if (!*text) return 0;
    unsigned value = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9' || value > (high - (unsigned)(*p - '0')) / 10u) return 0;
        value = value * 10u + (unsigned)(*p - '0');
        if (value > high) return 0;
    }
    if (value < low) return 0;
    *out = (int)value; return 1;
}

static int boolean(const char *text, int *out)
{
    if (!strcasecmp(text, "yes") || !strcasecmp(text, "y") || !strcasecmp(text, "on") ||
        !strcasecmp(text, "true") || !strcmp(text, "1")) { *out = 1; return 1; }
    if (!strcasecmp(text, "no") || !strcasecmp(text, "n") || !strcasecmp(text, "off") ||
        !strcasecmp(text, "false") || !strcmp(text, "0")) { *out = 0; return 1; }
    return 0;
}

static int text_set(char *out, size_t cap, const char *answer, int optional, char *error, size_t error_cap)
{
    if (!*answer) return 1;
    if (optional && !strcmp(answer, "-")) answer = "";
    size_t n = strlen(answer);
    if (n >= cap) return fail(error, error_cap, "This answer allows at most %zu UTF-8 bytes; shorten it and try again.", cap - 1u);
    config_wipe(out, cap); memcpy(out, answer, n + 1u);
    return 1;
}

/* Authentication is filled across several questions. Validate all complete
 * fields now, and validate the complete dependency set at Finish. */
static int validate_partial(const Config *cfg, char *error, size_t cap)
{
    Config check = *cfg;
    if (check.bouncer_type != CONFIG_BOUNCER_NONE) {
        char identity[192];
        if (!check.bouncer_user[0]) strcpy(check.bouncer_user, "account");
        if (!check.bouncer_network[0]) strcpy(check.bouncer_network, "network");
        int valid = config_bouncer_identity(&check, identity, sizeof identity) == 1;
        config_wipe(identity, sizeof identity);
        if (!valid) {
            config_wipe(&check, sizeof check);
            return fail(error, cap, "Bouncer fields allow at most 63 ASCII letters, digits, '.', '_' or '-'; enter each component separately.");
        }
        check.bouncer_type = CONFIG_BOUNCER_NONE;
    }
    (void)snprintf(check.auth, sizeof check.auth, "none");
    check.tls = 1;
    int result = config_validate(&check, error, cap) > 0;
    config_wipe(&check, sizeof check);
    return result;
}

/* Validate into a temporary Config before accepting an answer. Navigation
 * can revisit questions, so clearing an identity also retires related jobs. */
/* Only the displayed question is editable; a
 * validated answer advances the draft, not the live profile. Secret
 * confirmations require explicit re-entry rather than empty acceptance. */
int idle_setup_answer(IdleSetup *s, const char *answer, char *error, size_t cap)
{
    if (error && cap) error[0] = 0;
    if (s && s->pending) return fail(error, cap, "GPG encryption is still running; wait or go back to cancel it.");
    if (s) s->confirmed = 0;
    if (!s || !answer || strnlen(answer, CONFIG_PATH_MAX + 1u) > CONFIG_PATH_MAX || !irc_utf8_valid(answer))
        return fail(error, cap, "Invalid or oversized answer; existing settings are unchanged.");
    for (const unsigned char *p = (const unsigned char *)answer; *p; ++p)
        if (*p < 32 || *p == 127) return fail(error, cap, "Enter one answer without control characters or line breaks.");
    if (s->field == AUTH_GPG_MODE || s->field == SERVER_GPG_MODE) {
        size_t which = s->field == SERVER_GPG_MODE ? 1u : 0u;
        Creation *creation = &s->creation[which];
        int create = creation->create;
        if (*answer) {
            if (!strcasecmp(answer, "create") || !strcasecmp(answer, "new") || !strcmp(answer, "1")) create = 1;
            else if (!strcasecmp(answer, "existing") || !strcasecmp(answer, "file") || !strcmp(answer, "2")) create = 0;
            else return fail(error, cap, "Choose create for a new encrypted file or existing for a file you already have.");
        }
        if (create != creation->create) {
            reset_creation(creation); creation->create = create;
            if (create) offer_credential_path(s, which);
        } else if (create && !(which ? s->work.server_password_gpg_file[0] : s->work.password_gpg_file[0]))
            offer_credential_path(s, which);
        advance(s); return 1;
    }
    if (s->field == AUTH_GPG_KEY || s->field == SERVER_GPG_KEY) {
        Creation *creation = &s->creation[s->field == SERVER_GPG_KEY];
        const char *fingerprint = *answer ? answer : creation->fingerprint;
        if (!credential_fingerprint_valid(fingerprint))
            return fail(error, cap, "Use the full local GPG key fingerprint: 40 or 64 hexadecimal digits without spaces.");
        if (strcmp(fingerprint, creation->fingerprint)) {
            credential_create_cancel(creation->job); creation->job = NULL; creation->ready = 0;
            creation->published = 0;
            memcpy(creation->fingerprint, fingerprint, strlen(fingerprint) + 1u);
        }
        advance(s); return 1;
    }
    if (s->field == AUTH_GPG_SECRET || s->field == SERVER_GPG_SECRET) {
        Creation *creation = &s->creation[s->field == SERVER_GPG_SECRET];
        const char *secret = *answer ? answer : creation->secret;
        size_t length = strlen(secret);
        if (!length || length > CONFIG_SECRET_MAX)
            return fail(error, cap, "Enter a password of 1 to 1024 UTF-8 bytes.");
        if (strcmp(secret, creation->secret)) {
            credential_create_cancel(creation->job); creation->job = NULL; creation->ready = 0;
            creation->published = 0;
            config_wipe(creation->secret, sizeof creation->secret);
            memcpy(creation->secret, secret, length + 1u);
        }
        creation->confirmed = 0;
        advance(s); return 1;
    }
    if (s->field == AUTH_GPG_CONFIRM || s->field == SERVER_GPG_CONFIRM) {
        Creation *creation = &s->creation[s->field == SERVER_GPG_CONFIRM];
        if (!*answer || strcmp(answer, creation->secret))
            return fail(error, cap, "The passwords do not match; type the same password again or go back to change it.");
        creation->confirmed = 1;
        advance(s); return 1;
    }
    if (s->field == PROFILE) {
        int panel = s->application ? 0 : (int)s->index + 1;
        if (*answer && !numeric(answer, 0, CLIENT_MAX_SERVERS, &panel))
            return fail(error, cap, "Choose 0 for application preferences or a displayed server panel number.");
        if (!(panel ? select_profile(s, (size_t)(panel - 1), error, cap) :
                      select_application(s, error, cap))) return 0;
        advance(s); return 1;
    }
    if (s->field == FINISH) {
        int action = s->action;
        if (*answer && !numeric(answer, 1, s->application ? 2u : 4u, &action))
            return fail(error, cap, "%s", s->application ?
                        "Choose 1 (save) or 2 (apply without saving)." :
                        "Choose 1 (save and connect), 2 (save only), 3 (connect once), or 4 (apply only).");
        if (!s->application && (!s->work.server[0] || !s->work.nick[0]))
            return fail(error, cap, "Server and nickname are required; Escape returns to the questions.");
        if (config_validate(&s->work, error, cap) < 0) return 0;
        for (size_t i = 0; i < 2; ++i) if (s->creation[i].create &&
            (!s->creation[i].confirmed || !*s->creation[i].secret ||
             !credential_fingerprint_valid(s->creation[i].fingerprint)))
            return fail(error, cap, "Complete and confirm each GPG password before Finish.");
        if (s->creation[0].create && s->creation[1].create &&
            !strcmp(s->work.password_gpg_file, s->work.server_password_gpg_file))
            return fail(error, cap, "Choose different new files for the authentication and server passwords.");
        s->action = action; s->confirmed = 1; return 2;
    }
    Config next = s->work;
    int source = s->source, server_source = s->server_source, cleared = s->cleared, okay = 1;
    int repeat = 0, reset_credentials = 0;
#define TEXT(member, optional) text_set(next.member, sizeof next.member, answer, optional, error, cap)
    switch (s->field) {
    case SERVER:
        okay = TEXT(server, 0);
        if (okay && !next.server[0]) okay = fail(error, cap, "Enter an IRC server hostname or IP address.");
        if (okay && s->work.server[0] && strcasecmp(next.server, s->work.server)) {
            clear_connection_credentials(&next); source = server_source = SOURCE_NONE;
            reset_credentials = 1;
            config_wipe(next.channels, sizeof next.channels); next.autoconnect = 0;
            config_wipe(next.ca_file, sizeof next.ca_file); cleared |= 1;
        }
        break;
    case TLS:
        if (*answer && !boolean(answer, &next.tls)) okay = fail(error, cap, "Enter yes for verified TLS or no for unencrypted IRC.");
        if (okay && next.bouncer_type != CONFIG_BOUNCER_NONE && !next.tls)
            okay = fail(error, cap, "Bouncer connections require verified TLS. Change Connection type to direct before disabling TLS.");
        if (okay && next.tls != s->work.tls) {
            if (next.port == 6697 || next.port == 6667) next.port = next.tls ? 6697 : 6667;
            if (!next.tls) { clear_connection_credentials(&next); source = server_source = SOURCE_NONE; cleared |= 2; reset_credentials = 1; }
        }
        break;
    case PORT:
        if (*answer && !numeric(answer, 1, 65535, &next.port)) okay = fail(error, cap, "Port must be a number from 1 to 65535.");
        break;
    case NICK:
        okay = TEXT(nick, 0);
        if (okay && !next.nick[0]) okay = fail(error, cap, "A nickname is required.");
        break;
    case BOUNCER: {
        int type = next.bouncer_type;
        if (*answer) {
            if (!strcasecmp(answer, "direct") || !strcasecmp(answer, "none")) type = CONFIG_BOUNCER_NONE;
            else if (!strcasecmp(answer, "znc")) type = CONFIG_BOUNCER_ZNC;
            else if (!strcasecmp(answer, "soju")) type = CONFIG_BOUNCER_SOJU;
            else okay = fail(error, cap, "Choose direct, znc, or soju.");
        }
        if (okay && type != next.bouncer_type) {
            next.bouncer_type = type; next.bouncer_away = 0;
            config_wipe(next.bouncer_user, sizeof next.bouncer_user);
            config_wipe(next.bouncer_network, sizeof next.bouncer_network);
            config_wipe(next.bouncer_client, sizeof next.bouncer_client);
            if (type != CONFIG_BOUNCER_NONE) next.tls = 1;
            clear_connection_credentials(&next); source = server_source = SOURCE_NONE;
            cleared |= 4; reset_credentials = 1;
        }
        break;
    }
    case BOUNCER_USER: okay = TEXT(bouncer_user, 0);
        if (okay && !next.bouncer_user[0]) okay = fail(error, cap, "Enter the bouncer account name.");
        break;
    case BOUNCER_NETWORK: okay = TEXT(bouncer_network, 0);
        if (okay && !next.bouncer_network[0]) okay = fail(error, cap, "Enter the network name configured on your bouncer.");
        break;
    case BOUNCER_CLIENT: okay = TEXT(bouncer_client, 1); break;
    case BOUNCER_AWAY:
        if (*answer && !boolean(answer, &next.bouncer_away)) okay = fail(error, cap, "Enter yes or no for this profile's automatic-away rules.");
        break;
    case USERNAME:
        okay = TEXT(username, 0);
        if (okay && !next.username[0]) okay = fail(error, cap, "An IRC username is required.");
        break;
    case REALNAME: okay = TEXT(realname, 1); break;
    case AUTOCONNECT:
        if (*answer && !boolean(answer, &next.autoconnect)) okay = fail(error, cap, "Enter yes or no for automatic startup connection.");
        break;
    case CHANNELS: okay = TEXT(channels, 1); break;
    case AUTH:
        if (*answer) {
            if (!strcasecmp(answer, "none")) { clear_source(&next, 0); source = SOURCE_NONE; next.auth_user[0] = 0; strcpy(next.auth, "none"); }
            else if (!strcasecmp(answer, "sasl")) strcpy(next.auth, "sasl");
            else okay = fail(error, cap, "Enter none or sasl.");
        }
        break;
    case AUTH_USER:
        okay = TEXT(auth_user, 0);
        if (okay && !next.auth_user[0]) okay = fail(error, cap, "Enter the account name used for SASL authentication.");
        break;
    case AUTH_SOURCE: case SERVER_SOURCE: {
        int server = s->field == SERVER_SOURCE, choice = server ? server_source : source;
        if (*answer) {
            for (choice = SOURCE_NONE; choice <= SOURCE_PLAIN; ++choice) if (!strcasecmp(answer, source_name(choice))) break;
            if (choice > SOURCE_PLAIN) okay = fail(error, cap, "Choose none, file, gpg, or plain.");
        }
        if (okay && choice == SOURCE_NONE && (!server || next.bouncer_type == CONFIG_BOUNCER_ZNC))
            okay = fail(error, cap, "%s", next.bouncer_type != CONFIG_BOUNCER_NONE ?
                "Choose file, gpg, or plain for the bouncer password." :
                "SASL needs file, gpg, or plain. Escape returns to authentication choices.");
        if (okay) {
            if (choice != (server ? server_source : source)) clear_source(&next, server);
            if (server) server_source = choice; else source = choice;
        }
        break;
    }
    case AUTH_VALUE: case SERVER_VALUE: {
        int server = s->field == SERVER_VALUE, choice = server ? server_source : source;
        okay = credential_path_answer(s, &next, server, choice, answer, error, cap);
        break;
    }
    case CA_FILE: okay = TEXT(ca_file, 1); break;
    case HISTORY:
        if (*answer && !numeric(answer, 50, 5000, &next.history_lines)) okay = fail(error, cap, "History lines must be a number from 50 to 5000.");
        break;
    case SEND_MODE:
        if (*answer) {
            if (!strcasecmp(answer, "direct")) next.send_review = 0;
            else if (!strcasecmp(answer, "review")) next.send_review = 1;
            else okay = fail(error, cap, "Enter direct (one Enter) or review (two Enters).");
        }
        break;
    case COMPOSER_MODE:
        if (*answer) {
            if (!strcasecmp(answer, "line")) next.composer_fullscreen = 0;
            else if (!strcasecmp(answer, "fullscreen")) next.composer_fullscreen = 1;
            else okay = fail(error, cap, "Enter line or fullscreen.");
        }
        break;
    case OUTPUT_FORMAT:
        if (*answer) {
            if (!strcasecmp(answer, "compact")) next.output_full = 0;
            else if (!strcasecmp(answer, "full")) next.output_full = 1;
            else okay = fail(error, cap, "Enter compact or full.");
        }
        break;
    case QUEUE_NOTICES:
        if (*answer && !boolean(answer, &next.queue_notices)) okay = fail(error, cap, "Enter yes or no for routine queue notifications.");
        break;
    case PROGRAM_MESSAGES:
        if (*answer && !boolean(answer, &next.program_messages)) okay = fail(error, cap, "Enter yes or no for program messages.");
        break;
    case PERSISTENT_LOGGING:
        if (*answer && !boolean(answer, &next.persistent_logging)) okay = fail(error, cap, "Enter yes or no for persistent logging.");
        break;
    case SINGLE_SESSION:
        if (*answer && !boolean(answer, &next.single_session)) okay = fail(error, cap, "Enter yes or no for single-session mode.");
        break;
    case COMMAND_SUGGESTIONS:
        if (*answer) {
            int mode;
            for (mode = CONFIG_SUGGEST_ALPHABETICAL; mode <= CONFIG_SUGGEST_CONTEXTUAL; ++mode)
                if (!strcasecmp(answer, commands_suggestion_mode_name(mode))) break;
            if (mode > CONFIG_SUGGEST_CONTEXTUAL)
                okay = fail(error, cap, "Choose common, history, frequent, alphabetical, or contextual.");
            else next.command_suggestions = mode;
        }
        break;
    case DEFAULT_INTERFACE:
        if (*answer) {
            if (!strcasecmp(answer, "idle")) next.default_interface = CONFIG_INTERFACE_IDLE;
            else if (!strcasecmp(answer, "ncurses")) next.default_interface = CONFIG_INTERFACE_NCURSES;
            else okay = fail(error, cap, "Enter idle or ncurses for the next launch interface.");
        }
        break;
    case WATCH_WORD: case IGNORE_MASK: {
        int watch = s->field == WATCH_WORD;
        size_t *count = watch ? &next.word_count : &next.ignore_count;
        char (*entries)[CONFIG_WORD_SIZE] = watch ? next.words : next.ignores;
        size_t maximum = watch ? CONFIG_WORD_MAX : CONFIG_IGNORE_MAX;
        if (s->item >= maximum) break;
        if (!*answer && s->item == *count) break;
        if (!strcmp(answer, "-")) {
            if (s->item < *count) {
                if (s->item + 1u < *count)
                    memmove(entries[s->item], entries[s->item + 1u], (*count - s->item - 1u) * sizeof entries[0]);
                --*count; config_wipe(entries[*count], sizeof entries[0]); repeat = 2;
            }
        } else {
            okay = text_set(entries[s->item], sizeof entries[0], answer, 0, error, cap);
            if (okay) {
                if (s->item == *count) ++*count;
                repeat = s->item + 1u < maximum;
            }
        }
        break;
    }
    default: okay = fail(error, cap, "Invalid configuration question; cancel and start again."); break;
    }
#undef TEXT
    if (okay && next.bouncer_type != CONFIG_BOUNCER_NONE &&
        ((s->field == BOUNCER_USER && strcmp(next.bouncer_user, s->work.bouncer_user)) ||
         (s->field == BOUNCER_NETWORK && strcmp(next.bouncer_network, s->work.bouncer_network)) ||
         (s->field == BOUNCER_CLIENT && strcmp(next.bouncer_client, s->work.bouncer_client)) ||
         (s->field == PORT && next.port != s->work.port) ||
         (s->field == CA_FILE && strcmp(next.ca_file, s->work.ca_file)))) {
        clear_connection_credentials(&next); source = server_source = SOURCE_NONE;
        cleared |= 4; reset_credentials = 1;
    }
    if (okay) okay = validate_partial(&next, error, cap);
    if (okay) {
        if (reset_credentials || (s->field == AUTH && !strcmp(next.auth, "none"))) {
            reset_creation(&s->creation[0]);
            if (reset_credentials) reset_creation(&s->creation[1]);
        }
        if (source != s->source) {
            reset_creation(&s->creation[0]);
            s->creation[0].create = source == SOURCE_GPG && !*next.password_gpg_file;
        }
        if (server_source != s->server_source) {
            reset_creation(&s->creation[1]);
            s->creation[1].create = server_source == SOURCE_GPG && !*next.server_password_gpg_file;
        }
        for (size_t i = 0; i < 2; ++i) {
            const char *path = i ? next.server_password_gpg_file : next.password_gpg_file;
            const char *old = i ? s->work.server_password_gpg_file : s->work.password_gpg_file;
            if (strcmp(path, old)) {
                credential_create_cancel(s->creation[i].job);
                s->creation[i].job = NULL;
                s->creation[i].ready = 0;
                s->creation[i].managed_default = 0;
                s->creation[i].published = 0;
            }
        }
        config_wipe(&s->work, sizeof s->work);
        s->work = next;
        s->source = source;
        s->server_source = server_source;
        s->cleared = cleared;
        if (reset_credentials && s->field == CA_FILE && next.bouncer_type != CONFIG_BOUNCER_NONE) {
            /* CA selection follows password questions. Ask for the newly
             * required source immediately instead of stranding Finish. */
            s->field = next.bouncer_type == CONFIG_BOUNCER_SOJU ? AUTH_SOURCE : SERVER_SOURCE;
            s->item = 0;
        } else if (repeat == 1) ++s->item;
        else if (repeat != 2) advance(s);
    }
    config_wipe(&next, sizeof next);
    return okay;
}

/* Merge only edits made by this draft, preserving unedited live nick changes
 * and previously staged values that arrived while the questions were open. */
static void merge(const IdleSetup *s, Config *next)
{
#define TEXT(member) do { if (strcmp(s->work.member, s->original.member)) memcpy(next->member, s->work.member, sizeof next->member); } while (0)
#define VALUE(member) do { if (s->work.member != s->original.member) next->member = s->work.member; } while (0)
    if (s->application) {
        VALUE(history_lines); VALUE(send_review); VALUE(composer_fullscreen); VALUE(output_full); VALUE(queue_notices); VALUE(program_messages); VALUE(persistent_logging); VALUE(single_session); VALUE(command_suggestions); VALUE(default_interface);
        return;
    }
    int current_identity_changed = connection_identity_changed(&s->original, next);
    TEXT(server); VALUE(tls); VALUE(port); TEXT(nick); TEXT(username); TEXT(realname);
    VALUE(bouncer_type); VALUE(bouncer_away); TEXT(bouncer_user); TEXT(bouncer_network); TEXT(bouncer_client);
    VALUE(autoconnect); TEXT(channels); TEXT(auth); TEXT(auth_user);
    TEXT(password); TEXT(password_file); TEXT(password_gpg_file);
    TEXT(server_password); TEXT(server_password_file); TEXT(server_password_gpg_file);
    TEXT(ca_file);
#undef TEXT
#undef VALUE
    if (s->work.word_count != s->original.word_count || memcmp(s->work.words, s->original.words, sizeof s->work.words)) {
        next->word_count = s->work.word_count; memcpy(next->words, s->work.words, sizeof next->words);
    }
    if (s->work.ignore_count != s->original.ignore_count || memcmp(s->work.ignores, s->original.ignores, sizeof s->work.ignores)) {
        next->ignore_count = s->work.ignore_count; memcpy(next->ignores, s->work.ignores, sizeof next->ignores);
    }
    /* A changed endpoint must never inherit credentials or joins added to
     * its old profile after this draft began, even if those fields originally
     * were empty. Subsequent answers intentionally configure the new group. */
    int connection_edited = connection_credentials_edited(&s->original, &s->work);
    if (s->cleared || connection_edited) {
        memcpy(next->auth, s->work.auth, sizeof next->auth);
        memcpy(next->auth_user, s->work.auth_user, sizeof next->auth_user);
        memcpy(next->password, s->work.password, sizeof next->password);
        memcpy(next->password_file, s->work.password_file, sizeof next->password_file);
        memcpy(next->password_gpg_file, s->work.password_gpg_file, sizeof next->password_gpg_file);
        memcpy(next->server_password, s->work.server_password, sizeof next->server_password);
        memcpy(next->server_password_file, s->work.server_password_file, sizeof next->server_password_file);
        memcpy(next->server_password_gpg_file, s->work.server_password_gpg_file, sizeof next->server_password_gpg_file);
    }
    if ((s->cleared & 4) || connection_edited) {
        next->bouncer_type = s->work.bouncer_type;
        memcpy(next->bouncer_user, s->work.bouncer_user, sizeof next->bouncer_user);
        memcpy(next->bouncer_network, s->work.bouncer_network, sizeof next->bouncer_network);
        memcpy(next->bouncer_client, s->work.bouncer_client, sizeof next->bouncer_client);
    }
    if (s->work.bouncer_type != s->original.bouncer_type) next->bouncer_away = s->work.bouncer_away;
    if (connection_edited) {
        /* Credentials and the endpoint/identity they were entered for are
         * one transaction. Unrelated live nick/UI changes still merge. */
        memcpy(next->server, s->work.server, sizeof next->server);
        memcpy(next->ca_file, s->work.ca_file, sizeof next->ca_file);
        next->port = s->work.port; next->tls = s->work.tls;
    }
    if (s->cleared || (connection_edited && current_identity_changed)) {
        next->autocommands.count = s->work.autocommands.count;
        memcpy(next->autocommands.lines, s->work.autocommands.lines, sizeof next->autocommands.lines);
    }
    if (s->cleared & 1) {
        memcpy(next->channels, s->work.channels, sizeof next->channels);
        memcpy(next->ca_file, s->work.ca_file, sizeof next->ca_file);
        next->autoconnect = s->work.autoconnect;
        next->autocommands = s->work.autocommands;
    }
}

/* A save may replace its file successfully and then report a directory-fsync
 * error. Do not roll a referenced credential back out from under that saved
 * file. If an existing regular config cannot be inspected, retaining harmless
 * completed ciphertext is safer than risking a dangling saved credential. */
static int retain_saved_credentials(IdleSetup *s, const ClientSlot *slot)
{
    const char *path = s->index ? slot->config_path : s->client->primary_path;
    if (!*path) return 0;
    Config saved;
    if (s->work.minimal) config_minimal_defaults(&saved);
    else config_defaults(&saved);
    char error[512];
    int loaded = config_load(&saved, path, error, sizeof error);
    struct stat st;
    int uncertain = loaded < 0 && !lstat(path, &st) && S_ISREG(st.st_mode);
    int kept = 0;
    for (size_t i = 0; i < 2; ++i) if (s->creation[i].job) {
        const char *created = i ? s->work.server_password_gpg_file : s->work.password_gpg_file;
        int referenced = loaded > 0 && (!strcmp(created, saved.password_gpg_file) ||
                                        !strcmp(created, saved.server_password_gpg_file));
        if (uncertain || referenced) {
            int published = credential_create_published(s->creation[i].job);
            credential_create_finish(s->creation[i].job);
            config_wipe(&s->creation[i], sizeof s->creation[i]);
            s->creation[i].published = published;
            ++kept;
        }
    }
    config_wipe(&saved, sizeof saved);
    config_wipe(error, sizeof error);
    return kept;
}

/* Short notices retain the action/error and point to a complete local view.
 * Full paths are never silently shortened: add them here only when they fit. */
static void note_created_credentials(const IdleSetup *s, char *message, size_t cap, int applied)
{
    unsigned count = 0;
    for (size_t i = 0; i < 2; ++i) if (s->creation[i].published) ++count;
    if (!count || !message || !cap) return;
    char detail[768];
    (void)snprintf(detail, sizeof detail, "%s", message);
    (void)snprintf(message, cap, "Encrypted credential%s created. %s %s",
        count == 1 ? "" : "s",
        applied ? "Use /autoauth status on this server for full file paths." :
                  "Full file paths are in the Finish review; settings may not be applied.", detail);
    size_t used = strlen(message);
    for (size_t i = 0; i < 2; ++i) if (s->creation[i].published) {
        const char *path = i ? s->work.server_password_gpg_file : s->work.password_gpg_file;
        const char *label = i ? " Server file: " : " Authentication file: ";
        if (strlen(label) + strlen(path) < cap - used) {
            int n = snprintf(message + used, cap - used, "%s%s", label, path);
            if (n > 0) used += (size_t)n;
        }
    }
    config_wipe(detail, sizeof detail);
}

/* Finish is staged: validate/merge, finish requested encryption, publish
 * credential files, apply/save settings, then optionally connect. Partial
 * persistence is reported; cleanup must retain credentials a saved file may
 * already reference. Pending helper work resumes through idle_setup_poll(). */
int idle_setup_commit(IdleSetup *s, char *message, size_t cap)
{
    if (s && s->pending) return 2;
    if (!s || s->field != FINISH || !s->confirmed)
        return fail(message, cap, "Choose a final action before applying settings.");
    s->confirmed = 0;
    ClientSlot *slot = &s->client->slots[s->index];
    if (!slot->used || slot->token != s->token || slot->load_failed)
        return fail(message, cap, "%s", s->application ?
                    "The application configuration changed; cancel and reopen the panel." :
                    "The selected server profile changed; cancel and reopen the panel.");
    if (s->application) {
        Config next;
        if (slot->app) app_config_effective(slot->app, &next);
        else next = slot->seed;
        merge(s, &next);
        int save = s->action == 1, okay;
        if (slot->app) okay = app_config_apply(slot->app, &next, save, message, cap);
        else if (config_validate(&next, message, cap) < 0) okay = 0;
        else if (save) okay = client_save(s->client, 0, &next, message, cap) > 0;
        else {
            slot->seed = next;
            okay = 1;
        }
        /* History allocation/save has succeeded. Propagate already validated
         * global scalar preferences without another write or history resize. */
        if (okay) okay = client_interface_apply(s->client, &next, 0, message, cap) > 0;
        config_wipe(&next, sizeof next);
        if (!okay) return 0;
        if (message && cap) (void)snprintf(message, cap, "Application preferences %s.",
                                         save ? "saved" : "applied without saving");
        return 1;
    }
    int save = s->action == 1 || s->action == 2, connect = s->action == 1 || s->action == 3;
    if (connect && slot->app && (slot->app->net || slot->app->sim))
        return fail(message, cap, "Already connecting or connected. Choose 2 or 4 to stage settings; /disconnect before a new /connect.");
    Config next;
    if (slot->app) app_config_effective(slot->app, &next);
    else next = slot->seed;
    merge(s, &next);
    if (!next.server[0] || !next.nick[0] || !next.username[0]) {
        config_wipe(&next, sizeof next);
        return fail(message, cap, "Server, nickname, and IRC username are required; Escape returns to the questions.");
    }
    if (config_validate(&next, message, cap) < 0) {
        config_wipe(&next, sizeof next);
        return 0;
    }
    int create = s->creation[0].create || s->creation[1].create;
    if (create && s->client->test_mode) {
        config_wipe(&next, sizeof next);
        return fail(message, cap, "GPG file creation is disabled in --test-mode; no helper, keyring, or file was accessed.");
    }
    for (size_t i = 0; i < 2; ++i) if (s->creation[i].create && !s->creation[i].ready) {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
            config_wipe(&next, sizeof next);
            return fail(message, cap, "Cannot time the GPG operation; nothing changed.");
        }
        int64_t milliseconds = (int64_t)now.tv_sec * 1000 + (int64_t)now.tv_nsec / 1000000;
        const char *path = i ? s->work.server_password_gpg_file : s->work.password_gpg_file;
        s->creation[i].job = credential_create_start(path, s->creation[i].fingerprint,
                                                      s->creation[i].secret, milliseconds, message, cap);
        config_wipe(&next, sizeof next);
        if (!s->creation[i].job) {
            note_created_credentials(s, message, cap, 0);
            return 0;
        }
        s->pending = 1;
        if (message && cap) (void)snprintf(message, cap, "Creating encrypted credentials; settings have not been applied.");
        return 2;
    }
    for (size_t i = 0; i < 2; ++i) if (s->creation[i].create) {
        int published = credential_create_publish(s->creation[i].job, message, cap);
        s->creation[i].published = credential_create_published(s->creation[i].job);
        if (published < 0) {
            config_wipe(&next, sizeof next);
            note_created_credentials(s, message, cap, 0);
            return 0;
        }
    }
    App *app = client_get_app(s->client, s->index, message, cap);
    if (!app) {
        config_wipe(&next, sizeof next);
        note_created_credentials(s, message, cap, 0);
        return 0;
    }
    int okay = app_config_apply(app, &next, save, message, cap);
    config_wipe(&next, sizeof next);
    if (!okay) {
        int kept = create && save ? retain_saved_credentials(s, slot) : 0;
        if (kept && message && cap) {
            char detail[512];
            (void)snprintf(detail, sizeof detail, "%s", message);
            (void)snprintf(message, cap, "Encrypted files were kept because the saved configuration may reference them. %s", detail);
            config_wipe(detail, sizeof detail);
        } else if (create && message && cap) {
            size_t used = strlen(message);
            if (used < cap) (void)snprintf(message + used, cap - used,
                " Completed encrypted files are retained for this setup; retry Finish or cancel.");
        }
        note_created_credentials(s, message, cap, 0);
        return 0;
    }
    for (size_t i = 0; i < 2; ++i) if (s->creation[i].job) {
        int published = credential_create_published(s->creation[i].job);
        credential_create_finish(s->creation[i].job);
        config_wipe(&s->creation[i], sizeof s->creation[i]);
        s->creation[i].published = published;
    }
    if (connect) {
        char detail[512];
        if (client_connect(s->client, s->index, detail, sizeof detail) < 0) {
            fail(message, cap, "Settings %s, but connection failed: %s", save ? "saved" : "applied", detail);
            note_created_credentials(s, message, cap, 1);
            return 0;
        }
        App *old = s->client->slots[s->client->current].app;
        if (old) app_set_visible(old, 0);
        s->client->current = s->index;
        app_switch(app, 0);
    }
    if (message && cap) (void)snprintf(message, cap, "Server %zu: settings %s%s.", s->index + 1u,
                                     save ? "saved" : "applied without saving", connect ? "; connecting" : "");
    note_created_credentials(s, message, cap, 1);
    return 1;
}

int idle_setup_pending(const IdleSetup *s) { return s && s->pending; }

/* Complete one or both sequential helper jobs, then re-enter the same
 * commit path. No copied confirmation can bypass a changed slot token. */
int idle_setup_poll(IdleSetup *s, int64_t now, char *message, size_t cap)
{
    if (!s || !s->pending) return 0;
    ClientSlot *slot = &s->client->slots[s->index];
    if (!slot->used || slot->token != s->token || slot->load_failed) {
        cancel_creation_jobs(s);
        fail(message, cap, "The selected server profile changed; encryption was cancelled without applying settings.");
        return -1;
    }
    for (size_t i = 0; i < 2; ++i) if (s->creation[i].create && !s->creation[i].ready) {
        Creation *creation = &s->creation[i];
        if (!creation->job) {
            const char *path = i ? s->work.server_password_gpg_file : s->work.password_gpg_file;
            creation->job = credential_create_start(path, creation->fingerprint, creation->secret, now, message, cap);
            if (!creation->job) {
                cancel_creation_jobs(s);
                return -1;
            }
        }
        int result = credential_create_poll(creation->job, now, message, cap);
        if (result < 0) {
            cancel_creation_jobs(s);
            return -1;
        }
        if (!result) return 0;
        creation->ready = 1;
    }
    s->pending = 0;
    s->confirmed = 1;
    return idle_setup_commit(s, message, cap) == 1 ? 1 : -1;
}
