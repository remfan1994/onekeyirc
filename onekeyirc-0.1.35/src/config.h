#ifndef VIPIRC_CONFIG_H
#define VIPIRC_CONFIG_H

#include <stddef.h>

#define CONFIG_PATH_MAX 4096
#define CONFIG_WORD_MAX 32
#define CONFIG_WORD_SIZE 128
#define CONFIG_IGNORE_MAX 32
#define CONFIG_IGNORE_SIZE 128
#define CONFIG_SECRET_MAX 1024
#define CONFIG_FOCUS_REVERSE 0
#define CONFIG_FOCUS_BRACKETS 1
#define CONFIG_PROFILE_MAX 7
#define CONFIG_PROFILE_ID_SIZE 64
#define CONFIG_AUTOCOMMAND_MAX 16
#define CONFIG_AUTOCOMMAND_SIZE 512
#define CONFIG_COMMAND_USAGE_MAX 96u
#define CONFIG_COMMAND_NAME_SIZE 24u
#define CONFIG_COMMAND_USAGE_LIMIT 2147483647u
#define CONFIG_HOTKEY_MAX 64u
#define CONFIG_HOTKEY_NAME_SIZE 24u

/* Terminal-independent, canonical key identities. Ordinary printable keys
 * are deliberately not bindings: they remain text in the editor. */
typedef enum ConfigHotkeyKey {
    CONFIG_KEY_UP = 0x120000, CONFIG_KEY_DOWN, CONFIG_KEY_LEFT, CONFIG_KEY_RIGHT,
    CONFIG_KEY_HOME, CONFIG_KEY_END, CONFIG_KEY_PAGE_UP, CONFIG_KEY_PAGE_DOWN,
    CONFIG_KEY_INSERT, CONFIG_KEY_DELETE, CONFIG_KEY_BACKSPACE, CONFIG_KEY_TAB,
    CONFIG_KEY_SHIFT_TAB, CONFIG_KEY_ENTER, CONFIG_KEY_ESCAPE,
    CONFIG_KEY_F1 = 0x120100,
    CONFIG_KEY_ALT = 0x130000
} ConfigHotkeyKey;

typedef enum ConfigHotkeyActionId {
    CONFIG_ACTION_NONE = 0, CONFIG_ACTION_COMPOSE, CONFIG_ACTION_COMMAND,
    CONFIG_ACTION_BINDINGS, CONFIG_ACTION_HELP, CONFIG_ACTION_MANUAL,
    CONFIG_ACTION_SUMMARY, CONFIG_ACTION_SERVERS, CONFIG_ACTION_CHANNELS,
    CONFIG_ACTION_QUERIES, CONFIG_ACTION_UNREAD, CONFIG_ACTION_MENTIONS,
    CONFIG_ACTION_TRANSFERS, CONFIG_ACTION_SETTINGS, CONFIG_ACTION_QUICKSTART,
    CONFIG_ACTION_NEXT, CONFIG_ACTION_PREV, CONFIG_ACTION_CLEARINPUT,
    CONFIG_ACTION_CANCEL, CONFIG_ACTION_BACKGROUND, CONFIG_ACTION_QUIT
} ConfigHotkeyActionId;

typedef struct ConfigHotkey { unsigned key; int action; } ConfigHotkey;
typedef struct ConfigHotkeyAction {
    int id;
    const char *name, *description, *command;
} ConfigHotkeyAction;

typedef enum ConfigSuggestMode {
    CONFIG_SUGGEST_ALPHABETICAL = 0, CONFIG_SUGGEST_COMMON,
    CONFIG_SUGGEST_HISTORY, CONFIG_SUGGEST_FREQUENT,
    CONFIG_SUGGEST_CONTEXTUAL
} ConfigSuggestMode;

typedef enum ConfigInterface {
    CONFIG_INTERFACE_NCURSES = 0, CONFIG_INTERFACE_IDLE = 1
} ConfigInterface;

typedef enum ConfigBouncer {
    CONFIG_BOUNCER_NONE = 0, CONFIG_BOUNCER_ZNC, CONFIG_BOUNCER_SOJU
} ConfigBouncer;

typedef struct ConfigCommandUsage {
    char name[CONFIG_COMMAND_NAME_SIZE];
    unsigned count;
} ConfigCommandUsage;

typedef struct ConfigAutocommands {
    int enabled, delay_seconds, interval_seconds;
    size_t count;
    char lines[CONFIG_AUTOCOMMAND_MAX][CONFIG_AUTOCOMMAND_SIZE];
} ConfigAutocommands;

typedef enum ConfigAutoKind {
    CONFIG_AUTO_MSG, CONFIG_AUTO_JOIN, CONFIG_AUTO_NICK,
    CONFIG_AUTO_AWAY, CONFIG_AUTO_BACK, CONFIG_AUTO_WHOIS
} ConfigAutoKind;
typedef struct ConfigAutoCommand {
    ConfigAutoKind kind;
    char target[201], text[CONFIG_AUTOCOMMAND_SIZE];
} ConfigAutoCommand;

/* Validate one bounded network command without execution or side effects.
 * 1=valid, -1=error. out may be NULL; failed outputs are wiped. Errors never
 * include any command argument. Callers must wipe successful secret copies. */
int config_autocommand_parse(const char *, ConfigAutoCommand *, char *error, size_t error_cap);

typedef struct ConfigProfiles {
    char ids[CONFIG_PROFILE_MAX][CONFIG_PROFILE_ID_SIZE];
    size_t count;
    /* Empty selects the primary configuration; otherwise a listed stable ID. */
    char default_id[CONFIG_PROFILE_ID_SIZE];
} ConfigProfiles;

/* Configuration is an owned value, including bounded arrays and inline
 * secret storage. Copies do not share allocations; wipe temporary copies when
 * finished. Loading, validation and explicit persistence are separate APIs. */
typedef struct Config {
    char server[256];
    int port, tls;
    char nick[64], username[64], realname[128], channels[1024];
    int autoconnect;
    char auth[8], auth_user[256];
    char password_file[CONFIG_PATH_MAX];
    char password_gpg_file[CONFIG_PATH_MAX];
    char password[CONFIG_SECRET_MAX + 1];
    char server_password_file[CONFIG_PATH_MAX];
    char server_password_gpg_file[CONFIG_PATH_MAX];
    char server_password[CONFIG_SECRET_MAX + 1];
    char ca_file[CONFIG_PATH_MAX];
    /* One upstream network per profile. Credentials keep their existing
     * loaders; only the login identity is assembled from these fields. */
    int bouncer_type, bouncer_away;
    char bouncer_user[64], bouncer_network[64], bouncer_client[64];
    int user_list, mouse, hints, shortcut_labels, focus_style, history_lines;
    int send_review, output_full, queue_notices, program_messages;
    int persistent_logging, single_session;
    int background_away, autoaway_minutes;
    char part_message[256], quit_message[256];
    char background_away_message[256], autoaway_message[256];
    /* Application-wide next-launch preference, owned by the primary config.
     * Explicit frontend flags do not change this saved choice. */
    int default_interface;
    int command_suggestions;
    /* Minimalist composer/command presentation: 0=line below live tail,
     * 1=multi-line editor with a smaller live-tail pane. */
    int composer_fullscreen;
    /* Primary-owned terminal policy. Default 0 preserves inherited ISIG;
     * 1 enables it without assigning interrupt/suspend/quit characters. */
    int force_terminal_signals;
    /* Empty by default. Primary-owned local actions, never shell commands,
     * network text, or credentials. Frontends exclude paste and modal input. */
    ConfigHotkey hotkeys[CONFIG_HOTKEY_MAX];
    size_t hotkey_count;
    /* Canonical command names only. Arguments and command history are never
     * persisted. A private per-config .commands sidecar retains the counts. */
    ConfigCommandUsage command_usage[CONFIG_COMMAND_USAGE_MAX];
    size_t command_usage_count;
    ConfigAutocommands autocommands;
    /* Writer/parser namespace; never loaded from a user-controlled key. */
    int minimal;
    char words[CONFIG_WORD_MAX][CONFIG_WORD_SIZE];
    size_t word_count;
    char ignores[CONFIG_IGNORE_MAX][CONFIG_IGNORE_SIZE];
    size_t ignore_count;
} Config;

/* Names are ASCII and case-insensitive. Ctrl-H/I/J/M/[ normalize to their
 * terminal aliases Backspace/Tab/Enter/Escape. Zero means an invalid key. */
unsigned config_hotkey_key(const char *name);
int config_hotkey_key_name(unsigned key, char *out, size_t cap);
size_t config_hotkey_action_count(void);
const ConfigHotkeyAction *config_hotkey_action_at(size_t index);
const ConfigHotkeyAction *config_hotkey_action(int id);
const ConfigHotkeyAction *config_hotkey_action_find(const char *name);
int config_hotkey_lookup(const Config *, unsigned key);
/* Replace/add a binding, or remove it with CONFIG_ACTION_NONE. Mutations
 * are bounded and transactional; no disk writes. 1=success, -1=invalid. */
int config_hotkey_set(Config *, unsigned key, int action, char *error, size_t cap);

void config_defaults(Config *config);
void config_minimal_defaults(Config *config);
int config_default_path(char *out, size_t cap, char *error, size_t error_cap);
int config_minimal_path(char *out, size_t cap, char *error, size_t error_cap);
/* Load overlays the caller's defaults atomically. 1=loaded, 0=missing, -1=error. */
int config_load(Config *config, const char *path, char *error, size_t error_cap);
/* Save replaces a file atomically with 0600 permissions in a private
 * parent. 1=success, -1=error; a directory-sync error can follow replacement,
 * so callers must not interpret every error as "the old file is unchanged". */
int config_save(const Config *config, const char *path, char *error, size_t error_cap);
/* Publish a complete private configuration only if the destination is absent.
 * Unlike Save, this never replaces any existing object, including one that
 * appears after the initial check. A reported sync/cleanup failure may leave
 * the explicitly reported completed configuration in place. */
int config_create(const Config *config, const char *path, char *error, size_t error_cap);
/* Validate the complete value without opening files or connecting.
 * 1=valid, -1=invalid. This does not establish remote account validity. */
int config_validate(const Config *config, char *error, size_t error_cap);
/* Format the bouncer account/network identity, never its password.
 * 1=valid identity, 0=direct connection, -1=invalid or insufficient output.
 * The output is empty on failure. Input components use ASCII letters,
 * digits, '.', '_' and '-'; account and network are required. */
int config_bouncer_identity(const Config *, char *out, size_t cap);
/* Open CONFIG_PATH.log for append, traversing no symlink components and
 * requiring an owned parent with exact mode 0700. Never creates directories.
 * The returned descriptor is nonblocking and close-on-exec; the caller must
 * verify the opened file's type, ownership, link count and mode before writing.
 * Returns a descriptor, or -1 with a diagnostic. */
int config_open_log(const char *config_path, char *error, size_t error_cap);
/* Open the same private sidecar read-only for bounded catch-up browsing.
 * The caller still validates size/snapshot semantics; unsafe paths are refused. */
int config_open_log_read(const char *config_path, char *error, size_t error_cap);
/* The caller supplies the authoritative config path, not a sidecar path.
 * Empty paths do nothing. Loading is transactional; missing file returns 0.
 * Never call these for offline/test-mode sessions. */
int config_command_usage_load(Config *, const char *config_path, char *error, size_t error_cap);
int config_command_usage_save(const Config *, const char *config_path, char *error, size_t error_cap);
/* Secret contents are not interpreted as shell commands or environment names. */
int config_read_secret(const char *path, char *out, size_t cap,
                       char *error, size_t error_cap);
/* Open a bounded, owned 0600/0400 ciphertext file without following symlinks.
 * Caller owns the returned close-on-exec descriptor; -1 means failure. */
int config_open_gpg_file(const char *path, char *error, size_t error_cap);
void config_wipe(void *p, size_t n);

/* One primary config owns an adjacent .servers catalog and .d directory.
 * IDs are safe basenames, never paths. Load returns 1 for a valid catalog,
 * 0 for absence (and clears the output), or -1 without replacing the output. */
int config_profiles_load(ConfigProfiles *, const char *primary,
                         char *error, size_t error_cap);
int config_profile_path(char *out, size_t cap, const char *primary,
                        const char *id, char *error, size_t error_cap);
int config_profiles_check(const char *primary, char *error, size_t error_cap);
int config_minimal_profiles_check(const char *primary, char *error, size_t error_cap);
/* Explicit save only. An empty id receives a fresh stable ID; preserve it
 * on failure so retries cannot create duplicate catalog entries. committed
 * reports that catalog discovery succeeded even if final fsync failed. */
int config_profile_save(const Config *, const char *primary,
                        char id[CONFIG_PROFILE_ID_SIZE], int *committed,
                        char *error, size_t error_cap);
/* Record the profile selected on the next startup. Empty id selects primary.
 * The id must already be listed. An unchanged selection writes nothing. */
int config_profile_set_default(const char *primary, const char *id, int *committed,
                               char *error, size_t error_cap);
/* Removing a catalog reference never deletes its physical profile file. */
int config_profile_remove(const char *primary, const char *id, int *committed,
                          char *error, size_t error_cap);

#endif
