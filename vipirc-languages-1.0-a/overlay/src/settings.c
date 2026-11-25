/* Full-interface configuration forms. Values are data, never commands.
 *
 * Field metadata/accessors precede form lifetime and target selection.
 * commit_field() stages one edit; perform_action() merges Apply/Save into
 * the live effective configuration; key/paste handlers and drawing follow.
 * Guided questions use IdleSetup instead; this module owns the table forms. */
#include "settings.h"
#include "editor_curses.h"
#include "l10n.h"
#include "security_diagnostic_l10n.h"
#include "display_l10n.h"
#include "editor.h"
#include "focus.h"
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef enum FieldKind { FIELD_TEXT, FIELD_SECRET, FIELD_NUMBER, FIELD_BOOL, FIELD_CHOICE } FieldKind;
typedef enum FormAction { FORM_EDIT, FORM_APPLY, FORM_SAVE, FORM_CANCEL, FORM_NEW_CANCEL, FORM_ACTIONS } FormAction;
typedef struct Field {
    const char *label, *hint;
    FieldKind kind;
    size_t offset, capacity;
    int low, high;
    const char *choices[5];
} Field;

static int irc_label_cells(const char *s)
{
    int cells = 0;
    mbstate_t state = {0};
    while (*s && cells < 512) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, s, MB_CUR_MAX, &state);
        if (n == (size_t)-1 || n == (size_t)-2 || n == 0) {
            ++s; ++cells;
            memset(&state, 0, sizeof state);
            continue;
        }
        int w = wcwidth(wc);
        cells += w < 0 ? 1 : w;
        s += n;
    }
    return cells;
}

#define TXT(label, member, hint) \
    {label, hint, FIELD_TEXT, offsetof(Config, member), sizeof(((Config *)0)->member), 0, 0, {NULL, NULL}}
#define SECRET(label, member, hint) \
    {label, hint, FIELD_SECRET, offsetof(Config, member), sizeof(((Config *)0)->member), 0, 0, {NULL, NULL}}
#define NUM(label, member, low, high, hint) \
    {label, hint, FIELD_NUMBER, offsetof(Config, member), 0, low, high, {NULL, NULL}}
#define BOOL(label, member, hint) \
    {label, hint, FIELD_BOOL, offsetof(Config, member), 0, 0, 1, {NULL, NULL}}
#define CHOICE(label, member, one, two, hint) \
    {label, hint, FIELD_CHOICE, offsetof(Config, member), 0, 0, 1, {one, two}}

static const Field fields[] = {
    TXT("Server", server, "Next connection: hostname or numeric IP address."),
    NUM("Port", port, 1, 65535, "Next connection: 1..65535. TLS normally uses 6697; plaintext 6667."),
    BOOL("Transport", tls, "Next connection: TLS verifies and encrypts; Plain IRC is unencrypted."),
    TXT("Nickname", nick, "Next connection. Use /nick to change the current session's nickname."),
    TXT("Username", username, "Next connection: an ASCII IRC username, at most 32 bytes."),
    TXT("Real name", realname, "Next connection: UTF-8 descriptive text, at most 127 bytes."),
    TXT("Auto-join channels", channels, "Next connection: comma-separated channel names; empty means server view."),
    BOOL("Auto-connect", autoconnect, "Connect automatically at future startup; does not connect this session."),
    /* auth is a string rather than an integer; its two choices use the
     * dedicated accessor below. The offset still identifies its field. */
    CHOICE("Auto-authentication", auth, "none", "sasl", "Next connection. SASL PLAIN is sent only through verified TLS."),
    TXT("Authentication user", auth_user, "Next connection: account name for SASL authentication."),
    TXT("Password file", password_file, "Next connection: private file containing the SASL password; contents stay hidden."),
    TXT("GPG password file", password_gpg_file, "Next connection: GPG-encrypted SASL password; unlock its key outside vipirc."),
    SECRET("Plaintext password", password, "Next connection: store the SASL password in this private config file."),
    TXT("Server password file", server_password_file, "Next connection: private server-password file; contents stay hidden."),
    TXT("GPG server password file", server_password_gpg_file, "Next connection: GPG-encrypted server PASS; unlock its key outside vipirc."),
    SECRET("Plaintext server password", server_password, "Next connection: store server PASS in this private config file."),
    TXT("CA file", ca_file, "Next connection: optional certificate-authority PEM path; empty uses system trust."),
    {"Connection type", "Direct IRC, ZNC, or soju. Changing type clears old credentials, startup commands and bouncer identity, requires TLS for bouncers, and keeps the provider's port.",
     FIELD_CHOICE, offsetof(Config, bouncer_type), 0, CONFIG_BOUNCER_NONE, CONFIG_BOUNCER_SOJU,
     {"Direct IRC", "ZNC", "soju"}},
    TXT("Bouncer username", bouncer_user, "Bouncer account only, not NickServ. Up to 63 ASCII letters, digits, '.', '_' or '-'; changing it clears old credentials."),
    TXT("Bouncer network", bouncer_network, "One network name configured on your bouncer. Up to 63 ASCII letters, digits, '.', '_' or '-'; changing it clears old credentials."),
    TXT("Bouncer client name", bouncer_client, "Optional stable device name, such as laptop. Up to 63 ASCII letters, digits, '.', '_' or '-'; changing it clears old credentials."),
    BOOL("Bouncer automatic away", bouncer_away, "Off lets the bouncer manage automatic away. On applies this client's idle/background-away rules to this profile. Manual /away and /back always work."),
    BOOL("User list", user_list, "Apply now: show the current channel's user list when width permits."),
    BOOL("Mouse", mouse, "Apply now: enable supported terminal mouse events."),
    BOOL("Hints", hints, "Apply now: show optional tips; errors and safety warnings stay visible."),
    BOOL("Shortcut labels", shortcut_labels, "Apply now: show ordinary F1..F6 labels in the top bar. User bindings can override those keys in chat and Command."),
    BOOL("Queue notices", queue_notices, "Apply now: show routine queued/paced acknowledgements; errors and confirmation remain visible."),
    BOOL("Program messages", program_messages, "Apply now: show low-priority local feedback such as draft cancellation and input-cleared notices; errors and confirmations remain visible."),
    BOOL("Persistent logging", persistent_logging, "Apply now: private logs stop at 16 MiB each. Archive the log and toggle off/on to resume. Off leaves history memory-only."),
    BOOL("Single-session mode", single_session, "Apply now: allow only one background job for this client namespace so bare --resume is unambiguous."),
    TXT("PART message", part_message, "Apply now: default reason for /part and channel Close. Empty sends no default PART reason."),
    TXT("QUIT message", quit_message, "Apply now: default reason when quitting without an explicit message. Empty sends a bare QUIT."),
    BOOL("Background away", background_away, "Apply now: after /background or managed terminal loss, mark live IRC sessions away without delaying detach."),
    TXT("Background away message", background_away_message, "Apply now: AWAY text used for client-generated background/terminal-loss away status."),
    NUM("Auto-away minutes", autoaway_minutes, 0, 1440, "Apply now: minutes without local input before automatic AWAY. 0 disables idle auto-away."),
    TXT("Auto-away message", autoaway_message, "Apply now: AWAY text used by the local-idle timer. Manual /away is never cleared automatically."),
    {"Command suggestions", "Choose common tasks, recent history, personal usage, alphabetical order, or commands for the invoking screen.",
     FIELD_CHOICE, offsetof(Config, command_suggestions), 0, 0, 4,
     {"Alphabetical", "Commonly used", "Recent history", "Most used with this configuration", "Contextual"}},
    CHOICE("Default interface", default_interface, "ncurses", "idle", "Next launch: ncurses is the full interface; idle is minimalist. Explicit -i or -N overrides this choice for that launch."),
    BOOL("Use signals", use_signals, "Apply now: yes makes terminal Ctrl-C interrupt and Ctrl-Z suspend; no (default) makes those keys cancel an unsent compose. External signals still work."),
    NUM("History lines", history_lines, 50, 5000, "Apply now: 50..5000 lines per conversation, subject to global limits.")
};

#define BASE_FIELDS (sizeof fields / sizeof fields[0])
#define INTERFACE_FIRST 22u
/* The seventeen application preferences end at Use signals.
 * History lines stays in each server form because applying it resizes that
 * server's buffers; client_interface_apply deliberately does not resize. */
#define INTERFACE_FIELDS 17u

/* base/work record a private edit session. The Client and selected App are
 * borrowed; target_token rejects a closed/reused server slot before applying.
 * Drawing reads this draft but does not publish it. */
struct Settings {
    Client *client;
    size_t target;
    uint64_t target_token;
    int home, interface;
    size_t home_map[CLIENT_MAX_SERVERS + 2], home_count;
    Config work, base;
    Editor editor;
    size_t selected, first, editing_field;
    int active, changed, editing, choosing, choice;
    int credentials_reset, bouncer_reset, startup_reset, autocommands_reset;
    int new_server, advanced, new_result;
    int paste, paste_accept, paste_rejected;
    int edit_scroll, edit_top, edit_rows, edit_left, edit_width;
    FormAction action;
    int action_x[FORM_ACTIONS], action_width[FORM_ACTIONS], compact_actions;
    int choice_top, choice_left, choice_width;
    char message[512];
};

static void message(Settings *s, const char *value)
{
    char localized[sizeof s->message];
    irc_sanitize(s->message, sizeof s->message,
                 irc_security_diagnostic(value, localized, sizeof localized));
}

static size_t ignore_fields_start(const Settings *s)
{
    return BASE_FIELDS + s->work.word_count + (s->work.word_count < CONFIG_WORD_MAX ? 1u : 0u);
}

static size_t field_count(const Settings *s)
{
    if (s->home) return s->home_count;
    if (s->interface) return INTERFACE_FIELDS;
    if (s->new_server) return s->advanced ? INTERFACE_FIRST : 7u;
    return ignore_fields_start(s) + s->work.ignore_count - (s->client ? INTERFACE_FIELDS : 0u) +
           (s->work.ignore_count < CONFIG_IGNORE_MAX ? 1u : 0u);
}

static size_t field_index(const Settings *s, size_t row)
{
    static const size_t basic[] = {0, 1, 2, 3, 6, 7, 17};
    if (s->interface) return row + INTERFACE_FIRST;
    if (s->new_server) return s->advanced ? row : basic[row];
    return s->client && row >= INTERFACE_FIRST ? row + INTERFACE_FIELDS : row;
}

static int action_count(const Settings *s)
{
    return s->new_server || s->client ? 5 : 4;
}

static int *integer_field(Config *c, const Field *f)
{
    return (int *)((unsigned char *)c + f->offset);
}

static const int *const_integer_field(const Config *c, const Field *f)
{
    return (const int *)((const unsigned char *)c + f->offset);
}

static int choice_value(const Config *c, const Field *f)
{
    if (f->offset == offsetof(Config, auth)) return !strcmp(c->auth, "sasl");
    int value = *const_integer_field(c, f);
    return value >= f->low && value <= f->high ? value : f->low;
}

static void clear_credentials(Settings *s)
{
    Config *c = &s->work;
    config_wipe(c->auth, sizeof c->auth);
    strcpy(c->auth, c->bouncer_type == CONFIG_BOUNCER_SOJU ? "sasl" : "none");
    config_wipe(c->auth_user, sizeof c->auth_user);
    config_wipe(c->password, sizeof c->password);
    config_wipe(c->password_file, sizeof c->password_file);
    config_wipe(c->password_gpg_file, sizeof c->password_gpg_file);
    config_wipe(c->server_password, sizeof c->server_password);
    config_wipe(c->server_password_file, sizeof c->server_password_file);
    config_wipe(c->server_password_gpg_file, sizeof c->server_password_gpg_file);
    /* Startup messages may themselves contain account or channel secrets. */
    config_wipe(c->autocommands.lines, sizeof c->autocommands.lines);
    c->autocommands.count = 0;
    s->autocommands_reset = 1;
    s->credentials_reset = 1;
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

static int bouncer_identity_field(const Field *f)
{
    return f->offset == offsetof(Config, bouncer_user) ||
           f->offset == offsetof(Config, bouncer_network) ||
           f->offset == offsetof(Config, bouncer_client);
}

static int bouncer_field_enabled(const Settings *s, const Field *f)
{
    int type = s->work.bouncer_type;
    if (bouncer_identity_field(f) || f->offset == offsetof(Config, bouncer_away))
        return type != CONFIG_BOUNCER_NONE;
    if (type == CONFIG_BOUNCER_NONE) return 1;
    if (f->offset == offsetof(Config, auth) || f->offset == offsetof(Config, auth_user) ||
        f->offset == offsetof(Config, channels)) return 0;
    if (type == CONFIG_BOUNCER_ZNC &&
        (f->offset == offsetof(Config, password) || f->offset == offsetof(Config, password_file) ||
         f->offset == offsetof(Config, password_gpg_file))) return 0;
    if (type == CONFIG_BOUNCER_SOJU &&
        (f->offset == offsetof(Config, server_password) || f->offset == offsetof(Config, server_password_file) ||
         f->offset == offsetof(Config, server_password_gpg_file))) return 0;
    return 1;
}

static void set_choice(Settings *s, const Field *f, int value)
{
    Config *c = &s->work;
    if (f->offset == offsetof(Config, bouncer_type) && value != c->bouncer_type) {
        c->bouncer_type = value; c->bouncer_away = 0;
        config_wipe(c->bouncer_user, sizeof c->bouncer_user);
        config_wipe(c->bouncer_network, sizeof c->bouncer_network);
        config_wipe(c->bouncer_client, sizeof c->bouncer_client);
        clear_credentials(s); s->bouncer_reset = 1;
        if (value != CONFIG_BOUNCER_NONE) c->tls = 1;
        if (s->new_server && !s->advanced) {
            s->advanced = 1; s->selected = (size_t)(f - fields); s->first = 0;
        }
        message(s, value == CONFIG_BOUNCER_NONE ?
            "Direct IRC staged. Previous bouncer identity, credentials and startup commands cleared; enter this server's authentication if required. The port is unchanged." :
            "Choice staged. Previous credentials and startup commands cleared; enter the bouncer identity and its password source. The provider's port is unchanged.");
        return;
    }
    if (f->offset == offsetof(Config, auth))
        snprintf(c->auth, sizeof c->auth, "%s", f->choices[value]);
    else *integer_field(c, f) = value;
    message(s, irc_tr("Choice staged. Apply or Save to use it."));
}

static void localized_settings_number(char *out, size_t cap,
                                      const char *key, const char *prefix, size_t value)
{
    char digits[32];
    (void)snprintf(digits, sizeof digits, "%zu", value);
    const IrcDisplayField fields[] = {{"number", digits}};
    if (!irc_display_render(out, cap, key, fields, 1))
        (void)snprintf(out, cap, "%s %zu", prefix, value);
}

static void field_label(const Settings *s, size_t index, char *out, size_t cap)
{
    if (index < BASE_FIELDS) snprintf(out, cap, "%s", irc_tr(fields[index].label));
    else if (index >= ignore_fields_start(s)) {
        size_t ignore = index - ignore_fields_start(s);
        if (ignore < s->work.ignore_count) localized_settings_number(out, cap, "Ignore mask {number}", "Ignore mask", ignore + 1);
        else snprintf(out, cap, "%s", irc_tr("Add ignore mask"));
    } else if (index - BASE_FIELDS < s->work.word_count)
        localized_settings_number(out, cap, "Watch term {number}", "Watch term", index - BASE_FIELDS + 1);
    else snprintf(out, cap, "%s", irc_tr("Add watch term"));
}

static const char *field_hint(const Settings *s, size_t index)
{
    if (index < BASE_FIELDS) {
        const Field *f = &fields[index];
        if (!bouncer_field_enabled(s, f)) {
            if (s->work.bouncer_type == CONFIG_BOUNCER_NONE)
                return "Select ZNC or soju in Connection type before entering bouncer settings.";
            if (f->offset == offsetof(Config, channels))
                return "The bouncer restores channel membership. Use /join or /part after connecting; saved auto-join is ignored for this profile.";
            return s->work.bouncer_type == CONFIG_BOUNCER_ZNC ?
                "ZNC uses its bouncer identity and exactly one Server password source, containing PASSWORD ONLY. SASL fields are unused." :
                "soju uses its bouncer identity and exactly one authentication Password source, containing PASSWORD ONLY. Authentication user and Server password fields are unused.";
        }
        if (s->work.bouncer_type == CONFIG_BOUNCER_ZNC &&
            (f->offset == offsetof(Config, server_password) || f->offset == offsetof(Config, server_password_file) ||
             f->offset == offsetof(Config, server_password_gpg_file)))
            return "ZNC bouncer PASSWORD ONLY, without username/network prefix. Configure exactly one server-password source; use /panel to create a GPG file.";
        if (s->work.bouncer_type == CONFIG_BOUNCER_SOJU &&
            (f->offset == offsetof(Config, password) || f->offset == offsetof(Config, password_file) ||
             f->offset == offsetof(Config, password_gpg_file)))
            return "soju bouncer PASSWORD ONLY, not the upstream NickServ password. Configure exactly one authentication-password source; use /panel to create a GPG file.";
        return f->hint;
    }
    if (index >= ignore_fields_start(s))
        return "Apply now: exact nick or nick!user@host with * and ?. Empty removes; 32 masks maximum.";
    if (index - BASE_FIELDS < s->work.word_count)
        return "Apply now: literal mention text. Set this field empty to remove it.";
    return "Apply now: add a literal mention term, at most 127 UTF-8 bytes (32 terms maximum).";
}

/* Keep complete English lookup keys until the display boundary. Do not
 * strip prefixes before translation: they are part of the catalog key. */
static const char *field_display_hint(const Settings *s, size_t index)
{
    if (s->new_server && index == 0) return irc_tr("Hostname or numeric IP address.");
    if (s->new_server && index == 3) return irc_tr("Nickname for this server.");
    return irc_tr(field_hint(s, index));
}

static void field_value(const Settings *s, size_t index, char *out, size_t cap)
{
    if (index >= BASE_FIELDS) {
        if (index >= ignore_fields_start(s)) {
            size_t ignore = index - ignore_fields_start(s);
            snprintf(out, cap, "%s", ignore < s->work.ignore_count ? s->work.ignores[ignore] : "");
        } else {
            size_t term = index - BASE_FIELDS;
            snprintf(out, cap, "%s", term < s->work.word_count ? s->work.words[term] : "");
        }
        return;
    }
    const Field *f = &fields[index];
    if (f->kind == FIELD_TEXT)
        snprintf(out, cap, "%s", (const char *)&s->work + f->offset);
    else if (f->kind == FIELD_SECRET)
        snprintf(out, cap, "%s", *((const char *)&s->work + f->offset) ? irc_tr("(set; hidden)") : "");
    else if (f->kind == FIELD_NUMBER)
        snprintf(out, cap, "%d", *const_integer_field(&s->work, f));
    else if (f->kind == FIELD_BOOL && f->offset == offsetof(Config, tls))
        snprintf(out, cap, "%s", irc_tr(s->work.tls ? "TLS (verified)" : "Plain IRC"));
    else if (f->kind == FIELD_BOOL)
        snprintf(out, cap, "%s", *const_integer_field(&s->work, f) ? irc_tr("[x] on") : irc_tr("[ ] off"));
    else snprintf(out, cap, "%s", irc_tr(f->choices[choice_value(&s->work, f)]));
}

Settings *settings_create(void)
{
    Settings *s = calloc(1, sizeof *s);
    if (s) ed_init(&s->editor);
    return s;
}

void settings_destroy(Settings *s)
{
    if (!s) return;
    config_wipe(s, sizeof *s);
    free(s);
}

void settings_open(Settings *s, const App *a)
{
    if (!s) return;
    config_wipe(s, sizeof *s);
    app_config_effective(a, &s->work);
    s->base = s->work;
    ed_init(&s->editor);
    s->active = 1;
    s->action = FORM_EDIT;
    message(s, a->test_mode ? irc_tr("OFFLINE TEST MODE: Apply previews in memory; Save is disabled.") : "");
}

void settings_open_client(Settings *s, Client *client)
{
    if (!s || !client) return;
    config_wipe(s, sizeof *s);
    ed_init(&s->editor);
    s->client = client;
    s->target = SIZE_MAX;
    s->active = s->home = 1;
    s->home_map[s->home_count++] = SIZE_MAX; /* application interface */
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (client->slots[i].used) s->home_map[s->home_count++] = i;
    s->home_map[s->home_count++] = SIZE_MAX - 1; /* new server */
    message(s, client->test_mode ? irc_tr("OFFLINE TEST MODE: Apply previews in memory; Save is disabled.") : "");
}

static int open_target(Settings *s, size_t index, int interface)
{
    Client *client = s->client;
    char error[512];
    App *target = client_get_app(client, index, error, sizeof error);
    if (!target) { message(s, error); return 0; }
    settings_open(s, target);
    s->client = client;
    s->target = index;
    s->target_token = client->slots[index].token;
    s->interface = interface;
    return 1;
}

int settings_open_server(Settings *s, Client *client, size_t index, int nickname)
{
    if (!s || !client || index >= CLIENT_MAX_SERVERS || !client->slots[index].used) return 0;
    settings_open_client(s, client);
    if (!open_target(s, index, 0)) return 0;
    s->selected = nickname ? 3u : 0u;
    return 1;
}

static App *target_app(Settings *s, App *fallback)
{
    if (!s->client) return fallback;
    if (s->target >= CLIENT_MAX_SERVERS || !s->client->slots[s->target].used ||
        s->client->slots[s->target].token != s->target_token ||
        !s->client->slots[s->target].app) {
        message(s, irc_tr("That saved server is no longer available. Return to Settings and choose another."));
        return NULL;
    }
    return s->client->slots[s->target].app;
}

static void open_home_selection(Settings *s)
{
    if (!s->home || s->selected >= s->home_count) return;
    size_t target = s->home_map[s->selected];
    if (target == SIZE_MAX) open_target(s, 0, 1);
    else if (target == SIZE_MAX - 1) {
        Config config;
        Client *client = s->client;
        client_new_config(client, &config);
        settings_open_new(s, &config);
        s->client = client;
        config_wipe(&config, sizeof config);
    } else open_target(s, target, 0);
}

void settings_open_new(Settings *s, const Config *config)
{
    if (!s) return;
    config_wipe(s, sizeof *s);
    s->work = s->base = *config;
    ed_init(&s->editor);
    s->active = s->new_server = 1;
    s->action = FORM_EDIT;
    message(s, "");
}

int settings_take_new(Settings *s, Config *config)
{
    if (!s || !s->new_result) return 0;
    int result = s->new_result;
    *config = s->work;
    config_wipe(s, sizeof *s);
    return result;
}

int settings_active(const Settings *s)
{
    return s && s->active;
}

int settings_context_help(const Settings *s, char *screen, size_t screen_cap,
                          char *item, size_t item_cap, char *detail, size_t detail_cap)
{
    if (!settings_active(s) || s->editing || s->paste) return 0;
    snprintf(screen, screen_cap, "%s", irc_tr(s->home ? "Settings" : s->interface ?
             "Interface settings" : s->new_server ? "New server settings" : "Server settings"));
    if (s->home) {
        if (s->selected >= s->home_count) return 0;
        size_t target = s->home_map[s->selected];
        if (target == SIZE_MAX) {
            snprintf(item, item_cap, "%s", irc_tr("Interface"));
            snprintf(detail, detail_cap, "%s", irc_tr("Open application preferences, including command suggestions. Apply changes the running client; Save keeps them for the next startup."));
        } else if (target == SIZE_MAX - 1u) {
            snprintf(item, item_cap, "%s", irc_tr("New server"));
            snprintf(detail, detail_cap, "%s", irc_tr("Enter a server and nickname. Add keeps it disconnected; Connect starts the new connection. Save its settings to keep the profile."));
        } else {
            char number[32];
            (void)snprintf(number, sizeof number, "%zu", target);
            const IrcDisplayField values[] = {{"number", number}};
            if (!irc_display_render(item, item_cap, "Server {number}", values, 1))
                snprintf(item, item_cap, "Server %zu", target);
            snprintf(detail, detail_cap, "%s", irc_tr("Open this server's connection, authentication, startup and conversation preferences. Editing this form does not change the selected conversation."));
        }
        return 1;
    }
    if (s->action != FORM_EDIT && !s->choosing) {
        const char *labels[] = {"Edit", "Apply", "Save", "Back", "Cancel"};
        const char *creating[] = {"Edit", "Add", "Connect", "Basic or Advanced", "Cancel"};
        snprintf(item, item_cap, "%s", s->new_server ? irc_tr(creating[s->action]) :
                 irc_tr(!s->client && s->action == FORM_CANCEL ? "Cancel" : labels[s->action]));
        snprintf(detail, detail_cap, "%s", s->new_server ?
            irc_tr("Add creates a disconnected server. Connect also starts the connection. Basic or Advanced changes which connection fields are shown. Cancel discards the form.") :
            irc_tr("Apply uses staged preferences now. Save also writes them to the configuration file. Back returns to the settings groups. Cancel closes the form and discards unapplied edits."));
        return 1;
    }
    if (s->selected >= field_count(s)) return 0;
    size_t index = field_index(s, s->selected);
    char label[320];
    field_label(s, index, label, sizeof label);
    if (s->choosing && index < BASE_FIELDS)
        snprintf(item, item_cap, "%s: %s", label, irc_tr(fields[index].choices[s->choice]));
    else snprintf(item, item_cap, "%s", label);
    snprintf(detail, detail_cap, "%s", field_display_hint(s, index));
    if (index < BASE_FIELDS && fields[index].offset == offsetof(Config, command_suggestions))
        snprintf(detail, detail_cap, "%s", irc_tr("Commonly used prioritizes common tasks for the live connection. Recent history lists retained commands. Most used ranks this configuration's saved command-name counts. Alphabetical sorts names. Contextual uses the screen that opened command entry. Down selects; Enter inserts for review. Delete forgets a history entry or resets a personal count. Apply uses the mode now; Save keeps the choice."));
    return 1;
}

int settings_at_top(const Settings *s)
{
    return settings_active(s) && !s->editing && !s->choosing && !s->paste &&
           !s->selected && s->action == FORM_EDIT;
}

unsigned settings_mouse_state(const Settings *s)
{
    if (!settings_active(s)) return 0;
    return 1u | (s->editing ? 2u : 0u) | (s->choosing ? 4u : 0u) |
           (s->new_server ? 8u : 0u) | (s->advanced ? 16u : 0u) |
           (s->home ? 32u : 0u) | (s->interface ? 64u : 0u) |
           (s->client && s->target < CLIENT_MAX_SERVERS ? (unsigned)(s->target + 1) << 8 : 0u);
}

void settings_focus_ignores(Settings *s)
{
    if (!settings_active(s)) return;
    if (s->home) open_target(s, s->client->current, 0);
    if (!s->home && !s->interface) s->selected = ignore_fields_start(s) - (s->client ? INTERFACE_FIELDS : 0u);
}

static void close_form(Settings *s, App *a)
{
    int changed = s->changed || s->editing;
    config_wipe(s, sizeof *s);
    app_status(a, "%s", irc_tr(changed ? "Unapplied settings discarded." : "Settings closed."));
}

void settings_close(Settings *s, App *a)
{
    if (settings_active(s)) close_form(s, a);
}

int settings_pasting(const Settings *s)
{
    return settings_active(s) && s->paste;
}

static void edit_field(Settings *s)
{
    char text[CONFIG_PATH_MAX + 32];
    char safe[ED_MAX_BYTES + 1];
    if (s->selected >= field_count(s)) return;
    size_t index = field_index(s, s->selected);
    if (index < BASE_FIELDS) {
        const Field *f = &fields[index];
        if (!bouncer_field_enabled(s, f)) { message(s, field_hint(s, index)); return; }
        if (f->kind == FIELD_BOOL) {
            if (f->offset == offsetof(Config, tls) && s->work.bouncer_type != CONFIG_BOUNCER_NONE) {
                message(s, irc_tr("Bouncer connections require verified TLS. Select Direct IRC before disabling TLS.")); return;
            }
            int *value = integer_field(&s->work, f);
            *value = !*value;
            if (f->offset == offsetof(Config, tls) && !*value) clear_credentials(s);
            s->changed = 1;
            message(s, irc_tr("Choice staged. Apply or Save to use it."));
            return;
        }
        if (f->kind == FIELD_CHOICE) {
            s->choosing = 1;
            s->choice = choice_value(&s->work, f);
            message(s, "");
            return;
        }
    }
    if (index < BASE_FIELDS && fields[index].kind == FIELD_SECRET)
        snprintf(text, sizeof text, "%s", (const char *)&s->work + fields[index].offset);
    else field_value(s, index, text, sizeof text);
    irc_sanitize(safe, sizeof safe, text);
    int filtered = strcmp(safe, text) != 0;
    if (index < BASE_FIELDS && fields[index].kind == FIELD_SECRET && filtered) {
        message(s, irc_tr("This password contains bytes the field editor cannot represent. Edit the private config directly."));
        config_wipe(text, sizeof text); config_wipe(safe, sizeof safe);
        return;
    }
    if (!ed_from_utf8(&s->editor, safe)) {
        message(s, irc_tr("This value is not valid UTF-8 and cannot be edited here."));
        config_wipe(text, sizeof text); config_wipe(safe, sizeof safe);
        return;
    }
    /* Selecting the old value makes replacement direct; arrows retain the
     * normal editor semantics for changing just part of a path or name. */
    ed_select_all(&s->editor);
    s->editing = 1;
    s->editing_field = index;
    s->edit_scroll = 0;
    message(s, filtered ? irc_tr("Unsupported stored characters filtered for editing. Enter stages; Esc discards.") : "");
    config_wipe(text, sizeof text); config_wipe(safe, sizeof safe);
}

static int commit_list_field(Settings *s, const char *text, size_t n)
{
    int ignore = s->editing_field >= ignore_fields_start(s);
    size_t index = s->editing_field - (ignore ? ignore_fields_start(s) : BASE_FIELDS);
    size_t *count = ignore ? &s->work.ignore_count : &s->work.word_count;
    size_t maximum = ignore ? CONFIG_IGNORE_MAX : CONFIG_WORD_MAX;
    size_t stride = ignore ? CONFIG_IGNORE_SIZE : CONFIG_WORD_SIZE;
    char *values = ignore ? (char *)s->work.ignores : (char *)s->work.words;
    if (n >= stride) {
        message(s, ignore ? irc_tr("Ignore masks can contain at most 127 ASCII bytes.") :
                "Watch terms can contain at most 127 UTF-8 bytes.");
        return 0;
    }
    if (ignore && n && !irc_valid_ignore_mask(text)) {
        message(s, irc_tr("Use an exact nickname or nick!user@host mask with * and ? wildcards."));
        return 0;
    }
    if (!n) {
        if (index < *count) {
            if (index + 1 < *count)
                memmove(values + index * stride, values + (index + 1) * stride,
                        (*count - index - 1) * stride);
            --*count;
            memset(values + *count * stride, 0, stride);
        }
        return 1;
    }
    if (index > *count || index >= maximum) {
        message(s, ignore ? irc_tr("The ignore list is full.") : irc_tr("The watch-term list is full."));
        return 0;
    }
    memset(values + index * stride, 0, stride);
    memcpy(values + index * stride, text, n + 1);
    if (index == *count) ++*count;
    return 1;
}

/* A field edit updates the form draft only. Preserve
 * its validation error and current selection until the user explicitly
 * chooses Apply or Save; do not connect as a side effect of text editing. */
static void commit_field(Settings *s)
{
    char text[ED_MAX_BYTES + 1];
    int server_changed = 0, identity_changed = 0;
    size_t n = ed_to_utf8(&s->editor, text, sizeof text);
    if (n == SIZE_MAX) { message(s, irc_tr("The field is too long.")); goto done; }
    if (strchr(text, '\n') || strchr(text, '\r') || strchr(text, '\t')) {
        message(s, irc_tr("Settings fields must be a single line.")); goto done;
    }
    int secret = s->editing_field < BASE_FIELDS && fields[s->editing_field].kind == FIELD_SECRET;
    if (!secret && n && (text[0] == ' ' || text[n - 1] == ' ')) {
        message(s, irc_tr("Leading or trailing spaces cannot be represented in this config format.")); goto done;
    }
    if (s->editing_field >= BASE_FIELDS) {
        if (!commit_list_field(s, text, n)) goto done;
    } else {
        const Field *f = &fields[s->editing_field];
        if (f->kind == FIELD_NUMBER) {
            char *end;
            errno = 0;
            long number = strtol(text, &end, 10);
            if (!*text || errno || *end || number < f->low || number > f->high) {
                char error[160];
                snprintf(error, sizeof error, "%s must be an integer from %d to %d.", f->label, f->low, f->high);
                message(s, error); goto done;
            }
            if (f->offset == offsetof(Config, port) && s->work.bouncer_type != CONFIG_BOUNCER_NONE &&
                s->work.port != (int)number) { clear_credentials(s); identity_changed = 1; }
            *integer_field(&s->work, f) = (int)number;
        } else {
            if (n >= f->capacity) {
                char error[160];
                snprintf(error, sizeof error, "%s allows at most %zu UTF-8 bytes.", f->label, f->capacity - 1);
                message(s, error); goto done;
            }
            char *destination = (char *)&s->work + f->offset;
            if (bouncer_identity_field(f)) {
                Config check = s->work;
                char identity[192];
                memcpy((char *)&check + f->offset, text, n + 1u);
                if (!check.bouncer_user[0]) strcpy(check.bouncer_user, "account");
                if (!check.bouncer_network[0]) strcpy(check.bouncer_network, "network");
                int valid = config_bouncer_identity(&check, identity, sizeof identity) == 1;
                config_wipe(&check, sizeof check); config_wipe(identity, sizeof identity);
                if (!valid) { message(s, irc_tr("Bouncer fields use ASCII letters, digits, '.', '_' or '-'; enter components without '/' or '@'.")); goto done; }
            }
            if (f->offset == offsetof(Config, server) &&
                !irc_equal(s->work.server, text, IRC_CASE_ASCII)) {
                server_changed = 1;
                clear_credentials(s); s->startup_reset = 1;
                memset(s->work.channels, 0, sizeof s->work.channels);
            } else if (s->work.bouncer_type != CONFIG_BOUNCER_NONE && strcmp(destination, text) &&
                       (bouncer_identity_field(f) || f->offset == offsetof(Config, ca_file))) {
                clear_credentials(s); identity_changed = 1; s->bouncer_reset = 1;
            }
            config_wipe(destination, f->capacity);
            memcpy(destination, text, n + 1);
        }
    }
    s->changed = 1;
    s->editing = 0;
    config_wipe(&s->editor, sizeof s->editor);
    size_t count = field_count(s);
    if (s->selected >= count) s->selected = count - 1;
    message(s, server_changed ? irc_tr("Server changed: inherited auth, startup commands and auto-join values cleared. Enter settings for the new server.") :
            identity_changed ? irc_tr("Value staged. Previous credentials and startup commands cleared; enter the password source for this connection identity.") :
            "Value staged. Apply or Save to use it.");
done:
    config_wipe(text, sizeof text);
}

/* Explicit action boundary: field edits alone never save or connect. */
static void perform_action(Settings *s, App *a)
{
    char error[512];
    Config desired;
    if (s->home) {
        open_home_selection(s);
        return;
    }
    if (s->action == FORM_EDIT) {
        edit_field(s);
        return;
    }
    if (s->new_server) {
        if (s->action == FORM_NEW_CANCEL) {
            close_form(s, a);
            return;
        }
        if (s->action == FORM_CANCEL) {
            size_t index = field_index(s, s->selected);
            s->advanced = !s->advanced;
            s->selected = 0;
            for (size_t row = 0; row < field_count(s); ++row)
                if (field_index(s, row) == index) s->selected = row;
            s->first = 0;
            s->action = FORM_EDIT;
            return;
        }
        if (!*s->work.server || !*s->work.nick) {
            message(s, irc_tr("Server and nickname are required."));
            return;
        }
        if (config_validate(&s->work, error, sizeof error) < 0) {
            message(s, error);
            return;
        }
        if (s->client) {
            size_t index;
            Client *client = s->client;
            int connect = s->action == FORM_SAVE;
            if (client_add(client, &s->work, &index, error, sizeof error) < 0) {
                message(s, error);
                return;
            }
            int connected = !connect || client_connect(client, index, error, sizeof error) > 0;
            if (!open_target(s, index, 0)) {
                char detail[512];
                snprintf(detail, sizeof detail, "%s", s->message);
                settings_open_client(s, client);
                message(s, detail);
            } else if (!connected) message(s, error);
            else {
                message(s, connect ? irc_tr("Server added and connection started. Save keeps its startup settings.") :
                        irc_tr("Server added without connecting. Save keeps its startup settings."));
            }
            return;
        }
        s->new_result = s->action == FORM_SAVE ? 2 : 1;
        s->active = 0;
        return;
    }
    if (s->client && s->action == FORM_CANCEL) {
        int changed = s->changed;
        settings_open_client(s, s->client);
        if (changed) message(s, irc_tr("Unapplied edits discarded. Select another settings group."));
        return;
    }
    if (s->client && s->action == FORM_NEW_CANCEL) {
        close_form(s, a);
        return;
    }
    if (s->action == FORM_CANCEL) {
        close_form(s, a);
        return;
    }
    if (s->action == FORM_SAVE && a->test_mode) {
        message(s, irc_tr("OFFLINE TEST MODE: Save is disabled; use Apply for an in-memory preview."));
        return;
    }
    App *target = target_app(s, a);
    if (!target) return;
    /* A server may update our nickname or membership while the panel is
     * open. Keep those changes unless the user actually edited that field. */
    app_config_effective(target, &desired);
    int current_identity_changed = connection_identity_changed(&s->base, &desired);
    for (size_t i = 0; i < BASE_FIELDS; ++i) {
        const Field *f = &fields[i];
        size_t size = f->kind == FIELD_TEXT || f->kind == FIELD_SECRET ? f->capacity :
                      f->offset == offsetof(Config, auth) ? sizeof s->work.auth : sizeof(int);
        const unsigned char *edited = (const unsigned char *)&s->work + f->offset;
        const unsigned char *base = (const unsigned char *)&s->base + f->offset;
        int differs = f->kind == FIELD_TEXT || f->kind == FIELD_SECRET || f->offset == offsetof(Config, auth) ?
                      strcmp((const char *)edited, (const char *)base) != 0 :
                      memcmp(edited, base, size) != 0;
        if (differs) memcpy((unsigned char *)&desired + f->offset, edited, size);
    }
    int connection_edited = connection_credentials_edited(&s->base, &s->work);
#define COPY(member) memcpy(desired.member, s->work.member, sizeof desired.member)
    if (s->credentials_reset || connection_edited) {
        COPY(auth); COPY(auth_user); COPY(password); COPY(password_file); COPY(password_gpg_file);
        COPY(server_password); COPY(server_password_file); COPY(server_password_gpg_file);
    }
    if (s->bouncer_reset || connection_edited) {
        desired.bouncer_type = s->work.bouncer_type;
        COPY(bouncer_user); COPY(bouncer_network); COPY(bouncer_client);
    }
    if (s->work.bouncer_type != s->base.bouncer_type) desired.bouncer_away = s->work.bouncer_away;
    if (connection_edited) {
        COPY(server); COPY(ca_file);
        desired.port = s->work.port;
        desired.tls = s->work.tls;
    }
    if (s->startup_reset) COPY(channels);
    if (s->autocommands_reset || (connection_edited && current_identity_changed)) {
        desired.autocommands.count = s->work.autocommands.count;
        COPY(autocommands.lines);
    }
#undef COPY
    if (s->work.word_count != s->base.word_count ||
        memcmp(s->work.words, s->base.words, sizeof s->work.words) != 0) {
        desired.word_count = s->work.word_count;
        memcpy(desired.words, s->work.words, sizeof desired.words);
    }
    if (s->work.ignore_count != s->base.ignore_count ||
        memcmp(s->work.ignores, s->base.ignores, sizeof s->work.ignores) != 0) {
        desired.ignore_count = s->work.ignore_count;
        memcpy(desired.ignores, s->work.ignores, sizeof desired.ignores);
    }
    int applied = s->interface ?
        client_interface_apply(s->client, &desired, s->action == FORM_SAVE, error, sizeof error) > 0 :
        app_config_apply(target, &desired, s->action == FORM_SAVE, error, sizeof error);
    if (!applied) {
        config_wipe(&desired, sizeof desired);
        message(s, error);
        return;
    }
    config_wipe(&desired, sizeof desired);
    app_config_effective(target, &s->work);
    s->base = s->work;
    s->changed = s->credentials_reset = s->bouncer_reset = s->startup_reset = s->autocommands_reset = 0;
    if (s->action == FORM_SAVE) {
        if (s->client) {
            message(s, s->interface ? irc_tr("Application preferences saved and applied to every server.") :
                    irc_tr("Server configuration saved. Reconnect to use connection changes."));
            s->action = FORM_EDIT;
        } else {
            config_wipe(s, sizeof *s);
            app_status(a, "%s", irc_tr("Configuration saved. Reconnect to use connection changes."));
        }
    } else {
        message(s, s->interface ? irc_tr("Application preferences applied to every server. Save keeps them for next startup.") :
                irc_tr("Preferences applied in memory. Connect or reconnect to use connection changes."));
        s->action = FORM_EDIT;
    }
}

void settings_begin_paste(Settings *s)
{
    if (!settings_active(s)) return;
    s->paste = 1;
    s->paste_accept = s->editing;
    s->paste_rejected = 0;
}

void settings_end_paste(Settings *s)
{
    if (!s || !s->paste) return;
    s->paste = 0;
    if (s->paste_accept)
        message(s, s->paste_rejected ? irc_tr("Paste contained rejected controls or exceeded input limits; review the field.") :
                irc_tr("Field paste captured as data. Enter stages it; nothing has been saved."));
    s->paste_accept = 0;
}

/* When bracketed paste is active, data enters only
 * the current field editor. Ordinary navigation, confirmation, and custom
 * shortcuts must not interpret bytes from a pasted sequence. */
void settings_key(Settings *s, App *a, wint_t key, int special)
{
    if (!settings_active(s)) return;
    a->dirty = 1;
    if (s->paste) {
        if (s->paste_accept && !special) {
            if (key == '\t') key = ' ';
            if (key == '\r' || key == '\n' || !ed_insert(&s->editor, (wchar_t)key)) s->paste_rejected = 1;
        }
        return;
    }
    if (!special && key == 27) { close_form(s, a); return; }
    if ((!special && key == '\t') || (special && key == KEY_BTAB)) {
        if (s->editing) {
            commit_field(s);
            if (s->editing) return;
        }
        if (s->choosing) {
            set_choice(s, &fields[field_index(s, s->selected)], s->choice);
            s->choosing = 0;
            s->changed = 1;
        }
        size_t count = field_count(s);
        s->selected = special ? (s->selected + count - 1) % count : (s->selected + 1) % count;
        s->action = FORM_EDIT;
        return;
    }
    int enter = (special && key == KEY_ENTER) || (!special && (key == '\r' || key == '\n'));
    if (s->editing) {
        int secret = s->editing_field < BASE_FIELDS && fields[s->editing_field].kind == FIELD_SECRET;
        if (enter) { commit_field(s); return; }
        if (special) {
            switch (key) {
            case KEY_LEFT:
                if (secret) ed_left(&s->editor);
                else ed_visual_left(&s->editor, s->edit_width);
                break;
            case KEY_RIGHT:
                if (secret) ed_right(&s->editor);
                else ed_visual_right(&s->editor, s->edit_width);
                break;
            case KEY_UP:
                if (secret) ed_move_vertical(&s->editor, -1, s->edit_width);
                else ed_visual_move_vertical(&s->editor, -1, s->edit_width);
                break;
            case KEY_DOWN:
                if (secret) ed_move_vertical(&s->editor, 1, s->edit_width);
                else ed_visual_move_vertical(&s->editor, 1, s->edit_width);
                break;
            case KEY_HOME:
                if (secret) ed_home(&s->editor);
                else ed_visual_home(&s->editor, s->edit_width);
                break;
            case KEY_END:
                if (secret) ed_end(&s->editor);
                else ed_visual_end(&s->editor, s->edit_width);
                break;
            case KEY_DC: (void)ed_delete(&s->editor); break;
            case KEY_BACKSPACE: (void)ed_backspace(&s->editor); break;
            default: break;
            }
        } else if (key == 8 || key == 127) (void)ed_backspace(&s->editor);
        else if (key >= 32 && !ed_insert(&s->editor, (wchar_t)key))
            message(s, irc_tr("Field input limit reached or unsupported control character."));
        return;
    }
    int up = (special && key == KEY_UP) || (!special && key == 'k');
    int down = (special && key == KEY_DOWN) || (!special && key == 'j');
    int left = (special && key == KEY_LEFT) || (!special && key == 'h');
    int right = (special && key == KEY_RIGHT) || (!special && key == 'l');
    if (s->home) {
        if (up && s->selected) --s->selected;
        else if (down && s->selected + 1 < s->home_count) ++s->selected;
        else if ((special && key == KEY_HOME) || (!special && key == 'g')) s->selected = 0;
        else if ((special && key == KEY_END) || (!special && key == 'G')) s->selected = s->home_count - 1;
        else if (enter) open_home_selection(s);
        return;
    }
    if (s->choosing) {
        const Field *field = &fields[field_index(s, s->selected)];
        if ((up || left) && s->choice > field->low) --s->choice;
        else if ((down || right) && s->choice < field->high) ++s->choice;
        else if (enter) {
            set_choice(s, &fields[field_index(s, s->selected)], s->choice);
            s->choosing = 0;
            s->changed = 1;
        }
        return;
    }
    size_t count = field_count(s);
    if (up && s->selected) --s->selected;
    else if (down && s->selected + 1 < count) ++s->selected;
    else if (left) s->action = s->action ? (FormAction)(s->action - 1) : (FormAction)(action_count(s) - 1);
    else if (right) s->action = (FormAction)((s->action + 1u) % (unsigned)action_count(s));
    else if ((special && key == KEY_HOME) || (!special && key == 'g')) s->selected = 0;
    else if ((special && key == KEY_END) || (!special && key == 'G')) s->selected = count - 1;
    else if (special && key == KEY_PPAGE) s->selected = s->selected > 10 ? s->selected - 10 : 0;
    else if (special && key == KEY_NPAGE) s->selected = s->selected + 10 < count ? s->selected + 10 : count - 1;
    else if (enter) perform_action(s, a);
}

static void focus_text(const App *a, SettingsText text, int y, int x, int width,
                       const char *value, int selected)
{
    (void)a;
    if (!selected) { text(y, x, width, value, A_NORMAL); return; }
    /* The focus outline hugs the visible text. A full-row width here left a
     * stray closing bracket against the terminal's right edge. */
    int cells = 0;
    mbstate_t state;
    memset(&state, 0, sizeof state);
    for (const char *p = value; *p && cells < width - 2;) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, p, MB_CUR_MAX, &state);
        int advance;
        if (n == (size_t)-1 || n == (size_t)-2 || !n) {
            memset(&state, 0, sizeof state); n = 1; advance = 1;
        } else {
            advance = wcwidth(wc);
            if (advance < 0) advance = 1;
        }
        if (advance > width - 2 - cells) break;
        cells += advance;
        p += n;
    }
    if (width > cells + 2) width = cells + 2;
    attr_t attrs = vipirc_focus_attrs();
    text(y, x, 1, "[", attrs);
    if (width > 2) text(y, x + 1, width - 2, value, attrs);
    if (width > 1) text(y, x + width - 1, 1, "]", attrs);
}

static void draw_actions(Settings *s, const App *a, SettingsText text)
{
    const char *normal[] = {"Edit", "Apply", "Save", "Cancel"};
    const char *universal[] = {"Edit", "Apply", "Save", "Back", "Cancel"};
    const char *creating[] = {"Edit", "Add", "Connect", s->advanced ? "Basic" : "Advanced", "Cancel"};
    const char **labels = s->new_server ? creating : s->client ? universal : normal;
    int total = 0;
    for (int i = 0; i < action_count(s); ++i) {
        s->action_x[i] = -1;
        s->action_width[i] = irc_label_cells(irc_tr(labels[i])) + 2;
        total += s->action_width[i] + 1;
    }
    s->compact_actions = total > COLS;
    int y = LINES - 2;
    if (s->compact_actions) {
        text(y, 0, 1, "<", A_BOLD);
        text(y, COLS - 1, 1, ">", A_BOLD);
        s->action_x[s->action] = 2;
        s->action_width[s->action] = COLS - 4;
        focus_text(a, text, y, 2, COLS - 4, irc_tr(labels[s->action]), 1);
        return;
    }
    int x = 0;
    for (int i = 0; i < action_count(s); ++i) {
        s->action_x[i] = x;
        focus_text(a, text, y, x, s->action_width[i], irc_tr(labels[i]), s->action == (FormAction)i);
        x += s->action_width[i] + 1;
    }
}

/* Both visual editor paths use the same localized, display-only counters. */
static void settings_editor_position(char *line, size_t cap, size_t bytes, int row, int col)
{
    char byte_text[32], row_text[32], col_text[32];
    (void)snprintf(byte_text, sizeof byte_text, "%zu", bytes);
    (void)snprintf(row_text, sizeof row_text, "%d", row);
    (void)snprintf(col_text, sizeof col_text, "%d", col);
    const IrcDisplayField values[] = {{"bytes", byte_text}, {"row", row_text}, {"col", col_text}};
    if (!irc_display_render(line, cap, "{bytes} UTF-8 bytes | row {row} col {col}", values, 3))
        (void)snprintf(line, cap, "%zu UTF-8 bytes | row %d col %d", bytes, row, col);
}

static void draw_field_editor(Settings *s, const App *a, SettingsText text)
{
    char label[320], line[512];
    field_label(s, s->editing_field, label, sizeof label);
    s->edit_left = 2;
    s->edit_width = COLS - 4;
    s->edit_top = 6;
    s->edit_rows = LINES - 9;
    if (s->edit_rows > 5) s->edit_rows = 5;
    if (s->edit_rows < 1) s->edit_rows = 1;
    for (int y = 3; y < LINES - 1; ++y) mvhline(y, 0, ' ', COLS);
    const IrcDisplayField edit_fields[] = {{"field", label}};
    if (!irc_display_render(line, sizeof line, "Edit: {field}", edit_fields, 1))
        snprintf(line, sizeof line, "Edit: %s", label);
    text(3, 0, COLS, line, A_BOLD);
    if (a->cfg.hints) text(4, 0, COLS, irc_tr("Enter: stage | Esc: vipirc"), A_DIM);
    int secret = s->editing_field < BASE_FIELDS && fields[s->editing_field].kind == FIELD_SECRET;
    IrcCursesEditorPosition visual;
    if (!secret && irc_curses_editor_visual(&s->editor, s->edit_width,
            s->edit_top, s->edit_left, s->edit_rows, &s->edit_scroll,
            s->editor.selection_all ? A_REVERSE : A_NORMAL, &visual)) {
        settings_editor_position(line, sizeof line, s->editor.bytes, visual.row + 1, visual.col + 1);
        text(LINES - 2, 0, COLS, line, A_NORMAL);
        text(LINES - 1, 0, COLS, s->message, A_NORMAL);
        int py = s->edit_top + visual.row - visual.first, px = s->edit_left + visual.col;
        if (py >= 0 && py < LINES && px >= 0 && px < COLS) {
            (void)mvchgat(py, px, visual.span, A_REVERSE | A_UNDERLINE, 0, NULL);
            (void)move(py, px);
        }
        (void)curs_set(1);
        return;
    }
    int total, row, col;
    ed_layout(&s->editor, s->edit_width, &total, &row, &col);
    if (s->edit_scroll >= total) s->edit_scroll = total - 1;
    if (s->edit_scroll < 0) s->edit_scroll = 0;
    if (row < s->edit_scroll) s->edit_scroll = row;
    if (row >= s->edit_scroll + s->edit_rows) s->edit_scroll = row - s->edit_rows + 1;
    int r = 0, c = 0, wrapped = 0;
    attrset(s->editor.selection_all ? A_REVERSE : A_NORMAL);
    for (size_t i = 0; i < s->editor.len;) {
        int advance = ed_cluster_width(&s->editor, i, s->edit_width);
        if (advance && c + advance > s->edit_width) { ++r; c = 0; }
        size_t end = ed_next(&s->editor, i);
        if (r >= s->edit_scroll && r < s->edit_scroll + s->edit_rows && s->editor.data[i] != L'\n') {
            int w = wcwidth(s->editor.data[i]);
            move(s->edit_top + r - s->edit_scroll, s->edit_left + c);
            if (s->editing_field < BASE_FIELDS && fields[s->editing_field].kind == FIELD_SECRET) {
                int cells = advance > 0 ? advance : 1;
                for (int cell = 0; cell < cells; ++cell) addch('*');
            } else if (w < 0 || w > s->edit_width) addch('?');
            else {
                if (!w) addch('?');
                (void)addnwstr(s->editor.data + i, (int)(end - i));
            }
        }
        if (s->editor.data[i] == L'\n') {
            if (!wrapped) ++r;
            c = 0; wrapped = 0;
        } else {
            c += advance;
            wrapped = c >= s->edit_width;
            if (wrapped) { ++r; c = 0; }
        }
        if (r >= s->edit_scroll + s->edit_rows) break;
        i = end;
    }
    attrset(A_NORMAL);
    settings_editor_position(line, sizeof line, s->editor.bytes, row + 1, col + 1);
    text(LINES - 2, 0, COLS, line, A_NORMAL);
    text(LINES - 1, 0, COLS, s->message, A_NORMAL);
    int py = s->edit_top + row - s->edit_scroll, px = s->edit_left + col;
    if (py >= 0 && py < LINES && px >= 0 && px < COLS) {
        int span = ed_cluster_width(&s->editor, s->editor.pos, s->edit_width);
        if (span < 1) span = 1;
        (void)mvchgat(py, px, span, A_REVERSE | A_UNDERLINE, 0, NULL);
        move(py, px);
    }
    (void)curs_set(1);
}

static void draw_home(Settings *s, const App *a, SettingsText text)
{
    char label[1024], safe[1024];
    snprintf(label, sizeof label, "%s%s | %s", a->test_mode ? irc_tr("[OFFLINE] ") : "", irc_tr("Settings"), irc_tr("All servers"));
    text(2, 0, COLS, label, A_BOLD);
    if (a->cfg.hints)
        text(3, 0, COLS, irc_tr("Choose Interface or a saved server."), A_DIM);
    size_t visible = (size_t)(LINES - 7);
    if (s->selected >= s->home_count) s->selected = s->home_count - 1;
    if (s->selected < s->first) s->first = s->selected;
    if (s->selected >= s->first + visible) s->first = s->selected - visible + 1;
    for (size_t row = 0; row < visible && row + s->first < s->home_count; ++row) {
        size_t target = s->home_map[row + s->first];
        if (target == SIZE_MAX) snprintf(label, sizeof label, "%s", irc_tr("Interface"));
        else if (target == SIZE_MAX - 1) snprintf(label, sizeof label, "%s", irc_tr("New server..."));
        else {
            ClientSlot *slot = &s->client->slots[target];
            Config config;
            if (slot->app) app_config_effective(slot->app, &config);
            else config = slot->seed;
            char number[32];
            (void)snprintf(number, sizeof number, "%zu", target);
            const char *host = config.server[0] ? config.server : irc_tr("(not configured)");
            const char *state = config.autoconnect ? irc_tr("on") : irc_tr("off");
            const char *status = slot->load_failed ? irc_tr("load failed") : slot->saved ? irc_tr("saved") : irc_tr("unsaved");
            const IrcDisplayField values[] = {{"number", number}, {"host", host}, {"state", state}, {"status", status}};
            if (!irc_display_render(label, sizeof label,
                    "Server {number}: {host} | auto-connect {state} | {status}", values, 4))
                snprintf(label, sizeof label, "Server %zu: %s | auto-connect %s | %s", target, host, state, status);
            config_wipe(&config, sizeof config);
        }
        irc_sanitize(safe, sizeof safe, label);
        focus_text(a, text, 4 + (int)row, 0, COLS, safe, row + s->first == s->selected);
    }
    int open_width = irc_label_cells(irc_tr("Open")) + 2;
    if (open_width > COLS) open_width = COLS;
    focus_text(a, text, LINES - 2, 0, open_width, irc_tr("Open"), 1);
    if (a->cfg.hints && open_width + 2 < COLS)
        text(LINES - 2, open_width + 2, COLS - open_width - 2, irc_tr("Esc: vipirc"), A_DIM);
    text(LINES - 1, 0, COLS, s->message, A_NORMAL);
    (void)curs_set(0);
    move(0, 0);
    wnoutrefresh(stdscr);
}

void settings_draw(Settings *s, const App *a, SettingsText text)
{
    if (!settings_active(s)) return;
    /* The translated label and full path value must both fit before terminal
     * cell clipping. Truncating snprintf's byte output could split UTF-8. */
    char line[320 + CONFIG_PATH_MAX + 32 + 2], path[CONFIG_PATH_MAX + 1];
    const App *target = a;
    if (s->client && s->target < CLIENT_MAX_SERVERS &&
        s->client->slots[s->target].token == s->target_token &&
        s->client->slots[s->target].app) target = s->client->slots[s->target].app;
    irc_sanitize(path, sizeof path, target->config_path);
    erase();
    if (LINES < 9 || COLS < 20) {
        text(2, 0, COLS, irc_tr("Resize; Esc: vipirc."), A_NORMAL);
        (void)curs_set(0); move(0, 0); wnoutrefresh(stdscr); return;
    }
    if (s->home) { draw_home(s, a, text); return; }
    if (s->new_server) snprintf(line, sizeof line, "%s%s%s",
        a->test_mode ? irc_tr("[OFFLINE] ") : "", s->advanced ?
        irc_tr("New server | All connection fields") : irc_tr("New server | Basic connection fields"),
        s->changed ? " *" : "");
    else if (s->interface) snprintf(line, sizeof line, "%s%s%s",
        a->test_mode ? irc_tr("[OFFLINE] ") : "", irc_tr("Settings | Interface"), s->changed ? " *" : "");
    else if (s->client) {
        char title[512], number[32];
        (void)snprintf(number, sizeof number, "%zu", s->target);
        const IrcDisplayField fields[] = {{"number", number}};
        if (!irc_display_render(title, sizeof title,
                "Settings | Server {number}", fields, 1))
            (void)snprintf(title, sizeof title, "Settings | Server %zu", s->target);
        (void)snprintf(line, sizeof line, "%s%s%s",
            a->test_mode ? irc_tr("[OFFLINE] ") : "", title, s->changed ? " *" : "");
    }
    else snprintf(line, sizeof line, "%s%s%s", a->test_mode ? irc_tr("[OFFLINE] ") : "", irc_tr("Settings | Configuration"), s->changed ? " *" : "");
    text(2, 0, COLS, line, A_BOLD);
    if (!s->new_server) {
        snprintf(line, sizeof line, "%s %s", irc_tr("Config:"), *path ? path : irc_tr("(not saved)"));
        text(3, 0, COLS, line, A_DIM);
    } else if (a->cfg.hints) text(3, 0, COLS, irc_tr("Add: disconnected | Connect: start now"), A_DIM);
    size_t visible = (size_t)(LINES - 8), count = field_count(s);
    if (s->selected >= count) s->selected = count - 1;
    if (s->selected < s->first) s->first = s->selected;
    if (s->selected >= s->first + visible) s->first = s->selected - visible + 1;
    for (size_t row = 0; row < visible && s->first + row < count; ++row) {
        size_t index = field_index(s, s->first + row);
        char label[320], value[CONFIG_PATH_MAX + 32], safe[sizeof line];
        field_label(s, index, label, sizeof label);
        field_value(s, index, value, sizeof value);
        snprintf(line, sizeof line, "%s: %s", label, *value ? value : irc_tr("(empty)"));
        irc_sanitize(safe, sizeof safe, line);
        focus_text(a, text, 4 + (int)row, 0, COLS, safe, s->first + row == s->selected);
    }
    const char *hint = field_display_hint(s, field_index(s, s->selected));
    if (a->cfg.hints) text(LINES - 4, 0, COLS, hint, A_DIM);
    /* A blank row separates field help from actions. Scope and config path
     * stay beside the heading, rather than repeating instructions below. */
    draw_actions(s, a, text);
    text(LINES - 1, 0, COLS, s->message, A_NORMAL);
    (void)curs_set(0);
    move(0, 0);
    if (s->editing) draw_field_editor(s, a, text);
    else if (s->choosing) {
        char label[320];
        size_t index = field_index(s, s->selected);
        const Field *field = &fields[index];
        int options = field->high - field->low + 1;
        int hints = a->cfg.hints && options + 4 <= LINES - 2 ? 2 : 0;
        int height = options + hints + 2;
        field_label(s, index, label, sizeof label);
        s->choice_width = COLS > 46 ? 44 : COLS - 2;
        s->choice_left = (COLS - s->choice_width) / 2;
        /* Rows zero and one belong to the shared header and divider, which
         * are painted after this form. Keep every popup below both rows,
         * including all five options at the supported 9 by 20 minimum. */
        s->choice_top = 2 + (LINES - 2 - height) / 2;
        /* A bounded reverse-video surface separates the chooser from the
         * form behind it, including on monochrome and remote terminals. */
        attr_t backdrop = A_REVERSE;
        for (int row = 0; row < height; ++row) {
            (void)attr_set(backdrop, 0, NULL);
            (void)mvhline(s->choice_top + row, s->choice_left, ' ', s->choice_width);
        }
        (void)attr_set(backdrop | A_BOLD, 0, NULL);
        (void)mvhline(s->choice_top, s->choice_left + 1, ACS_HLINE, s->choice_width - 2);
        (void)mvhline(s->choice_top + height - 1, s->choice_left + 1, ACS_HLINE, s->choice_width - 2);
        (void)mvvline(s->choice_top + 1, s->choice_left, ACS_VLINE, height - 2);
        (void)mvvline(s->choice_top + 1, s->choice_left + s->choice_width - 1, ACS_VLINE, height - 2);
        (void)mvaddch(s->choice_top, s->choice_left, ACS_ULCORNER);
        (void)mvaddch(s->choice_top, s->choice_left + s->choice_width - 1, ACS_URCORNER);
        (void)mvaddch(s->choice_top + height - 1, s->choice_left, ACS_LLCORNER);
        (void)mvaddch(s->choice_top + height - 1, s->choice_left + s->choice_width - 1, ACS_LRCORNER);
        text(s->choice_top, s->choice_left + 2, s->choice_width - 4,
             s->choice_width >= 25 ? label : field->offset == offsetof(Config, command_suggestions) ?
             irc_tr("Command suggestions") : field->offset == offsetof(Config, bouncer_type) ? irc_tr("Connection") :
             field->offset == offsetof(Config, default_interface) ? irc_tr("Interface") : irc_tr("Auto-auth"), backdrop | A_BOLD);
        for (int option = field->low; option <= field->high; ++option) {
            static const char *const short_modes[] = {"Alphabetic", "Common", "History", "Most used", "Contextual"};
            const char *name = field->offset == offsetof(Config, command_suggestions) && s->choice_width < 25 ?
                               irc_tr(short_modes[option]) : irc_tr(field->choices[option]);
            snprintf(line, sizeof line, "(%c) %s", s->choice == option ? '*' : ' ', name);
            attr_t selected = s->choice == option ? vipirc_focus_attrs() | A_UNDERLINE : backdrop;
            text(s->choice_top + option - field->low + 1, s->choice_left + 2, s->choice_width - 4, line, selected);
        }
        if (hints) {
            text(s->choice_top + options + 1, s->choice_left + 2, s->choice_width - 4, irc_tr("Enter: choose"), backdrop | A_DIM);
            text(s->choice_top + options + 2, s->choice_left + 2, s->choice_width - 4, irc_tr("Esc: vipirc"), backdrop | A_DIM);
        }
        attrset(A_NORMAL);
    }
    wnoutrefresh(stdscr);
}

#if defined(KEY_MOUSE) && defined(BUTTON1_PRESSED)
void settings_mouse(Settings *s, App *a, const MEVENT *event)
{
    if (!settings_active(s) || s->paste || LINES < 9 || COLS < 20 ||
        event->x < 0 || event->x >= COLS || event->y < 0 || event->y >= LINES) return;
    int direction = 0;
#ifdef BUTTON4_PRESSED
    if (event->bstate & BUTTON4_PRESSED) direction = -1;
#endif
#ifdef BUTTON5_PRESSED
    if (event->bstate & BUTTON5_PRESSED) direction = 1;
#endif
    a->dirty = 1;
    if (s->home) {
        if (direction < 0) s->selected = s->selected > 3 ? s->selected - 3 : 0;
        else if (direction > 0) s->selected = s->selected + 3 < s->home_count ? s->selected + 3 : s->home_count - 1;
        else if (event->bstate & BUTTON1_PRESSED) {
            if (event->y >= 4 && event->y < LINES - 3) {
                size_t row = s->first + (size_t)(event->y - 4);
                if (row < s->home_count) { s->selected = row; open_home_selection(s); }
            } else if (event->y == LINES - 2 && event->x < irc_label_cells(irc_tr("Open")) + 2) open_home_selection(s);
        }
        return;
    }
    if (s->editing) {
        if ((event->bstate & BUTTON1_PRESSED) && event->y >= s->edit_top &&
            event->y < s->edit_top + s->edit_rows && event->x >= s->edit_left &&
            event->x < s->edit_left + s->edit_width) {
            if (s->editing_field < BASE_FIELDS && fields[s->editing_field].kind == FIELD_SECRET)
                ed_set_cell(&s->editor, s->edit_scroll + event->y - s->edit_top,
                            event->x - s->edit_left, s->edit_width);
            else
                ed_visual_set_cell(&s->editor, s->edit_scroll + event->y - s->edit_top,
                                   event->x - s->edit_left, s->edit_width);
        }
        return;
    }
    if (s->choosing) {
        const Field *field = &fields[field_index(s, s->selected)];
        if (direction < 0 && s->choice > field->low) --s->choice;
        else if (direction > 0 && s->choice < field->high) ++s->choice;
        else if ((event->bstate & BUTTON1_PRESSED) && event->x > s->choice_left &&
                 event->x < s->choice_left + s->choice_width - 1 &&
                 event->y > s->choice_top && event->y < s->choice_top + field->high - field->low + 2) {
            s->choice = field->low + event->y - s->choice_top - 1;
            settings_key(s, a, '\r', 0);
        }
        return;
    }
    size_t count = field_count(s);
    if (direction < 0) s->selected = s->selected > 3 ? s->selected - 3 : 0;
    else if (direction > 0) s->selected = s->selected + 3 < count ? s->selected + 3 : count - 1;
    else if (event->bstate & BUTTON1_PRESSED) {
        if (event->y >= 4 && event->y < LINES - 4) {
            size_t index = s->first + (size_t)(event->y - 4);
            if (index < count) { s->selected = index; s->action = FORM_EDIT; edit_field(s); }
        } else if (event->y == LINES - 2) {
            if (s->compact_actions && event->x == 0) s->action = s->action ? (FormAction)(s->action - 1) : (FormAction)(action_count(s) - 1);
            else if (s->compact_actions && event->x == COLS - 1) s->action = (FormAction)((s->action + 1u) % (unsigned)action_count(s));
            else for (int action = 0; action < action_count(s); ++action)
                if (s->action_x[action] >= 0 && event->x >= s->action_x[action] &&
                    event->x < s->action_x[action] + s->action_width[action]) {
                    s->action = (FormAction)action;
                    perform_action(s, a);
                    break;
                }
        }
    }
}
#endif
