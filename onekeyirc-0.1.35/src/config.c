/* Configuration values, validation and private-file persistence.
 *
 * The file is organized as key/action names and defaults, descriptor-based
 * path/reader helpers, INI parsing and validation, then explicit writers,
 * profile catalogs and credential-source access. Config is an owned value;
 * candidate copies let parsing fail without changing a live configuration.
 * Paths and values are literal: this module performs no shell expansion.
 */
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include "protocol.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_FILE_MAX 65536
_Static_assert(CONFIG_IGNORE_SIZE > IRC_IGNORE_MASK_MAX, "Ignore storage needs a terminating NUL");

static int problem(char *error, size_t cap, const char *fmt, ...)
{
    va_list ap;
    if (error && cap) {
        va_start(ap, fmt);
        (void)vsnprintf(error, cap, fmt, ap);
        va_end(ap);
    }
    return -1;
}

void config_wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n--)
        *v++ = 0;
}

/* This allowlist describes local program functions, not a command language.
 * Persisting only these names prevents a shortcut file from carrying IRC
 * messages, credentials, shell commands, or a confirmation bypass. */
static const ConfigHotkeyAction hotkey_actions[] = {
    {CONFIG_ACTION_COMPOSE, "compose", "Focus the chat composer", NULL},
    {CONFIG_ACTION_COMMAND, "command", "Open the command prompt", NULL},
    {CONFIG_ACTION_BINDINGS, "bindings", "Open or list custom key bindings", "/bindings"},
    {CONFIG_ACTION_HELP, "help", "Open help for the current screen", NULL},
    {CONFIG_ACTION_MANUAL, "manual", "Read the full manual", "/help all"},
    {CONFIG_ACTION_SUMMARY, "summary", "Review all server activity", "/summary"},
    {CONFIG_ACTION_SERVERS, "servers", "List server connections", "/servers"},
    {CONFIG_ACTION_CHANNELS, "channels", "List channels on this server", "/channels"},
    {CONFIG_ACTION_QUERIES, "queries", "List private conversations", "/queries"},
    {CONFIG_ACTION_UNREAD, "unread", "List unread conversations", "/unread"},
    {CONFIG_ACTION_MENTIONS, "mentions", "Review retained mentions", "/mentions"},
    {CONFIG_ACTION_TRANSFERS, "transfers", "Review file transfers", "/transfers"},
    {CONFIG_ACTION_SETTINGS, "settings", "Open application settings", "/panel"},
    {CONFIG_ACTION_QUICKSTART, "quickstart", "Open connection setup", "/quickstart"},
    {CONFIG_ACTION_NEXT, "next", "Select the next conversation", "/next"},
    {CONFIG_ACTION_PREV, "prev", "Select the previous conversation", "/prev"},
    {CONFIG_ACTION_CLEARINPUT, "clearinput", "Clear the current input draft", NULL},
    {CONFIG_ACTION_CANCEL, "cancel", "Cancel this server's unsent queue", "/cancel"},
    {CONFIG_ACTION_BACKGROUND, "background", "Detach this terminal", "/background"},
    {CONFIG_ACTION_QUIT, "quit", "Quit all server sessions", "/quit"}
};

static const char *const hotkey_names[] = {
    "Up", "Down", "Left", "Right", "Home", "End", "PageUp", "PageDown",
    "Insert", "Delete", "Backspace", "Tab", "Shift-Tab", "Enter", "Escape"
};

static unsigned hotkey_normalize(unsigned key)
{
    switch (key) {
    case 8: case 127: return CONFIG_KEY_BACKSPACE;
    case 9: return CONFIG_KEY_TAB;
    case 10: case 13: return CONFIG_KEY_ENTER;
    case 27: return CONFIG_KEY_ESCAPE;
    default: return key;
    }
}

static int hotkey_equal(const char *a, const char *b)
{
    if (!a || !b) return 0;
    for (; *a && *b; ++a, ++b) {
        unsigned char x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 'a' - 'A');
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 'a' - 'A');
        if (x != y) return 0;
    }
    return *a == *b;
}

unsigned config_hotkey_key(const char *name)
{
    if (!name) return 0;
    size_t length = strnlen(name, CONFIG_HOTKEY_NAME_SIZE);
    if (!length || length >= CONFIG_HOTKEY_NAME_SIZE) return 0;
    char lower[CONFIG_HOTKEY_NAME_SIZE];
    for (size_t i = 0; i <= length; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch + 'a' - 'A');
        lower[i] = (char)ch;
    }
    if (length == 6 && !memcmp(lower, "ctrl-", 5)) {
        if (lower[5] >= 'a' && lower[5] <= 'z')
            return hotkey_normalize((unsigned)(lower[5] - 'a' + 1));
        if (lower[5] == '[') return CONFIG_KEY_ESCAPE;
    }
    if (length == 5 && !memcmp(lower, "alt-", 4) &&
        ((lower[4] >= 'a' && lower[4] <= 'z') || (lower[4] >= '0' && lower[4] <= '9')))
        return CONFIG_KEY_ALT + (unsigned char)lower[4];
    if (lower[0] == 'f' && length >= 2 && length <= 3 && lower[1] >= '1' && lower[1] <= '9') {
        unsigned number = (unsigned)(lower[1] - '0');
        if (length == 3) {
            if (lower[2] < '0' || lower[2] > '9') return 0;
            number = number * 10u + (unsigned)(lower[2] - '0');
        }
        if (number <= 12) return CONFIG_KEY_F1 + number - 1u;
    }
    for (size_t i = 0; i < sizeof hotkey_names / sizeof hotkey_names[0]; ++i)
        if (hotkey_equal(lower, hotkey_names[i])) return CONFIG_KEY_UP + (unsigned)i;
    if (!strcmp(lower, "esc")) return CONFIG_KEY_ESCAPE;
    if (!strcmp(lower, "return")) return CONFIG_KEY_ENTER;
    if (!strcmp(lower, "pgup")) return CONFIG_KEY_PAGE_UP;
    if (!strcmp(lower, "pgdn")) return CONFIG_KEY_PAGE_DOWN;
    return 0;
}

int config_hotkey_key_name(unsigned key, char *out, size_t cap)
{
    char name[CONFIG_HOTKEY_NAME_SIZE];
    key = hotkey_normalize(key);
    if (out && cap) out[0] = 0;
    if (!out || !cap) return 0;
    if (key >= CONFIG_KEY_UP && key <= CONFIG_KEY_ESCAPE)
        (void)snprintf(name, sizeof name, "%s", hotkey_names[key - CONFIG_KEY_UP]);
    else if (key >= CONFIG_KEY_F1 && key < CONFIG_KEY_F1 + 12u)
        (void)snprintf(name, sizeof name, "F%u", key - CONFIG_KEY_F1 + 1u);
    else if (key >= 1 && key <= 26)
        (void)snprintf(name, sizeof name, "Ctrl-%c", (int)('A' + key - 1u));
    else if ((key >= CONFIG_KEY_ALT + 'a' && key <= CONFIG_KEY_ALT + 'z') ||
             (key >= CONFIG_KEY_ALT + '0' && key <= CONFIG_KEY_ALT + '9'))
        (void)snprintf(name, sizeof name, "Alt-%c", (int)(key - CONFIG_KEY_ALT));
    else return 0;
    size_t length = strlen(name);
    if (length >= cap) return 0;
    memcpy(out, name, length + 1u);
    return 1;
}

size_t config_hotkey_action_count(void)
{
    return sizeof hotkey_actions / sizeof hotkey_actions[0];
}

const ConfigHotkeyAction *config_hotkey_action_at(size_t index)
{
    return index < config_hotkey_action_count() ? &hotkey_actions[index] : NULL;
}

const ConfigHotkeyAction *config_hotkey_action(int id)
{
    for (size_t i = 0; i < config_hotkey_action_count(); ++i)
        if (hotkey_actions[i].id == id) return &hotkey_actions[i];
    return NULL;
}

const ConfigHotkeyAction *config_hotkey_action_find(const char *name)
{
    if (!name || strnlen(name, CONFIG_HOTKEY_NAME_SIZE) >= CONFIG_HOTKEY_NAME_SIZE) return NULL;
    for (size_t i = 0; i < config_hotkey_action_count(); ++i)
        if (hotkey_equal(hotkey_actions[i].name, name)) return &hotkey_actions[i];
    return NULL;
}

int config_hotkey_lookup(const Config *c, unsigned key)
{
    if (!c || c->hotkey_count > CONFIG_HOTKEY_MAX) return CONFIG_ACTION_NONE;
    key = hotkey_normalize(key);
    for (size_t i = 0; i < c->hotkey_count; ++i)
        if (c->hotkeys[i].key == key && config_hotkey_action(c->hotkeys[i].action))
            return c->hotkeys[i].action;
    return CONFIG_ACTION_NONE;
}

static int hotkeys_validate(const Config *c, char *error, size_t cap)
{
    char name[CONFIG_HOTKEY_NAME_SIZE];
    if (c->hotkey_count > CONFIG_HOTKEY_MAX)
        return problem(error, cap, "At most %u custom key bindings are allowed", CONFIG_HOTKEY_MAX);
    for (size_t i = 0; i < c->hotkey_count; ++i) {
        const ConfigHotkey *b = &c->hotkeys[i];
        if (!config_hotkey_key_name(b->key, name, sizeof name) ||
            config_hotkey_key(name) != b->key || !config_hotkey_action(b->action))
            return problem(error, cap, "Invalid custom key binding");
        for (size_t j = 0; j < i; ++j)
            if (c->hotkeys[j].key == b->key)
                return problem(error, cap, "Duplicate custom key binding");
    }
    return 1;
}

/* The key/action table is purely user preference.
 * No binding is installed implicitly; validation and canonical key names
 * must agree with both frontends and the serialized [hotkeys] section. */
int config_hotkey_set(Config *c, unsigned key, int action, char *error, size_t cap)
{
    char name[CONFIG_HOTKEY_NAME_SIZE];
    key = hotkey_normalize(key);
    if (!c || !config_hotkey_key_name(key, name, sizeof name) ||
        (action != CONFIG_ACTION_NONE && !config_hotkey_action(action)))
        return problem(error, cap, "Unknown key or action; use /bindings keys or /bindings actions");
    if (hotkeys_validate(c, error, cap) < 0) return -1;
    size_t index = 0;
    while (index < c->hotkey_count && c->hotkeys[index].key != key) ++index;
    if (action == CONFIG_ACTION_NONE) {
        if (index < c->hotkey_count) {
            --c->hotkey_count;
            memmove(&c->hotkeys[index], &c->hotkeys[index + 1u],
                    (c->hotkey_count - index) * sizeof c->hotkeys[0]);
            memset(&c->hotkeys[c->hotkey_count], 0, sizeof c->hotkeys[0]);
        }
    } else {
        if (index == CONFIG_HOTKEY_MAX)
            return problem(error, cap, "At most %u custom key bindings are allowed", CONFIG_HOTKEY_MAX);
        c->hotkeys[index] = (ConfigHotkey){key, action};
        if (index == c->hotkey_count) ++c->hotkey_count;
    }
    if (error && cap) error[0] = 0;
    return 1;
}

void config_defaults(Config *c)
{
    memset(c, 0, sizeof *c);
    c->port = 6697;
    c->tls = 1;
    c->user_list = 1;
    c->mouse = 1;
    c->hints = 1;
    c->shortcut_labels = 1;
    c->focus_style = CONFIG_FOCUS_REVERSE;
    c->history_lines = 500;
    c->queue_notices = 1;
    c->program_messages = 1;
    c->background_away = 1;
    c->autoaway_minutes = 15;
    (void)snprintf(c->quit_message, sizeof c->quit_message, "Client quit");
    (void)snprintf(c->background_away_message, sizeof c->background_away_message, "Detached from terminal");
    (void)snprintf(c->autoaway_message, sizeof c->autoaway_message, "Idle");
    c->default_interface = CONFIG_INTERFACE_NCURSES;
    c->command_suggestions = CONFIG_SUGGEST_COMMON;
    c->autocommands.enabled = 1;
    c->autocommands.delay_seconds = 15;
    c->autocommands.interval_seconds = 2;
    (void)snprintf(c->auth, sizeof c->auth, "none");
    (void)snprintf(c->realname, sizeof c->realname, "vipirc user");
}

void config_minimal_defaults(Config *c)
{
    config_defaults(c);
    c->minimal = 1;
    c->default_interface = CONFIG_INTERFACE_IDLE;
    c->history_lines = 100;
    c->mouse = c->user_list = 0;
    (void)snprintf(c->realname, sizeof c->realname, "onekeyirc user");
}

static int default_path(char *out, size_t cap, char *error, size_t error_cap,
                        const char *program)
{
    const char *base = getenv("XDG_CONFIG_HOME");
    int length;
    if (base && *base) {
        if (*base != '/')
            return problem(error, error_cap, "XDG_CONFIG_HOME must be absolute");
        length = snprintf(out, cap, "%s/%s/config.ini", base, program);
    } else {
        const char *home = getenv("HOME");
        struct passwd *pw;
        if (!home || *home != '/') {
            pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : NULL;
        }
        if (!home || *home != '/')
            return problem(error, error_cap, "Cannot determine an absolute home directory");
        length = snprintf(out, cap, "%s/.config/%s/config.ini", home, program);
    }
    if (length < 0 || (size_t)length >= cap)
        return problem(error, error_cap, "Configuration path is too long");
    return 1;
}

int config_default_path(char *out, size_t cap, char *error, size_t error_cap)
{
    return default_path(out, cap, error, error_cap, "vipirc");
}

/* Walk pinned directory descriptors without following intermediate or leaf
 * symlinks. The returned descriptor belongs to the caller; leaf is the final
 * basename for an *at operation.
 * create only permits missing directories; it does not relax existing-path
 * checks. Callers with exact-mode requirements must perform those separately. */
static int parent_dir(const char *path, int create, int private_final,
                      char *leaf, size_t leaf_cap, char *error, size_t error_cap)
{
    char copy[CONFIG_PATH_MAX], *last, *part, *save = NULL;
    int fd;
    struct stat st;
    if (!path || !*path || strlen(path) >= sizeof copy) {
        errno = EINVAL;
        return problem(error, error_cap, "Invalid or overlong file path");
    }
    memcpy(copy, path, strlen(path) + 1);
    last = strrchr(copy, '/');
    if (last) {
        *last++ = '\0';
    } else {
        last = copy;
    }
    if (!*last || strcmp(last, ".") == 0 || strcmp(last, "..") == 0 || strlen(last) >= leaf_cap) {
        errno = EINVAL;
        return problem(error, error_cap, "Invalid file name");
    }
    memcpy(leaf, last, strlen(last) + 1);
    fd = open(path[0] == '/' ? "/" : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return problem(error, error_cap, "Cannot open file directory: %s", strerror(errno));
    if (last != copy) {
        for (part = strtok_r(copy, "/", &save); part; part = strtok_r(NULL, "/", &save)) {
            int next;
            if (strcmp(part, ".") == 0)
                continue;
            if (strcmp(part, "..") == 0) {
                close(fd);
                errno = EINVAL;
                return problem(error, error_cap, "Parent traversal (..) is not allowed in file paths");
            }
            next = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0 && errno == ENOENT && create) {
                if (mkdirat(fd, part, 0700) == 0 || errno == EEXIST)
                    next = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            }
            if (next < 0) {
                int saved_errno = errno;
                close(fd);
                errno = saved_errno;
                return problem(error, error_cap, "Cannot open file directory (symlinks are refused): %s", strerror(saved_errno));
            }
            close(fd);
            fd = next;
        }
    }
    if (private_final && (fstat(fd, &st) != 0 || st.st_uid != getuid() || (st.st_mode & 077) != 0)) {
        close(fd);
        return problem(error, error_cap, "Configuration directory must belong to you and have mode 0700");
    }
    return fd;
}

int config_open_log(const char *config_path, char *error, size_t error_cap)
{
    char leaf[256], name[256];
    int dir = parent_dir(config_path, 0, 1, leaf, sizeof leaf, error, error_cap);
    if (dir < 0) return -1;
    struct stat st;
    if (fstat(dir, &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() ||
        (st.st_mode & 07777) != 0700) {
        close(dir);
        errno = EACCES;
        return problem(error, error_cap, "Log directory must belong to you and have exact mode 0700");
    }
    int length = snprintf(name, sizeof name, "%s.log", leaf);
    if (length < 0 || (size_t)length >= sizeof name) {
        close(dir);
        errno = ENAMETOOLONG;
        return problem(error, error_cap, "Log filename exceeds its byte limit");
    }
    /* O_NONBLOCK prevents a substituted FIFO from hanging before the caller
     * can validate the actual opened descriptor. The directory is already
     * pinned, so a symlink in an ancestor cannot redirect this append. */
    int fd = openat(dir, name, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC |
                    O_NOFOLLOW | O_NONBLOCK, 0600);
    int saved = errno;
    close(dir);
    if (fd < 0) {
        errno = saved;
        return problem(error, error_cap, "Cannot open private log file: %s", strerror(saved));
    }
    return fd;
}

int config_open_log_read(const char *config_path, char *error, size_t error_cap)
{
    char leaf[256], name[256];
    int dir = parent_dir(config_path, 0, 1, leaf, sizeof leaf, error, error_cap);
    if (dir < 0) return -1;
    struct stat st;
    if (fstat(dir, &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() ||
        (st.st_mode & 07777) != 0700) {
        close(dir);
        errno = EACCES;
        return problem(error, error_cap, "Log directory must belong to you and have exact mode 0700");
    }
    int length = snprintf(name, sizeof name, "%s.log", leaf);
    if (length < 0 || (size_t)length >= sizeof name) {
        close(dir);
        errno = ENAMETOOLONG;
        return problem(error, error_cap, "Log filename exceeds its byte limit");
    }
    int fd = openat(dir, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    int saved = errno;
    close(dir);
    if (fd < 0) {
        errno = saved;
        return problem(error, error_cap, "Cannot open private log file: %s", strerror(saved));
    }
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
        st.st_nlink != 1 || (st.st_mode & 07777) != 0600 || st.st_size < 0 ||
        (uintmax_t)st.st_size > (16u * 1024u * 1024u)) {
        close(fd);
        errno = EACCES;
        return problem(error, error_cap, "Existing log file is not an owned 0600 regular file within the 16 MiB limit");
    }
    return fd;
}

/* Check only whether a candidate exists. Traverse through descriptors so a
 * missing target below a symlink, an inaccessible directory, or another path
 * error cannot masquerade as a missing default and choose a different config.
 * An existing leaf wins even when unsafe: config_load performs its usual
 * ownership, file-type, mode and syntax validation without a fallback. */
static int default_path_present(const char *path, char *error, size_t error_cap)
{
    char leaf[256];
    int dir = parent_dir(path, 0, 0, leaf, sizeof leaf, error, error_cap);
    if (dir < 0) return errno == ENOENT ? 0 : -1;
    struct stat st;
    int result = fstatat(dir, leaf, &st, AT_SYMLINK_NOFOLLOW);
    int saved_errno = errno;
    close(dir);
    if (result == 0) return 1;
    if (saved_errno == ENOENT) return 0;
    return problem(error, error_cap, "Cannot inspect configuration path: %s",
                   strerror(saved_errno));
}

int config_minimal_path(char *out, size_t cap, char *error, size_t error_cap)
{
    if (default_path(out, cap, error, error_cap, "onekeyirc") < 0) return -1;
    int present = default_path_present(out, error, error_cap);
    if (present < 0) return -1;
    if (present) return 1;

    /* Keep the legacy configuration and every adjacent profile/count/credential
     * file in place. Explicit --config never calls this default selector. */
    char legacy[CONFIG_PATH_MAX];
    if (default_path(legacy, sizeof legacy, error, error_cap, "1keyirc") < 0) return -1;
    present = default_path_present(legacy, error, error_cap);
    if (present < 0) return -1;
    if (present) {
        size_t length = strlen(legacy);
        if (length >= cap) return problem(error, error_cap, "Configuration path is too long");
        memcpy(out, legacy, length + 1);
    }
    if (error && error_cap) error[0] = '\0';
    return 1;
}

/* Open via a pinned parent directory and refuse
 * symlinks, non-regular files, unexpected owners and unsafe modes. The
 * result is 1=read, 0=missing, -1=error; secrets require stricter modes. */
static int read_private(const char *path, char *buf, size_t cap, int secret,
                        size_t *length, mode_t *permissions, char *error, size_t error_cap)
{
    char leaf[256];
    int dir, fd, result = -1;
    struct stat st;
    size_t used = 0;
    dir = parent_dir(path, 0, 0, leaf, sizeof leaf, error, error_cap);
    if (dir < 0)
        return errno == ENOENT ? 0 : -1;
    fd = openat(dir, leaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    close(dir);
    if (fd < 0) {
        if (errno == ENOENT)
            return 0;
        return problem(error, error_cap, "Cannot open private file (symlinks are refused): %s", strerror(errno));
    }
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
        (secret ? ((st.st_mode & 07777) != 0600 && (st.st_mode & 07777) != 0400) :
                  ((st.st_mode & 022) != 0))) {
        problem(error, error_cap, secret ? "Secret must be your regular file with mode 0600 or 0400" :
                "Configuration must be your regular file, not writable by other users");
        goto done;
    }
    if (st.st_size < 0 || (uintmax_t)st.st_size >= cap) {
        problem(error, error_cap, "Private file exceeds its size limit");
        goto done;
    }
    if (permissions) *permissions = st.st_mode & 07777;
    while (used < cap - 1) {
        ssize_t got = read(fd, buf + used, cap - 1 - used);
        if (got > 0) {
            used += (size_t)got;
        } else if (got == 0) {
            break;
        } else if (errno != EINTR) {
            problem(error, error_cap, "Cannot read private file: %s", strerror(errno));
            goto done;
        }
    }
    if (used == cap - 1) {
        unsigned char extra;
        ssize_t got;
        do { got = read(fd, &extra, 1); } while (got < 0 && errno == EINTR);
        if (got != 0) {
            problem(error, error_cap, "Private file exceeds its size limit");
            goto done;
        }
    }
    if (memchr(buf, '\0', used)) {
        problem(error, error_cap, "NUL bytes are not allowed in private text files");
        goto done;
    }
    buf[used] = '\0';
    *length = used;
    result = 1;
done:
    close(fd);
    if (result < 0)
        config_wipe(buf, cap);
    return result;
}

static char *trim(char *p)
{
    char *end;
    while (*p == ' ' || *p == '\t')
        ++p;
    end = p + strlen(p);
    while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        *--end = '\0';
    return p;
}

static int copy_value(char *dest, size_t cap, const char *value)
{
    size_t n = strlen(value);
    if (n >= cap)
        return 0;
    memcpy(dest, value, n + 1);
    return 1;
}

static int boolean(const char *s, int *out)
{
    if (strcmp(s, "true") == 0 || strcmp(s, "yes") == 0 || strcmp(s, "1") == 0)
        *out = 1;
    else if (strcmp(s, "false") == 0 || strcmp(s, "no") == 0 || strcmp(s, "0") == 0)
        *out = 0;
    else
        return 0;
    return 1;
}

static int number(const char *s, int low, int high, int *out)
{
    long n;
    char *end;
    if (!*s)
        return 0;
    errno = 0;
    n = strtol(s, &end, 10);
    if (errno || *end || n < low || n > high)
        return 0;
    *out = (int)n;
    return 1;
}

static const char *const suggestion_modes[] = {
    "alphabetical", "common", "history", "frequent", "contextual"
};

static int usage_name_valid(const char *name)
{
    size_t size = strnlen(name, CONFIG_COMMAND_NAME_SIZE);
    if (!size || size >= CONFIG_COMMAND_NAME_SIZE) return 0;
    for (size_t i = 0; i < size; ++i)
        if (name[i] < 'a' || name[i] > 'z') return 0;
    return 1;
}

static int usage_value(Config *c, const char *name, const char *value)
{
    int count;
    if (!usage_name_valid(name) || !number(value, 0, (int)CONFIG_COMMAND_USAGE_LIMIT, &count)) return 0;
    for (size_t i = 0; i < c->command_usage_count; ++i)
        if (!strcmp(c->command_usage[i].name, name)) return 0;
    if (c->command_usage_count >= CONFIG_COMMAND_USAGE_MAX) return 0;
    ConfigCommandUsage *entry = &c->command_usage[c->command_usage_count++];
    memcpy(entry->name, name, strlen(name) + 1u);
    entry->count = (unsigned)count;
    return 1;
}

static int set_value(Config *c, const char *section, const char *key, const char *value)
{
#define TEXT(k, field) if (strcmp(key, k) == 0) return copy_value(c->field, sizeof c->field, value)
#define SECRET(k, field) if (strcmp(key, k) == 0) { \
    if (strlen(value) >= sizeof c->field) return 0; \
    config_wipe(c->field, sizeof c->field); \
    return copy_value(c->field, sizeof c->field, value); }
    if (strcmp(section, "connection") == 0) {
        TEXT("server", server);
        TEXT("nick", nick);
        TEXT("username", username);
        TEXT("realname", realname);
        TEXT("channel", channels);
        TEXT("auth", auth);
        TEXT("auth_user", auth_user);
        TEXT("password_file", password_file);
        TEXT("password_gpg_file", password_gpg_file);
        SECRET("password", password);
        TEXT("server_password_file", server_password_file);
        TEXT("server_password_gpg_file", server_password_gpg_file);
        SECRET("server_password", server_password);
        TEXT("ca_file", ca_file);
        if (strcmp(key, "port") == 0) return number(value, 1, 65535, &c->port);
        if (strcmp(key, "tls") == 0) return boolean(value, &c->tls);
        if (strcmp(key, "autoconnect") == 0) return boolean(value, &c->autoconnect);
    } else if (strcmp(section, "bouncer") == 0) {
        TEXT("username", bouncer_user);
        TEXT("network", bouncer_network);
        TEXT("client", bouncer_client);
        if (!strcmp(key, "automatic_away")) return boolean(value, &c->bouncer_away);
        if (!strcmp(key, "type")) {
            if (!strcmp(value, "none")) c->bouncer_type = CONFIG_BOUNCER_NONE;
            else if (!strcmp(value, "znc")) c->bouncer_type = CONFIG_BOUNCER_ZNC;
            else if (!strcmp(value, "soju")) c->bouncer_type = CONFIG_BOUNCER_SOJU;
            else return 0;
            return 1;
        }
    } else if (strcmp(section, "input") == 0) {
        if (!strcmp(key, "force_terminal_signals")) return boolean(value, &c->force_terminal_signals);
        if (!strcmp(key, "send_mode")) {
            if (strcmp(value, "direct") && strcmp(value, "review")) return 0;
            c->send_review = !strcmp(value, "review");
            return 1;
        }
        if (!strcmp(key, "command_suggestions")) {
            for (int i = 0; i <= CONFIG_SUGGEST_CONTEXTUAL; ++i)
                if (!strcmp(value, suggestion_modes[i])) { c->command_suggestions = i; return 1; }
            return 0;
        }
        if (!strcmp(key, "composer_mode")) {
            if (strcmp(value, "line") && strcmp(value, "fullscreen")) return 0;
            c->composer_fullscreen = !strcmp(value, "fullscreen");
            return 1;
        }
    } else if (!strcmp(section, "hotkeys")) {
        unsigned binding = config_hotkey_key(key);
        const ConfigHotkeyAction *action = config_hotkey_action_find(value);
        /* Commands replace deliberately; ambiguous duplicate file entries
         * fail the whole load, including aliases such as Ctrl-I and Tab. */
        if (!binding || !action || config_hotkey_lookup(c, binding)) return 0;
        return config_hotkey_set(c, binding, action->id, NULL, 0) > 0;
    } else if (strcmp(section, "output") == 0) {
        if (!strcmp(key, "queue_notices")) return boolean(value, &c->queue_notices);
        if (!strcmp(key, "program_messages")) return boolean(value, &c->program_messages);
        if (!strcmp(key, "persistent_logging")) return boolean(value, &c->persistent_logging);
        if (!strcmp(key, "format")) {
            if (strcmp(value, "compact") && strcmp(value, "full")) return 0;
            c->output_full = !strcmp(value, "full");
            return 1;
        }
    } else if (!strcmp(section, "session")) {
        if (!strcmp(key, "single_session")) return boolean(value, &c->single_session);
        if (!strcmp(key, "default_interface")) {
            if (!strcmp(value, "ncurses")) c->default_interface = CONFIG_INTERFACE_NCURSES;
            else if (!strcmp(value, "idle")) c->default_interface = CONFIG_INTERFACE_IDLE;
            else return 0;
            return 1;
        }
    } else if (!strcmp(section, "presence")) {
        TEXT("part_message", part_message);
        TEXT("quit_message", quit_message);
        TEXT("background_away_message", background_away_message);
        TEXT("autoaway_message", autoaway_message);
        if (!strcmp(key, "background_away")) return boolean(value, &c->background_away);
        if (!strcmp(key, "autoaway_minutes")) return number(value, 0, 1440, &c->autoaway_minutes);
    } else if (!strcmp(section, "autocommands")) {
        if (!strcmp(key, "enabled")) return boolean(value, &c->autocommands.enabled);
        if (!strcmp(key, "delay_seconds")) return number(value, 0, 300, &c->autocommands.delay_seconds);
        if (!strcmp(key, "interval_seconds")) return number(value, 2, 60, &c->autocommands.interval_seconds);
        if (!strcmp(key, "command")) {
            if (c->autocommands.count >= CONFIG_AUTOCOMMAND_MAX ||
                config_autocommand_parse(value, NULL, NULL, 0) < 0) return 0;
            memcpy(c->autocommands.lines[c->autocommands.count++], value, strlen(value) + 1);
            return 1;
        }
    } else if (strcmp(section, "history") == 0 && strcmp(key, "lines") == 0) {
        return number(value, 50, 5000, &c->history_lines);
    } else if (strcmp(section, "interface") == 0 && !c->minimal) {
        if (strcmp(key, "user_list") == 0) return boolean(value, &c->user_list);
        if (strcmp(key, "mouse") == 0) return boolean(value, &c->mouse);
        if (strcmp(key, "hints") == 0) return boolean(value, &c->hints);
        if (strcmp(key, "shortcut_labels") == 0) return boolean(value, &c->shortcut_labels);
        if (strcmp(key, "keymap") == 0) {
            /* Older profiles chose a navigation preset. Both spellings
             * now mean the one arrow-and-hjkl navigation scheme. */
            return !strcmp(value, "original") || !strcmp(value, "vim");
        }
        if (strcmp(key, "focus_style") == 0) {
            /* Retain old profiles without keeping a cosmetic preference.
             * Focus now has one explicit outline in every terminal. */
            return !strcmp(value, "reverse") || !strcmp(value, "brackets");
        }
        if (strcmp(key, "history_lines") == 0) return number(value, 50, 5000, &c->history_lines);
    } else if (strcmp(section, "mentions") == 0 && strcmp(key, "word") == 0) {
        if (!*value || c->word_count >= CONFIG_WORD_MAX || strlen(value) >= CONFIG_WORD_SIZE)
            return 0;
        memcpy(c->words[c->word_count++], value, strlen(value) + 1);
        return 1;
    } else if (strcmp(section, "ignore") == 0 && strcmp(key, "mask") == 0) {
        if (c->ignore_count >= CONFIG_IGNORE_MAX || !irc_valid_ignore_mask(value))
            return 0;
        memcpy(c->ignores[c->ignore_count++], value, strlen(value) + 1);
        return 1;
    }
#undef TEXT
#undef SECRET
    return 0;
}

static int validate_text(const char *s, size_t capacity, const char *field,
                         char *error, size_t error_cap)
{
    size_t length = strnlen(s, capacity);
    if (length == capacity)
        return problem(error, error_cap, "%s exceeds its text limit", field);
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)s[i] < 32 || (unsigned char)s[i] == 127)
            return problem(error, error_cap, "%s cannot contain control characters", field);
    if (length && (s[0] == ' ' || s[length - 1] == ' '))
        return problem(error, error_cap, "%s cannot begin or end with spaces", field);
    return 1;
}

static int validate_password(const char *s, size_t capacity, const char *field,
                            char *error, size_t error_cap)
{
    size_t length = strnlen(s, capacity);
    if (length == capacity) return problem(error, error_cap, "%s exceeds its byte limit", field);
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)s[i] < 32 || (unsigned char)s[i] == 127)
            return problem(error, error_cap, "%s cannot contain control bytes", field);
    return 1;
}

static int server_valid(const char *host)
{
    const unsigned char *p = (const unsigned char *)host;
    unsigned char address[sizeof(struct in6_addr)];
    if (!*host) return 1; /* An interactive prompt will request the missing host. */
    if (strchr(host, ':'))
        return inet_pton(AF_INET6, host, address) == 1;
    if (inet_pton(AF_INET, host, address) == 1)
        return 1;
    for (; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.'))
            return 0;
    return host[0] != '.';
}

static int username_valid(const char *name)
{
    const unsigned char *p = (const unsigned char *)name;
    if (strlen(name) > 32) return 0;
    for (; *p; ++p)
        if (*p <= 32 || *p >= 127 || *p == ':' || *p == ',' || *p == '@' || *p == '!')
            return 0;
    return 1;
}

static int bouncer_component(const char *value, size_t capacity, int required)
{
    size_t length = strnlen(value, capacity);
    if (length >= capacity || (required && !length)) return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)value[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-'))
            return 0;
    }
    return 1;
}

int config_bouncer_identity(const Config *c, char *out, size_t cap)
{
    if (out && cap) out[0] = 0;
    if (!c) return -1;
    if (c->bouncer_type == CONFIG_BOUNCER_NONE) return 0;
    if (!out || !cap ||
        (c->bouncer_type != CONFIG_BOUNCER_ZNC && c->bouncer_type != CONFIG_BOUNCER_SOJU) ||
        !bouncer_component(c->bouncer_user, sizeof c->bouncer_user, 1) ||
        !bouncer_component(c->bouncer_network, sizeof c->bouncer_network, 1) ||
        !bouncer_component(c->bouncer_client, sizeof c->bouncer_client, 0)) return -1;
    int n;
    if (c->bouncer_type == CONFIG_BOUNCER_ZNC)
        n = snprintf(out, cap, "%s%s%s/%s", c->bouncer_user,
                     c->bouncer_client[0] ? "@" : "", c->bouncer_client, c->bouncer_network);
    else
        n = snprintf(out, cap, "%s/%s%s%s", c->bouncer_user, c->bouncer_network,
                     c->bouncer_client[0] ? "@" : "", c->bouncer_client);
    if (n < 0 || (size_t)n >= cap) { out[0] = 0; return -1; }
    return 1;
}

static int channels_valid(const char *channels)
{
    char copy[1024], *part, *next;
    size_t count = 0;
    if (!*channels) return 1;
    memcpy(copy, channels, strlen(channels) + 1);
    for (part = copy; ; part = next) {
        next = strchr(part, ',');
        if (next) *next++ = '\0';
        if (!irc_valid_channel(part) || ++count > 29) return 0;
        if (!next) break;
    }
    return 1;
}

int config_validate(const Config *c, char *error, size_t error_cap)
{
    size_t i;
    struct TextField { const char *name, *value; size_t capacity; };
#define FIELD(name, member) {name, c->member, sizeof(c->member)}
    const struct TextField fields[] = {
        FIELD("server", server), FIELD("nick", nick), FIELD("username", username),
        FIELD("realname", realname), FIELD("channel", channels), FIELD("auth", auth),
        FIELD("auth_user", auth_user), FIELD("password_file", password_file),
        FIELD("password_gpg_file", password_gpg_file),
        FIELD("server_password_file", server_password_file),
        FIELD("server_password_gpg_file", server_password_gpg_file),
        FIELD("ca_file", ca_file), FIELD("part_message", part_message),
        FIELD("quit_message", quit_message), FIELD("background_away_message", background_away_message),
        FIELD("autoaway_message", autoaway_message),
        FIELD("bouncer.username", bouncer_user), FIELD("bouncer.network", bouncer_network),
        FIELD("bouncer.client", bouncer_client)
    };
#undef FIELD
    if (c->port < 1 || c->port > 65535)
        return problem(error, error_cap, "port must be between 1 and 65535 (received %d)", c->port);
    if (c->tls != 0 && c->tls != 1)
        return problem(error, error_cap, "tls must be true (verified TLS) or false (plaintext)");
    if (c->autoconnect != 0 && c->autoconnect != 1)
        return problem(error, error_cap, "autoconnect must be true or false");
    if (c->bouncer_type < CONFIG_BOUNCER_NONE || c->bouncer_type > CONFIG_BOUNCER_SOJU)
        return problem(error, error_cap, "bouncer.type must be none, znc or soju");
    if (c->bouncer_away != 0 && c->bouncer_away != 1)
        return problem(error, error_cap, "bouncer.automatic_away must be true or false");
    if (c->send_review != 0 && c->send_review != 1)
        return problem(error, error_cap, "send_mode must be direct or review");
    if (c->force_terminal_signals != 0 && c->force_terminal_signals != 1)
        return problem(error, error_cap, "force_terminal_signals must be true or false");
    if (c->output_full != 0 && c->output_full != 1)
        return problem(error, error_cap, "output format must be compact or full");
    if (c->queue_notices != 0 && c->queue_notices != 1)
        return problem(error, error_cap, "queue_notices must be true or false");
    if (c->program_messages != 0 && c->program_messages != 1)
        return problem(error, error_cap, "program_messages must be true or false");
    if (c->persistent_logging != 0 && c->persistent_logging != 1)
        return problem(error, error_cap, "persistent_logging must be true or false");
    if (c->single_session != 0 && c->single_session != 1)
        return problem(error, error_cap, "single_session must be true or false");
    if (c->background_away != 0 && c->background_away != 1)
        return problem(error, error_cap, "background_away must be true or false");
    if (c->autoaway_minutes < 0 || c->autoaway_minutes > 1440)
        return problem(error, error_cap, "autoaway_minutes must be between 0 and 1440");
    if (c->default_interface != CONFIG_INTERFACE_NCURSES && c->default_interface != CONFIG_INTERFACE_IDLE)
        return problem(error, error_cap, "default_interface must be ncurses or idle");
    if (c->command_suggestions < CONFIG_SUGGEST_ALPHABETICAL || c->command_suggestions > CONFIG_SUGGEST_CONTEXTUAL)
        return problem(error, error_cap, "command_suggestions must be alphabetical, common, history, frequent or contextual");
    if (c->composer_fullscreen != 0 && c->composer_fullscreen != 1)
        return problem(error, error_cap, "composer_mode must be line or fullscreen");
    if (hotkeys_validate(c, error, error_cap) < 0) return -1;
    if (c->command_usage_count > CONFIG_COMMAND_USAGE_MAX)
        return problem(error, error_cap, "Too many retained command usage counters");
    for (i = 0; i < c->command_usage_count; ++i) {
        if (!usage_name_valid(c->command_usage[i].name) || c->command_usage[i].count > CONFIG_COMMAND_USAGE_LIMIT)
            return problem(error, error_cap, "Invalid command usage counter");
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(c->command_usage[i].name, c->command_usage[j].name))
                return problem(error, error_cap, "Duplicate command usage counter");
    }
    if (c->autocommands.enabled != 0 && c->autocommands.enabled != 1)
        return problem(error, error_cap, "autocommands.enabled must be true or false");
    if (c->autocommands.delay_seconds < 0 || c->autocommands.delay_seconds > 300)
        return problem(error, error_cap, "autocommands.delay_seconds must be between 0 and 300");
    if (c->autocommands.interval_seconds < 2 || c->autocommands.interval_seconds > 60)
        return problem(error, error_cap, "autocommands.interval_seconds must be between 2 and 60");
    if (c->autocommands.count > CONFIG_AUTOCOMMAND_MAX)
        return problem(error, error_cap, "autocommands.command accepts at most %d entries", CONFIG_AUTOCOMMAND_MAX);
    if (c->autocommands.enabled && c->autocommands.count && !c->tls)
        return problem(error, error_cap, "Enabled automatic commands require verified TLS");
    for (i = 0; i < c->autocommands.count; ++i)
        if (config_autocommand_parse(c->autocommands.lines[i], NULL, NULL, 0) < 0)
            return problem(error, error_cap, "Invalid automatic command at position %zu; use /msg, /join, /nick, /away, /back or /whois", i + 1);
    if (c->minimal != 0 && c->minimal != 1)
        return problem(error, error_cap, "Internal configuration format flag is invalid");
    if (c->user_list != 0 && c->user_list != 1)
        return problem(error, error_cap, "user_list must be true or false");
    if (c->mouse != 0 && c->mouse != 1)
        return problem(error, error_cap, "mouse must be true or false");
    if (c->hints != 0 && c->hints != 1)
        return problem(error, error_cap, "hints must be true or false");
    if (c->shortcut_labels != 0 && c->shortcut_labels != 1)
        return problem(error, error_cap, "shortcut_labels must be true or false");
    if (c->focus_style != CONFIG_FOCUS_REVERSE && c->focus_style != CONFIG_FOCUS_BRACKETS)
        return problem(error, error_cap, "focus_style must be reverse or brackets");
    if (c->history_lines < 50 || c->history_lines > 5000)
        return problem(error, error_cap, "history lines must be between 50 and 5000 (received %d)", c->history_lines);
    if (c->word_count > CONFIG_WORD_MAX)
        return problem(error, error_cap, "mentions.word accepts at most %d entries", CONFIG_WORD_MAX);
    if (c->ignore_count > CONFIG_IGNORE_MAX)
        return problem(error, error_cap, "ignore.mask accepts at most %d entries", CONFIG_IGNORE_MAX);
    for (i = 0; i < sizeof fields / sizeof fields[0]; ++i)
        if (validate_text(fields[i].value, fields[i].capacity, fields[i].name, error, error_cap) < 0)
            return -1;
    if (validate_password(c->password, sizeof c->password, "password", error, error_cap) < 0 ||
        validate_password(c->server_password, sizeof c->server_password, "server_password", error, error_cap) < 0)
        return -1;
    if (!server_valid(c->server))
        return problem(error, error_cap, "server must be a hostname or unbracketed numeric IP address");
    if (c->nick[0] && !irc_valid_nick(c->nick))
        return problem(error, error_cap, "nick must be a valid ASCII IRC nickname of at most 63 bytes");
    if (!username_valid(c->username))
        return problem(error, error_cap, "username must be an ASCII IRC token of at most 32 bytes");
    if (!channels_valid(c->channels))
        return problem(error, error_cap, "channel must list at most 29 valid comma-separated channels, without empty entries");
    if (!irc_utf8_valid(c->realname))
        return problem(error, error_cap, "realname must be valid UTF-8");
    if (!irc_utf8_valid(c->auth_user))
        return problem(error, error_cap, "auth_user must be valid UTF-8");
    if (!irc_utf8_valid(c->part_message) || !irc_utf8_valid(c->quit_message) ||
        !irc_utf8_valid(c->background_away_message) || !irc_utf8_valid(c->autoaway_message))
        return problem(error, error_cap, "Presence messages must be valid UTF-8");
    if (c->background_away && !c->background_away_message[0])
        return problem(error, error_cap, "background_away_message cannot be empty while background_away is enabled");
    if (c->autoaway_minutes && !c->autoaway_message[0])
        return problem(error, error_cap, "autoaway_message cannot be empty while autoaway is enabled");
    if (strcmp(c->auth, "none") != 0 && strcmp(c->auth, "sasl") != 0)
        return problem(error, error_cap, "auth must be none or sasl");
    int sasl_sources = (c->password_file[0] != 0) + (c->password_gpg_file[0] != 0) + (c->password[0] != 0);
    int server_sources = (c->server_password_file[0] != 0) +
                         (c->server_password_gpg_file[0] != 0) + (c->server_password[0] != 0);
    if (sasl_sources > 1)
        return problem(error, error_cap, "SASL password: choose only one of password, password_file, or password_gpg_file");
    if (server_sources > 1)
        return problem(error, error_cap, "Server password: choose only one of server_password, server_password_file, or server_password_gpg_file");
    if (!c->tls && (strcmp(c->auth, "none") != 0 || server_sources))
        return problem(error, error_cap, "Authentication requires verified TLS");
    if (strcmp(c->auth, "sasl") == 0 &&
        ((c->bouncer_type != CONFIG_BOUNCER_SOJU && !c->auth_user[0]) || sasl_sources != 1))
        return problem(error, error_cap, c->bouncer_type == CONFIG_BOUNCER_SOJU ?
                       "soju requires exactly one SASL password source; its login is derived from [bouncer]" :
                       "SASL requires auth_user and exactly one password source");
    if (strlen(c->server_password) > 503)
        return problem(error, error_cap, "server_password allows at most 503 bytes");
    if (c->bouncer_type != CONFIG_BOUNCER_NONE) {
        char identity[192];
        if (!c->tls)
            return problem(error, error_cap, "Bouncer connections require verified TLS");
        if (config_bouncer_identity(c, identity, sizeof identity) != 1)
            return problem(error, error_cap,
                "Bouncer username and network are required; each component allows at most 63 ASCII letters, digits, '.', '_' or '-'");
        if (c->bouncer_type == CONFIG_BOUNCER_ZNC) {
            if (strcmp(c->auth, "none") || sasl_sources || c->auth_user[0] || server_sources != 1)
                return problem(error, error_cap,
                    "ZNC requires auth=none and exactly one server-password source; clear SASL credentials and auth_user");
            if (c->server_password[0] && strlen(identity) + 1u + strlen(c->server_password) > 503u)
                return problem(error, error_cap, "ZNC login identity and password exceed the IRC PASS limit");
        } else if (strcmp(c->auth, "sasl") || sasl_sources != 1 || server_sources || c->auth_user[0]) {
            return problem(error, error_cap,
                "soju requires auth=sasl and exactly one authentication-password source; clear server passwords and auth_user (the bouncer identity supplies it)");
        }
    }
    for (i = 0; i < c->word_count; ++i) {
        char field[48];
        (void)snprintf(field, sizeof(field), "mentions.word[%zu]", i + 1);
        if (validate_text(c->words[i], sizeof(c->words[i]), field, error, error_cap) < 0) return -1;
        if (!c->words[i][0] || !irc_utf8_valid(c->words[i]))
            return problem(error, error_cap, "Mention words must be nonempty and contain no controls");
    }
    for (i = 0; i < c->ignore_count; ++i) {
        char field[48];
        (void)snprintf(field, sizeof(field), "ignore.mask[%zu]", i + 1);
        if (validate_text(c->ignores[i], sizeof(c->ignores[i]), field, error, error_cap) < 0) return -1;
        if (!irc_valid_ignore_mask(c->ignores[i]))
            return problem(error, error_cap,
                           "Ignore masks must be ASCII nicknames or nick!user@host masks of at most 127 bytes");
    }
    return 1;
}

/* Parse into a copy, then publish the complete validated value once.
 * Every exit wipes candidate/raw text because either may contain inline
 * credentials or automatic commands. A missing file leaves defaults intact. */
int config_load(Config *c, const char *path, char *error, size_t error_cap)
{
    char *data, *line, *next, section[32] = "";
    size_t length, line_no = 0;
    int result;
    mode_t permissions = 0;
    Config candidate = *c;
    data = malloc(CONFIG_FILE_MAX + 1);
    if (!data) {
        config_wipe(&candidate, sizeof candidate);
        return problem(error, error_cap, "Out of memory reading configuration");
    }
    result = read_private(path, data, CONFIG_FILE_MAX + 1, 0, &length, &permissions, error, error_cap);
    if (result <= 0) {
        config_wipe(data, CONFIG_FILE_MAX + 1);
        free(data);
        config_wipe(&candidate, sizeof candidate);
        return result;
    }
    (void)length;
    candidate.word_count = 0;
    candidate.ignore_count = 0;
    candidate.command_usage_count = 0;
    candidate.hotkey_count = 0;
    memset(candidate.hotkeys, 0, sizeof candidate.hotkeys);
    memset(candidate.command_usage, 0, sizeof candidate.command_usage);
    memset(candidate.ignores, 0, sizeof(candidate.ignores));
    candidate.autocommands.count = 0;
    config_wipe(candidate.autocommands.lines, sizeof candidate.autocommands.lines);
    for (line = data; line; line = next) {
        char *text, *equals;
        next = strchr(line, '\n');
        if (next) *next++ = '\0';
        ++line_no;
        size_t line_length = strlen(line);
        if (next && line_length && line[line_length - 1] == '\r')
            line[line_length - 1] = 0;
        text = line;
        while (*text == ' ' || *text == '\t') ++text;
        if (!*text || *text == '#' || *text == ';')
            continue;
        if (*text == '[') {
            char *end = strchr(text, ']');
            if (!end || *trim(end + 1))
                goto invalid;
            *end = '\0';
            text = trim(text + 1);
            if (strcmp(text, "connection") != 0 && strcmp(text, "interface") != 0 &&
                strcmp(text, "mentions") != 0 && strcmp(text, "ignore") != 0 &&
                strcmp(text, "input") != 0 && strcmp(text, "output") != 0 &&
                strcmp(text, "session") != 0 && strcmp(text, "presence") != 0 &&
                strcmp(text, "history") != 0 && strcmp(text, "autocommands") != 0 &&
                strcmp(text, "bouncer") != 0 && strcmp(text, "hotkeys") != 0)
                goto invalid;
            if (candidate.minimal && !strcmp(text, "interface")) {
                result = problem(error, error_cap,
                    "This configuration format has no [interface] settings; remove that section or select a compatible configuration file");
                goto done;
            }
            if (!copy_value(section, sizeof section, text))
                goto invalid;
            continue;
        }
        equals = strchr(text, '=');
        if (!equals)
            goto invalid;
        *equals++ = '\0';
        char *key = trim(text), *value;
        if ((!strcmp(section, "connection") && (!strcmp(key, "password") || !strcmp(key, "server_password"))) ||
            (!strcmp(section, "autocommands") && !strcmp(key, "command"))) {
            /* Inline secrets are literal bytes after '='. Preserve spaces,
             * '#', ';' and additional '='; CRLF's single CR was removed above. */
            value = equals;
        } else value = trim(equals);
        if (!set_value(&candidate, section, key, value))
            goto invalid;
    }
    if ((candidate.password[0] || candidate.server_password[0]) &&
        permissions != 0600 && permissions != 0400) {
        result = problem(error, error_cap, "Configuration containing plaintext passwords must have mode 0600 or 0400");
        goto done;
    }
    if (candidate.autocommands.count && permissions != 0600 && permissions != 0400) {
        result = problem(error, error_cap, "Configuration containing automatic commands must have mode 0600 or 0400");
        goto done;
    }
    if (config_validate(&candidate, error, error_cap) < 0) {
        result = -1;
        goto done;
    }
    *c = candidate;
    result = 1;
    goto done;
invalid:
    result = problem(error, error_cap, "Invalid or unknown configuration entry at line %zu", line_no);
done:
    config_wipe(data, CONFIG_FILE_MAX + 1);
    free(data);
    config_wipe(&candidate, sizeof candidate);
    return result;
}

static int random_bytes(void *out, size_t size)
{
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    size_t done = 0;
    if (fd < 0)
        return 0;
    while (done < size) {
        ssize_t n = read(fd, (char *)out + done, size - done);
        if (n > 0) done += (size_t)n;
        else if (n < 0 && errno == EINTR) continue;
        else { close(fd); return 0; }
    }
    close(fd);
    return 1;
}

static void write_config(FILE *file, const void *value)
{
    const Config *c = value;
    (void)fprintf(file,
        "# %s configuration. /save rewrites this file; keep custom comments elsewhere.\n"
        "# Paths are literal (no ~, environment substitution, or shell commands).\n"
        "# Channels are comma separated. # inside a value is literal, not a comment.\n"
        "# Inline password/server_password bytes after '=' are literal, including spaces.\n"
        "[connection]\nserver=%s\nport=%d\ntls=%s\nnick=%s\nusername=%s\nrealname=%s\n"
        "channel=%s\nautoconnect=%s\nauth=%s\nauth_user=%s\npassword_file=%s\npassword_gpg_file=%s\npassword=%s\n"
        "server_password_file=%s\nserver_password_gpg_file=%s\nserver_password=%s\nca_file=%s\n",
        c->minimal ? "onekeyirc" : "vipirc",
        c->server, c->port, c->tls ? "true" : "false", c->nick, c->username, c->realname,
        c->channels, c->autoconnect ? "true" : "false", c->auth, c->auth_user,
        c->password_file, c->password_gpg_file, c->password,
        c->server_password_file, c->server_password_gpg_file, c->server_password, c->ca_file);
    (void)fprintf(file,
        "\n[bouncer]\ntype=%s\nusername=%s\nnetwork=%s\nclient=%s\nautomatic_away=%s\n",
        c->bouncer_type == CONFIG_BOUNCER_ZNC ? "znc" :
        c->bouncer_type == CONFIG_BOUNCER_SOJU ? "soju" : "none",
        c->bouncer_user, c->bouncer_network, c->bouncer_client,
        c->bouncer_away ? "true" : "false");
    if (c->minimal)
        (void)fprintf(file, "\n[history]\nlines=%d\n", c->history_lines);
    else
        (void)fprintf(file, "\n[interface]\nuser_list=%s\nmouse=%s\nhints=%s\nshortcut_labels=%s\nhistory_lines=%d\n",
                      c->user_list ? "true" : "false", c->mouse ? "true" : "false",
                      c->hints ? "true" : "false", c->shortcut_labels ? "true" : "false", c->history_lines);
    (void)fprintf(file, "\n[input]\nsend_mode=%s\ncommand_suggestions=%s\ncomposer_mode=%s\nforce_terminal_signals=%s\n\n[output]\nformat=%s\nqueue_notices=%s\nprogram_messages=%s\npersistent_logging=%s\n"
                  "\n[session]\nsingle_session=%s\ndefault_interface=%s\n"
                  "\n[presence]\npart_message=%s\nquit_message=%s\nbackground_away=%s\nbackground_away_message=%s\nautoaway_minutes=%d\nautoaway_message=%s\n"
                  "\n[autocommands]\nenabled=%s\ndelay_seconds=%d\ninterval_seconds=%d\n",
                  c->send_review ? "review" : "direct", suggestion_modes[c->command_suggestions],
                  c->composer_fullscreen ? "fullscreen" : "line", c->force_terminal_signals ? "true" : "false",
                  c->output_full ? "full" : "compact",
                  c->queue_notices ? "true" : "false", c->program_messages ? "true" : "false",
                  c->persistent_logging ? "true" : "false", c->single_session ? "true" : "false",
                  c->default_interface == CONFIG_INTERFACE_IDLE ? "idle" : "ncurses",
                  c->part_message, c->quit_message, c->background_away ? "true" : "false",
                  c->background_away_message, c->autoaway_minutes, c->autoaway_message,
                  c->autocommands.enabled ? "true" : "false",
                  c->autocommands.delay_seconds, c->autocommands.interval_seconds);
    for (size_t i = 0; i < c->autocommands.count; ++i)
        (void)fprintf(file, "command=%s\n", c->autocommands.lines[i]);
    (void)fprintf(file, "\n# Custom key bindings are empty by default. /bindings actions lists functions.\n[hotkeys]\n");
    for (size_t i = 0; i < c->hotkey_count; ++i) {
        char key[CONFIG_HOTKEY_NAME_SIZE];
        const ConfigHotkeyAction *action = config_hotkey_action(c->hotkeys[i].action);
        /* config_save validates the complete table before opening a file. */
        if (action && config_hotkey_key_name(c->hotkeys[i].key, key, sizeof key))
            (void)fprintf(file, "%s=%s\n", key, action->name);
    }
    (void)fprintf(file, "\n[mentions]\n");
    for (size_t i = 0; i < c->word_count; ++i)
        (void)fprintf(file, "word=%s\n", c->words[i]);
    (void)fprintf(file, "\n[ignore]\n");
    for (size_t i = 0; i < c->ignore_count; ++i)
        (void)fprintf(file, "mask=%s\n", c->ignores[i]);
}

/* One file transaction: pin parent, write/sync/close a private temporary,
 * then publish by link (create) or rename (replace), and sync the directory.
 * committed records publication, not completion of every durability step.
 * After publication, a later error must leave the new destination in place. */
static int write_private(const char *path, void (*writer)(FILE *, const void *),
                         const void *value, int create_only, int *committed,
                         char *error, size_t error_cap)
{
    char leaf[256], temp[64];
    unsigned char random[12];
    int dir, fd = -1, result = -1;
    size_t i;
    struct stat st;
    FILE *file = NULL;
    if (committed) *committed = 0;
    dir = parent_dir(path, 1, 1, leaf, sizeof leaf, error, error_cap);
    if (dir < 0)
        return -1;
    if (fstatat(dir, leaf, &st, AT_SYMLINK_NOFOLLOW) == 0) {
        if (create_only) {
            problem(error, error_cap, "Configuration already exists; it was not overwritten");
            goto done;
        }
        if (!S_ISREG(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 022)) {
            problem(error, error_cap, "Refusing to replace an unsafe configuration file");
            goto done;
        }
    } else if (errno != ENOENT) {
        problem(error, error_cap, "Cannot inspect existing configuration: %s", strerror(errno));
        goto done;
    }
    if (!random_bytes(random, sizeof random)) {
        problem(error, error_cap, "Cannot generate a temporary file name");
        goto done;
    }
    memcpy(temp, ".vipirc-", 8);
    for (i = 0; i < sizeof random; ++i)
        (void)snprintf(temp + 8 + i * 2, sizeof temp - 8 - i * 2, "%02x", random[i]);
    fd = openat(dir, temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        problem(error, error_cap, "Cannot create private configuration: %s", strerror(errno));
        goto done;
    }
    file = fdopen(fd, "w");
    if (!file) {
        problem(error, error_cap, "Cannot write configuration: %s", strerror(errno));
        close(fd);
        fd = -1;
        goto remove_temp;
    }
    /* The optional plaintext fields must not linger in a stdio heap buffer. */
    if (setvbuf(file, NULL, _IONBF, 0) != 0) {
        problem(error, error_cap, "Cannot disable buffering for private configuration output");
        goto remove_temp;
    }
    writer(file, value);
    if (ferror(file) || fflush(file) != 0 || fsync(fd) != 0) {
        problem(error, error_cap, "Cannot flush configuration: %s", strerror(errno));
        (void)fclose(file);
        file = NULL;
        fd = -1;
        goto remove_temp;
    }
    if (fclose(file) != 0) {
        file = NULL;
        fd = -1;
        problem(error, error_cap, "Cannot close new configuration: %s", strerror(errno));
        goto remove_temp;
    }
    file = NULL;
    fd = -1;
    if (create_only) {
        /* The initial absence check is only a diagnostic. Publish the fully
         * written inode without replacing a file created in the meantime.
         * Both names are in the pinned private directory, so linkat provides
         * portable atomic no-clobber creation without exposing partial data. */
        if (linkat(dir, temp, dir, leaf, 0) != 0) {
            problem(error, error_cap, errno == EEXIST ?
                    "Configuration already exists; it was not overwritten" :
                    "Cannot create configuration without replacing an existing file: %s", strerror(errno));
            goto remove_temp;
        }
        if (committed) *committed = 1;
        if (unlinkat(dir, temp, 0) != 0) {
            problem(error, error_cap, "Configuration was created, but temporary-name cleanup failed: %s", strerror(errno));
            goto done;
        }
    } else {
        if (renameat(dir, temp, dir, leaf) != 0) {
            problem(error, error_cap, "Cannot replace configuration: %s", strerror(errno));
            goto remove_temp;
        }
        if (committed) *committed = 1;
    }
    if (fsync(dir) != 0 && errno != EINVAL) {
        problem(error, error_cap, "Configuration was %s but directory synchronization failed: %s",
                create_only ? "created" : "replaced", strerror(errno));
        goto done;
    }
    result = 1;
    goto done;
remove_temp:
    (void)unlinkat(dir, temp, 0);
done:
    if (file) (void)fclose(file);
    else if (fd >= 0) close(fd);
    close(dir);
    return result;
}

int config_save(const Config *c, const char *path, char *error, size_t error_cap)
{
    if (config_validate(c, error, error_cap) < 0) return -1;
    return write_private(path, write_config, c, 0, NULL, error, error_cap);
}

int config_create(const Config *c, const char *path, char *error, size_t error_cap)
{
    if (config_validate(c, error, error_cap) < 0) return -1;
    return write_private(path, write_config, c, 1, NULL, error, error_cap);
}

static int usage_path(char *out, size_t cap, const char *path, char *error, size_t error_cap)
{
    int n = snprintf(out, cap, "%s.commands", path);
    return n > 0 && (size_t)n < cap ? 1 : problem(error, error_cap, "Command usage path is too long");
}

int config_command_usage_load(Config *c, const char *config_path, char *error, size_t error_cap)
{
    char path[CONFIG_PATH_MAX], data[8192];
    size_t length = 0;
    if (!c || !config_path || !*config_path) return 0;
    if (usage_path(path, sizeof path, config_path, error, error_cap) < 0) return -1;
    int result = read_private(path, data, sizeof data, 1, &length, NULL, error, error_cap);
    if (result <= 0) return result;
    Config candidate;
    memset(&candidate, 0, sizeof candidate);
    char *next;
    for (char *line = data; line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *entry = trim(line);
        if (!*entry || *entry == '#') continue;
        char *equals = strchr(entry, '=');
        if (!equals) { result = -1; break; }
        *equals++ = 0;
        if (!usage_value(&candidate, trim(entry), trim(equals))) { result = -1; break; }
    }
    if (result > 0) {
        memcpy(c->command_usage, candidate.command_usage, sizeof c->command_usage);
        c->command_usage_count = candidate.command_usage_count;
    } else problem(error, error_cap, "Invalid saved command usage; counters were not loaded");
    config_wipe(&candidate, sizeof candidate);
    config_wipe(data, sizeof data);
    (void)length;
    return result;
}

static void write_usage(FILE *file, const void *value)
{
    const Config *c = value;
    (void)fprintf(file, "# Command names and counts only. No arguments or command history.\n");
    for (size_t i = 0; i < c->command_usage_count; ++i)
        (void)fprintf(file, "%s=%u\n", c->command_usage[i].name, c->command_usage[i].count);
}

int config_command_usage_save(const Config *c, const char *config_path, char *error, size_t error_cap)
{
    char path[CONFIG_PATH_MAX];
    if (!c || !config_path || !*config_path) return 0;
    if (c->command_usage_count > CONFIG_COMMAND_USAGE_MAX)
        return problem(error, error_cap, "Invalid command usage count");
    for (size_t i = 0; i < c->command_usage_count; ++i) {
        if (!usage_name_valid(c->command_usage[i].name) || c->command_usage[i].count > CONFIG_COMMAND_USAGE_LIMIT)
            return problem(error, error_cap, "Invalid command usage entry");
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(c->command_usage[i].name, c->command_usage[j].name))
                return problem(error, error_cap, "Duplicate command usage entry");
    }
    if (usage_path(path, sizeof path, config_path, error, error_cap) < 0) return -1;
    return write_private(path, write_usage, c, 0, NULL, error, error_cap);
}

static int profile_id_valid(const char *id)
{
    size_t n;
    if (!id || !(n = strnlen(id, CONFIG_PROFILE_ID_SIZE)) || n >= CONFIG_PROFILE_ID_SIZE)
        return 0;
    if (!((id[0] >= 'a' && id[0] <= 'z') || (id[0] >= 'A' && id[0] <= 'Z') ||
          (id[0] >= '0' && id[0] <= '9')) || strstr(id, "..")) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char ch = (unsigned char)id[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) return 0;
    }
    return 1;
}

static int profile_suffix(char *out, size_t cap, const char *primary, const char *suffix,
                          char *error, size_t error_cap)
{
    int n;
    if (!primary || !*primary || strlen(primary) >= CONFIG_PATH_MAX)
        return problem(error, error_cap, "Invalid primary configuration path");
    n = snprintf(out, cap, "%s%s", primary, suffix);
    if (n < 0 || (size_t)n >= cap)
        return problem(error, error_cap, "Server profile path is too long");
    return 1;
}

int config_profile_path(char *out, size_t cap, const char *primary, const char *id,
                        char *error, size_t error_cap)
{
    char suffix[CONFIG_PROFILE_ID_SIZE + 4];
    if (!profile_id_valid(id)) return problem(error, error_cap, "Invalid server profile ID");
    (void)snprintf(suffix, sizeof(suffix), ".d/%s", id);
    return profile_suffix(out, cap, primary, suffix, error, error_cap);
}

int config_profiles_load(ConfigProfiles *profiles, const char *primary,
                         char *error, size_t error_cap)
{
    char path[CONFIG_PATH_MAX], data[4097], *line, *next;
    size_t length, line_no = 0, default_line = 0;
    int section = 0, result;
    ConfigProfiles candidate;
    memset(&candidate, 0, sizeof(candidate));
    if (profile_suffix(path, sizeof(path), primary, ".servers", error, error_cap) < 0) return -1;
    result = read_private(path, data, sizeof(data), 0, &length, NULL, error, error_cap);
    if (result <= 0) {
        if (!result) *profiles = candidate;
        return result;
    }
    (void)length;
    for (line = data; line; line = next) {
        char *text, *equals;
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        ++line_no;
        text = trim(line);
        if (!*text || *text == '#' || *text == ';') continue;
        if (!strcmp(text, "[servers]")) {
            section = 1;
            continue;
        }
        equals = strchr(text, '=');
        if (!section || !equals) goto invalid;
        *equals++ = 0;
        equals = trim(equals);
        text = trim(text);
        if (!strcmp(text, "default")) {
            if (default_line || !profile_id_valid(equals)) goto invalid;
            default_line = line_no;
            (void)copy_value(candidate.default_id, sizeof(candidate.default_id), equals);
            continue;
        }
        if (strcmp(text, "profile") || !profile_id_valid(equals) ||
            candidate.count >= CONFIG_PROFILE_MAX) goto invalid;
        for (size_t i = 0; i < candidate.count; ++i)
            if (!strcmp(candidate.ids[i], equals)) goto invalid;
        (void)copy_value(candidate.ids[candidate.count++], CONFIG_PROFILE_ID_SIZE, equals);
    }
    if (candidate.default_id[0]) {
        size_t i;
        for (i = 0; i < candidate.count; ++i)
            if (!strcmp(candidate.ids[i], candidate.default_id)) break;
        if (i == candidate.count)
            return problem(error, error_cap,
                           "Default server at catalog line %zu is not a listed profile", default_line);
    }
    *profiles = candidate;
    return 1;
invalid:
    return problem(error, error_cap, "Invalid server catalog entry at line %zu", line_no);
}

static int profiles_check(const char *primary, char *error, size_t error_cap, int minimal)
{
    ConfigProfiles profiles;
    Config config;
    char path[CONFIG_PATH_MAX], detail[512];
    if (config_profiles_load(&profiles, primary, error, error_cap) < 0) return -1;
    for (size_t i = 0; i < profiles.count; ++i) {
        if (config_profile_path(path, sizeof(path), primary, profiles.ids[i], error, error_cap) < 0) return -1;
        if (minimal) config_minimal_defaults(&config);
        else config_defaults(&config);
        int result = config_load(&config, path, detail, sizeof(detail));
        if (result <= 0 || !config.server[0]) {
            int failed = problem(error, error_cap, "Server profile %s: %s", profiles.ids[i],
                                 result < 0 ? detail : !result ? "file is missing" : "server is required");
            config_wipe(&config, sizeof config);
            return failed;
        }
        config_wipe(&config, sizeof config);
    }
    return 1;
}

int config_profiles_check(const char *primary, char *error, size_t error_cap)
{
    return profiles_check(primary, error, error_cap, 0);
}

int config_minimal_profiles_check(const char *primary, char *error, size_t error_cap)
{
    return profiles_check(primary, error, error_cap, 1);
}

static int lock_profiles(const char *primary, char *error, size_t error_cap)
{
    char path[CONFIG_PATH_MAX], leaf[256];
    struct stat st;
    struct flock lock;
    int dir, fd;
    if (profile_suffix(path, sizeof(path), primary, ".servers.lock", error, error_cap) < 0) return -1;
    dir = parent_dir(path, 1, 1, leaf, sizeof(leaf), error, error_cap);
    if (dir < 0) return -1;
    fd = openat(dir, leaf, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    close(dir);
    if (fd < 0) return problem(error, error_cap, "Cannot open server catalog lock: %s", strerror(errno));
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 07777) != 0600) {
        close(fd);
        return problem(error, error_cap, "Server catalog lock must be your regular file with mode 0600");
    }
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    if (fcntl(fd, F_SETLK, &lock) < 0) {
        int saved_errno = errno;
        close(fd);
        return problem(error, error_cap, saved_errno == EACCES || saved_errno == EAGAIN ?
                       "Server catalog is busy; retry the action." : "Cannot lock server catalog: %s", strerror(saved_errno));
    }
    return fd;
}

static void write_profiles(FILE *file, const void *value)
{
    const ConfigProfiles *profiles = value;
    (void)fputs("# Additional IRC server profiles. Files are in the adjacent .d directory.\n[servers]\n", file);
    for (size_t i = 0; i < profiles->count; ++i)
        (void)fprintf(file, "profile=%s\n", profiles->ids[i]);
    if (profiles->default_id[0])
        (void)fprintf(file, "default=%s\n", profiles->default_id);
}

static int new_profile_id(const char *primary, char id[CONFIG_PROFILE_ID_SIZE],
                          char *error, size_t error_cap)
{
    unsigned char random[12];
    char path[CONFIG_PATH_MAX], leaf[256];
    struct stat st;
    for (int attempt = 0; attempt < 16; ++attempt) {
        if (!random_bytes(random, sizeof(random)))
            return problem(error, error_cap, "Cannot generate a server profile ID");
        memcpy(id, "server-", 7);
        for (size_t i = 0; i < sizeof(random); ++i)
            (void)snprintf(id + 7 + i * 2, CONFIG_PROFILE_ID_SIZE - 7 - i * 2, "%02x", random[i]);
        memcpy(id + 31, ".ini", 5);
        if (config_profile_path(path, sizeof(path), primary, id, error, error_cap) < 0) return -1;
        int dir = parent_dir(path, 1, 1, leaf, sizeof(leaf), error, error_cap);
        if (dir < 0) return -1;
        int exists = fstatat(dir, leaf, &st, AT_SYMLINK_NOFOLLOW);
        int saved_errno = errno;
        close(dir);
        if (exists < 0 && saved_errno == ENOENT) return 1;
        if (exists < 0) return problem(error, error_cap, "Cannot inspect new profile: %s", strerror(saved_errno));
    }
    id[0] = 0;
    return problem(error, error_cap, "Cannot allocate a unique server profile ID");
}

/* Serialize catalog edits under the catalog lock. Profile bytes are
 * saved before adding their ID to discovery, so this is not one transaction
 * across both files. Retain an allocated ID on failure for a stable retry. */
int config_profile_save(const Config *config, const char *primary,
                        char id[CONFIG_PROFILE_ID_SIZE], int *committed,
                        char *error, size_t error_cap)
{
    ConfigProfiles profiles;
    char path[CONFIG_PATH_MAX], catalog[CONFIG_PATH_MAX];
    int fd, result = -1, listed = 0;
    if (committed) *committed = 0;
    if (config_validate(config, error, error_cap) < 0) return -1;
    if (!config->server[0]) return problem(error, error_cap, "A saved server profile needs a server");
    if (*id && !profile_id_valid(id)) return problem(error, error_cap, "Invalid server profile ID");
    if (profile_suffix(catalog, sizeof(catalog), primary, ".servers", error, error_cap) < 0) return -1;
    fd = lock_profiles(primary, error, error_cap);
    if (fd < 0) return -1;
    if (config_profiles_load(&profiles, primary, error, error_cap) < 0) goto done;
    for (size_t i = 0; i < profiles.count; ++i)
        if (*id && !strcmp(profiles.ids[i], id)) listed = 1;
    if (listed && committed) *committed = 1;
    if (!listed && profiles.count == CONFIG_PROFILE_MAX) {
        problem(error, error_cap, "Saved server limit reached (8 including primary); close a saved secondary first.");
        goto done;
    }
    if (!*id && new_profile_id(primary, id, error, error_cap) < 0) goto done;
    if (config_profile_path(path, sizeof(path), primary, id, error, error_cap) < 0) goto done;
    if (config_save(config, path, error, error_cap) < 0) goto done;
    if (listed) {
        result = 1;
        goto done;
    }
    (void)copy_value(profiles.ids[profiles.count++], CONFIG_PROFILE_ID_SIZE, id);
    result = write_private(catalog, write_profiles, &profiles, 0, committed, error, error_cap);
done:
    close(fd);
    return result;
}

/* Removal updates catalog membership, not the
 * old profile file contents. This makes removing a profile different from
 * securely erasing any credentials or archived configuration bytes. */
int config_profile_remove(const char *primary, const char *id, int *committed,
                          char *error, size_t error_cap)
{
    ConfigProfiles profiles;
    char catalog[CONFIG_PATH_MAX];
    size_t index;
    int fd, result = -1;
    if (committed) *committed = 0;
    if (!profile_id_valid(id)) return problem(error, error_cap, "Invalid server profile ID");
    if (profile_suffix(catalog, sizeof(catalog), primary, ".servers", error, error_cap) < 0) return -1;
    fd = lock_profiles(primary, error, error_cap);
    if (fd < 0) return -1;
    if (config_profiles_load(&profiles, primary, error, error_cap) < 0) goto done;
    for (index = 0; index < profiles.count; ++index)
        if (!strcmp(profiles.ids[index], id)) break;
    if (index == profiles.count) {
        if (committed) *committed = 1;
        result = 1;
        goto done;
    }
    if (index + 1 < profiles.count)
        memmove(profiles.ids[index], profiles.ids[index + 1],
                (profiles.count - index - 1) * CONFIG_PROFILE_ID_SIZE);
    memset(profiles.ids[--profiles.count], 0, CONFIG_PROFILE_ID_SIZE);
    if (!strcmp(profiles.default_id, id)) profiles.default_id[0] = 0;
    result = write_private(catalog, write_profiles, &profiles, 0, committed, error, error_cap);
done:
    close(fd);
    return result;
}

int config_profile_set_default(const char *primary, const char *id, int *committed,
                               char *error, size_t error_cap)
{
    ConfigProfiles profiles;
    char catalog[CONFIG_PATH_MAX];
    int fd, result = -1;
    if (committed) *committed = 0;
    if (!id || (*id && !profile_id_valid(id)))
        return problem(error, error_cap, "Invalid default server profile ID");
    if (profile_suffix(catalog, sizeof(catalog), primary, ".servers", error, error_cap) < 0)
        return -1;
    if (config_profiles_load(&profiles, primary, error, error_cap) < 0) return -1;
    if (*id) {
        size_t i;
        for (i = 0; i < profiles.count; ++i)
            if (!strcmp(profiles.ids[i], id)) break;
        if (i == profiles.count)
            return problem(error, error_cap, "Default server profile is not in the saved catalog");
    }
    /* This read is the selection's linearization point when no change is
     * necessary. In particular, a primary-only setup creates no catalog,
     * profile directory, or lock file just to retain the existing default. */
    if (!strcmp(profiles.default_id, id)) {
        if (committed) *committed = 1;
        return 1;
    }
    fd = lock_profiles(primary, error, error_cap);
    if (fd < 0) return -1;
    if (config_profiles_load(&profiles, primary, error, error_cap) < 0) goto done;
    if (*id) {
        size_t i;
        for (i = 0; i < profiles.count; ++i)
            if (!strcmp(profiles.ids[i], id)) break;
        if (i == profiles.count) {
            problem(error, error_cap, "Default server profile is not in the saved catalog");
            goto done;
        }
    }
    if (!strcmp(profiles.default_id, id)) {
        if (committed) *committed = 1;
        result = 1;
        goto done;
    }
    (void)copy_value(profiles.default_id, sizeof(profiles.default_id), id);
    result = write_private(catalog, write_profiles, &profiles, 0, committed, error, error_cap);
done:
    close(fd);
    return result;
}

int config_read_secret(const char *path, char *out, size_t cap,
                       char *error, size_t error_cap)
{
    char buf[CONFIG_SECRET_MAX + 3];
    size_t length, i;
    int result = read_private(path, buf, sizeof buf, 1, &length, NULL, error, error_cap);
    if (result <= 0) {
        if (cap) config_wipe(out, cap);
        if (result == 0)
            problem(error, error_cap, "Secret file does not exist");
        return -1;
    }
    if (length && buf[length - 1] == '\n') {
        --length;
        if (length && buf[length - 1] == '\r') --length;
    }
    if (!length || length > CONFIG_SECRET_MAX || length >= cap) {
        problem(error, error_cap, "Secret is empty or exceeds its size limit");
        result = -1;
        goto done;
    }
    for (i = 0; i < length; ++i) {
        if ((unsigned char)buf[i] < 32 || (unsigned char)buf[i] == 127) {
            problem(error, error_cap, "Secret cannot contain control characters or extra lines");
            result = -1;
            goto done;
        }
    }
    memcpy(out, buf, length);
    out[length] = '\0';
    result = 1;
done:
    if (result < 0 && cap) config_wipe(out, cap);
    config_wipe(buf, sizeof buf);
    return result;
}

static int gpg_file_problem(const char *path, char *error, size_t cap, const char *fmt, ...)
{
    char detail[224], display[257];
    size_t length = path ? strnlen(path, CONFIG_PATH_MAX) : 0;
    if (length < sizeof display) {
        if (length) memcpy(display, path, length);
        display[length] = 0;
    } else {
        memcpy(display, path, 120); memcpy(display + 120, "...", 3);
        memcpy(display + 123, path + length - 133, 133); display[256] = 0;
    }
    for (unsigned char *p = (unsigned char *)display; *p; ++p)
        if (*p < 32 || *p == 127) *p = '?';
    va_list args;
    va_start(args, fmt); (void)vsnprintf(detail, sizeof detail, fmt, args); va_end(args);
    return problem(error, cap, "%s GPG file: %s", detail, display);
}

int config_open_gpg_file(const char *path, char *error, size_t error_cap)
{
    char leaf[256], detail[224];
    struct stat st;
    int dir = parent_dir(path, 0, 0, leaf, sizeof leaf, detail, sizeof detail);
    if (dir < 0) return gpg_file_problem(path, error, error_cap, "%s.", detail);
    int fd = openat(dir, leaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    int saved = errno;
    close(dir);
    if (fd < 0) {
        if (saved == ENOENT) return gpg_file_problem(path, error, error_cap, "The GPG credential file does not exist.");
        if (saved == ELOOP) return gpg_file_problem(path, error, error_cap, "Symlink GPG credential files are refused.");
        return gpg_file_problem(path, error, error_cap, "Cannot open GPG credential file (symlinks are refused): %s.", strerror(saved));
    }
    if (fstat(fd, &st) < 0) {
        saved = errno; close(fd);
        return gpg_file_problem(path, error, error_cap, "Cannot inspect the GPG credential file: %s.", strerror(saved));
    }
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        return gpg_file_problem(path, error, error_cap, "The GPG credential must be a regular file.");
    }
    if (st.st_uid != getuid()) {
        close(fd);
        return gpg_file_problem(path, error, error_cap, "The GPG credential is owned by UID %ju; your UID is %ju.",
                                (uintmax_t)st.st_uid, (uintmax_t)getuid());
    }
    if ((st.st_mode & 07777) != 0600 && (st.st_mode & 07777) != 0400) {
        close(fd);
        return gpg_file_problem(path, error, error_cap, "The GPG credential has mode %04o; use private mode 0600 or 0400.",
                                (unsigned)(st.st_mode & 07777));
    }
    if (st.st_size < 1 || st.st_size > 65536) {
        close(fd);
        return gpg_file_problem(path, error, error_cap, "The GPG credential must contain 1 to 65536 encrypted bytes.");
    }
    return fd;
}
