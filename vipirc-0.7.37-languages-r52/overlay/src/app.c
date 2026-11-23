/* One server's IRC state, shared by both frontends.
 *
 * Read this file by responsibility: retained history and LIST cache; App
 * lifetime/configuration; connection and presence; output plans/queues;
 * registration and replies; bounded network progress; local commands.
 * app_network is the event-loop entry, incoming_message the parsed-message
 * boundary, and app_command the frontend command entry. The Client owns
 * this App; frontends borrow its state and receive synchronous display events.
 */
#define _POSIX_C_SOURCE 200809L
#include "app.h"
#include "l10n.h"
#include "display_l10n.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const char *const vipirc_manual[];

#ifndef ONEKEY_ONLY
#include "manual_path.h"

/* The language overlay's full manuals live in installed data files, not in
 * IRC traffic or configuration. The English manual stays compiled in. Only
 * one selected document is loaded and retained; absence or rejection falls
 * back to that original English reference. This code never interprets text
 * as a command and never invokes a shell. */
#define MANUAL_FILE_MAX (512u * 1024u)
#define MANUAL_LINE_MAX 8192u
#define MANUAL_LINES_MAX APP_HELP_LINES

static char manual_sibling_dir[PATH_MAX];
static char *manual_owned_text;
static char **manual_owned_lines;
static int manual_checked;

void app_manual_set_executable_path(const char *argv0)
{
    char resolved[PATH_MAX];
    char *slash;
    size_t length;
    if (!argv0 || !strchr(argv0, '/') || !realpath(argv0, resolved)) return;
    slash = strrchr(resolved, '/');
    if (!slash) return;
    *slash = '\0';
    length = strlen(resolved);
    if (length + sizeof "/doc" > sizeof manual_sibling_dir) return;
    memcpy(manual_sibling_dir, resolved, length);
    memcpy(manual_sibling_dir + length, "/doc", sizeof "/doc");
}

static const char *manual_language(void)
{
    const char *value = getenv("VIPIRC_LANG");
    if (!value || !*value) value = getenv("ONEKEYIRC_LANG");
    if (!value || !*value) value = getenv("LC_ALL");
    if (!value || !*value) value = getenv("LC_MESSAGES");
    if (!value || !*value) value = getenv("LANG");
    if (!value) return NULL;
    static const char *const names[] =
        {"id", "ms", "tr", "es", "pt", "fr", "de", "ru", "it", "ar"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
        if (value[0] == names[i][0] && value[1] == names[i][1] &&
            (!value[2] || value[2] == '_' || value[2] == '-' ||
             value[2] == '.' || value[2] == '@')) return names[i];
    }
    return NULL;
}

/* UTF-8 and terminal-control validation. Manual files may be user-installed;
 * never pass ESC, C0/C1, malformed UTF-8 or invisible bidi overrides to the
 * terminal, even if somebody replaces a translated resource after install. */
static int manual_safe_text(const unsigned char *s, size_t length)
{
    size_t i = 0;
    while (i < length) {
        uint32_t code;
        unsigned char c = s[i++];
        if (c < 0x80) {
            if (c != '\n' && c != '\t' && (c < 0x20 || c == 0x7f)) return 0;
            continue;
        }
        size_t trail;
        if (c >= 0xc2 && c <= 0xdf) { code = c & 0x1f; trail = 1; }
        else if (c >= 0xe0 && c <= 0xef) { code = c & 0x0f; trail = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { code = c & 0x07; trail = 3; }
        else return 0;
        if (length - i < trail) return 0;
        for (size_t j = 0; j < trail; ++j) {
            if ((s[i] & 0xc0) != 0x80) return 0;
            code = (code << 6) | (s[i++] & 0x3f);
        }
        if ((trail == 1 && code < 0x80) ||
            (trail == 2 && code < 0x800) ||
            (trail == 3 && code < 0x10000) ||
            code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) ||
            (code >= 0x80 && code <= 0x9f) ||
            code == 0x061c || code == 0x200e || code == 0x200f ||
            (code >= 0x202a && code <= 0x202e) ||
            (code >= 0x2066 && code <= 0x206f) ||
            (code >= 0xfff9 && code <= 0xfffb)) return 0;
    }
    return 1;
}

/* Return 0 on missing, malformed, unsafe or overlarge input, with no partial
 * document committed. Open with NOFOLLOW where available, then fstat the
 * opened handle. All I/O occurs before the manual UI begins drawing. */
static int manual_try_file(const char *dir, const char *lang)
{
    char path[PATH_MAX];
    /* O_NONBLOCK prevents an attacker-supplied FIFO from hanging the client
     * before fstat can reject non-regular files. Regular files ignore it. */
    int fd, flags = O_RDONLY | O_NONBLOCK;
    struct stat st;
    char *body;
    char **lines;
    size_t length, got = 0, count = 0, line_length = 0, pos = 0;
    ssize_t n;
    if (!dir || dir[0] != '/' || !lang) return 0;
    n = snprintf(path, sizeof path, "%s/FULL-MANUAL-%s.txt", dir, lang);
    if (n < 0 || (size_t)n >= sizeof path) return 0;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    fd = open(path, flags);
    if (fd < 0) return 0;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 1 ||
        (uintmax_t)st.st_size > MANUAL_FILE_MAX) { close(fd); return 0; }
    length = (size_t)st.st_size;
    body = malloc(length + 1u);
    if (!body) { close(fd); return 0; }
    while (got < length) {
        n = read(fd, body + got, length - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { free(body); close(fd); return 0; }
        got += (size_t)n;
    }
    char extra;
    do { n = read(fd, &extra, 1); } while (n < 0 && errno == EINTR);
    close(fd);
    if (n != 0 || !manual_safe_text((const unsigned char *)body, length)) {
        free(body);
        return 0;
    }
    for (size_t i = 0; i < length; ++i) {
        if (body[i] == '\n') {
            if (line_length > MANUAL_LINE_MAX) { free(body); return 0; }
            ++count;
            line_length = 0;
        } else ++line_length;
    }
    if (body[length - 1] != '\n') ++count;
    if (line_length > MANUAL_LINE_MAX || !count || count > MANUAL_LINES_MAX) {
        free(body);
        return 0;
    }
    lines = calloc(count + 1u, sizeof *lines);
    if (!lines) { free(body); return 0; }
    for (size_t i = 0, index = 0; i < length; ++i) {
        if (i == pos) lines[index++] = body + pos;
        if (body[i] == '\n') {
            body[i] = '\0';
            pos = i + 1u;
        }
    }
    /* Empty last line is not a separate entry when the file ends in LF. */
    body[length] = '\0';
    manual_owned_text = body;
    manual_owned_lines = lines;
    return 1;
}

const char *const *app_selected_manual(void)
{
    if (!manual_checked) {
        const char *language = manual_language();
        manual_checked = 1;
        if (language) {
            const char *override = getenv("VIPIRC_MANUAL_DIR");
            if (override && *override) {
                /* An explicit override never falls back to another resource
                 * directory, preventing accidental use of stale documents. */
                (void)manual_try_file(override, language);
            } else if (!manual_try_file(manual_sibling_dir, language)) {
                (void)manual_try_file(VIPIRC_MANUAL_INSTALL_DIR, language);
            }
        }
    }
    return manual_owned_lines ? (const char *const *)manual_owned_lines : vipirc_manual;
}
#endif /* !ONEKEY_ONLY */


static void write_output(App *a, int64_t now);

/* A frame belongs to one queue or to inflight, never both. Once inflight,
 * its bytes remain owned until the write finishes or the connection closes.
 * This includes TLS retries which have not accepted any bytes yet. */
struct OutLine {
    struct OutLine *next;
    size_t length, offset;
    uint64_t dcc_id;
    size_t autocommand;
    int chat, sensitive, anchor, bouncer_control;
    int previous_automatic_away, previous_manual_away_pending;
    int previous_automatic_away_clear_pending;
    int previous_backgrounded;
    int64_t previous_automatic_away_retry_at;
    char data[IRC_BODY_MAX + 3];
};

static void copy_text(char *out, size_t cap, const char *text)
{
    size_t n;
    if (!cap) return;
    if (!text) text = "";
    n = strlen(text);
    if (n >= cap) n = cap - 1;
    memmove(out, text, n);
    out[n] = 0;
}

static int bouncer_service(const App *a, const char *name)
{
    if (!a || !name) return 0;
    if (a->cfg.bouncer_type == CONFIG_BOUNCER_SOJU)
        return irc_equal(name, "BouncerServ", IRC_CASE_ASCII);
    if (a->cfg.bouncer_type != CONFIG_BOUNCER_ZNC || name[0] != '*' || !name[1] ||
        strlen(name) > IRC_NICK_MAX) return 0;
    for (size_t i = 1; name[i]; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static int automatic_away_allowed(const App *a)
{
    return a->cfg.bouncer_type == CONFIG_BOUNCER_NONE || a->cfg.bouncer_away;
}

static int write_all_fd(int fd, const char *data, size_t length)
{
    while (length) {
        ssize_t n = write(fd, data, length);
        if (n > 0) {
            data += (size_t)n;
            length -= (size_t)n;
        }
        else if (n < 0 && errno == EINTR) continue;
        else return 0;
    }
    return 1;
}

static void persistent_log_error(App *a, const char *reason)
{
    if (!a || a->persistent_log_failed) return;
    a->persistent_log_failed = 1;
    (void)snprintf(a->status, sizeof a->status, "Persistent logging unavailable for this server: %s", reason);
    a->status_kind = APP_STATUS_GENERAL;
    a->dirty = 1;
    if (a->status_event) a->status_event(a->event_context, a, a->status);
}

/* FNV-1a is a compact accidental-change check, not an authentication tag.
 * The private file's ownership/mode and device/inode are checked separately. */
static uint64_t log_checksum(uint64_t value, const char *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        value ^= (unsigned char)bytes[i];
        value *= UINT64_C(1099511628211);
    }
    return value;
}

int64_t app_catchup_log_end(const Buffer *b, int fd)
{
    struct stat st;
    if (!b || !b->last_own_log_valid || b->last_own_log_offset < 0 ||
        !b->last_own_log_length || b->last_own_log_length >= APP_TEXT + 4096u ||
        fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
        st.st_nlink != 1 || (st.st_mode & 07777) != 0600 || st.st_size < 0 ||
        (uintmax_t)st.st_size > APP_PERSISTENT_LOG_MAX ||
        st.st_dev != b->last_own_log_device || st.st_ino != b->last_own_log_inode ||
        b->last_own_log_offset >= (int64_t)st.st_size ||
        b->last_own_log_length > (uintmax_t)st.st_size - (uintmax_t)b->last_own_log_offset)
        return -1;
    char bytes[1024];
    size_t used = 0;
    uint64_t checksum = UINT64_C(14695981039346656037);
    while (used < b->last_own_log_length) {
        size_t remaining = b->last_own_log_length - used;
        if (remaining > sizeof bytes) remaining = sizeof bytes;
        ssize_t count = pread(fd, bytes, remaining, (off_t)b->last_own_log_offset + (off_t)used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            config_wipe(bytes, sizeof bytes);
            return -1;
        }
        checksum = log_checksum(checksum, bytes, (size_t)count);
        used += (size_t)count;
    }
    config_wipe(bytes, sizeof bytes);
    return checksum == b->last_own_log_checksum ? (int64_t)st.st_size : -1;
}

/* Optional private transcript and exact sent-record anchors. */
static int64_t persistent_log_line(App *a, size_t index, const char *line, int anchor)
{
    if (!a || !a->cfg.persistent_logging || a->test_mode || a->persistent_log_failed ||
        a->persistent_log_suppressed ||
        index >= a->buffer_count || a->buffers[index].kind == B_HELP || a->buffers[index].kind == B_MENTIONS) return -1;
    /* Unsaved secondary profiles do not have an authoritative private sidecar location yet. */
    if (!a->config_path[0]) return -1;
    char error[256] = "Cannot open the private log file";
    /* The helper pins an owned private directory, refuses symlink ancestors,
     * and opens nonblocking so a FIFO cannot stall the check below. */
    int fd = config_open_log(a->config_path, error, sizeof error);
    if (fd < 0) {
        persistent_log_error(a, error);
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
        st.st_nlink != 1 || (st.st_mode & 07777) != 0600) {
        close(fd);
        persistent_log_error(a, "existing log file is not an owned 0600 regular file");
        return -1;
    }
    char prefix[256];
    int p = snprintf(prefix, sizeof prefix, "[%s] ", a->buffers[index].name);
    size_t line_length = strlen(line);
    size_t total = p > 0 ? (size_t)p + line_length + 1u : 0u;
    /* Do not wait on another writer. The lock serializes the size check and
     * append across sessions sharing this profile, without stalling networking. */
    struct flock lock;
    memset(&lock, 0, sizeof lock);
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    if (fcntl(fd, F_SETLK, &lock) < 0) {
        close(fd);
        persistent_log_error(a, "log file is busy or cannot be locked");
        return -1;
    }
    if (fstat(fd, &st) < 0 || st.st_size < 0 ||
        (uintmax_t)st.st_size > APP_PERSISTENT_LOG_MAX ||
        total > APP_PERSISTENT_LOG_MAX - (uintmax_t)st.st_size) {
        close(fd);
        persistent_log_error(a, "16 MiB log limit reached; archive the log and toggle logging off/on");
        return -1;
    }
    int64_t start_offset = (int64_t)st.st_size;
    char *record = p > 0 && (size_t)p < sizeof prefix && total > line_length ? malloc(total) : NULL;
    uint64_t checksum = 0;
    int okay = record != NULL;
    if (record) {
        memcpy(record, prefix, (size_t)p);
        memcpy(record + (size_t)p, line, line_length);
        record[total - 1u] = '\n';
        okay = write_all_fd(fd, record, total);
        if (okay && anchor) checksum = log_checksum(UINT64_C(14695981039346656037), record, total);
        config_wipe(record, total);
        free(record);
    }
    if (close(fd) < 0) okay = 0;
    if (!okay) {
        persistent_log_error(a, "write failed");
        return -1;
    }
    if (anchor) {
        Buffer *b = &a->buffers[index];
        b->last_own_log_device = st.st_dev;
        b->last_own_log_inode = st.st_ino;
        b->last_own_log_length = total;
        b->last_own_log_checksum = checksum;
    }
    return start_offset;
}

static int ascii_contains(const char *haystack, const char *needle)
{
    if (!*needle) return 1;
    for (; *haystack; ++haystack) {
        const unsigned char *h = (const unsigned char *)haystack;
        const unsigned char *n = (const unsigned char *)needle;
        while (*h && *n && tolower(*h) == tolower(*n)) { ++h; ++n; }
        if (!*n) return 1;
    }
    return 0;
}

/* Bounded LIST cache, local filtering and presentation ordering. */
void app_list_clear(App *a)
{
    if (!a) return;
    free(a->list.entries);
    memset(&a->list, 0, sizeof a->list);
    a->list.sort = APP_LIST_SORT_USERS;
    a->list.descending = 1;
    ++a->list.revision;
    a->dirty = 1;
}

static void list_interrupt(App *a)
{
    if (!a->list.active) return;
    /* Only this connection/request can supply its end marker. Preserve the
     * partial cache and filters, but release the request on loss or rejection
     * so a later refresh/clear cannot wait forever for that marker. */
    a->list.active = 0;
    a->list.complete = 0;
    ++a->list.revision;
    a->dirty = 1;
}

static int list_sort_key;
static int list_sort_descending;

static int list_compare(const void *left, const void *right)
{
    const ChannelListEntry *a = left, *b = right;
    int d = 0;
    if (list_sort_key == APP_LIST_SORT_USERS) {
        if (a->users < b->users) d = -1;
        else if (a->users > b->users) d = 1;
    } else {
        const char *x = list_sort_key == APP_LIST_SORT_TOPIC ? a->topic : a->channel;
        const char *y = list_sort_key == APP_LIST_SORT_TOPIC ? b->topic : b->channel;
        while (*x && *y) {
            int cx = tolower((unsigned char)*x), cy = tolower((unsigned char)*y);
            if (cx != cy) { d = cx < cy ? -1 : 1; break; }
            ++x; ++y;
        }
        if (!d && *x != *y) d = (unsigned char)*x < (unsigned char)*y ? -1 : 1;
    }
    if (!d) {
        const unsigned char *x = (const unsigned char *)a->channel;
        const unsigned char *y = (const unsigned char *)b->channel;
        while (*x && *y) {
            int cx = tolower(*x), cy = tolower(*y);
            if (cx != cy) { d = cx < cy ? -1 : 1; break; }
            ++x; ++y;
        }
        if (!d && *x != *y) d = *x < *y ? -1 : 1;
    }
    return list_sort_descending ? -d : d;
}

/* The comparator reads temporary file-scope sort keys.
 * It is safe in this single-threaded event loop, not as a concurrent API.
 * Revision numbers prevent re-sorting an unchanged cached directory. */
void app_list_sort(App *a, int sort, int descending)
{
    if (!a) return;
    if (sort < APP_LIST_SORT_CHANNEL || sort > APP_LIST_SORT_TOPIC) sort = APP_LIST_SORT_USERS;
    descending = !!descending;
    if (a->list.sort != sort || a->list.descending != descending) {
        a->list.sort = sort;
        a->list.descending = descending;
        a->list.sorted_revision = 0;
    }
    if (a->list.count > 1 && a->list.sorted_revision != a->list.revision) {
        list_sort_key = a->list.sort;
        list_sort_descending = a->list.descending;
        qsort(a->list.entries, a->list.count, sizeof *a->list.entries, list_compare);
    }
    a->list.sorted_revision = a->list.revision;
    a->dirty = 1;
}

size_t app_list_visible_count(const App *a)
{
    if (!a) return 0;
    if (a->list.display_limit && a->list.count > a->list.display_limit) return a->list.display_limit;
    return a->list.count;
}

static int list_match(const ChannelList *list, const char *channel, uint64_t users, const char *topic)
{
    if (list->min_set && users < list->min_users) return 0;
    if (list->max_set && users > list->max_users) return 0;
    if (*list->name_filter && !ascii_contains(channel, list->name_filter)) return 0;
    if (*list->topic_filter && !ascii_contains(topic, list->topic_filter)) return 0;
    return 1;
}

static int list_store(App *a, const char *channel, uint64_t users, const char *topic)
{
    ChannelList *list = &a->list;
    char display_topic[APP_LIST_TOPIC];
    /* Directory renderers consume sanitized text. Match and sort the same
     * bounded topic that is displayed, including removal of IRC styling. */
    irc_sanitize(display_topic, sizeof display_topic, topic);
    ++list->received;
    if (!list_match(list, channel, users, display_topic)) return 0;
    ++list->matched;
    if (list->count >= APP_LIST_MAX) {
        list->truncated = 1;
        return 1;
    }
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2u : 128u;
        if (capacity > APP_LIST_MAX) capacity = APP_LIST_MAX;
        ChannelListEntry *entries = realloc(list->entries, capacity * sizeof *entries);
        if (!entries) {
            list->truncated = 1;
            return 1;
        }
        list->entries = entries;
        list->capacity = capacity;
    }
    ChannelListEntry *entry = &list->entries[list->count++];
    copy_text(entry->channel, sizeof entry->channel, channel);
    entry->users = users;
    copy_text(entry->topic, sizeof entry->topic, display_topic);
    ++list->revision;
    list->sorted_revision = 0;
    a->dirty = 1;
    return 1;
}

/* Mode selection is permanent for this process. All transport entry points,
 * including the clock, branch before touching the real-network module. */
/* Uniform transport access: real Net or in-process Sim, never both. */
int64_t app_now_ms(const App *a)
{
    return a->test_mode ? sim_now_ms() : net_now_ms();
}
static int io_has(const App *a)
{
    return a->test_mode ? a->sim != NULL : a->net != NULL;
}
static NetState io_state(const App *a)
{
    return a->test_mode ? (sim_failed(a->sim) ? NET_FAILED : NET_READY) : net_state(a->net);
}
static void io_close(App *a)
{
    if (a->test_mode) {
        sim_close(a->sim);
        a->sim = NULL;
    } else {
        if (a->net) net_close(a->net);
        a->net = NULL;
    }
}
static const char *io_error(const App *a)
{
    return a->test_mode ? sim_error(a->sim) : net_error(a->net);
}
static int io_pending(const App *a)
{
    return a->test_mode ? sim_pending(a->sim) : net_pending(a->net);
}
static int io_step(App *a, short events, int64_t now)
{
    if (a->test_mode) {
        size_t word = a->cfg.word_count ? sim_watch_index(a->sim) % a->cfg.word_count : 0;
        return sim_step(a->sim, now, a->cfg.word_count ? a->cfg.words[word] : "");
    }
    return net_step(a->net, events, now);
}
static ssize_t io_read(App *a, void *out, size_t cap)
{
    return a->test_mode ? sim_read(a->sim, out, cap) : net_read(a->net, out, cap);
}
static ssize_t io_write(App *a, const void *data, size_t size)
{
    return a->test_mode ? sim_write(a->sim, data, size) : net_write(a->net, data, size);
}
static int auth_required(const App *a)
{
    return !a->test_mode && !strcmp(a->cfg.auth, "sasl");
}
static const char *session_channels(const App *a)
{
    return a->test_mode ? a->test_channels : a->cfg.channels;
}

int app_set_test_mode(App *a)
{
    if (a->test_mode) return 1;
    if (a->net || a->sim) {
        app_status(a, "%s", irc_tr("Enable offline test mode before opening a connection."));
        return 0;
    }
    a->test_mode = 1;
    copy_text(a->test_channels, sizeof(a->test_channels),
              a->cfg.channels[0] ? a->cfg.channels : "#test-one,#test-two");
    app_status(a, "%s", irc_tr("OFFLINE TEST MODE stopped. /connect starts the in-process simulator."));
    return 1;
}

static void stamp(char *out, size_t cap)
{
    time_t now = time(NULL);
    struct tm local;
    if (!localtime_r(&now, &local) ||
        !strftime(out, cap, "%Y-%m-%d %H:%M:%S", &local))
        copy_text(out, cap, "0000-00-00 00:00:00");
}

static void forget_closed_query(Buffer *b, size_t position)
{
    if (!b->line_queries || !b->line_queries[position][0]) return;
    b->line_queries[position][0] = 0;
    if (b->line_query_count) --b->line_query_count;
    if (!b->line_query_count) {
        free(b->line_queries);
        b->line_queries = NULL;
    }
}

/* Retained conversation rings and synchronous presentation callbacks. */
static void line_pop(App *a, Buffer *b)
{
    char *line;
    if (!b->line_count)
        return;
    line = b->lines[b->line_start];
    a->log_bytes -= strlen(line) + 1;
    free(line);
    b->lines[b->line_start] = NULL;
    if (b->line_origins) b->line_origins[b->line_start] = 0;
    forget_closed_query(b, b->line_start);
    if (b->last_own_retained && b->last_own_from_end + 1u == b->line_count)
        b->last_own_retained = 0;
    b->line_start = (b->line_start + 1) % b->line_cap;
    --b->line_count;
}

static void line_activity(App *a, size_t index, int activity, int mention)
{
    Buffer *b = &a->buffers[index];
    if (activity && (!a->visible || index != a->current)) {
        if (b->unread < INT_MAX) ++b->unread;
        if (mention && b->unread_mentions < INT_MAX) ++b->unread_mentions;
    }
    a->dirty = 1;
}

static void line_store(App *a, size_t index, const char *line,
                       int activity, int mention, size_t source, int persist, int anchor)
{
    Buffer *b;
    size_t bytes, i;
    char *p;
    if (index >= a->buffer_count)
        return;
    b = &a->buffers[index];
    if (a->line_event) a->line_event(a->event_context, a, index, line, mention, anchor);
    if (anchor) {
        b->last_own_log_valid = 0;
        b->last_own_retained = 0;
    }
    int64_t log_offset = persist ? persistent_log_line(a, index, line, anchor) : -1;
    if (anchor && log_offset >= 0) {
        b->last_own_log_offset = log_offset;
        b->last_own_log_valid = 1;
    }
    if (a->stream_only) {
        line_activity(a, index, activity, mention);
        return;
    }
    bytes = strlen(line) + 1;
    if (bytes > APP_LOG_BYTES)
        return;
    while (a->log_bytes + bytes > APP_LOG_BYTES) {
        for (i = 0; i < a->buffer_count; ++i)
            if (i != 2 && a->buffers[i].line_count)
                break;
        if (i == a->buffer_count) {
            if (a->buffer_count > 2 && a->buffers[2].line_count) i = 2;
            else return;
        }
        line_pop(a, &a->buffers[i]);
    }
    if (b->line_count == b->line_cap)
        line_pop(a, b);
    p = malloc(bytes);
    if (!p) {
        app_status(a, "%s", irc_tr("Out of memory; a display line was not retained."));
        return;
    }
    memcpy(p, line, bytes);
    if (anchor) {
        b->last_own_retained = 1;
        b->last_own_from_end = 0;
    }
    else if (b->last_own_retained && b->last_own_from_end < SIZE_MAX) ++b->last_own_from_end;
    size_t position = (b->line_start + b->line_count) % b->line_cap;
    b->lines[position] = p;
    if (b->line_origins)
        b->line_origins[position] = source < a->buffer_count ? a->buffers[source].id : 0;
    ++b->line_count;
    a->log_bytes += bytes;
    if (b->scroll && b->scroll != SIZE_MAX)
        ++b->scroll;
    line_activity(a, index, activity, mention);
}

static void status_format(App *a, AppStatusKind kind, const char *format, va_list ap)
{
    char text[1024];
    (void)vsnprintf(text, sizeof(text), format, ap);
    irc_sanitize(a->status, sizeof(a->status), text);
    a->status_kind = kind;
    a->dirty = 1;
    if (a->status_event) a->status_event(a->event_context, a, a->status);
}

void app_status(App *a, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    status_format(a, APP_STATUS_GENERAL, format, ap);
    va_end(ap);
}

void app_queue_notice(App *a, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    status_format(a, APP_STATUS_QUEUE_NOTICE, format, ap);
    va_end(ap);
}

void app_program_notice(App *a, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    status_format(a, APP_STATUS_PROGRAM_NOTICE, format, ap);
    va_end(ap);
}

const char *app_status_text(const App *a)
{
    if (a->status_kind == APP_STATUS_QUEUE_NOTICE && !a->cfg.queue_notices) return "";
    if (a->status_kind == APP_STATUS_PROGRAM_NOTICE && !a->cfg.program_messages) return "";
    return a->status;
}

void app_log(App *a, size_t view, const char *format, ...)
{
    char raw[APP_TEXT + 1024], safe[APP_TEXT + 1024];
    char line[APP_TEXT + 1088], timestamp[32];
    va_list ap;
    va_start(ap, format);
    (void)vsnprintf(raw, sizeof(raw), format, ap);
    va_end(ap);
    irc_sanitize(safe, sizeof(safe), raw);
    if (a->message_time[0]) copy_text(timestamp, sizeof timestamp, a->message_time);
    else stamp(timestamp, sizeof(timestamp));
    (void)snprintf(line, sizeof(line), "[%s] %s", timestamp, safe);
    line_store(a, view, line, !a->message_history, 0, view, 1, 0);
}

size_t app_buffer(App *a, const char *name, BufferKind kind, int create)
{
    size_t i;
    Buffer *b;
    for (i = 0; i < a->buffer_count; ++i)
        if (a->buffers[i].kind == kind &&
            irc_equal(a->buffers[i].name, name, a->casemap))
            return i;
    if (!create)
        return SIZE_MAX;
    if (a->buffer_count == APP_BUFFERS || strlen(name) > IRC_TARGET_MAX || a->next_buffer_id == UINT64_MAX) {
        app_status(a, "%s", irc_tr("View limit reached (32); message shown in the server view."));
        return SIZE_MAX;
    }
    i = a->buffer_count;
    b = &a->buffers[i];
    memset(b, 0, sizeof(*b));
    b->kind = kind;
    irc_sanitize(b->name, sizeof(b->name), name);
    b->line_cap = kind == B_HELP && a->cfg.history_lines < APP_HELP_LINES ?
                  APP_HELP_LINES : (size_t)a->cfg.history_lines;
    b->lines = calloc(b->line_cap, sizeof(*b->lines));
    if (kind == B_MENTIONS) b->line_origins = calloc(b->line_cap, sizeof(*b->line_origins));
    if (!b->lines || (kind == B_MENTIONS && !b->line_origins)) {
        free(b->lines);
        free(b->line_origins);
        memset(b, 0, sizeof *b);
        app_status(a, "%s", irc_tr("Cannot allocate a view."));
        return SIZE_MAX;
    }
    b->id = ++a->next_buffer_id;
    ++a->buffer_count;
    a->dirty = 1;
    return i;
}

void app_switch(App *a, size_t index)
{
    if (index >= a->buffer_count || a->buffers[index].kind == B_HELP)
        return;
    a->current = index;
    if (a->visible) {
        a->buffers[index].unread = 0;
        a->buffers[index].unread_mentions = 0;
    }
    a->dirty = 1;
}

void app_set_visible(App *a, int visible)
{
    int changed = a->visible != !!visible;
    a->visible = !!visible;
    if (a->visible && a->current < a->buffer_count) {
        Buffer *b = &a->buffers[a->current];
        if (b->unread || b->unread_mentions) {
            b->unread = b->unread_mentions = 0;
            changed = 1;
        }
    }
    if (changed) a->dirty = 1;
}

void app_activity(const App *a, size_t *unread, size_t *mentions)
{
    size_t total_unread = 0, total_mentions = 0;
    for (size_t i = 0; i < a->buffer_count; ++i) {
        const Buffer *b = &a->buffers[i];
        size_t add;
        if (b->kind == B_MENTIONS || b->kind == B_HELP) continue;
        add = b->unread > 0 ? (size_t)b->unread : 0;
        total_unread = add > SIZE_MAX - total_unread ? SIZE_MAX : total_unread + add;
        add = b->unread_mentions > 0 ? (size_t)b->unread_mentions : 0;
        total_mentions = add > SIZE_MAX - total_mentions ? SIZE_MAX : total_mentions + add;
    }
    if (unread) *unread = total_unread;
    if (mentions) *mentions = total_mentions;
}

uint64_t app_mention_origin(const App *a, size_t logical_index)
{
    if (!a || a->buffer_count < 2) return 0;
    const Buffer *mentions = &a->buffers[1];
    if (!mentions->line_origins || logical_index >= mentions->line_count) return 0;
    return mentions->line_origins[(mentions->line_start + logical_index) % mentions->line_cap];
}

size_t app_mention_view(const App *a, size_t logical_index)
{
    uint64_t origin = app_mention_origin(a, logical_index);
    if (!origin) return SIZE_MAX;
    for (size_t i = 0; i < a->buffer_count; ++i)
        if (a->buffers[i].id == origin) return i;
    return SIZE_MAX;
}

size_t app_open_mention(App *a, uint64_t origin)
{
    if (!a || a->buffer_count < 2 || !origin) return SIZE_MAX;
    for (size_t i = 0; i < a->buffer_count; ++i)
        if (a->buffers[i].id == origin) return i;
    Buffer *mentions = &a->buffers[1];
    char query[64] = "";
    int retained = 0;
    for (size_t i = 0; i < mentions->line_count; ++i) {
        size_t position = (mentions->line_start + i) % mentions->line_cap;
        if (!mentions->line_origins || mentions->line_origins[position] != origin) continue;
        retained = 1;
        if (mentions->line_queries && mentions->line_queries[position][0]) {
            copy_text(query, sizeof query, mentions->line_queries[position]);
            break;
        }
    }
    if (!query[0]) {
        app_status(a, "%s", retained ? irc_tr("That channel view is closed. Join it again or clear mentions.") :
                   "That source conversation is no longer available.");
        return SIZE_MAX;
    }
    if (!irc_valid_nick(query)) {
        app_status(a, "%s", irc_tr("That retained query nickname is invalid. Clear mentions to remove its records."));
        return SIZE_MAX;
    }
    size_t view = app_buffer(a, query, B_QUERY, 1);
    if (view == SIZE_MAX) return SIZE_MAX;
    uint64_t replacement = a->buffers[view].id;
    for (size_t i = 0; i < mentions->line_count; ++i) {
        size_t position = (mentions->line_start + i) % mentions->line_cap;
        if (mentions->line_origins[position] != origin) continue;
        mentions->line_origins[position] = replacement;
        forget_closed_query(mentions, position);
    }
    app_program_notice(a, "Opened query %s. No message was sent.", query);
    return view;
}

/* A closed query remains an explicit local destination while a mention from it
 * is retained. Allocate only at the first such close; names are never parsed
 * from peer-controlled display text. A failed allocation preserves the query. */
static int remember_closed_query(App *a, const Buffer *source)
{
    if (source->kind != B_QUERY || a->buffer_count < 2) return 1;
    Buffer *mentions = &a->buffers[1];
    size_t matches = 0;
    if (!mentions->line_origins) return 1;
    for (size_t i = 0; i < mentions->line_count; ++i)
        if (mentions->line_origins[(mentions->line_start + i) % mentions->line_cap] == source->id)
            ++matches;
    if (!matches) return 1;
    if (!irc_valid_nick(source->name)) {
        app_status(a, "%s", irc_tr("Cannot retain this query's invalid nickname. Clear mentions before closing it."));
        return 0;
    }
    if (!mentions->line_queries) {
        mentions->line_queries = calloc(mentions->line_cap, sizeof(*mentions->line_queries));
        if (!mentions->line_queries) {
            app_status(a, "%s", irc_tr("Cannot retain this query's mentions: out of memory. Clear mentions before closing it."));
            return 0;
        }
    }
    for (size_t i = 0; i < mentions->line_count; ++i) {
        size_t position = (mentions->line_start + i) % mentions->line_cap;
        if (mentions->line_origins[position] != source->id) continue;
        if (!mentions->line_queries[position][0]) ++mentions->line_query_count;
        copy_text(mentions->line_queries[position], sizeof(*mentions->line_queries), source->name);
    }
    return 1;
}

void app_cycle(App *a, int direction)
{
    if (!a->buffer_count || !direction) return;
    size_t index = a->current;
    for (size_t i = 0; i < a->buffer_count; ++i) {
        index = direction > 0 ? (index + 1) % a->buffer_count :
                (index + a->buffer_count - 1) % a->buffer_count;
        if (a->buffers[index].kind != B_HELP) {
            app_switch(a, index);
            return;
        }
    }
}

void app_clear_buffer(App *a, size_t index)
{
    Buffer *b;
    if (index >= a->buffer_count)
        return;
    b = &a->buffers[index];
    while (b->line_count)
        line_pop(a, b);
    b->scroll = 0;
    b->unread = 0;
    b->unread_mentions = 0;
    a->dirty = 1;
}

size_t app_clear_mentions(App *a)
{
    if (!a || a->buffer_count < 2 || a->buffers[1].kind != B_MENTIONS) return 0;
    size_t count = a->buffers[1].line_count;
    app_clear_buffer(a, 1);
    return count;
}

void app_help(App *a)
{
#ifdef ONEKEY_ONLY
    app_status(a, "%s", irc_tr("Use /help ls for help topics or /help commands for command syntax."));
#else
    size_t i;
    const char *const *manual = app_selected_manual();
    app_clear_buffer(a, 2);
    for (i = 0; manual[i]; ++i)
        line_store(a, 2, manual[i], 0, 0, SIZE_MAX, 0, 0);
    a->buffers[2].unread = 0;
    a->buffers[2].scroll = SIZE_MAX;
#endif
}

/* App lifetime and live-versus-next-connection configuration. */
int app_init(App *a, const Config *cfg, const char *path)
{
    memset(a, 0, sizeof(*a));
    a->cfg = *cfg;
    a->visible = 1;
    if (cfg->history_lines < 50 || cfg->history_lines > 5000)
        return 0;
    copy_text(a->config_path, sizeof(a->config_path), path);
    copy_text(a->nick, sizeof(a->nick), cfg->nick);
    a->casemap = IRC_CASE_RFC1459;
    a->wire_limit = 512;
    a->list.sort = APP_LIST_SORT_USERS;
    a->list.descending = 1;
    copy_text(a->prefix_modes, sizeof(a->prefix_modes), "ov");
    copy_text(a->prefix_chars, sizeof(a->prefix_chars), "@+");
    copy_text(a->chanmodes, sizeof(a->chanmodes), "b,k,l,imnst");
    if (app_buffer(a, "server", B_SERVER, 1) != 0 ||
        app_buffer(a, "mentions", B_MENTIONS, 1) != 1 ||
        app_buffer(a, "help", B_HELP, 1) != 2) {
        app_free(a);
        return 0;
    }
    app_status(a, "%s", irc_tr("Type /help for the manual. /connect starts the configured connection."));
    return 1;
}

typedef struct StartupField {
    size_t offset, size;
    int text;
} StartupField;
#define STARTUP_TEXT(field) { offsetof(Config, field), sizeof(((Config *)0)->field), 1 }
#define STARTUP_INT(field) { offsetof(Config, field), sizeof(((Config *)0)->field), 0 }
#define STARTUP_BYTES(field) { offsetof(Config, field), sizeof(((Config *)0)->field), 2 }
static const StartupField startup_fields[] = {
    STARTUP_TEXT(server), STARTUP_INT(port), STARTUP_INT(tls),
    STARTUP_TEXT(nick), STARTUP_TEXT(username), STARTUP_TEXT(realname),
    STARTUP_TEXT(channels), STARTUP_TEXT(auth), STARTUP_TEXT(auth_user),
    STARTUP_TEXT(password_file), STARTUP_TEXT(password_gpg_file), STARTUP_TEXT(password),
    STARTUP_TEXT(server_password_file), STARTUP_TEXT(server_password_gpg_file),
    STARTUP_TEXT(server_password), STARTUP_TEXT(ca_file), STARTUP_BYTES(autocommands),
    STARTUP_INT(bouncer_type), STARTUP_TEXT(bouncer_user), STARTUP_TEXT(bouncer_network),
    STARTUP_TEXT(bouncer_client), STARTUP_INT(bouncer_away)
};
#undef STARTUP_TEXT
#undef STARTUP_INT
#undef STARTUP_BYTES
_Static_assert(sizeof(startup_fields) / sizeof(startup_fields[0]) <= sizeof(unsigned) * CHAR_BIT,
               "pending settings mask must hold all startup fields");

/* Produce the desired next-connection configuration without changing the
 * live connection identity. This value can contain credentials; callers must
 * wipe temporary copies after use, just as they do loaded configurations. */
void app_config_effective(const App *a, Config *out)
{
    size_t i;
    memcpy(out, &a->cfg, sizeof(*out));
    for (i = 0; i < sizeof(startup_fields) / sizeof(startup_fields[0]); ++i) {
        const StartupField *field = &startup_fields[i];
        if (a->config_pending_mask & (1u << i))
            memcpy((char *)out + field->offset,
                   (const char *)&a->config_pending + field->offset, field->size);
    }
}

static int settings_error(char *error, size_t cap, const char *message)
{
    if (error && cap) (void)snprintf(error, cap, "%s", message);
    return 0;
}

static int save_configuration(App *a, const Config *cfg, char *error, size_t cap)
{
    int result = a->save_config ? a->save_config(a->save_context, cfg, error, cap) :
                                config_save(cfg, a->config_path, error, cap);
    if (result > 0) a->config_saved = 1;
    return result;
}

/* Prepare all replacement history storage before
 * publishing config changes. The live connection and its next-connect
 * settings are distinct; a failed apply must not leave half a resize. */
int app_config_apply(App *a, const Config *candidate, int save,
                     char *error, size_t error_cap)
{
    Config desired;
    char **replacement[APP_BUFFERS] = { 0 };
    uint64_t *origins[APP_BUFFERS] = { 0 };
    char (*queries[APP_BUFFERS])[64] = { 0 };
    size_t capacities[APP_BUFFERS] = { 0 };
    size_t i, j;
    unsigned pending = 0;
    memcpy(&desired, candidate, sizeof(desired));
    Config previous;
    app_config_effective(a, &previous);
    if ((strncmp(desired.server, previous.server, sizeof desired.server) ||
         desired.port != previous.port || desired.tls != previous.tls ||
         desired.bouncer_type != previous.bouncer_type ||
         strncmp(desired.bouncer_user, previous.bouncer_user, sizeof desired.bouncer_user) ||
         strncmp(desired.bouncer_network, previous.bouncer_network, sizeof desired.bouncer_network)) &&
        desired.autocommands.count == previous.autocommands.count &&
        !memcmp(desired.autocommands.lines, previous.autocommands.lines, sizeof desired.autocommands.lines)) {
        desired.autocommands.count = 0;
        config_wipe(desired.autocommands.lines, sizeof desired.autocommands.lines);
    }
    config_wipe(&previous, sizeof previous);
    if (config_validate(&desired, error, error_cap) < 0) {
        config_wipe(&desired, sizeof desired); return 0;
    }
    if (save && a->test_mode) {
        config_wipe(&desired, sizeof desired);
        return settings_error(error, error_cap,
                              "OFFLINE TEST MODE: Save is disabled; configuration was not written.");
    }

    for (i = 0; i < sizeof(startup_fields) / sizeof(startup_fields[0]); ++i) {
        const StartupField *field = &startup_fields[i];
        const char *wanted = (const char *)&desired + field->offset;
        const char *running = (const char *)&a->cfg + field->offset;
        int different = field->text == 1 ? strcmp(wanted, running) != 0 :
                        field->text == 2 ? memcmp(wanted, running, field->size) != 0 :
                        *(const int *)(const void *)wanted != *(const int *)(const void *)running;
        if (different) pending |= 1u << i;
    }

    /* Allocate before writing or mutating anything. A failed Save cannot
     * discard history, partially apply preferences or overwrite pending edits. */
    for (i = 0; i < a->buffer_count; ++i) {
        const Buffer *b = &a->buffers[i];
        capacities[i] = b->kind == B_HELP && desired.history_lines < APP_HELP_LINES ?
                        APP_HELP_LINES : (size_t)desired.history_lines;
        if (capacities[i] != b->line_cap) {
            replacement[i] = calloc(capacities[i], sizeof(*replacement[i]));
            if (b->line_origins) origins[i] = calloc(capacities[i], sizeof(*origins[i]));
            if (b->line_queries) queries[i] = calloc(capacities[i], sizeof(*queries[i]));
            if (!replacement[i] || (b->line_origins && !origins[i]) ||
                (b->line_queries && !queries[i])) {
                for (j = 0; j < a->buffer_count; ++j) {
                    free(replacement[j]); free(origins[j]); free(queries[j]);
                }
                config_wipe(&desired, sizeof desired);
                return settings_error(error, error_cap, "Cannot resize message history; settings were not applied.");
            }
        }
    }
    if (save && save_configuration(a, &desired, error, error_cap) < 0) {
        for (i = 0; i < a->buffer_count; ++i) {
            free(replacement[i]); free(origins[i]); free(queries[i]);
        }
        config_wipe(&desired, sizeof desired);
        return 0;
    }

    for (i = 0; i < a->buffer_count; ++i) {
        Buffer *b = &a->buffers[i];
        if (!replacement[i]) continue;
        while (b->line_count > capacities[i]) line_pop(a, b);
        for (j = 0; j < b->line_count; ++j) {
            replacement[i][j] = b->lines[(b->line_start + j) % b->line_cap];
            if (origins[i]) origins[i][j] = b->line_origins[(b->line_start + j) % b->line_cap];
            if (queries[i] && b->line_queries)
                memcpy(queries[i][j], b->line_queries[(b->line_start + j) % b->line_cap], sizeof(*queries[i]));
        }
        free(b->lines);
        free(b->line_origins);
        free(b->line_queries);
        b->lines = replacement[i];
        b->line_origins = origins[i];
        b->line_queries = queries[i];
        if (!b->line_query_count) { free(b->line_queries); b->line_queries = NULL; }
        b->line_cap = capacities[i];
        b->line_start = 0;
    }
    a->cfg.autoconnect = desired.autoconnect;
    a->cfg.user_list = desired.user_list;
    a->cfg.mouse = desired.mouse;
    a->cfg.hints = desired.hints;
    a->cfg.shortcut_labels = desired.shortcut_labels;
    a->cfg.focus_style = desired.focus_style;
    a->cfg.history_lines = desired.history_lines;
    a->cfg.send_review = desired.send_review;
    a->cfg.output_full = desired.output_full;
    a->cfg.queue_notices = desired.queue_notices;
    a->cfg.program_messages = desired.program_messages;
    if (a->cfg.persistent_logging != desired.persistent_logging) {
        a->persistent_log_failed = 0;
        /* Turning logging off or back on creates a durability boundary. An
         * older byte offset must never be mistaken for the latest activity
         * in a new/replaced log. The bounded in-memory anchor remains valid. */
        for (i = 0; i < a->buffer_count; ++i) a->buffers[i].last_own_log_valid = 0;
    }
    a->cfg.persistent_logging = desired.persistent_logging;
    a->cfg.single_session = desired.single_session;
    a->cfg.background_away = desired.background_away;
    a->cfg.autoaway_minutes = desired.autoaway_minutes;
    copy_text(a->cfg.part_message, sizeof a->cfg.part_message, desired.part_message);
    copy_text(a->cfg.quit_message, sizeof a->cfg.quit_message, desired.quit_message);
    copy_text(a->cfg.background_away_message, sizeof a->cfg.background_away_message, desired.background_away_message);
    copy_text(a->cfg.autoaway_message, sizeof a->cfg.autoaway_message, desired.autoaway_message);
    a->cfg.default_interface = desired.default_interface;
    a->cfg.command_suggestions = desired.command_suggestions;
    a->cfg.composer_fullscreen = desired.composer_fullscreen;
    a->cfg.use_signals = desired.use_signals;
    memcpy(a->cfg.hotkeys, desired.hotkeys, sizeof a->cfg.hotkeys);
    a->cfg.hotkey_count = desired.hotkey_count;
    memcpy(a->cfg.command_usage, desired.command_usage, sizeof a->cfg.command_usage);
    a->cfg.command_usage_count = desired.command_usage_count;
    /* Presentation format is an initialization choice, not a setting change. */
    memcpy(a->cfg.words, desired.words, sizeof(a->cfg.words));
    a->cfg.word_count = desired.word_count;
    memcpy(a->cfg.ignores, desired.ignores, sizeof(a->cfg.ignores));
    a->cfg.ignore_count = desired.ignore_count;
    memcpy(&a->config_pending, &desired, sizeof(desired));
    config_wipe(&desired, sizeof desired);
    a->config_pending_mask = pending;
    a->dirty = 1;
    if (error && error_cap) error[0] = 0;
    return 1;
}

/* Queue ownership and automatic-command cancellation bookkeeping. */
static void outline_free(OutLine *line)
{
    if (line) {
        config_wipe(line, sizeof(*line));
        free(line);
    }
}

static void queue_clear(App *a)
{
    OutLine *p, *next;
    a->autocommand_presence = NULL;
    for (p = a->control_head; p; p = next) {
        next = p->next;
        outline_free(p);
    }
    for (p = a->chat_head; p; p = next) {
        next = p->next;
        outline_free(p);
    }
    outline_free(a->inflight);
    a->control_head = a->control_tail = NULL;
    a->chat_head = a->chat_tail = a->inflight = NULL;
    a->control_count = a->chat_count = 0;
    a->autocommand_next = 0;
    a->autocommand_active = a->autocommand_waiting = 0;
    a->autocommand_due = 0;
}

static void autocommand_presence_restore(App *a, const OutLine *line)
{
    if (a->autocommand_presence != line) return;
    a->automatic_away = line->previous_automatic_away;
    a->manual_away_pending = line->previous_manual_away_pending;
    a->automatic_away_clear_pending = line->previous_automatic_away_clear_pending;
    a->automatic_away_retry_at = line->previous_automatic_away_retry_at;
    /* A re-detach may occur while a script temporarily owns presence. Its
     * cancellation must not revive an older foreground clear request. */
    if (a->automatic_away_clear_pending && !line->previous_backgrounded &&
        a->backgrounded && a->cfg.background_away && automatic_away_allowed(a)) {
        a->automatic_away_clear_pending = 0;
        a->automatic_away_retry_at = 0;
    }
    a->autocommand_presence = NULL;
}

static int autocommand_presence_confirm(App *a, int away)
{
    OutLine *line = a->autocommand_presence;
    if (!line) return 0;
    /* This script frame is still unsent, so the reply acknowledges earlier
     * state. Update its rollback snapshot without acknowledging its intent. */
    if (away) line->previous_manual_away_pending = 0;
    else {
        line->previous_automatic_away = APP_AUTO_AWAY_NONE;
        line->previous_automatic_away_clear_pending = 0;
        line->previous_automatic_away_retry_at = 0;
    }
    return 1;
}

size_t app_autocommands_cancel(App *a)
{
    size_t cancelled = a->autocommand_active && a->autocommand_next < a->cfg.autocommands.count ?
                       a->cfg.autocommands.count - a->autocommand_next : 0;
    for (int chat = 0; chat <= 1; ++chat) {
        OutLine **link = chat ? &a->chat_head : &a->control_head;
        OutLine *last = NULL, *line;
        size_t removed = 0;
        while ((line = *link) != NULL) {
            if (line->autocommand) {
                *link = line->next;
                autocommand_presence_restore(a, line);
                outline_free(line);
                ++removed;
            } else {
                last = line;
                link = &line->next;
            }
        }
        if (chat) {
            a->chat_tail = last;
            a->chat_count -= removed;
        } else {
            a->control_tail = last;
            a->control_count -= removed;
        }
        cancelled += removed;
    }
    /* TLS may require the original buffer even before any bytes are accepted.
     * Only disconnect may discard an in-flight frame. */
    a->autocommand_active = 0;
    a->autocommand_waiting = a->inflight && a->inflight->autocommand;
    a->autocommand_due = 0;
    if (cancelled) a->dirty = 1;
    return cancelled;
}

void app_free(App *a)
{
    size_t i;
    a->autocommand_presence = NULL;
    if (a->credential) credential_cancel(a->credential);
    a->credential = NULL;
    io_close(a);
    queue_clear(a);
    for (i = 0; i < a->buffer_count; ++i) {
        app_clear_buffer(a, i);
        free(a->buffers[i].lines);
        free(a->buffers[i].line_origins);
        free(a->buffers[i].line_queries);
        free(a->buffers[i].users);
    }
    free(a->list.entries);
    a->list.entries = NULL;
    config_wipe(a, sizeof(*a));
}

const char *app_connection_label(const App *a)
{
    if (a->test_mode)
        return a->sim ? (a->away ? "OFFLINE TEST MODE (running, away)" :
                        "OFFLINE TEST MODE (running)") : "OFFLINE TEST MODE (stopped)";
    if (!io_has(a))
        return a->reconnect_at ? "waiting to reconnect" : "disconnected";
    switch (io_state(a)) {
    case NET_RESOLVING: return "resolving";
    case NET_CONNECTING: return "connecting";
    case NET_HANDSHAKE: return "verifying TLS";
    case NET_READY:
        if (a->credential) return "decrypting credential (verified TLS)";
        if (!a->registered) return "registering";
        if (a->cfg.tls) return a->away ? "TLS connected (away)" : "TLS connected";
        return a->away ? "plaintext connected (away)" : "plaintext connected";
    case NET_FAILED: return "connection failed";
    }
    return "disconnected";
}

/* End protocol/queue state while preserving retained conversation buffers.
 * Automatic reconnect restores its own retry bookkeeping after this reset. */
void app_disconnect(App *a, const char *reason)
{
    size_t i;
    char safe[512];
    irc_sanitize(safe, sizeof(safe), reason ? reason : "Disconnected");
    a->autocommand_presence = NULL;
    a->reconnect_at = a->registered_at = 0;
    a->reconnect_attempts = 0;
    if (a->credential) credential_cancel(a->credential);
    a->credential = NULL;
    a->credential_kind = 0;
    io_close(a);
    queue_clear(a);
    list_interrupt(a);
    a->registered = a->started = a->cap_done = 0;
    a->cap_sasl = a->cap_time = a->cap_multi = a->cap_batch = a->cap_self = 0;
    a->cap_offered = a->cap_pending = 0;
    memset(a->batches, 0, sizeof a->batches);
    a->away = 0;
    a->automatic_away = APP_AUTO_AWAY_NONE;
    a->automatic_away_clear_pending = 0;
    a->automatic_away_retry_at = 0;
    a->manual_away_pending = 0;
    a->auth_started = a->auth_sent = a->authenticated = 0;
    a->incoming_len = 0;
    for (i = 0; i < a->buffer_count; ++i) {
        a->buffers[i].joined = 0;
        a->buffers[i].background_join = 0;
        a->buffers[i].user_count = 0;
        a->buffers[i].names_in_progress = 0;
        a->buffers[i].names_truncated = 0;
    }
    if (a->test_mode) {
        app_log(a, 0, "OFFLINE TEST MODE stopped: %s. Pending output cleared; /connect restarts simulation.", safe);
        {
        char localized[1024];
        const IrcDisplayField fields[] = {{"error", safe}};
        if (irc_display_render(localized, sizeof localized, "OFFLINE TEST MODE stopped: {error}", fields, 1))
            app_status(a, "%s", localized);
        else app_status(a, "OFFLINE TEST MODE stopped: %s", safe);
    }
    } else {
        app_log(a, 0, "%s. Pending output cleared; use /connect to reconnect.", safe);
        app_status(a, "%s", safe);
    }
}

void app_set_reconnect(App *a, int enabled)
{
    a->reconnect_enabled = !!enabled && !a->test_mode;
    if (!a->reconnect_enabled) {
        a->reconnect_at = 0;
        a->reconnect_attempts = 0;
    }
}

/* Local/manual/scripted presence share one ordered ownership model. */
static int presence_text_valid(const char *text)
{
    if (!text || strnlen(text, 256) >= 256 || !irc_utf8_valid(text)) return 0;
    for (size_t i = 0; text[i]; ++i)
        if ((unsigned char)text[i] < 32 || (unsigned char)text[i] == 127) return 0;
    return 1;
}

static void automatic_away_retry(App *a)
{
    int64_t now = app_now_ms(a);
    a->automatic_away_retry_at = now > INT64_MAX - 1000 ? INT64_MAX : now + 1000;
}

static int set_automatic_away(App *a, int kind, const char *message)
{
    if (!a || kind == APP_AUTO_AWAY_NONE || !message || !*message ||
        !a->registered || !io_has(a) || io_state(a) != NET_READY) return 0;
    if (!automatic_away_allowed(a)) return 1;
    /* Manual/configured away always wins. Do not replace it just because the
     * terminal detached or the local-idle deadline expired. */
    if ((a->away && a->automatic_away == APP_AUTO_AWAY_NONE) || a->manual_away_pending) return 1;
    if (a->automatic_away == kind) return 1;
    if (!presence_text_valid(message) || !app_outgoing(a, 0, "AWAY :%s", message)) {
        /* A server can reduce LINELEN below a configured away message.
         * Failed automatic output must not leave an expired zero-timeout
         * timer spinning; retain the request and retry at a bounded rate. */
        automatic_away_retry(a);
        return 0;
    }
    a->automatic_away = kind;
    a->autocommand_presence = NULL;
    a->automatic_away_clear_pending = 0;
    a->automatic_away_retry_at = 0;
    a->manual_away_pending = 0;
    return 1;
}

static void clear_automatic_away(App *a)
{
    if (!a) return;
    if (a->automatic_away == APP_AUTO_AWAY_NONE) {
        a->automatic_away_clear_pending = 0;
        return;
    }
    if (!a->registered || !io_has(a) || io_state(a) != NET_READY) {
        a->automatic_away = APP_AUTO_AWAY_NONE;
        a->autocommand_presence = NULL;
        a->automatic_away_clear_pending = 0;
        a->automatic_away_retry_at = 0;
        return;
    }
    if (app_outgoing(a, 0, "AWAY")) {
        a->automatic_away = APP_AUTO_AWAY_NONE;
        a->autocommand_presence = NULL;
        a->automatic_away_clear_pending = 0;
        a->automatic_away_retry_at = 0;
    } else {
        a->automatic_away_clear_pending = 1;
        automatic_away_retry(a);
    }
}

void app_user_activity(App *a)
{
    if (!a) return;
    a->last_local_activity = app_now_ms(a);
    OutLine *presence = a->autocommand_presence;
    if (presence && presence->previous_automatic_away != APP_AUTO_AWAY_NONE) {
        /* Frontends report activity before dispatching /cancel. A queued
         * script may mask the earlier automatic state until it is removed. */
        presence->previous_automatic_away_clear_pending = 1;
        presence->previous_automatic_away_retry_at = a->last_local_activity;
        presence->previous_backgrounded = a->backgrounded;
    }
    a->automatic_away_retry_at = 0;
    clear_automatic_away(a);
}

void app_set_backgrounded(App *a, int backgrounded)
{
    if (!a) return;
    a->backgrounded = backgrounded != 0;
    if (!a->backgrounded) {
        app_user_activity(a);
        return;
    }
    if (a->cfg.background_away && automatic_away_allowed(a)) {
        if (a->automatic_away_clear_pending) {
            a->automatic_away_clear_pending = 0;
            a->automatic_away_retry_at = 0;
        }
        (void)set_automatic_away(a, APP_AUTO_AWAY_BACKGROUND, a->cfg.background_away_message);
    }
}

static void presence_advance(App *a, int64_t now)
{
    if (!a || !a->registered) return;
    if (now < a->automatic_away_retry_at) return;
    if (a->automatic_away_clear_pending) {
        clear_automatic_away(a);
        return;
    }
    if (!automatic_away_allowed(a)) return;
    if (a->backgrounded) {
        if (a->cfg.background_away)
            (void)set_automatic_away(a, APP_AUTO_AWAY_BACKGROUND, a->cfg.background_away_message);
        return;
    }
    if (a->cfg.autoaway_minutes <= 0 || a->automatic_away != APP_AUTO_AWAY_NONE ||
        a->manual_away_pending || (a->away && a->automatic_away == APP_AUTO_AWAY_NONE)) return;
    int64_t deadline = a->last_local_activity + (int64_t)a->cfg.autoaway_minutes * 60000;
    if (now >= deadline)
        (void)set_automatic_away(a, APP_AUTO_AWAY_IDLE, a->cfg.autoaway_message);
}

int app_request_quit(App *a, const char *reason)
{
    if (!a) return 0;
    const char *message = reason && *reason ? reason : a->cfg.quit_message;
    if (!presence_text_valid(message)) {
        app_status(a, "%s", irc_tr("QUIT reason must be valid UTF-8 without control characters."));
        return 0;
    }
    if (io_has(a) && !a->inflight && io_state(a) == NET_READY) {
        queue_clear(a);
        if (*message) (void)app_outgoing(a, 0, "QUIT :%s", message);
        else (void)app_outgoing(a, 0, "QUIT");
        write_output(a, app_now_ms(a));
    }
    a->quit = 1;
    return 1;
}

/* Only callers with a typed transient transport failure may request retry.
 * Authentication, protocol and configuration failures use app_disconnect.
 * A minute of successful registration resets backoff; flapping does not. */
static void disconnect_retry(App *a, const char *reason, int retryable)
{
    int retry = retryable && a->reconnect_enabled && !a->test_mode && !a->quit;
    int64_t now = retry ? app_now_ms(a) : 0;
    unsigned attempts = a->reconnect_attempts;
    if (a->registered && a->registered_at && now >= a->registered_at &&
        now - a->registered_at >= 60000)
        attempts = 0;
    app_disconnect(a, reason);
    if (retry) {
        if (attempts > 6) attempts = 6;
        unsigned delay = 5u << attempts;
        if (delay > 300) delay = 300;
        a->reconnect_attempts = attempts < 6 ? attempts + 1 : attempts;
        int64_t wait = (int64_t)delay * 1000;
        a->reconnect_at = now > INT64_MAX - wait ? INT64_MAX : now + wait;
        app_log(a, 0, "Automatic reconnect in %u seconds; /disconnect cancels it. Pending messages will not be resent.", delay);
        {
            char shown[1024], seconds[32];
            (void)snprintf(seconds, sizeof seconds, "%u", delay);
            const IrcDisplayField values[] = {{"seconds", seconds}};
            if (irc_display_render(shown, sizeof shown,
                    "Automatic reconnect in {seconds} seconds; /disconnect cancels it.", values, 1))
                app_status(a, "%s", shown);
            else app_status(a, "Automatic reconnect in %u seconds; /disconnect cancels it.", delay);
        }
    }
}

static int safe_word(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!p || !*p)
        return 0;
    while (*p) {
        if (*p <= 0x20 || *p >= 0x7f || *p == ':' || *p == ',')
            return 0;
        ++p;
    }
    return 1;
}

static int configured_channels_valid(const char *channels)
{
    char copy[1024], *p, *next;
    size_t count = 0;
    if (strlen(channels) >= sizeof(copy))
        return 0;
    copy_text(copy, sizeof(copy), channels);
    for (p = copy; *p; p = next) {
        next = strchr(p, ',');
        if (next)
            *next++ = 0;
        else
            next = p + strlen(p);
        if (!irc_valid_channel(p) || ++count > APP_BUFFERS - 3)
            return 0;
        if (!*next)
            break;
    }
    return !*channels || channels[strlen(channels) - 1] != ',';
}

static int configured_channel(const App *a, const char *channel)
{
    char copy[sizeof(a->cfg.channels)], *p, *save = NULL;
    copy_text(copy, sizeof(copy), session_channels(a));
    for (p = strtok_r(copy, ",", &save); p; p = strtok_r(NULL, ",", &save))
        if (irc_equal(p, channel, a->casemap)) return 1;
    return 0;
}

/* New connection snapshots, including explicitly staged endpoint edits. */
static int connect_attempt(App *a, int automatic)
{
    char error[512];
    Config desired;
    size_t i;
    if (io_has(a)) {
        app_status(a, "%s", irc_tr("Already connecting or connected; use /disconnect first."));
        return 0;
    }
    if (automatic) desired = a->cfg;
    else app_config_effective(a, &desired);
    error[0] = 0;
    if (config_validate(&desired, error, sizeof(error)) < 0) {
        /* Keep the validator's specific field/error, without printing values
         * from any credential. A failed attempt leaves all pending edits intact. */
    } else if (!desired.server[0]) {
        copy_text(error, sizeof error, "server is empty; set a server hostname in this server's settings before connecting");
    } else if (!desired.nick[0]) {
        copy_text(error, sizeof error, "nick is empty; set a nickname in this server's settings before connecting");
    } else if (!irc_valid_nick(desired.nick)) {
        copy_text(error, sizeof error, "nick must be a valid ASCII IRC nickname of at most 63 bytes");
    } else if (!safe_word(desired.username)) {
        copy_text(error, sizeof error, "username is empty or invalid; use a nonempty ASCII IRC username");
    } else if (!configured_channels_valid(desired.channels)) {
        copy_text(error, sizeof error, "channel must contain at most 29 valid comma-separated channel names, without empty entries");
    } else if (a->connection_id == UINT64_MAX) {
        copy_text(error, sizeof error, "connection identifier limit reached; start a new client instance");
    }
    if (*error) {
        config_wipe(&desired, sizeof desired);
        {
            char shown[1024];
            const IrcDisplayField values[] = {{"error", error}};
            if (irc_display_render(shown, sizeof shown, "Cannot connect: {error}", values, 1))
                app_status(a, "%s", shown);
            else app_status(a, "Cannot connect: %s", error);
        }
        app_log(a, 0, "Cannot connect: %s", error);
        return 0;
    }
    if (!automatic && a->config_pending_mask) {
        int channels_changed = strcmp(desired.channels, a->cfg.channels) != 0;
        config_wipe(&a->cfg, sizeof a->cfg);
        memcpy(&a->cfg, &desired, sizeof a->cfg);
        config_wipe(&a->config_pending, sizeof a->config_pending);
        a->config_pending_mask = 0;
        if (a->test_mode && channels_changed)
            copy_text(a->test_channels, sizeof a->test_channels,
                      a->cfg.channels[0] ? a->cfg.channels : "#test-one,#test-two");
        app_log(a, 0, "Applied staged settings for this connection attempt.");
    }
    config_wipe(&desired, sizeof desired);
    copy_text(a->nick, sizeof(a->nick), a->cfg.nick);
    a->own_user[0] = a->own_host[0] = 0;
    a->registered = a->started = a->cap_done = 0;
    a->registered_at = 0;
    a->away = 0;
    a->automatic_away = APP_AUTO_AWAY_NONE;
    a->automatic_away_clear_pending = 0;
    a->automatic_away_retry_at = 0;
    a->manual_away_pending = 0;
    a->last_local_activity = app_now_ms(a);
    a->cap_sasl = a->cap_time = a->cap_multi = a->cap_batch = a->cap_self = 0;
    a->cap_offered = a->cap_pending = 0;
    memset(a->batches, 0, sizeof a->batches);
    a->message_time[0] = 0;
    a->message_history = 0;
    a->auth_started = a->auth_sent = a->authenticated = 0;
    a->incoming_len = 0;
    a->bad_lines = 0;
    a->autocommand_next = 0;
    a->autocommand_active = a->autocommand_waiting = 0;
    a->autocommand_presence = NULL;
    a->autocommand_due = 0;
    a->casemap = IRC_CASE_RFC1459;
    a->wire_limit = 512;
    copy_text(a->prefix_modes, sizeof(a->prefix_modes), "ov");
    copy_text(a->prefix_chars, sizeof(a->prefix_chars), "@+");
    copy_text(a->chanmodes, sizeof(a->chanmodes), "b,k,l,imnst");
    a->began = a->last_receive = app_now_ms(a);
    a->last_chat = a->last_ping = 0;
    for (i = 0; i < a->buffer_count; ++i) {
        a->buffers[i].joined = 0;
        a->buffers[i].background_join = 0;
        a->buffers[i].user_count = 0;
        a->buffers[i].names_in_progress = 0;
        a->buffers[i].names_truncated = 0;
    }
    if (a->test_mode)
        a->sim = sim_open(error, sizeof(error));
    else
        a->net = net_open(a->cfg.server, a->cfg.port, a->cfg.tls,
                          a->cfg.ca_file, error, sizeof(error));
    if (!io_has(a)) {
        app_status(a, "%s", error);
        app_log(a, 0, "Connection failed: %s", error);
        return 0;
    }
    ++a->connection_id;
    if (a->test_mode) {
        app_log(a, 0, "OFFLINE TEST MODE: starting the in-process simulator. No network or authentication is performed.");
        app_status(a, "%s", irc_tr("OFFLINE TEST MODE: starting simulated registration."));
    } else {
        app_log(a, 0, "Connecting to %s:%d. Connection encryption: %s.", a->cfg.server, a->cfg.port,
                a->cfg.tls ? "TLS required; server certificate verification enabled" :
                             "OFF (unencrypted IRC; TLS disabled)");
        if (!a->cfg.tls && a->cfg.port == 6697)
            app_log(a, 0, "Port 6697 normally expects TLS, but plaintext is selected. Check this server's Transport setting if connection fails.");
        {
            char shown[1024], port[32];
            (void)snprintf(port, sizeof port, "%d", a->cfg.port);
            const IrcDisplayField values[] = {{"server", a->cfg.server}, {"port", port}};
            if (irc_display_render(shown, sizeof shown, "Connecting to {server}:{port}", values, 2))
                app_status(a, "%s", shown);
            else app_status(a, "Connecting to %s:%d", a->cfg.server, a->cfg.port);
        }
    }
    return 1;
}

int app_connect(App *a)
{
    if (!io_has(a)) {
        a->reconnect_at = 0;
        a->reconnect_attempts = 0;
    }
    return connect_attempt(a, 0);
}

/* Bounded wire frames and immutable user-reviewed send plans. */
int app_outgoing(App *a, int chat, const char *format, ...)
{
    char body[IRC_BODY_MAX + 1];
    OutLine *line;
    int length;
    size_t i, count;
    va_list ap;
    if (!io_has(a) || io_state(a) != NET_READY)
        return 0;
    va_start(ap, format);
    length = vsnprintf(body, sizeof(body), format, ap);
    va_end(ap);
    if (length < 1 || (size_t)length > IRC_BODY_MAX ||
        (a->wire_limit && (size_t)length + 2 > a->wire_limit)) {
        config_wipe(body, sizeof(body));
        app_status(a, "%s", irc_tr("Outgoing command exceeds the protocol byte limit."));
        return 0;
    }
    for (i = 0; i < (size_t)length; ++i) {
        if (body[i] == '\r' || body[i] == '\n') {
            config_wipe(body, sizeof(body));
            app_status(a, "%s", irc_tr("Rejected an outgoing line containing CR or LF."));
            return 0;
        }
    }
    count = chat ? a->chat_count : a->control_count;
    if (a->inflight && !!a->inflight->chat == !!chat)
        ++count;
    if (count >= APP_QUEUE) {
        config_wipe(body, sizeof(body));
        app_status(a, "%s", irc_tr("Outgoing queue is full; wait before sending more."));
        return 0;
    }
    line = calloc(1, sizeof(*line));
    if (!line) {
        config_wipe(body, sizeof(body));
        app_status(a, "%s", irc_tr("Cannot allocate outgoing command."));
        return 0;
    }
    memcpy(line->data, body, (size_t)length);
    line->data[length] = '\r';
    line->data[length + 1] = '\n';
    line->length = (size_t)length + 2;
    line->chat = chat;
    config_wipe(body, sizeof(body));
    if (chat) {
        if (a->chat_tail) a->chat_tail->next = line;
        else a->chat_head = line;
        a->chat_tail = line;
        ++a->chat_count;
    } else {
        if (a->control_tail) a->control_tail->next = line;
        else a->control_head = line;
        a->control_tail = line;
        ++a->control_count;
    }
    a->dirty = 1;
    return 1;
}

size_t app_budget(const App *a, const char *target, int action)
{
    if (bouncer_service(a, target)) {
        size_t limit = a->wire_limit && a->wire_limit < 512u ? a->wire_limit : 512u;
        size_t overhead = 12u + strlen(target); /* PRIVMSG, target, " :", CRLF. */
        return !action && limit > overhead ? limit - overhead : 0;
    }
    /* A server may prepend '~' or otherwise rewrite USER. Until its actual
     * relay identity arrives, reserve the entire supported user field. */
    static const char unknown_user[] =
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    size_t n = irc_payload_budget(a->wire_limit, "PRIVMSG", target, a->nick,
                                  a->own_user[0] ? a->own_user : unknown_user,
                                  a->own_host);
    return action ? (n > 9 ? n - 9 : 0) : n;
}

int app_queue_dcc_offer(App *a, uint64_t id, const char *nick, const char *body)
{
    if (!a->registered || !id || !irc_valid_nick(nick) || !body ||
        strncmp(body, "DCC SEND ", 9) || !irc_utf8_valid(body)) {
        app_status(a, "%s", irc_tr("A registered connection, nickname and valid DCC SEND offer are required."));
        return 0;
    }
    size_t length = strlen(body);
    if (length > IRC_BODY_MAX || length + 2u > app_budget(a, nick, 0)) {
        app_status(a, "%s", irc_tr("DCC offer exceeds the current IRC message budget."));
        return 0;
    }
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)body[i] < 0x20 || (unsigned char)body[i] == 0x7f) {
            app_status(a, "%s", irc_tr("DCC offer cannot contain control characters."));
            return 0;
        }
    if (a->inflight && a->inflight->dcc_id == id) {
        app_status(a, "%s", irc_tr("That DCC offer is already in flight.")); return 0;
    }
    for (const OutLine *line = a->chat_head; line; line = line->next)
        if (line->dcc_id == id) { app_status(a, "%s", irc_tr("That DCC offer is already queued.")); return 0; }
    if (!app_outgoing(a, 1, "PRIVMSG %s :\001%s\001", nick, body)) return 0;
    a->chat_tail->dcc_id = id;
    return 1;
}

int app_cancel_dcc_offer(App *a, uint64_t id)
{
    OutLine **link = &a->chat_head, *last = NULL, *line;
    int removed = 0;
    if (!id) return 0;
    while ((line = *link) != NULL) {
        if (line->dcc_id == id) {
            *link = line->next;
            outline_free(line);
            --a->chat_count;
            removed = 1;
        } else {
            last = line;
            link = &line->next;
        }
    }
    a->chat_tail = last;
    if (removed) a->dirty = 1;
    return a->inflight && a->inflight->dcc_id == id ? 2 : removed;
}

static int plan_chunks(const char *text, size_t budget, IrcChunk *chunks,
                        size_t *count)
{
    char line[APP_TEXT + 1];
    size_t offset = 0, total = 0, length, n, i;
    const char *p = text, *end;
    *count = 0;
    if (!budget || strlen(text) > APP_TEXT || !irc_utf8_valid(text)) return 0;
    while (*p) {
        end = strchr(p, '\n');
        length = end ? (size_t)(end - p) : strlen(p);
        for (i = 0; i < length; ++i) {
            unsigned char c = (unsigned char)p[i];
            if (c < 0x20 || c == 0x7f) return 0;
        }
        if (length) {
            memcpy(line, p, length);
            line[length] = 0;
            if (!irc_split(line, budget, chunks + total, APP_QUEUE - total, &n)) return 0;
            for (i = 0; i < n; ++i) chunks[total + i].offset += offset;
            total += n;
        }
        if (!end) break;
        offset += length + 1;
        p = end + 1;
    }
    *count = total;
    return 1;
}

static int auth_word_is(const char *text, size_t length, const char *word)
{
    if (strlen(word) != length) return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
        if (c != (unsigned char)word[i]) return 0;
    }
    return 1;
}

/* Bounded slices matter here: a split can create a service command that was
 * not at the beginning of the composed line. This recognizes common command
 * forms, not arbitrary secrets in ordinary prose or unknown service syntax. */
static int credential_command_text(const char *text, size_t length)
{
    static const char *const verbs[] = {
        "identify", "id", "auth", "authenticate", "login", "password",
        "pass", "register", "ghost", "recover", "release", "group",
        "drop", "resetpass", "setpass", "changepass", "verify", "oper"
    };
    size_t start = 0, end;
    while (start < length && (text[start] == ' ' || text[start] == '\t' || text[start] == ':'))
        ++start;
    end = start;
    while (end < length && text[end] != ' ' && text[end] != '\t') ++end;
    for (size_t i = 0; i < sizeof verbs / sizeof verbs[0]; ++i)
        if (auth_word_is(text + start, end - start, verbs[i])) return 1;
    int saset = auth_word_is(text + start, end - start, "saset");
    if (!saset && !auth_word_is(text + start, end - start, "set")) return 0;
    /* SET PASSWORD value and SASET [account] PASSWORD value. */
    for (int field = 0; field < (saset ? 2 : 1); ++field) {
        start = end;
        while (start < length && (text[start] == ' ' || text[start] == '\t' || text[start] == ':'))
            ++start;
        end = start;
        while (end < length && text[end] != ' ' && text[end] != '\t') ++end;
        if (auth_word_is(text + start, end - start, "password") ||
            auth_word_is(text + start, end - start, "pass")) return 1;
    }
    return 0;
}

static int private_plan_requires_tls(const char *target, const char *text,
                                      int action, const IrcChunk *chunks, size_t count)
{
    /* A named bouncer service may accept credentials under arbitrary verbs.
     * This pure history predicate has no live App, so be conservative even
     * when a plan is inspected after a profile or connection changes. */
    if (irc_equal(target, "BouncerServ", IRC_CASE_ASCII)) return 1;
    if (action || !irc_valid_nick(target)) return 0;
    const char *p = text;
    /* Also inspect logical lines before splitting, so cutting an auth verb
     * across chunks cannot hide it from the actual-chunk checks below. */
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t length = end ? (size_t)(end - p) : strlen(p);
        if (credential_command_text(p, length)) return 1;
        if (!end) break;
        p = end + 1;
    }
    for (size_t i = 0; i < count; ++i)
        if (credential_command_text(text + chunks[i].offset, chunks[i].length)) return 1;
    return 0;
}

int app_plan_history_safe(const SendPlan *plan)
{
    IrcChunk expected[APP_QUEUE];
    size_t count = 0;
    if (!plan || !memchr(plan->target, 0, sizeof plan->target) ||
        !memchr(plan->text, 0, sizeof plan->text) ||
        !irc_valid_target(plan->target) || !plan->count || plan->count > APP_QUEUE ||
        !plan_chunks(plan->text, plan->budget, expected, &count) || count != plan->count)
        return 0;
    for (size_t i = 0; i < count; ++i)
        if (plan->chunks[i].offset != expected[i].offset ||
            plan->chunks[i].length != expected[i].length) return 0;
    /* A credential verb may begin only after the actual wire split. The
     * original command's first word alone cannot decide history privacy. */
    return !private_plan_requires_tls(plan->target, plan->text, plan->action, expected, count);
}

/* A SendPlan is a preview, not a queued message or durable
 * authorization. It captures the connection and exact byte ranges so
 * the frontend can review a split before asking to commit it. */
int app_plan(App *a, const char *target, const char *text, int action, SendPlan *plan)
{
    size_t i, len;
    int service = bouncer_service(a, target);
    if (!a->registered) {
        app_status(a, "%s", irc_tr("Not registered with a server; /connect first."));
        return 0;
    }
    if ((!irc_valid_target(target) && !service) || !text || !*text || !irc_utf8_valid(text)) {
        app_status(a, "%s", irc_tr("A valid target and nonempty UTF-8 text are required."));
        return 0;
    }
    len = strlen(text);
    if (len > APP_TEXT) {
        {
            char shown[512], limit[32];
            (void)snprintf(limit, sizeof limit, "%d", APP_TEXT);
            const IrcDisplayField fields[] = {{"limit", limit}};
            if (irc_display_render(shown, sizeof shown, "Input exceeds {limit} UTF-8 bytes.", fields, 1))
                app_status(a, "%s", shown);
            else app_status(a, "Input exceeds %d UTF-8 bytes.", APP_TEXT);
        }
        return 0;
    }
    if (service && (a->test_mode || !a->cfg.tls || action || strchr(text, '\n') ||
        (a->cfg.bouncer_type == CONFIG_BOUNCER_SOJU && !a->authenticated))) {
        app_status(a, "%s", irc_tr("Bouncer control requires one plain command over the authenticated TLS connection."));
        return 0;
    }
    for (i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        if ((c < 0x20 && c != '\n') || c == 0x7f) {
            app_status(a, "%s", irc_tr("Messages cannot contain control characters except composed newlines."));
            return 0;
        }
    }
    memset(plan, 0, sizeof(*plan));
    copy_text(plan->target, sizeof(plan->target), target);
    copy_text(plan->text, sizeof(plan->text), text);
    plan->action = action;
    plan->connection_id = a->connection_id;
    plan->budget = app_budget(a, target, action);
    if (service && len > plan->budget) {
        config_wipe(plan, sizeof *plan);
        app_status(a, "%s", irc_tr("Bouncer command exceeds one protocol line; nothing queued."));
        return 0;
    }
    if (!plan_chunks(plan->text, plan->budget, plan->chunks, &plan->count) ||
        !plan->count) {
        app_status(a, "%s", irc_tr("Text cannot fit within 64 messages and the current byte limit."));
        return 0;
    }
    if (!a->test_mode && !a->cfg.tls &&
        private_plan_requires_tls(plan->target, plan->text, plan->action, plan->chunks, plan->count)) {
        config_wipe(plan, sizeof *plan);
        app_status(a, "%s", irc_tr("Private authentication commands require verified TLS; nothing queued."));
        return 0;
    }
    return 1;
}

/* Revalidate a reviewed plan against the current
 * connection, queue capacity and wire budget. Allocate the full chain
 * first; no partial message group may enter the outbound queue. */
int app_send_plan(App *a, const SendPlan *plan)
{
    OutLine *first = NULL, *last = NULL, *p, *next;
    size_t i, budget, occupied, expected_count = 0;
    IrcChunk expected[APP_QUEUE];
    int service = bouncer_service(a, plan->target);
    if (!a->registered || !io_has(a) || !plan->count || plan->count > APP_QUEUE ||
        (!irc_valid_target(plan->target) && !service)) {
        app_status(a, "%s", irc_tr("Cannot send this plan in the current connection state."));
        return 0;
    }
    if (service && (a->test_mode || !a->cfg.tls || plan->action || plan->count != 1 ||
        strchr(plan->text, '\n') ||
        (a->cfg.bouncer_type == CONFIG_BOUNCER_SOJU && !a->authenticated))) {
        app_status(a, "%s", irc_tr("Bouncer control plan is no longer valid; nothing queued."));
        return 0;
    }
    if (plan->connection_id != a->connection_id) {
        app_status(a, "%s", irc_tr("Connection changed after this message was prepared; nothing queued. Review and submit it again."));
        return 0;
    }
    if (!plan_chunks(plan->text, plan->budget, expected, &expected_count) ||
        expected_count != plan->count) {
        app_status(a, "%s", irc_tr("Send plan text changed or contains controls; nothing queued."));
        return 0;
    }
    int sensitive = service || private_plan_requires_tls(plan->target, plan->text, plan->action,
                                               expected, expected_count);
    if (!a->test_mode && !a->cfg.tls && sensitive) {
        app_status(a, "%s", irc_tr("Private authentication commands require verified TLS; nothing queued."));
        return 0;
    }
    budget = app_budget(a, plan->target, plan->action);
    occupied = a->chat_count + (a->inflight && a->inflight->chat ? 1u : 0u);
    if (plan->count > APP_QUEUE - occupied) {
        {
            char shown[512], count[32];
            (void)snprintf(count, sizeof count, "%zu", plan->count);
            const IrcDisplayField fields[] = {{"count", count}};
            if (irc_display_render(shown, sizeof shown,
                    "The outgoing queue cannot hold all {count} messages; nothing queued.", fields, 1))
                app_status(a, "%s", shown);
            else app_status(a, "The outgoing queue cannot hold all %zu messages; nothing queued.", plan->count);
        }
        return 0;
    }
    /* Construct a private chain first. Allocation or revalidation failure
     * cannot expose only a prefix of the user's accepted composition. */
    for (i = 0; i < plan->count; ++i) {
        int length;
        if (plan->chunks[i].offset != expected[i].offset ||
            plan->chunks[i].length != expected[i].length ||
            plan->chunks[i].length > budget)
            goto changed;
        p = calloc(1, sizeof(*p));
        if (!p)
            goto nomem;
        if (plan->action)
            length = snprintf(p->data, sizeof(p->data), "PRIVMSG %s :\001ACTION %.*s\001\r\n",
                              plan->target, (int)plan->chunks[i].length,
                              plan->text + plan->chunks[i].offset);
        else
            length = snprintf(p->data, sizeof(p->data), "PRIVMSG %s :%.*s\r\n",
                              plan->target, (int)plan->chunks[i].length,
                              plan->text + plan->chunks[i].offset);
        if (length < 1 || (size_t)length >= sizeof(p->data)) {
            outline_free(p);
            goto changed;
        }
        p->length = (size_t)length;
        p->chat = 1;
        /* One user composition can be split into several IRC PRIVMSGs. Catch-up
         * anchors the first chunk so reopening history never begins halfway
         * through the user's most recent composition. */
        p->anchor = !service && i == 0;
        /* The complete validated batch determines privacy, including its
         * ordinary first line and fragments whose auth verb was split away.
         * Do not trust a caller-supplied sensitivity marker in SendPlan. */
        p->sensitive = sensitive;
        p->bouncer_control = service ? a->cfg.bouncer_type : 0;
        if (last) last->next = p;
        else first = p;
        last = p;
    }
    if (a->chat_tail) a->chat_tail->next = first;
    else a->chat_head = first;
    a->chat_tail = last;
    a->chat_count += plan->count;
    app_queue_notice(a, "%zu message%s queued; paced at one per 1.2 seconds.", plan->count,
               plan->count == 1 ? "" : "s");
    return 1;
changed:
    app_status(a, "%s", irc_tr("The message limit changed; nothing queued. Submit again to review the new split."));
    goto cleanup;
nomem:
    app_status(a, "%s", irc_tr("Cannot allocate the complete send; nothing queued."));
cleanup:
    for (p = first; p; p = next) {
        next = p->next;
        outline_free(p);
    }
    return 0;
}

/* Privacy decisions precede local echo and persistent transcript writes. */
static int authentication_service(const App *a, const char *source)
{
    static const char *const services[] = {
        "nickserv", "authserv", "chanserv", "operserv", "hostserv",
        "memoserv", "groupserv", "ns", "as", "cs", "os"
    };
    if (!a || !source) return 0;
    size_t length = strcspn(source, "!@");
    if (!length || length > IRC_NICK_MAX) return 0;
    char nick[IRC_NICK_MAX + 1];
    memcpy(nick, source, length);
    nick[length] = 0;
    for (size_t i = 0; i < sizeof services / sizeof services[0]; ++i)
        if (irc_equal(nick, services[i], a->casemap)) return 1;
    return 0;
}

/* Classify outgoing copies by destination/content, not by the currently
 * selected nick: a bouncer can replay our older identity after a nick change. */
static int persistent_sensitive_outgoing(const App *a, const char *target, const char *text)
{
    if (!a || !target || !text) return 0;
    if (authentication_service(a, target)) return 1;
    return credential_command_text(text, strlen(text));
}

static void chat_display(App *a, const char *nick, const char *target,
                         const char *text, int action, int notice, int mine, int anchor)
{
    char safe_nick[128], safe_target[256], safe_text[2048];
    char timestamp[32], line[2560];
    const char *view_target;
    size_t view, i, queries = 0;
    BufferKind kind;
    int mention = 0;
    irc_sanitize(safe_nick, sizeof(safe_nick), nick);
    irc_sanitize(safe_target, sizeof(safe_target), target);
    irc_sanitize(safe_text, sizeof(safe_text), text);
    view_target = irc_valid_channel(target) ? target : (mine ? target : nick);
    kind = irc_valid_channel(target) ? B_CHANNEL : B_QUERY;
    view = app_buffer(a, view_target, kind, 0);
    if (view == SIZE_MAX) {
        for (i = 0; i < a->buffer_count; ++i)
            if (a->buffers[i].kind == B_QUERY) ++queries;
        /* Unsolicited private senders cannot consume all channel slots.
         * Explicit outgoing /msg remains able to open any available view. */
        if (kind == B_QUERY && queries < 8 &&
            (!mine || a->message_history || a->cfg.bouncer_type != CONFIG_BOUNCER_NONE))
            view = app_buffer(a, view_target, kind, 1);
        else if (kind == B_CHANNEL && a->message_history)
            view = app_buffer(a, view_target, kind, 1);
    }
    if (view == SIZE_MAX)
        view = 0;
    if (a->message_time[0]) copy_text(timestamp, sizeof timestamp, a->message_time);
    else stamp(timestamp, sizeof(timestamp));
    const char *history = a->message_history ? "[history] " : "";
    if (action)
        (void)snprintf(line, sizeof(line), "[%s] %s[%s] * %s %s", timestamp,
                       history, safe_target, safe_nick, safe_text);
    else if (notice)
        (void)snprintf(line, sizeof(line), "[%s] %s[%s] -%s- %s", timestamp,
                       history, safe_target, safe_nick, safe_text);
    else
        (void)snprintf(line, sizeof(line), "[%s] %s[%s] <%s> %s", timestamp,
                       history, safe_target, safe_nick, safe_text);
    if (!mine && !a->message_history) {
        mention = irc_mentions(safe_text, a->nick, a->casemap) ||
                  irc_equal(target, a->nick, a->casemap);
        for (i = 0; !mention && i < a->cfg.word_count; ++i)
            mention = irc_contains(safe_text, a->cfg.words[i], a->casemap);
    }
    line_store(a, view, line, !mine && !a->message_history, mention, view,
               !(mine && persistent_sensitive_outgoing(a, target, text)), anchor);
    if (mention)
        line_store(a, 1, line, 1, 1, view, 0, 0);
}

static void own_echo(App *a, const OutLine *line)
{
    char body[IRC_BODY_MAX + 1];
    IrcMessage m;
    size_t n;
    if (line->autocommand || line->bouncer_control) return;
    int previous_suppression = a->persistent_log_suppressed;
    if (line->sensitive) a->persistent_log_suppressed = 1;
    memcpy(body, line->data, line->length - 2);
    body[line->length - 2] = 0;
    if (irc_parse(body, &m) && !strcmp(m.command, "PRIVMSG") && m.nparams == 2) {
        char *text = m.params[1];
        n = strlen(text);
        if (n >= 9 && !memcmp(text, "\001ACTION ", 8) && text[n - 1] == '\001') {
            text[n - 1] = 0;
            chat_display(a, a->nick, m.params[0], text + 8, 1, 0, 1, line->anchor);
        } else {
            chat_display(a, a->nick, m.params[0], text, 0, 0, 1, line->anchor);
        }
    }
    a->persistent_log_suppressed = previous_suppression;
    config_wipe(body, sizeof body);
}

static int queued_private_auth(const OutLine *line)
{
    char body[IRC_BODY_MAX + 1];
    IrcMessage m;
    int sensitive = 0;
    if (!line->chat || line->length < 2u || line->length > IRC_BODY_MAX + 2u) return 0;
    if (line->sensitive) return 1;
    memcpy(body, line->data, line->length - 2u);
    body[line->length - 2u] = 0;
    if (irc_parse(body, &m) && m.nparams == 2u && !strcmp(m.command, "PRIVMSG") &&
        irc_valid_nick(m.params[0]))
        sensitive = credential_command_text(m.params[1], strlen(m.params[1]));
    config_wipe(body, sizeof body);
    return sensitive;
}

/* 1 fits, 0 changed byte budget, -1 private auth needs verified TLS. */
static int queued_fits(App *a, const OutLine *line)
{
    char body[IRC_BODY_MAX + 1];
    IrcMessage m;
    size_t n;
    int action = 0, fits = 0;
    if (line->offset)
        return 1;
    if (line->autocommand && line->length > a->wire_limit) return 0;
    if (!line->chat) return 1;
    if (!a->test_mode && !a->cfg.tls) {
        if (queued_private_auth(line)) return -1;
        /* Preflight the bounded pending batch before sending its first
         * ordinary chunk. A later credential chunk must not cause a partial
         * send if a caller bypassed the planner or changed the live policy. */
        for (const OutLine *pending = a->chat_head; pending; pending = pending->next)
            if (queued_private_auth(pending)) return -1;
    }
    memcpy(body, line->data, line->length - 2);
    body[line->length - 2] = 0;
    if (!irc_parse(body, &m) || m.nparams != 2 || strcmp(m.command, "PRIVMSG"))
        goto done;
    if (line->bouncer_control) {
        fits = a->registered && a->cfg.tls && bouncer_service(a, m.params[0]) &&
               (a->cfg.bouncer_type != CONFIG_BOUNCER_SOJU || a->authenticated) &&
               (!a->wire_limit || line->length <= a->wire_limit);
        goto done;
    }
    n = strlen(m.params[1]);
    if (n >= 9 && !memcmp(m.params[1], "\001ACTION ", 8) && m.params[1][n - 1] == '\001') {
        action = 1;
        n -= 9;
    }
    fits = n <= app_budget(a, m.params[0], action);
done:
    config_wipe(body, sizeof body);
    return fits;
}

static int64_t autocommand_deadline(int64_t now, int seconds)
{
    int64_t milliseconds = (int64_t)seconds * 1000;
    return now > INT64_MAX - milliseconds ? INT64_MAX : now + milliseconds;
}

/* Restricted startup commands advance through the same bounded queues. */
static void autocommands_start(App *a)
{
    if (a->test_mode || !a->cfg.tls || !a->cfg.autocommands.enabled ||
        !a->cfg.autocommands.count || a->cfg.autocommands.count > CONFIG_AUTOCOMMAND_MAX)
        return;
    a->autocommand_active = 1;
    a->autocommand_next = 0;
    a->autocommand_waiting = 0;
    a->autocommand_due = autocommand_deadline(app_now_ms(a), a->cfg.autocommands.delay_seconds);
}

static void autocommands_advance(App *a, int64_t now)
{
    ConfigAutoCommand command;
    size_t index = a->autocommand_next, view = SIZE_MAX;
    int queued = 0, created = 0;
    const char *failure = "could not queue the command; check the connection and outgoing queue";
    if (!a->autocommand_active || a->autocommand_waiting || a->test_mode ||
        !a->registered || (auth_required(a) && !a->authenticated) ||
        !io_has(a) || io_state(a) != NET_READY || now < a->autocommand_due)
        return;
    if (index >= a->cfg.autocommands.count || index >= CONFIG_AUTOCOMMAND_MAX) {
        a->autocommand_active = 0;
        return;
    }
    if (config_autocommand_parse(a->cfg.autocommands.lines[index], &command, NULL, 0) < 0) {
        failure = "invalid automatic command";
        goto done;
    }
    switch (command.kind) {
    case CONFIG_AUTO_MSG:
        if (strlen(command.text) > app_budget(a, command.target, 0)) {
            failure = "text exceeds the one-message byte limit; automatic messages are never split";
            break;
        }
        queued = app_outgoing(a, 1, "PRIVMSG %s :%s", command.target, command.text);
        break;
    case CONFIG_AUTO_JOIN:
        view = app_buffer(a, command.target, B_CHANNEL, 0);
        if (view == SIZE_MAX) {
            view = app_buffer(a, command.target, B_CHANNEL, 1);
            created = view != SIZE_MAX;
        }
        if (view == SIZE_MAX) { failure = "no channel view is available"; break; }
        queued = command.text[0] ? app_outgoing(a, 0, "JOIN %s %s", command.target, command.text) :
                                  app_outgoing(a, 0, "JOIN %s", command.target);
        if (queued && !a->buffers[view].joined) a->buffers[view].background_join = 1;
        break;
    case CONFIG_AUTO_NICK: queued = app_outgoing(a, 0, "NICK %s", command.target); break;
    case CONFIG_AUTO_AWAY:
        queued = command.text[0] ? app_outgoing(a, 0, "AWAY :%s", command.text) : app_outgoing(a, 0, "AWAY");
        break;
    case CONFIG_AUTO_BACK:
        queued = app_outgoing(a, 0, "AWAY");
        break;
    case CONFIG_AUTO_WHOIS: queued = app_outgoing(a, 0, "WHOIS %s", command.target); break;
    }
    if (queued) {
        OutLine *line = command.kind == CONFIG_AUTO_MSG ? a->chat_tail : a->control_tail;
        line->autocommand = index + 1;
        if (command.kind == CONFIG_AUTO_AWAY || command.kind == CONFIG_AUTO_BACK) {
            /* A script's queued presence intent has priority immediately,
             * but /cancel may still remove this frame before any write. */
            line->previous_automatic_away = a->automatic_away;
            line->previous_manual_away_pending = a->manual_away_pending;
            line->previous_automatic_away_clear_pending = a->automatic_away_clear_pending;
            line->previous_backgrounded = a->backgrounded;
            line->previous_automatic_away_retry_at = a->automatic_away_retry_at;
            a->autocommand_presence = line;
            a->automatic_away = APP_AUTO_AWAY_NONE;
            a->manual_away_pending = command.kind == CONFIG_AUTO_AWAY && command.text[0];
            a->automatic_away_clear_pending = 0;
            a->automatic_away_retry_at = 0;
        }
        a->autocommand_next = index + 1;
        a->autocommand_waiting = 1;
    }
done:
    config_wipe(&command, sizeof command);
    if (!queued) {
        (void)app_autocommands_cancel(a);
        if (created) (void)app_close_view(a, view);
        {
            char shown[1024], number[32];
            (void)snprintf(number, sizeof number, "%zu", index + 1);
            const IrcDisplayField values[] = {{"number", number}, {"error", failure}};
            if (irc_display_render(shown, sizeof shown,
                    "Automatic command {number} was not sent: {error}. Remaining automatic commands cancelled.", values, 2))
                app_status(a, "%s", shown);
            else
                app_status(a, "Automatic command %zu was not sent: %s. Remaining automatic commands cancelled.", index + 1, failure);
        }
        app_log(a, 0, "Automatic command %zu was not sent: %s. Remaining automatic commands cancelled.", index + 1, failure);
    }
}

/* Control frames precede paced chat, but neither may replace an in-flight
 * frame. The per-call batch bound lets other sessions and terminal work run. */
static void write_output(App *a, int64_t now)
{
    size_t batch = 0;
    ssize_t n;
    while (io_has(a) && batch++ < 16) {
        if (!a->inflight) {
            if (a->control_head) {
                a->inflight = a->control_head;
                a->control_head = a->inflight->next;
                if (!a->control_head) a->control_tail = NULL;
                --a->control_count;
            } else if (a->chat_head && (!a->last_chat || now - a->last_chat >= 1200)) {
                a->inflight = a->chat_head;
                a->chat_head = a->inflight->next;
                if (!a->chat_head) a->chat_tail = NULL;
                --a->chat_count;
            } else {
                break;
            }
            a->inflight->next = NULL;
            int fit = queued_fits(a, a->inflight);
            if (fit <= 0) {
                OutLine *p, *next;
                size_t automatic = a->inflight->autocommand;
                autocommand_presence_restore(a, a->inflight);
                outline_free(a->inflight);
                a->inflight = NULL;
                (void)app_autocommands_cancel(a);
                if (automatic && fit == 0) {
                    {
                    char shown[1024], number[32];
                    (void)snprintf(number, sizeof number, "%zu", automatic);
                    const IrcDisplayField values[] = {{"number", number}};
                    if (irc_display_render(shown, sizeof shown,
                            "Automatic command {number} was not sent: the message limit changed. Remaining automatic commands cancelled.", values, 1))
                        app_status(a, "%s", shown);
                    else
                        app_status(a, "Automatic command %zu was not sent: the message limit changed. Remaining automatic commands cancelled.", automatic);
                }
                    app_log(a, 0, "Automatic command %zu was not sent: the message limit changed. Remaining automatic commands cancelled.", automatic);
                    continue;
                }
                for (p = a->chat_head; p; p = next) {
                    next = p->next;
                    outline_free(p);
                }
                a->chat_head = a->chat_tail = NULL;
                a->chat_count = 0;
                if (fit < 0) {
                    app_log(a, 0, "Private authentication requires verified TLS; pending chat cancelled before sending.");
                    app_status(a, "%s", irc_tr("Authentication text was not sent; pending chat cancelled. Use verified TLS."));
                } else {
                    app_log(a, 0, "Pending chat cancelled because the relay identity changed the message limit.");
                    app_status(a, "%s", irc_tr("Message limit changed; pending chat cancelled. Re-enter the remaining text."));
                }
                continue;
            }
        }
        /* Even a zero-byte TLS retry owns the original frame. Cancellation
         * cannot roll back a script presence command once writing starts. */
        if (a->autocommand_presence == a->inflight) a->autocommand_presence = NULL;
        n = io_write(a, a->inflight->data + a->inflight->offset,
                       a->inflight->length - a->inflight->offset);
        if (n == -2)
            break;
        if (n <= 0) {
            char error[512];
            copy_text(error, sizeof(error), n == 0 ? "Server closed the connection" : io_error(a));
            disconnect_retry(a, error, !a->test_mode && (n == 0 || net_retryable(a->net)));
            return;
        }
        a->inflight->offset += (size_t)n;
        if (a->inflight->offset == a->inflight->length) {
            if (a->inflight->autocommand) {
                a->autocommand_waiting = 0;
                if (a->autocommand_next >= a->cfg.autocommands.count)
                    a->autocommand_active = 0;
                a->autocommand_due = a->autocommand_active ?
                    autocommand_deadline(now, a->cfg.autocommands.interval_seconds) : 0;
            }
            if (a->inflight->chat) {
                own_echo(a, a->inflight);
                a->last_chat = now;
            }
            outline_free(a->inflight);
            a->inflight = NULL;
            a->dirty = 1;
        } else {
            break;
        }
    }
}

/* Sorted rosters and bounded source identities. */
static int nick_compare(const char *a, const char *b, IrcCase mapping)
{
    size_t i;
    unsigned char ac, bc;
    for (i = 0; ; ++i) {
        ac = (unsigned char)a[i];
        bc = (unsigned char)b[i];
        if (ac >= 'A' && ac <= 'Z') ac = (unsigned char)(ac + 32);
        if (bc >= 'A' && bc <= 'Z') bc = (unsigned char)(bc + 32);
        if (mapping != IRC_CASE_ASCII) {
            if (ac == '[') ac = '{';
            if (ac == ']') ac = '}';
            if (ac == '\\') ac = '|';
            if (bc == '[') bc = '{';
            if (bc == ']') bc = '}';
            if (bc == '\\') bc = '|';
            if (mapping == IRC_CASE_RFC1459) {
                if (ac == '~') ac = '^';
                if (bc == '~') bc = '^';
            }
        }
        if (ac != bc) return ac < bc ? -1 : 1;
        if (!ac) return 0;
    }
}

static IrcCase sort_mapping;
static int user_compare(const void *left, const void *right)
{
    const User *a = left, *b = right;
    return nick_compare(a->nick, b->nick, sort_mapping);
}

/* Membership changes keep each bounded roster sorted. A binary search finds
 * both existing users and insertion positions without scanning the roster. */
static size_t user_position(const App *a, const Buffer *b, const char *nick)
{
    size_t first = 0, count = b->user_count;
    while (count) {
        size_t step = count / 2, at = first + step;
        if (nick_compare(b->users[at].nick, nick, a->casemap) < 0) {
            first = at + 1;
            count -= step + 1;
        } else count = step;
    }
    return first;
}

static size_t user_find(const App *a, const Buffer *b, const char *nick)
{
    size_t at = user_position(a, b, nick);
    return at < b->user_count && irc_equal(b->users[at].nick, nick, a->casemap) ? at : SIZE_MAX;
}

static void users_sort(App *a, Buffer *b)
{
    sort_mapping = a->casemap;
    if (b->user_count > 1)
        qsort(b->users, b->user_count, sizeof(*b->users), user_compare);
    a->dirty = 1;
}

/* The caller has already made one free slot; no allocation or full sort is
 * needed for an insertion or a renamed user's single change of position. */
static void user_insert(App *a, Buffer *b, const User *user, size_t at)
{
    if (at < b->user_count)
        memmove(b->users + at + 1, b->users + at,
                (b->user_count - at) * sizeof(*b->users));
    b->users[at] = *user;
    ++b->user_count;
    a->dirty = 1;
}

static void user_add(App *a, Buffer *b, const char *nick, unsigned modes)
{
    size_t i, cap;
    User *p;
    if (!irc_valid_nick(nick))
        return;
    i = user_position(a, b, nick);
    if (i < b->user_count && irc_equal(b->users[i].nick, nick, a->casemap)) {
        b->users[i].modes |= modes;
        a->dirty = 1;
        return;
    }
    if (b->user_count == APP_USERS) {
        b->names_truncated = 1;
        return;
    }
    if (b->user_count == b->user_cap) {
        cap = b->user_cap ? b->user_cap * 2 : 32;
        if (cap > APP_USERS) cap = APP_USERS;
        p = realloc(b->users, cap * sizeof(*p));
        if (!p) {
            b->names_truncated = 1;
            return;
        }
        b->users = p;
        b->user_cap = cap;
    }
    User user = {0};
    copy_text(user.nick, sizeof(user.nick), nick);
    user.modes = modes;
    user_insert(a, b, &user, i);
}

static void user_remove(App *a, Buffer *b, const char *nick)
{
    size_t i = user_find(a, b, nick);
    if (i == SIZE_MAX)
        return;
    if (i + 1 < b->user_count)
        memmove(b->users + i, b->users + i + 1,
                (b->user_count - i - 1) * sizeof(*b->users));
    --b->user_count;
    a->dirty = 1;
}

static int source_parts(const char *prefix, char *nick, char *user, char *host)
{
    char copy[512], *bang, *at;
    nick[0] = user[0] = host[0] = 0;
    if (!prefix)
        return 1;
    copy_text(copy, sizeof(copy), prefix);
    bang = strchr(copy, '!');
    at = strchr(bang ? bang + 1 : copy, '@');
    if (at) {
        *at++ = 0;
        if (strlen(at) >= 256) return 0;
        copy_text(host, 256, at);
    }
    if (bang) {
        *bang++ = 0;
        if (strlen(bang) >= 64 || strlen(copy) >= 64) return 0;
        copy_text(user, 64, bang);
    }
    /* A bare source may be a long server name. It is not a truncated nick. */
    if (strlen(copy) < 64)
        copy_text(nick, 64, copy);
    else if (at)
        return 0;
    return 1;
}

static void learn_self(App *a, const char *nick, const char *user, const char *host)
{
    if (!irc_equal(nick, a->nick, a->casemap))
        return;
    if (strlen(user) >= sizeof(a->own_user) || strlen(host) >= sizeof(a->own_host)) {
        app_disconnect(a, "Own relay identity exceeds supported bounds");
        return;
    }
    if (*user) copy_text(a->own_user, sizeof(a->own_user), user);
    if (*host) copy_text(a->own_host, sizeof(a->own_host), host);
}

/* Capability negotiation, credential acquisition and registration. */
static void cap_end(App *a)
{
    if (!a->cap_done) {
        if (!app_outgoing(a, 0, "CAP END")) {
            app_disconnect(a, "Cannot complete capability negotiation");
            return;
        }
        a->cap_done = 1;
        a->cap_pending = 0;
    }
}

static int token_has(const char *list, const char *name)
{
    const char *p = list;
    size_t n = strlen(name);
    while (*p) {
        while (*p == ' ') ++p;
        if (!strncmp(p, name, n) && (p[n] == 0 || p[n] == ' ' || p[n] == '='))
            return 1;
        while (*p && *p != ' ') ++p;
    }
    return 0;
}

static int sasl_plain_offered(const char *list)
{
    const char *p = list;
    int offered = -1;
    /* CAP values are processed left to right, including successive LS lines.
     * -1 means this line did not mention SASL; zero explicitly removes PLAIN
     * from the advertised mechanisms. An earlier offer must not override it. */
    while (*p) {
        while (*p == ' ') ++p;
        if (!strncmp(p, "sasl", 4) && (p[4] == 0 || p[4] == ' '))
            offered = 1;
        else if (!strncmp(p, "sasl=", 5)) {
            const char *q = p + 5;
            offered = 0;
            while (*q && *q != ' ') {
                if (!strncmp(q, "PLAIN", 5) && (q[5] == 0 || q[5] == ',' || q[5] == ' ')) {
                    offered = 1;
                    break;
                }
                while (*q && *q != ' ' && *q != ',') ++q;
                if (*q == ',') ++q;
            }
        }
        while (*p && *p != ' ') ++p;
    }
    return offered;
}

enum {
    CAP_SASL = 1u, CAP_MULTI = 2u, CAP_TIME = 4u,
    CAP_BATCH = 8u, CAP_SELF = 16u
};

static unsigned capability_bits(const char *list)
{
    unsigned bits = 0;
    if (token_has(list, "sasl")) bits |= CAP_SASL;
    if (token_has(list, "multi-prefix")) bits |= CAP_MULTI;
    if (token_has(list, "server-time")) bits |= CAP_TIME;
    if (token_has(list, "batch")) bits |= CAP_BATCH;
    if (token_has(list, "znc.in/self-message")) bits |= CAP_SELF;
    return bits;
}

static void capability_enabled(App *a, unsigned bits, int enabled)
{
    if (bits & CAP_SASL) a->cap_sasl = enabled;
    if (bits & CAP_MULTI) a->cap_multi = enabled;
    if (bits & CAP_TIME) a->cap_time = enabled;
    if (bits & CAP_BATCH) {
        a->cap_batch = enabled;
        if (!enabled) memset(a->batches, 0, sizeof a->batches);
    }
    if (bits & CAP_SELF) a->cap_self = enabled;
}

static void capability_ready(App *a)
{
    if (auth_required(a) && a->cap_sasl && !a->auth_started) {
        if (!a->cfg.tls || !app_outgoing(a, 0, "AUTHENTICATE PLAIN")) {
            app_disconnect(a, "Cannot start required SASL over verified TLS");
            return;
        }
        a->auth_started = 1;
    }
    if (!a->cap_pending && (!auth_required(a) || a->authenticated)) cap_end(a);
}

static void capability(App *a, const IrcMessage *m)
{
    const char *kind, *list;
    int required = auth_required(a);
    if (m->nparams < 3)
        return;
    kind = m->params[1];
    list = m->params[m->nparams - 1];
    /* CAP LS 302 enables cap-notify implicitly. Losing an optional capability
     * changes its interpretation immediately; NEW may be used next connect. */
    if (!strcmp(kind, "DEL")) {
        unsigned bits = capability_bits(list);
        capability_enabled(a, bits, 0);
        a->cap_offered &= ~bits;
        a->cap_pending &= ~bits;
        if ((bits & CAP_SASL) && required && !a->authenticated)
            app_disconnect(a, "Required SASL capability was withdrawn");
        else if (!a->cap_done && a->started == 2) capability_ready(a);
        return;
    }
    if (a->cap_done) return;
    if (!strcmp(kind, "LS")) {
        if (a->started != 1)
            return;
        a->cap_offered |= capability_bits(list) & ~CAP_SASL;
        int plain = sasl_plain_offered(list);
        if (plain > 0) a->cap_offered |= CAP_SASL;
        else if (!plain) a->cap_offered &= ~CAP_SASL;
        if (m->nparams >= 4 && !strcmp(m->params[2], "*"))
            return;
        if (required && !(a->cap_offered & CAP_SASL)) {
            app_disconnect(a, "Required SASL PLAIN is unavailable; no credentials sent");
            return;
        }
        unsigned wanted = a->cap_offered & (CAP_MULTI | CAP_TIME | CAP_BATCH | CAP_SELF);
        if (required) wanted |= CAP_SASL;
        if (wanted) {
            static const struct { unsigned bit; const char *name; } names[] = {
                {CAP_SASL, "sasl"}, {CAP_MULTI, "multi-prefix"}, {CAP_TIME, "server-time"},
                {CAP_BATCH, "batch"}, {CAP_SELF, "znc.in/self-message"}
            };
            char request[128] = "";
            size_t used = 0;
            for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
                if (!(wanted & names[i].bit)) continue;
                int n = snprintf(request + used, sizeof request - used, "%s%s", used ? " " : "", names[i].name);
                if (n < 0 || (size_t)n >= sizeof request - used) {
                    app_disconnect(a, "Capability request exceeds supported bounds");
                    return;
                }
                used += (size_t)n;
            }
            if (!app_outgoing(a, 0, "CAP REQ :%s", request)) {
                app_disconnect(a, "Cannot request capabilities");
                return;
            }
            a->cap_pending = wanted;
            a->started = 2;
        } else {
            cap_end(a);
        }
    } else if (!strcmp(kind, "ACK") && a->started == 2) {
        unsigned bits = capability_bits(list) & a->cap_pending;
        capability_enabled(a, bits, 1);
        a->cap_pending &= ~bits;
        capability_ready(a);
    } else if (!strcmp(kind, "NAK") && a->started == 2) {
        unsigned bits = *list ? capability_bits(list) & a->cap_pending : a->cap_pending;
        a->cap_pending &= ~bits;
        if (required && (bits & CAP_SASL))
            app_disconnect(a, "Required SASL capability was rejected");
        else
            capability_ready(a);
    }
}

enum { CREDENTIAL_SERVER = 1, CREDENTIAL_SASL = 2 };

/* 1=available now, 0=GPG job started, -1=failed. Credentials are fetched
 * only after TLS is ready, never while previewing configuration or offline. */
static int obtain_credential(App *a, int kind, char *out, size_t cap, char *error, size_t error_cap)
{
    const char *value = kind == CREDENTIAL_SERVER ? a->cfg.server_password : a->cfg.password;
    const char *file = kind == CREDENTIAL_SERVER ? a->cfg.server_password_file : a->cfg.password_file;
    const char *gpg = kind == CREDENTIAL_SERVER ? a->cfg.server_password_gpg_file : a->cfg.password_gpg_file;
    if (a->test_mode || !a->cfg.tls || a->credential) {
        (void)snprintf(error, error_cap, "%s", irc_tr("Credential request is not permitted in this connection state."));
        return -1;
    }
    if (*gpg) {
        a->credential = credential_start(gpg, app_now_ms(a), error, error_cap);
        if (!a->credential) return -1;
        a->credential_kind = kind;
        { /* Display labels only: no secret is ever rendered. */
            char shown[512];
            const char *credential = kind == CREDENTIAL_SERVER ?
                irc_tr("server password") : irc_tr("SASL password");
            const IrcDisplayField fields[] = {{"credential", credential}};
            if (irc_display_render(shown, sizeof shown,
                    "Decrypting {credential} with GPG.", fields, 1))
                app_status(a, "%s", shown);
            else app_status(a, "Decrypting %s with GPG.",
                    kind == CREDENTIAL_SERVER ? "server password" : "SASL password");
        }
        return 0;
    }
    if (*value) {
        size_t length = strlen(value);
        if (length >= cap) return -1;
        memcpy(out, value, length + 1);
        return 1;
    }
    return config_read_secret(file, out, cap, error, error_cap);
}

static void send_sasl_credential(App *a, const char *password)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char encoded[2048];
    char identity[256] = "";
    unsigned char raw[1536];
    size_t user_len, password_len, length, i, used = 0, part;
    int okay = 0;
    memset(raw, 0, sizeof raw);
    memset(encoded, 0, sizeof encoded);
    const char *account = a->cfg.auth_user;
    if (a->cfg.bouncer_type == CONFIG_BOUNCER_SOJU) {
        if (config_bouncer_identity(&a->cfg, identity, sizeof identity) != 1) goto done;
        account = identity;
    }
    user_len = strlen(account);
    password_len = strlen(password);
    length = user_len + password_len + 2;
    if (!password_len || length > sizeof(raw) || ((length + 2u) / 3u) * 4u >= sizeof encoded)
        goto done;
    raw[0] = 0;
    memcpy(raw + 1, account, user_len);
    raw[user_len + 1] = 0;
    memcpy(raw + user_len + 2, password, password_len);
    for (i = 0; i < length; i += 3) {
        unsigned v = (unsigned)raw[i] << 16;
        if (i + 1 < length) v |= (unsigned)raw[i + 1] << 8;
        if (i + 2 < length) v |= raw[i + 2];
        encoded[used++] = alphabet[(v >> 18) & 63u];
        encoded[used++] = alphabet[(v >> 12) & 63u];
        encoded[used++] = i + 1 < length ? alphabet[(v >> 6) & 63u] : '=';
        encoded[used++] = i + 2 < length ? alphabet[v & 63u] : '=';
    }
    encoded[used] = 0;
    for (i = 0; i < used; i += part) {
        part = used - i < 400 ? used - i : 400;
        if (!app_outgoing(a, 0, "AUTHENTICATE %.*s", (int)part, encoded + i))
            goto done;
    }
    if (used % 400 == 0 && !app_outgoing(a, 0, "AUTHENTICATE +"))
        goto done;
    a->auth_sent = 1;
    okay = 1;
done:
    config_wipe(raw, sizeof(raw));
    config_wipe(encoded, sizeof(encoded));
    config_wipe(identity, sizeof identity);
    if (!okay)
        app_disconnect(a, "SASL credentials could not be read or sent safely");
}

static void sasl_response(App *a, const IrcMessage *m)
{
    char password[CONFIG_SECRET_MAX + 1] = {0}, error[512] = "Invalid credential size.";
    if (a->test_mode || !a->auth_started || a->auth_sent || a->credential || !a->cfg.tls ||
        m->nparams != 1 || strcmp(m->params[0], "+")) {
        app_disconnect(a, "Unexpected SASL challenge");
        return;
    }
    int result = obtain_credential(a, CREDENTIAL_SASL, password, sizeof password, error, sizeof error);
    if (result > 0) send_sasl_credential(a, password);
    else if (result < 0) app_disconnect(a, error);
    config_wipe(password, sizeof password);
}

static void finish_registration(App *a, const char *password)
{
    int okay = 1;
    char identity[256] = "", combined[504] = "";
    a->began = a->last_receive = app_now_ms(a);
    if (a->cfg.bouncer_type == CONFIG_BOUNCER_ZNC && !a->test_mode) {
        if (!password || !*password || config_bouncer_identity(&a->cfg, identity, sizeof identity) != 1)
            okay = 0;
        else {
            int n = snprintf(combined, sizeof combined, "%s:%s", identity, password);
            if (n < 1 || (size_t)n >= sizeof combined) okay = 0;
            else password = combined;
        }
    }
    if (okay && password) {
        if (!a->cfg.tls || !*password || strlen(password) > 503) okay = 0;
        else okay = app_outgoing(a, 0, "PASS :%s", password);
    }
    if (okay) okay = app_outgoing(a, 0, "CAP LS 302");
    if (okay) okay = app_outgoing(a, 0, "NICK %s", a->nick);
    if (okay) okay = app_outgoing(a, 0, "USER %s 0 * :%s", a->cfg.username, a->cfg.realname);
    config_wipe(identity, sizeof identity);
    config_wipe(combined, sizeof combined);
    if (!okay) {
        app_disconnect(a, "Registration setup failed; check credentials and settings");
        return;
    }
    a->started = 1;
    {
            char translated[1024];
            const IrcDisplayField fields[] = {{"nick", a->nick}};
            if (irc_display_render(translated, sizeof translated, "Registering as {nick}", fields, 1))
                app_status(a, "%s", translated);
            else app_status(a, "Registering as %s", a->nick);
        }
}

static void begin_registration(App *a)
{
    char password[CONFIG_SECRET_MAX + 1] = {0}, error[512] = "Invalid credential size.";
    if (!a->test_mode && (a->cfg.server_password_file[0] || a->cfg.server_password_gpg_file[0] ||
                          a->cfg.server_password[0])) {
        int result = obtain_credential(a, CREDENTIAL_SERVER, password, sizeof password, error, sizeof error);
        if (result > 0) finish_registration(a, password);
        else if (result < 0) app_disconnect(a, error);
    } else finish_registration(a, NULL);
    config_wipe(password, sizeof password);
}

static void advance_credential(App *a, int64_t now)
{
    char password[CONFIG_SECRET_MAX + 1] = {0}, error[512];
    int result = credential_poll(a->credential, now, password, sizeof password, error, sizeof error);
    if (!result) return;
    int kind = a->credential_kind;
    credential_cancel(a->credential);
    a->credential = NULL;
    a->credential_kind = 0;
    if (result < 0) app_disconnect(a, error);
    else if (kind == CREDENTIAL_SERVER) finish_registration(a, password);
    else if (kind == CREDENTIAL_SASL) send_sasl_credential(a, password);
    else app_disconnect(a, "Unexpected credential state; nothing sent.");
    config_wipe(password, sizeof password);
}

static void autojoin(App *a)
{
    char channels[1024], *p, *next;
    copy_text(channels, sizeof(channels), session_channels(a));
    for (p = channels; *p; p = next) {
        next = strchr(p, ',');
        if (next) *next++ = 0;
        else next = p + strlen(p);
        if (!app_outgoing(a, 0, "JOIN %s", p)) {
            app_status(a, "%s", irc_tr("Autojoin queue full; remaining channels were not requested."));
            return;
        }
        if (!*next) break;
    }
}

/* Server-advertised limits, rosters and numeric replies. */
static void isupport(App *a, const IrcMessage *m)
{
    size_t i, n;
    IrcCase previous_mapping = a->casemap;
    for (i = 1; i < m->nparams; ++i) {
        char *p = m->params[i];
        if (!strncmp(p, "CASEMAPPING=", 12)) {
            p += 12;
            if (!strcmp(p, "ascii")) a->casemap = IRC_CASE_ASCII;
            else if (!strcmp(p, "rfc1459")) a->casemap = IRC_CASE_RFC1459;
            else if (!strcmp(p, "strict-rfc1459") || !strcmp(p, "rfc1459-strict"))
                a->casemap = IRC_CASE_STRICT;
            else {
                a->casemap = IRC_CASE_RFC1459;
                app_log(a, 0, "Unsupported case mapping %s; using RFC1459.", p);
            }
        } else if (!strncmp(p, "PREFIX=(", 8)) {
            char *end = strchr(p + 8, ')');
            if (end) {
                n = (size_t)(end - (p + 8));
                if (n && n <= 16 && strlen(end + 1) == n) {
                    memcpy(a->prefix_modes, p + 8, n); a->prefix_modes[n] = 0;
                    copy_text(a->prefix_chars, sizeof(a->prefix_chars), end + 1);
                }
            }
        } else if (!strncmp(p, "CHANMODES=", 10) && strlen(p + 10) < sizeof(a->chanmodes)) {
            copy_text(a->chanmodes, sizeof(a->chanmodes), p + 10);
        } else if (!strncmp(p, "LINELEN=", 8)) {
            char *end;
            unsigned long value;
            errno = 0;
            value = strtoul(p + 8, &end, 10);
            if (!errno && end != p + 8 && !*end && value >= 64 && value <= 512)
                a->wire_limit = (size_t)value;
        }
    }
    /* A new equivalence/order can invalidate binary roster lookup. Re-sort
     * once for an actual mapping change, rather than for every IRC event. */
    if (a->casemap != previous_mapping)
        for (i = 0; i < a->buffer_count; ++i)
            if (a->buffers[i].user_count > 1) users_sort(a, &a->buffers[i]);
}

static void names_reply(App *a, const IrcMessage *m)
{
    size_t view;
    Buffer *b;
    char copy[512], *p, *save;
    if (m->nparams < 4 || !irc_valid_channel(m->params[2]))
        return;
    view = app_buffer(a, m->params[2], B_CHANNEL, 0);
    if (view == SIZE_MAX) return;
    b = &a->buffers[view];
    if (!b->names_in_progress) {
        b->user_count = 0;
        b->names_truncated = 0;
        b->names_in_progress = 1;
    }
    copy_text(copy, sizeof(copy), m->params[3]);
    for (p = strtok_r(copy, " ", &save); p; p = strtok_r(NULL, " ", &save)) {
        unsigned modes = 0;
        char *q;
        while (*p && (q = strchr(a->prefix_chars, *p)) != NULL) {
            modes |= 1u << (unsigned)(q - a->prefix_chars);
            ++p;
        }
        q = strchr(p, '!');
        if (q) *q = 0;
        user_add(a, b, p, modes);
    }
    a->dirty = 1;
}

static int mode_argument(const App *a, char mode, int adding)
{
    const char *p = a->chanmodes;
    int group = 0;
    while (*p) {
        if (*p == ',') ++group;
        else if (*p == mode) return group < 2 || (group == 2 && adding);
        ++p;
    }
    return 0;
}

static void mode_reply(App *a, const IrcMessage *m)
{
    size_t view, argument = 2, i;
    const char *p, *prefix;
    int adding = 1;
    Buffer *b;
    if (m->nparams < 2 || !irc_valid_channel(m->params[0])) return;
    view = app_buffer(a, m->params[0], B_CHANNEL, 0);
    if (view == SIZE_MAX) return;
    b = &a->buffers[view];
    for (p = m->params[1]; *p; ++p) {
        if (*p == '+') { adding = 1; continue; }
        if (*p == '-') { adding = 0; continue; }
        prefix = strchr(a->prefix_modes, *p);
        if (prefix) {
            if (argument >= m->nparams) break;
            i = user_find(a, b, m->params[argument++]);
            if (i != SIZE_MAX) {
                unsigned bit = 1u << (unsigned)(prefix - a->prefix_modes);
                if (adding) b->users[i].modes |= bit;
                else b->users[i].modes &= ~bit;
            }
        } else if (mode_argument(a, *p, adding)) {
            if (argument >= m->nparams) break;
            ++argument;
        }
    }
    a->dirty = 1;
}

static void userhost_reply(App *a, const IrcMessage *m)
{
    char copy[512], *p, *save;
    if (m->nparams < 2) return;
    copy_text(copy, sizeof(copy), m->params[m->nparams - 1]);
    for (p = strtok_r(copy, " ", &save); p; p = strtok_r(NULL, " ", &save)) {
        char *equals = strchr(p, '='), *at, *star;
        if (!equals) continue;
        *equals++ = 0;
        star = strchr(p, '*'); if (star) *star = 0;
        if (*equals == '+' || *equals == '-') ++equals;
        at = strchr(equals, '@');
        if (!at) continue;
        *at++ = 0;
        learn_self(a, p, equals, at);
        if (!io_has(a)) return;
    }
}

static int reply_timestamp(const char *text, char *out, size_t cap)
{
    char *end;
    struct tm tm;
    unsigned long long value;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno || *text < '0' || *text > '9' || *end || value > (unsigned long long)LLONG_MAX)
        return 0;
    time_t when = (time_t)value;
    return (unsigned long long)when == value && localtime_r(&when, &tm) &&
           strftime(out, cap, "%Y-%m-%d %H:%M:%S %Z", &tm) != 0;
}

/* Keep protocol fields in the parser and state handlers. Presentation uses
 * bounded, sanitized records shared by the graphical and native frontends. */
static void server_reply(App *a, const IrcMessage *m)
{
    char *const *p = m->params;
    size_t n = m->nparams;
    int numeric = m->command[0] >= '0' && m->command[0] <= '9';
    int code = numeric ? (m->command[0] - '0') * 100 +
                        (m->command[1] - '0') * 10 + m->command[2] - '0' : -1;
    const char *last = n ? p[n - 1] : "";
    const char *error = NULL;
    size_t view;
    /* These replies identify a command that was rejected without completion.
     * Unrelated errors must not cancel a concurrent channel-directory reply. */
    if (a->list.active && n >= 3 && irc_equal(p[1], "LIST", IRC_CASE_ASCII) &&
        (code == 263 || code == 421 || code == 461)) {
        list_interrupt(a);
        {
            char shown[1024];
            const IrcDisplayField fields[] = {{"error", last}};
            if (irc_display_render(shown, sizeof shown,
                    "Channel list request failed: {error}. Use /list to retry.", fields, 1))
                app_status(a, "%s", shown);
            else
                app_status(a, "Channel list request failed: %s. Use /list to retry.", last);
        }
    }
    switch (code) {
    case 1: case 2: case 3: case 372: case 375: case 376:
        if (n >= 2) { app_log(a, 0, "%s", last); return; }
        break;
    case 4:
        /* Version and mode alphabets describe the connection, not chat. */
        return;
    case 221:
        if (n >= 2) { app_log(a, 0, "Your user modes: %s", p[1]); return; }
        break;
    case 301:
        if (n >= 3) { app_log(a, 0, "%s is away: %s", p[1], last); return; }
        break;
    case 311: case 314:
        if (n >= 6) {
            app_log(a, 0, "%s %s %s@%s (%s)", p[1], code == 311 ? "is" : "was",
                    p[2], p[3], last);
            return;
        }
        break;
    case 312:
        if (n >= 4) { app_log(a, 0, "Server for %s: %s (%s)", p[1], p[2], last); return; }
        break;
    case 307: case 313: case 320: case 378: case 379: case 671:
        if (n >= 3) { app_log(a, 0, "%s: %s", p[1], last); return; }
        break;
    case 317:
        if (n >= 4) {
            char *end;
            unsigned long long seconds;
            errno = 0;
            seconds = strtoull(p[2], &end, 10);
            if (!errno && *p[2] >= '0' && *p[2] <= '9' && !*end) {
                if (n >= 5) {
                    char when[80];
                    if (reply_timestamp(p[3], when, sizeof when)) {
                        app_log(a, 0, "%s has been idle for %llu seconds; connected since %s.",
                                p[1], seconds, when);
                        return;
                    }
                } else {
                    app_log(a, 0, "%s has been idle for %llu seconds.", p[1], seconds);
                    return;
                }
            }
        }
        break;
    case 318:
        if (n >= 3) { app_log(a, 0, "End of user information for %s: %s", p[1], last); return; }
        break;
    case 319:
        if (n >= 3) { app_log(a, 0, "%s's channels: %s", p[1], last); return; }
        break;
    case 321:
        if (a->list.active) {
            app_status(a, "%s", irc_tr("Receiving network channel list..."));
            a->dirty = 1;
            return;
        }
        app_log(a, 0, "Network channel list:");
        return;
    case 322:
        if (n >= 4 && a->list.active) {
            char *end = NULL;
            unsigned long long users;
            errno = 0;
            users = strtoull(p[2], &end, 10);
            if (!errno && end && end != p[2] && !*end && irc_valid_channel(p[1])) {
                int matched = list_store(a, p[1], (uint64_t)users, last);
                size_t shown = app_list_visible_count(a);
                if (matched && a->list.count && a->list.count <= shown)
                    app_log(a, 0, "%s (%llu users): %s", p[1], users, last);
                return;
            }
        }
        if (n >= 4) { app_log(a, 0, "%s (%s users): %s", p[1], p[2], last); return; }
        break;
    case 323:
        if (a->list.active) {
            a->list.active = 0;
            a->list.complete = 1;
            ++a->list.revision;
            app_list_sort(a, a->list.sort, a->list.descending);
            {
                char shown[1024], received[32], matched[32], cached[32];
                (void)snprintf(received, sizeof received, "%zu", a->list.received);
                (void)snprintf(matched, sizeof matched, "%zu", a->list.matched);
                (void)snprintf(cached, sizeof cached, "%zu", a->list.count);
                const IrcDisplayField fields[] = {
                    {"received", received}, {"matched", matched},
                    {"cached", cached},
                    {"note", a->list.truncated ? irc_tr(" (cache limit reached)") : ""}
                };
                if (irc_display_render(shown, sizeof shown,
                        "Channel list complete: {received} received, {matched} matched, {cached} cached{note}.",
                        fields, 4)) app_status(a, "%s", shown);
                else app_status(a, "Channel list complete: %zu received, %zu matched, %zu cached%s.",
                        a->list.received, a->list.matched, a->list.count,
                        a->list.truncated ? " (cache limit reached)" : "");
            }
            app_log(a, 0, "End of channel list: %zu received, %zu matched%s.",
                    a->list.received, a->list.matched, a->list.truncated ? "; cache truncated" : "");
            return;
        }
        if (n >= 2) { app_log(a, 0, "End of channel list: %s", last); return; }
        break;
    case 324:
        /* A mode reply may contain a channel key. Never display arguments. */
        if (n >= 3) { app_log(a, 0, "Modes for %s: %s", p[1], p[2]); return; }
        return;
    case 330:
        if (n >= 4) { app_log(a, 0, "%s is signed in as %s.", p[1], p[2]); return; }
        break;
    case 331:
        if (n >= 3) {
            view = app_buffer(a, p[1], B_CHANNEL, 0);
            app_log(a, view == SIZE_MAX ? 0 : view, "No topic is set for %s: %s", p[1], last);
            return;
        }
        break;
    case 332:
        if (n >= 3) { app_log(a, 0, "Topic for %s: %s", p[1], last); return; }
        break;
    case 333:
        if (n >= 4) {
            char when[80];
            if (reply_timestamp(p[3], when, sizeof when)) {
                view = app_buffer(a, p[1], B_CHANNEL, 0);
                app_log(a, view == SIZE_MAX ? 0 : view, "Topic for %s was set by %s on %s.", p[1], p[2], when);
                return;
            }
        }
        break;
    case 401: case 402: case 403:
        error = "Cannot find";
        break;
    case 404:
        error = "Cannot send to";
        break;
    case 405: case 437: case 471: case 473: case 474: case 475: case 476:
        /* 437 can name a channel or nickname; its explanation is authoritative. */
        error = code == 437 ? "Cannot use" : "Cannot join";
        break;
    case 482:
        error = "Cannot change";
        break;
    case 421:
        error = "Server does not recognize";
        break;
    case 461:
        error = "More information is required for";
        break;
    case 422:
        if (n >= 2) { app_log(a, 0, "No message of the day: %s", last); return; }
        break;
    default:
        break;
    }
    if (error && n >= 3) { app_log(a, 0, "%s %s: %s", error, p[1], last); return; }
    /* Unknown or incomplete replies retain their text without guessing what
     * a network-specific numeric means. The recipient is protocol routing. */
    char text[IRC_BODY_MAX + 1];
    size_t used = 0, first = numeric && n &&
        (n > 1 || irc_equal(p[0], a->nick, a->casemap) || !strcmp(p[0], "*")) ? 1u : 0u;
    text[0] = 0;
    for (size_t i = first; i < n; ++i) {
        int written = snprintf(text + used, sizeof text - used, "%s%s", used ? " " : "", p[i]);
        if (written < 0 || (size_t)written >= sizeof text - used) break;
        used += (size_t)written;
    }
    app_log(a, 0, "Server: %s", *text ? text : "Reply received without a message.");
}

/* BATCH ancestry classifies replay before any live-state dispatch. */
static int batch_reference_valid(const char *reference)
{
    size_t n = strlen(reference);
    if (!n || n > APP_BATCH_REF) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)reference[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return 0;
    }
    return 1;
}

static size_t batch_find(const App *a, const char *reference)
{
    for (size_t i = 0; i < APP_BATCHES; ++i)
        if (a->batches[i].reference[0] && !strcmp(a->batches[i].reference, reference)) return i;
    return SIZE_MAX;
}

static int batch_history(const App *a, const IrcMessage *m)
{
    char reference[APP_BATCH_REF + 1u];
    int result = irc_tag_value(m->tags, "batch", reference, sizeof reference);
    if (!result) return 0;
    /* An unknown/invalid reference cannot safely authorize live actions.
     * It may still be displayed as history, without changing live state. */
    if (result < 0 || !batch_reference_valid(reference)) return 1;
    size_t i = batch_find(a, reference);
    return i == SIZE_MAX || a->batches[i].history;
}

static void batch_update(App *a, const IrcMessage *m)
{
    char parent_reference[APP_BATCH_REF + 1u];
    size_t parent = SIZE_MAX, found;
    if (!a->cap_batch) return;
    if (!m->nparams || (m->params[0][0] != '+' && m->params[0][0] != '-') ||
        !batch_reference_valid(m->params[0] + 1)) goto invalid;
    int tagged = irc_tag_value(m->tags, "batch", parent_reference, sizeof parent_reference);
    if (tagged) {
        if (tagged < 0 || !batch_reference_valid(parent_reference) ||
            (parent = batch_find(a, parent_reference)) == SIZE_MAX) goto invalid;
    }
    found = batch_find(a, m->params[0] + 1);
    if (m->params[0][0] == '+') {
        if (m->nparams < 2 || !m->params[1][0] || found != SIZE_MAX) goto invalid;
        for (found = 0; found < APP_BATCHES && a->batches[found].reference[0]; ++found) {}
        if (found == APP_BATCHES) {
            app_disconnect(a, "Server exceeded the 16 active batch limit");
            return;
        }
        AppBatch *batch = &a->batches[found];
        copy_text(batch->reference, sizeof batch->reference, m->params[0] + 1);
        batch->parent = parent == SIZE_MAX ? 0u : (unsigned)parent + 1u;
        batch->history = !strcmp(m->params[1], "chathistory") ||
                         !strcmp(m->params[1], "znc.in/playback") ||
                         (parent != SIZE_MAX && a->batches[parent].history);
    } else {
        if (m->nparams != 1 || found == SIZE_MAX ||
            a->batches[found].parent != (parent == SIZE_MAX ? 0u : (unsigned)parent + 1u)) goto invalid;
        for (size_t i = 0; i < APP_BATCHES; ++i)
            if (a->batches[i].reference[0] && a->batches[i].parent == found + 1u) goto invalid;
        memset(&a->batches[found], 0, sizeof a->batches[found]);
    }
    return;
invalid:
    app_disconnect(a, "Malformed or inconsistent server batch framing");
}

static void incoming_dispatch(App *a, IrcMessage *m)
{
    char nick[64], user[64], host[256];
    const char *command = m->command;
    size_t i, view;
    if (!strcmp(command, "BATCH")) {
        batch_update(a, m);
        return;
    }
    /* Playback describes past events; only message text is displayed. Past
     * JOIN/PART, authentication, away replies and PING never change live state. */
    if (a->message_history && strcmp(command, "PRIVMSG") && strcmp(command, "NOTICE")) return;
    if (!source_parts(m->prefix, nick, user, host)) {
        size_t own_length = strlen(a->nick);
        if (m->prefix && !strncmp(m->prefix, a->nick, own_length) &&
            (m->prefix[own_length] == '!' || m->prefix[own_length] == '@'))
            app_disconnect(a, "Own relay identity exceeds supported bounds");
        else if (++a->bad_lines >= 8)
            app_disconnect(a, "Too many malformed source identities");
        return;
    }
    if (!strcmp(command, "PING")) {
        if (!m->nparams || !app_outgoing(a, 0, "PONG :%s", m->params[m->nparams - 1]))
            app_disconnect(a, "Invalid or excessive server PING requests");
        return;
    }
    if (!strcmp(command, "PONG")) return;
    if (!strcmp(command, "CAP")) { capability(a, m); return; }
    if (!strcmp(command, "AUTHENTICATE")) { sasl_response(a, m); return; }
    if (!strcmp(command, "903")) {
        if (!a->auth_sent) { app_disconnect(a, "Unexpected SASL success response"); return; }
        a->authenticated = 1;
        app_log(a, 0, "SASL authentication succeeded.");
        capability_ready(a);
        return;
    }
    if (!strcmp(command, "902") || !strcmp(command, "904") ||
        !strcmp(command, "905") || !strcmp(command, "906") || !strcmp(command, "907")) {
        if (auth_required(a))
            app_disconnect(a, "SASL authentication failed; no channels joined");
        return;
    }
    if (!strcmp(command, "900")) return;
    if (a->reconnect_enabled && !a->registered &&
        (!strcmp(command, "432") || !strcmp(command, "463"))) {
        server_reply(a, m);
        app_disconnect(a, !strcmp(command, "432") ?
            "Server rejected the configured nickname; automatic reconnect stopped" :
            "Server rejected this host; automatic reconnect stopped");
        return;
    }
    if (!strcmp(command, "464") || !strcmp(command, "465")) {
        server_reply(a, m);
        if (a->reconnect_enabled)
            app_disconnect(a, !strcmp(command, "464") ?
                "Server password rejected; automatic reconnect stopped" :
                "Server access denied; automatic reconnect stopped");
        return;
    }
    if (!strcmp(command, "ERROR")) {
        /* A registered server may send ERROR before shutdown. Pre-registration
         * rejection is held so wrong credentials/bans do not cause retry loops. */
        disconnect_retry(a, m->nparams ? m->params[m->nparams - 1] : "Server ERROR", a->registered);
        return;
    }
    if (!strcmp(command, "001")) {
        if (a->registered) return;
        if (auth_required(a) && !a->authenticated) {
            app_disconnect(a, "Server registered before required SASL authentication");
            return;
        }
        if (m->nparams && irc_valid_nick(m->params[0])) {
            copy_text(a->nick, sizeof(a->nick), m->params[0]);
            copy_text(a->cfg.nick, sizeof(a->cfg.nick), a->nick);
        }
        a->registered = 1;
        a->registered_at = app_now_ms(a);
        a->cap_done = 1;
        a->cap_pending = 0;
        if (a->test_mode)
            {
                char shown[512];
                const IrcDisplayField fields[] = {{"nick", a->nick}};
                if (irc_display_render(shown, sizeof shown, "OFFLINE TEST MODE running as {nick}. No network or authentication.", fields, 1))
                    app_status(a, "%s", shown);
                else app_status(a, "OFFLINE TEST MODE running as %s. No network or authentication.", a->nick);
            }
        else
            {
                char shown[512];
                const IrcDisplayField fields[] = {{"nick", a->nick}};
                if (irc_display_render(shown, sizeof shown, "Connected as {nick}. /help opens the manual.", fields, 1))
                    app_status(a, "%s", shown);
                else app_status(a, "Connected as %s. /help opens the manual.", a->nick);
            }
        (void)app_outgoing(a, 0, "USERHOST %s", a->nick);
        if (a->cfg.bouncer_type == CONFIG_BOUNCER_NONE || a->test_mode) autojoin(a);
        if (io_has(a)) {
            autocommands_start(a);
            server_reply(a, m);
        }
        return;
    } else if (!strcmp(command, "005")) {
        isupport(a, m);
        return;
    } else if (!strcmp(command, "302")) {
        userhost_reply(a, m);
        return;
    } else if (!strcmp(command, "305") || !strcmp(command, "306")) {
        if (a->registered && m->nparams &&
            irc_equal(m->params[0], a->nick, a->casemap) &&
            (!m->prefix || (!strchr(m->prefix, '!') && !strchr(m->prefix, '@')))) {
            a->away = !strcmp(command, "306");
            if (!autocommand_presence_confirm(a, a->away)) {
                if (a->away) a->manual_away_pending = 0;
                else {
                    a->automatic_away = APP_AUTO_AWAY_NONE;
                    /* A 305 may acknowledge an earlier BACK. A later
                     * nonempty AWAY remains pending until its 306 arrives. */
                    a->automatic_away_clear_pending = 0;
                    a->automatic_away_retry_at = 0;
                }
            }
            app_status(a, "%s", a->away ? irc_tr("Away status confirmed by the server.") :
                       irc_tr("Away status cleared by the server."));
        }
        return;
    } else if (!strcmp(command, "396") && m->nparams >= 2) {
        if (irc_equal(m->params[0], a->nick, a->casemap))
            learn_self(a, a->nick, "", m->params[1]);
        return;
    } else if (!strcmp(command, "421") && !a->registered &&
               m->nparams >= 2 && !strcmp(m->params[1], "CAP")) {
        if (auth_required(a))
            app_disconnect(a, "Server lacks CAP; required SASL unavailable");
        else
            a->cap_done = 1;
        return;
    } else if (!strcmp(command, "433") || !strcmp(command, "432")) {
        {
            char detail[1024] = "", shown[2048];
            int has_nick = m->nparams >= 2;
            int in_use = !strcmp(command, "433");
            if (m->nparams >= 3)
                (void)snprintf(detail, sizeof detail, " %s", m->params[m->nparams - 1]);
            if (has_nick) {
                const IrcDisplayField fields[] = {
                    {"nick", m->params[1]}, {"detail", detail}
                };
                const char *message = in_use ?
                    "Nickname {nick} is already in use.{detail} Use /nick another-nick." :
                    "Nickname {nick} was rejected.{detail} Use /nick another-nick.";
                if (irc_display_render(shown, sizeof shown, message, fields, 2)) {
                    app_status(a, "%s", shown);
                    return;
                }
            } else {
                const IrcDisplayField fields[] = {{"detail", detail}};
                const char *message = in_use ?
                    "Nickname is already in use.{detail} Use /nick another-nick." :
                    "Nickname was rejected.{detail} Use /nick another-nick.";
                if (irc_display_render(shown, sizeof shown, message, fields, 1)) {
                    app_status(a, "%s", shown);
                    return;
                }
            }
        }
        /* Preserve the existing English status if rendering cannot fit. */
        app_status(a, "Nickname%s%s %s.%s%s Use /nick another-nick.",
                   m->nparams >= 2 ? " " : "",
                   m->nparams >= 2 ? m->params[1] : "",
                   !strcmp(command, "433") ? "is already in use" : "was rejected",
                   m->nparams >= 3 ? " " : "", m->nparams >= 3 ? m->params[m->nparams - 1] : "");
        return;
    } else if (!strcmp(command, "PRIVMSG") || !strcmp(command, "NOTICE")) {
        int action = 0;
        char *text;
        size_t n;
        if (m->nparams < 2) return;
        /* Other clients or replay may copy a service command under an earlier
         * nickname. Its destination identifies private control traffic even
         * when the source no longer matches our current nick. Keep these
         * copies out of display/history/logs, as for locally sent controls. */
        if (bouncer_service(a, m->params[0])) return;
        if (bouncer_service(a, nick)) {
            int suppressed = a->persistent_log_suppressed;
            a->persistent_log_suppressed = 1;
            app_log(a, 0, "%s: %s", nick, m->params[1]);
            a->persistent_log_suppressed = suppressed;
            return;
        }
        /* Ignore only identifiable user messages. Bare server sources,
         * numerics, PING and membership updates always reach their handlers. */
        if (m->prefix && (strchr(m->prefix, '!') || strchr(m->prefix, '@')) &&
            irc_valid_nick(nick)) {
            for (i = 0; i < a->cfg.ignore_count && i < CONFIG_IGNORE_MAX; ++i)
                if (irc_ignore_match(a->cfg.ignores[i], nick, user, host, a->casemap))
                    return;
        }
        text = m->params[1];
        n = strlen(text);
        if (n >= 9 && !memcmp(text, "\001ACTION ", 8) && text[n - 1] == '\001') {
            action = 1;
            text[n - 1] = 0;
            text += 8;
        } else if (*text == '\001') {
            if (!a->message_history && a->dcc_offer && !strcmp(command, "PRIVMSG") &&
                irc_equal(m->params[0], a->nick, a->casemap) &&
                m->prefix && (strchr(m->prefix, '!') || strchr(m->prefix, '@')) &&
                irc_valid_nick(nick) && n >= 7 && text[n - 1] == '\001' &&
                !memcmp(text + 1, "DCC ", 4) && !memchr(text + 1, '\001', n - 2)) {
                char body[IRC_BODY_MAX + 1];
                memcpy(body, text + 1, n - 2);
                body[n - 2] = 0;
                if (a->dcc_offer(a->dcc_context, nick, body)) return;
            }
            /* Other CTCP requests have no automatic responses or execution. */
            app_log(a, 0, "CTCP from %s: %s", nick, text);
            return;
        }
        if (irc_valid_nick(nick) && irc_valid_target(m->params[0]))
            {
                int mine = irc_equal(nick, a->nick, a->casemap);
                chat_display(a, nick, m->params[0], text, action,
                             !strcmp(command, "NOTICE"), mine, 0);
            }
        else
            app_log(a, 0, "%s from %s: %s", command, m->prefix ? m->prefix : "server", text);
        return;
    } else if (!strcmp(command, "JOIN") && m->nparams && irc_valid_channel(m->params[0])) {
        int mine = irc_equal(nick, a->nick, a->casemap), active_channel = 0;
        if (!irc_valid_nick(nick)) return;
        view = app_buffer(a, m->params[0], B_CHANNEL, 0);
        if (view == SIZE_MAX) {
            if (!mine) return;
            if (a->cfg.bouncer_type == CONFIG_BOUNCER_NONE && !configured_channel(a, m->params[0])) {
                (void)app_outgoing(a, 0, "PART %s", m->params[0]);
                app_log(a, 0, "Leaving late or unrequested JOIN to %s.", m->params[0]);
                return;
            }
            view = app_buffer(a, m->params[0], B_CHANNEL, 1);
            if (view == SIZE_MAX) return;
        }
        for (i = 3; i < a->buffer_count; ++i)
            if (a->buffers[i].kind == B_CHANNEL && a->buffers[i].joined)
                active_channel = 1;
        if (mine) {
            a->buffers[view].joined = 1;
            a->buffers[view].names_in_progress = 0;
            learn_self(a, nick, user, host);
            if (!active_channel && a->current == 0 && !a->buffers[view].background_join)
                app_switch(a, view);
            a->buffers[view].background_join = 0;
        }
        user_add(a, &a->buffers[view], nick, 0);
        app_log(a, view, "%s has joined %s", nick, m->params[0]);
        return;
    } else if (!strcmp(command, "PART") && m->nparams) {
        view = app_buffer(a, m->params[0], B_CHANNEL, 0);
        if (view == SIZE_MAX) return;
        user_remove(a, &a->buffers[view], nick);
        if (irc_equal(nick, a->nick, a->casemap)) {
            a->buffers[view].joined = 0;
            a->buffers[view].user_count = 0;
            {
                char shown[512];
                const IrcDisplayField fields[] = {{"channel", m->params[0]}};
                if (irc_display_render(shown, sizeof shown, "Left {channel}; history retained. /close removes its view.", fields, 1))
                    app_status(a, "%s", shown);
                else app_status(a, "Left %s; history retained. /close removes its view.", m->params[0]);
            }
        }
        app_log(a, view, "%s has left %s%s%s", nick, m->params[0],
                m->nparams > 1 ? ": " : "", m->nparams > 1 ? m->params[1] : "");
        return;
    } else if (!strcmp(command, "QUIT")) {
        for (i = 3; i < a->buffer_count; ++i) {
            Buffer *b = &a->buffers[i];
            if (user_find(a, b, nick) != SIZE_MAX) {
                user_remove(a, b, nick);
                app_log(a, i, "%s has disconnected%s%s", nick, m->nparams ? ": " : "",
                        m->nparams ? m->params[0] : "");
            }
        }
        return;
    } else if (!strcmp(command, "NICK") && m->nparams && irc_valid_nick(m->params[0])) {
        const char *newnick = m->params[0];
        if (irc_equal(nick, a->nick, a->casemap)) {
            copy_text(a->nick, sizeof(a->nick), newnick);
            copy_text(a->cfg.nick, sizeof(a->cfg.nick), newnick);
        }
        for (i = 3; i < a->buffer_count; ++i) {
            Buffer *b = &a->buffers[i];
            size_t u = user_find(a, b, nick);
            if (u != SIZE_MAX) {
                User renamed = b->users[u];
                copy_text(renamed.nick, sizeof(renamed.nick), newnick);
                if (u + 1 < b->user_count)
                    memmove(b->users + u, b->users + u + 1,
                            (b->user_count - u - 1) * sizeof(*b->users));
                --b->user_count;
                user_insert(a, b, &renamed, user_position(a, b, renamed.nick));
                app_log(a, i, "%s is now known as %s", nick, newnick);
            }
            if (b->kind == B_QUERY && irc_equal(b->name, nick, a->casemap))
                copy_text(b->name, sizeof(b->name), newnick);
        }
        a->dirty = 1;
        return;
    } else if (!strcmp(command, "KICK") && m->nparams >= 2) {
        view = app_buffer(a, m->params[0], B_CHANNEL, 0);
        if (view == SIZE_MAX) return;
        user_remove(a, &a->buffers[view], m->params[1]);
        if (irc_equal(m->params[1], a->nick, a->casemap)) {
            a->buffers[view].joined = 0;
            a->buffers[view].user_count = 0;
        }
        app_log(a, view, "%s removed %s from %s%s%s", nick, m->params[1], m->params[0],
                m->nparams > 2 ? ": " : "", m->nparams > 2 ? m->params[2] : "");
        return;
    } else if (!strcmp(command, "353")) {
        names_reply(a, m);
        return;
    } else if (!strcmp(command, "366") && m->nparams >= 2) {
        view = app_buffer(a, m->params[1], B_CHANNEL, 0);
        if (view != SIZE_MAX) a->buffers[view].names_in_progress = 0;
        return;
    } else if (!strcmp(command, "MODE")) {
        mode_reply(a, m);
        view = m->nparams ? app_buffer(a, m->params[0], B_CHANNEL, 0) : SIZE_MAX;
        if (view != SIZE_MAX) {
            app_log(a, view, "%s changed modes: %s", nick,
                    m->nparams > 1 ? m->params[1] : "");
            return;
        }
        if (m->nparams >= 2)
            app_log(a, 0, "%s changed modes on %s: %s", nick, m->params[0], m->params[1]);
        return;
    } else if (!strcmp(command, "INVITE") && m->nparams >= 2) {
        app_log(a, 0, "%s invited %s to %s", nick,
                irc_equal(m->params[0], a->nick, a->casemap) ? "you" : m->params[0], m->params[1]);
        return;
    } else if ((!strcmp(command, "TOPIC") || !strcmp(command, "332")) && m->nparams >= 2) {
        size_t ch = !strcmp(command, "332") ? 1u : 0u;
        if (m->nparams > ch + 1) {
            view = app_buffer(a, m->params[ch], B_CHANNEL, 0);
            if (view != SIZE_MAX) {
                app_log(a, view, "Topic: %s", m->params[ch + 1]);
                return;
            }
        }
    }
    if (io_has(a)) server_reply(a, m);
}

/* Metadata is scoped to exactly one synchronous dispatch. Clearing both
 * sides prevents a history timestamp or display-only flag from leaking into
 * later local status output; m itself borrows the parser's line storage. */
static void incoming_message(App *a, IrcMessage *m)
{
    a->message_time[0] = 0;
    if (a->cap_time) (void)irc_server_time(m->tags, a->message_time, sizeof a->message_time);
    a->message_history = batch_history(a, m);
    incoming_dispatch(a, m);
    a->message_time[0] = 0;
    a->message_history = 0;
}

/* Event-loop progress: helper/setup work, bounded I/O, then due timers. */
/* One event-loop turn advances nonblocking I/O and
 * deadline-driven work. Call on timeout-only turns as well as readiness:
 * authentication, reconnects and output pacing must keep progressing. */
void app_network(App *a, short revents)
{
    int64_t now = app_now_ms(a);
    size_t reads, i;
    char bytes[1024];
    if (!a->test_mode) {
        credential_reap();
        if (a->credential) advance_credential(a, now);
    }
    if (!io_has(a)) {
        if (a->reconnect_enabled && !a->test_mode && a->reconnect_at &&
            now >= a->reconnect_at && !a->quit) {
            a->reconnect_at = 0;
            (void)connect_attempt(a, 1);
        }
        return;
    }
    if (io_step(a, revents, now) < 0 || io_state(a) == NET_FAILED) {
        char error[512];
        copy_text(error, sizeof(error), io_error(a));
        disconnect_retry(a, error, !a->test_mode && net_retryable(a->net));
        return;
    }
    if (io_state(a) != NET_READY) {
        a->dirty = 1;
        return;
    }
    if (!a->started && !a->credential)
        begin_registration(a);
    if (!io_has(a))
        return;
    /* Hold registration input until PASS is available, so an unsolicited 001
     * cannot register or auto-join while its required credential is pending. */
    if (!a->started && a->credential) return;
    write_output(a, now);
    /* Yield after bounded chunks even if a peer keeps the socket readable.
     * Complete lines borrow incoming until dispatch returns. */
    for (reads = 0; io_has(a) && reads < 8; ++reads) {
        ssize_t n;
        if (!io_pending(a) && !(revents & (POLLIN | POLLOUT | POLLHUP | POLLERR)))
            break;
        n = io_read(a, bytes, sizeof(bytes));
        if (n == -2)
            break;
        if (n <= 0) {
            char error[512];
            copy_text(error, sizeof(error), n == 0 ? "Server closed the connection" : io_error(a));
            disconnect_retry(a, error, !a->test_mode && (n == 0 || net_retryable(a->net)));
            return;
        }
        a->last_receive = now;
        for (i = 0; io_has(a) && i < (size_t)n; ++i) {
            unsigned char c = (unsigned char)bytes[i];
            if (!c) {
                if (!a->cfg.tls) {
                    char mismatch[512];
                    int looks_tls = a->incoming_len >= 3 &&
                        (unsigned char)a->incoming[0] >= 20 && (unsigned char)a->incoming[0] <= 23 &&
                        (unsigned char)a->incoming[1] == 3 && (unsigned char)a->incoming[2] <= 4;
                    (void)snprintf(mismatch, sizeof mismatch,
                        "Server sent a NUL byte on the unencrypted IRC connection to %s:%d (TLS disabled). %s Check Transport and Port, then /connect",
                        a->cfg.server, a->cfg.port, looks_tls ?
                        "The response looks like a TLS record; select verified TLS for a TLS endpoint." :
                        "This is not valid IRC text; the endpoint may require TLS.");
                    app_disconnect(a, mismatch);
                } else {
                    app_disconnect(a, "Server sent a NUL byte inside verified TLS; malformed IRC text rejected. Check that this endpoint is an IRC service");
                }
                return;
            }
            if (c == '\n') {
                IrcMessage message;
                if (a->incoming_len && a->incoming[a->incoming_len - 1] == '\r')
                    --a->incoming_len;
                a->incoming[a->incoming_len] = 0;
                if (a->incoming_len) {
                    if (irc_parse(a->incoming, &message)) {
                        /* Service replies can contain reset tokens or echoed
                         * credentials, including CTCP and unfamiliar replies.
                         * A bouncer may also replay an outgoing authentication
                         * message under an earlier nickname, or copy one from
                         * another attached client. Its destination/content,
                         * not a match with our current nick, decides privacy.
                         * Apply this before dispatch, including CTCP/fallback
                         * paths and qualified or malformed private targets;
                         * valid channel conversation keeps ordinary logging. */
                        int private_message = message.nparams >= 2 &&
                            (!strcmp(message.command, "PRIVMSG") || !strcmp(message.command, "NOTICE")) &&
                            !irc_valid_channel(message.params[0]);
                        a->persistent_log_suppressed = authentication_service(a, message.prefix) ||
                            (private_message && persistent_sensitive_outgoing(a, message.params[0], message.params[1]));
                        incoming_message(a, &message);
                        a->persistent_log_suppressed = 0;
                    } else if (++a->bad_lines >= 8) {
                        app_disconnect(a, "Too many malformed or oversized IRC lines");
                        return;
                    }
                }
                a->incoming_len = 0;
            } else {
                if (a->incoming_len == IRC_WIRE_MAX ||
                    (a->incoming_len && a->incoming[a->incoming_len - 1] == '\r')) {
                    app_disconnect(a, "Oversized IRC frame or embedded carriage return");
                    return;
                }
                a->incoming[a->incoming_len++] = (char)c;
            }
        }
    }
    if (!io_has(a))
        return;
    if (!a->registered && now - a->began > 60000) {
        disconnect_retry(a, "Registration timed out", !auth_required(a) && !a->cfg.server_password_file[0] &&
            !a->cfg.server_password_gpg_file[0] && !a->cfg.server_password[0]);
        return;
    }
    if (!a->cap_done && now - a->began > 15000) {
        if (auth_required(a)) {
            app_disconnect(a, "Required SASL negotiation timed out");
            return;
        }
        cap_end(a);
    }
    if (!io_has(a))
        return;
    if (a->registered && now - a->last_receive > 240000) {
        disconnect_retry(a, "Server stopped responding", 1);
        return;
    }
    if (a->registered && now - a->last_receive > 120000 && now - a->last_ping > 60000) {
        (void)app_outgoing(a, 0, "PING :vipirc-%lld", (long long)now);
        a->last_ping = now;
    }
    presence_advance(a, now);
    autocommands_advance(a, now);
    write_output(a, now);
}

int app_fd(const App *a)
{
    return a->test_mode || !a->net || a->credential_kind == CREDENTIAL_SERVER ? -1 : net_fd(a->net);
}

short app_events(const App *a)
{
    int want;
    if (!io_has(a))
        return 0;
    if (a->test_mode) return 0;
    if (a->credential_kind == CREDENTIAL_SERVER) return 0;
    want = a->inflight || a->control_head ||
           (a->chat_head && (!a->last_chat || app_now_ms(a) - a->last_chat >= 1200));
    return net_events(a->net, want);
}

int app_timeout(const App *a)
{
    int64_t remaining;
    int timeout = 1000;
    if (!a->test_mode && (a->credential || credential_cleanup_pending())) return 50;
    if (!io_has(a)) {
        if (a->reconnect_enabled && !a->test_mode && a->reconnect_at) {
            remaining = a->reconnect_at - app_now_ms(a);
            if (remaining <= 0) return 0;
            if (remaining < timeout) return (int)remaining;
        }
        return timeout;
    }
    if (io_pending(a))
        return 0;
    if (a->test_mode && (!a->started || a->control_head || a->inflight))
        return 0;
    /* Poll readiness drives blocked output. A zero timeout here would spin
     * while the transport is waiting for writable/readable TLS progress. */
    if (a->control_head || a->inflight)
        return 1000;
    if (a->chat_head) {
        remaining = a->last_chat + 1200 - app_now_ms(a);
        if (!a->last_chat || remaining <= 0) return 0;
        if (remaining < timeout) timeout = (int)remaining;
    }
    if (a->registered) {
        if (a->automatic_away_clear_pending) {
            remaining = a->automatic_away_retry_at - app_now_ms(a);
            if (remaining <= 0) return 0;
            if (remaining < timeout) timeout = (int)remaining;
        }
        if (automatic_away_allowed(a) && a->backgrounded && a->cfg.background_away && a->automatic_away == APP_AUTO_AWAY_NONE &&
            !a->manual_away_pending && !a->away) {
            remaining = a->automatic_away_retry_at - app_now_ms(a);
            if (remaining <= 0) return 0;
            if (remaining < timeout) timeout = (int)remaining;
        }
        if (automatic_away_allowed(a) && !a->backgrounded && a->cfg.autoaway_minutes > 0 &&
            a->automatic_away == APP_AUTO_AWAY_NONE && !a->manual_away_pending && !a->away) {
            int64_t due = a->last_local_activity + (int64_t)a->cfg.autoaway_minutes * 60000;
            if (a->automatic_away_retry_at > due) due = a->automatic_away_retry_at;
            remaining = due - app_now_ms(a);
            if (remaining <= 0) return 0;
            if (remaining < timeout) timeout = (int)remaining;
        }
    }
    if (!a->test_mode && a->registered && a->autocommand_active && !a->autocommand_waiting) {
        remaining = a->autocommand_due - app_now_ms(a);
        if (remaining <= 0) return 0;
        if (remaining < timeout) timeout = (int)remaining;
    }
    return a->test_mode ? sim_timeout(a->sim, app_now_ms(a)) : timeout;
}

/* Local view lifecycle and command parsing. */
static int channels_change(App *a, const char *channel, int add)
{
    char old[1024], fresh[1024] = "", *p, *next;
    size_t used = 0;
    int found = 0;
    copy_text(old, sizeof(old), session_channels(a));
    for (p = old; *p; p = next) {
        size_t n;
        next = strchr(p, ',');
        if (next) *next++ = 0;
        else next = p + strlen(p);
        if (irc_equal(p, channel, a->casemap)) {
            found = 1;
            if (!add) { if (!*next) break; else continue; }
        }
        n = strlen(p);
        if (used + n + (used ? 1u : 0u) >= sizeof(fresh)) return 0;
        if (used) fresh[used++] = ',';
        memcpy(fresh + used, p, n + 1);
        used += n;
        if (!*next) break;
    }
    if (add && !found) {
        size_t n = strlen(channel);
        if (used + n + (used ? 1u : 0u) >= sizeof(fresh)) return 0;
        if (used) fresh[used++] = ',';
        memcpy(fresh + used, channel, n + 1);
    }
    if (!configured_channels_valid(fresh)) return 0;
    if (a->test_mode) copy_text(a->test_channels, sizeof(a->test_channels), fresh);
    else copy_text(a->cfg.channels, sizeof(a->cfg.channels), fresh);
    return 1;
}

static int outline_target(const App *a, const OutLine *line,
                          const char *command, const char *target)
{
    char body[IRC_BODY_MAX + 1];
    IrcMessage message;
    if (!line || line->length < 2 || line->length > IRC_BODY_MAX + 2) return 0;
    memcpy(body, line->data, line->length - 2);
    body[line->length - 2] = 0;
    int match = irc_parse(body, &message) && !strcmp(message.command, command) &&
                message.nparams && irc_equal(message.params[0], target, a->casemap);
    config_wipe(body, sizeof body);
    return match;
}

static size_t autocommands_cancel_target(App *a, const char *target)
{
    size_t first = a->autocommand_next;
    if (!a->autocommand_active) return 0;
    if (a->autocommand_waiting && first) --first;
    for (size_t i = first; i < a->cfg.autocommands.count && i < CONFIG_AUTOCOMMAND_MAX; ++i) {
        ConfigAutoCommand command;
        int parsed = config_autocommand_parse(a->cfg.autocommands.lines[i], &command, NULL, 0);
        int match = parsed > 0 && (command.kind == CONFIG_AUTO_MSG || command.kind == CONFIG_AUTO_JOIN ||
                                   command.kind == CONFIG_AUTO_WHOIS) &&
                    irc_equal(command.target, target, a->casemap);
        config_wipe(&command, sizeof command);
        if (match) return app_autocommands_cancel(a);
    }
    return 0;
}

static size_t drop_target_queue(App *a, int chat, const char *command,
                                const char *target)
{
    OutLine **link = chat ? &a->chat_head : &a->control_head;
    OutLine *last = NULL, *p;
    size_t removed = 0;
    while ((p = *link) != NULL) {
        if (outline_target(a, p, command, target)) {
            *link = p->next;
            outline_free(p);
            ++removed;
        } else {
            last = p;
            link = &p->next;
        }
    }
    if (chat) {
        a->chat_tail = last;
        a->chat_count -= removed;
    } else {
        a->control_tail = last;
        a->control_count -= removed;
    }
    return removed;
}

int app_close_view(App *a, size_t index)
{
    Buffer *b;
    char name[201];
    size_t cancelled;
    int part = 0, inflight;
    if (index < 3 || index >= a->buffer_count) {
        app_status(a, "%s", irc_tr("Server, mentions and help views cannot be closed; /disconnect leaves the server."));
        return 0;
    }
    b = &a->buffers[index];
    copy_text(name, sizeof(name), b->name);
    if (!remember_closed_query(a, b)) return 0;
    /* A requested channel may still be awaiting JOIN acknowledgement. PART
     * follows any already-transmitted JOIN; queued JOINs are removed below. */
    if (b->kind == B_CHANNEL && a->registered &&
        (b->joined || b->background_join || configured_channel(a, name))) {
        int queued = *a->cfg.part_message ?
            app_outgoing(a, 0, "PART %s :%s", name, a->cfg.part_message) :
            app_outgoing(a, 0, "PART %s", name);
        if (!queued) return 0;
        part = 1;
    }
    (void)autocommands_cancel_target(a, name);
    if (b->kind == B_CHANNEL) {
        (void)channels_change(a, name, 0);
        (void)drop_target_queue(a, 0, "JOIN", name);
    }
    inflight = a->inflight && a->inflight->chat &&
               outline_target(a, a->inflight, "PRIVMSG", name);
    cancelled = drop_target_queue(a, 1, "PRIVMSG", name);
    if (a->view_closed) a->view_closed(a->view_context, index);
    app_clear_buffer(a, index);
    free(b->lines);
    free(b->line_origins);
    free(b->line_queries);
    free(b->users);
    if (index + 1 < a->buffer_count)
        memmove(a->buffers + index, a->buffers + index + 1,
                (a->buffer_count - index - 1) * sizeof(*a->buffers));
    --a->buffer_count;
    memset(&a->buffers[a->buffer_count], 0, sizeof(*a->buffers));
    if (a->current == index) {
        a->current = 0;
        if (a->visible)
            a->buffers[0].unread = a->buffers[0].unread_mentions = 0;
    } else if (a->current > index) {
        --a->current;
    }
    {
        char count_text[32], shown[2048];
        (void)snprintf(count_text, sizeof count_text, "%zu", cancelled);
        const IrcDisplayField fields[] = {
            {"target", name},
            {"part", part ? irc_tr(" (PART requested)") : ""},
            {"count", count_text},
            {"inflight", inflight ? irc_tr(" One in-flight message will finish.") : ""}
        };
        const char *message = cancelled == 1 ?
            "Closed {target}{part}; {count} queued message cancelled.{inflight}" :
            "Closed {target}{part}; {count} queued messages cancelled.{inflight}";
        if (irc_display_render(shown, sizeof shown, message, fields, 4)) {
            app_log(a, 0, "%s", shown);
            app_status(a, "%s", shown);
        } else {
            /* On translation failure preserve both user-visible notices. */
            app_log(a, 0, "Closed %s%s; %zu queued message%s cancelled.%s", name,
                    part ? " (PART requested)" : "", cancelled, cancelled == 1 ? "" : "s",
                    inflight ? " One in-flight message will finish." : "");
            app_status(a, "Closed %s%s; %zu queued message%s cancelled.%s", name,
                       part ? " (PART requested)" : "", cancelled, cancelled == 1 ? "" : "s",
                       inflight ? " One in-flight message will finish." : "");
        }
    }
    a->dirty = 1;
    return 1;
}

/* All user-visible numeric view arguments match the channel chooser: zero
 * is the server and positive numbers count channel views only. */
static size_t channel_number(const App *a, unsigned long number)
{
    size_t i;
    if (!number) return 0;
    for (i = 3; i < a->buffer_count; ++i) {
        if (a->buffers[i].kind == B_CHANNEL && --number == 0) return i;
    }
    return SIZE_MAX;
}

static size_t named_or_numbered_view(const App *a, const char *name, int channel_only)
{
    size_t i;
    char *end;
    unsigned long number;
    errno = 0;
    number = strtoul(name, &end, 10);
    if (*name && !*end && !errno) return channel_number(a, number);
    if (!channel_only) {
        for (i = 3; i < a->buffer_count; ++i)
            if (a->buffers[i].kind == B_QUERY &&
                irc_equal(a->buffers[i].name, name, a->casemap)) return i;
    }
    for (i = 0; i < a->buffer_count; ++i) {
        if (channel_only && a->buffers[i].kind != B_CHANNEL && i != 0) continue;
        if (irc_equal(a->buffers[i].name, name, a->casemap)) return i;
    }
    return SIZE_MAX;
}

static char *next_word(char **rest)
{
    char *p = *rest, *start;
    while (*p == ' ') ++p;
    start = p;
    while (*p && *p != ' ') ++p;
    if (*p) *p++ = 0;
    while (*p == ' ') ++p;
    *rest = p;
    return start;
}

int app_measure_draft(const App *a, size_t view, const char *utf8, int literal,
                      AppDraftMeasure *out)
{
    char command[APP_TEXT + 1], *rest, *name, *arg;
    const char *text = utf8, *target = NULL;
    const Buffer *b;
    IrcChunk chunks[APP_QUEUE];
    size_t i, line_bytes = 0, count = 0;
    int action = 0;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!a || !utf8 || view >= a->buffer_count ||
        strnlen(utf8, APP_TEXT + 1) > APP_TEXT || !irc_utf8_valid(utf8))
        return 0;
    for (i = 0; utf8[i]; ++i) {
        unsigned char c = (unsigned char)utf8[i];
        if ((c < 0x20 && c != '\n') || c == 0x7f) return 0;
    }
    b = &a->buffers[view];
    if (literal || utf8[0] != '/' || utf8[1] == '/') {
        if (b->kind != B_CHANNEL && b->kind != B_QUERY) return 0;
        target = b->name;
        if (!literal && utf8[0] == '/') ++text;
    } else {
        /* Use the same token extraction as app_command. Only message-producing
         * syntax is inspected here; no command handler is ever executed. */
        copy_text(command, sizeof(command), utf8 + 1);
        rest = command;
        name = next_word(&rest);
        if (strchr(name, '\n')) return 0;
        for (i = 0; name[i]; ++i)
            if (name[i] >= 'A' && name[i] <= 'Z') name[i] = (char)(name[i] + 32);
        if (!strcmp(name, "me")) {
            if (b->kind != B_CHANNEL && b->kind != B_QUERY) return 0;
            target = b->name;
            text = rest;
            action = 1;
        } else if (!strcmp(name, "msg")) {
            arg = next_word(&rest);
            if (!irc_valid_target(arg)) return 0;
            target = arg;
            text = rest;
        } else {
            return 0;
        }
    }
    if (!irc_valid_target(target)) return 0;
    copy_text(out->target, sizeof(out->target), target);
    out->action = action;
    out->budget = app_budget(a, target, action);
    for (i = 0; text[i]; ++i) {
        if (text[i] == '\n') {
            if (line_bytes > out->longest_line_bytes)
                out->longest_line_bytes = line_bytes;
            line_bytes = 0;
        } else {
            ++line_bytes;
            ++out->payload_bytes;
        }
    }
    if (line_bytes > out->longest_line_bytes) out->longest_line_bytes = line_bytes;
    if (!out->budget) return 1;
    /* The draft has already passed strict UTF-8 validation. A leading byte
     * therefore determines each scalar's byte length without decoding again.
     * Distinguish an impossible scalar from exceeding the 64-message cap. */
    for (i = 0; text[i];) {
        unsigned char c = (unsigned char)text[i];
        size_t bytes;
        if (c == '\n') { ++i; continue; }
        bytes = c < 0x80 ? 1u : c < 0xe0 ? 2u : c < 0xf0 ? 3u : 4u;
        if (bytes > out->budget) return 1;
        i += bytes;
    }
    out->fits = plan_chunks(text, out->budget, chunks, &count);
    out->messages = out->fits ? count : APP_QUEUE + 1u;
    return 1;
}

static int require_registered(App *a)
{
    if (a->registered) return 1;
    app_status(a, "%s", irc_tr("This command requires a registered connection."));
    return 0;
}

static int bouncer_control_send(App *a, const char *target, const char *text)
{
    if (a->test_mode || !a->registered || !io_has(a) || !a->cfg.tls ||
        !bouncer_service(a, target) ||
        (a->cfg.bouncer_type == CONFIG_BOUNCER_SOJU && !a->authenticated)) {
        app_status(a, "%s", irc_tr("Bouncer commands require the matching registered bouncer connection over verified TLS."));
        return 0;
    }
    if (!text || !*text || !irc_utf8_valid(text)) {
        app_status(a, "%s", irc_tr("A nonempty UTF-8 bouncer command is required."));
        return 0;
    }
    for (size_t i = 0; text[i]; ++i)
        if ((unsigned char)text[i] < 32 || (unsigned char)text[i] == 127) {
            app_status(a, "%s", irc_tr("Bouncer commands cannot contain line breaks or control characters."));
            return 0;
        }
    /* A control command is one bounded message: splitting it could execute a
     * different command. The normal paced queue and cancellation apply. */
    if (!app_outgoing(a, 1, "PRIVMSG %s :%s", target, text)) return 0;
    a->chat_tail->bouncer_control = a->cfg.bouncer_type;
    a->chat_tail->sensitive = 1;
    app_status(a, "%s", irc_tr("Bouncer command queued; its text is hidden from local history."));
    return 1;
}

/* LIST command grammar; only the optional mask is sent to the server. */
static void list_reset_results(App *a)
{
    a->list.count = 0;
    a->list.received = 0;
    a->list.matched = 0;
    a->list.active = 0;
    a->list.complete = 0;
    a->list.truncated = 0;
    a->list.sorted_revision = 0;
    ++a->list.revision;
    a->dirty = 1;
}

int app_list_repeat(App *a)
{
    if (!a) return 0;
    if (!require_registered(a)) return 0;
    if (a->list.active) {
        app_status(a, "%s", irc_tr("A network channel list is already in progress."));
        return 0;
    }
    if (!(*a->list.server_mask ? app_outgoing(a, 0, "LIST %s", a->list.server_mask) :
                                 app_outgoing(a, 0, "LIST")))
        return 0;
    list_reset_results(a);
    a->list.active = 1;
    app_status(a, "Network channel list requested%s; directory updates as replies arrive.",
               *a->list.server_mask ? " with a server mask" : "");
    return 1;
}

static int list_number(const char *text, uint64_t *value)
{
    char *end = NULL;
    unsigned long long parsed;
    if (!text || !*text || *text == '+' || *text == '-') return 0;
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno || !end || *end) return 0;
    *value = (uint64_t)parsed;
    return 1;
}

static int list_usage(App *a)
{
    app_status(a, "Usage: /list [MASK] [--min-users N] [--max-users N] [--name TEXT] [--topic TEXT] [--sort channel|users|topic] [--asc|--desc] [--limit N], /list show [--sort channel|users|topic] [--asc|--desc] [--limit N], or /list clear");
    return 0;
}

static int list_command(App *a, char *rest)
{
    char mask[201] = "", name_filter[201] = "", topic_filter[257] = "";
    uint64_t min_users = 0, max_users = 0, limit = 0;
    int min_set = 0, max_set = 0, sort = APP_LIST_SORT_USERS, descending = 1;
    int mask_set = 0;
    char *p = rest;
    char *word;
    while (*p == ' ') ++p;
    if (!strncmp(p, "show", 4) && (!p[4] || p[4] == ' ')) {
        int show_sort = a->list.sort;
        int show_descending = a->list.descending;
        size_t show_limit = a->list.display_limit;
        p += 4;
        while (*(word = next_word(&p))) {
            if (!strcmp(word, "--asc")) { show_descending = 0; continue; }
            if (!strcmp(word, "--desc")) { show_descending = 1; continue; }
            if (!strcmp(word, "--sort") || !strcmp(word, "--limit")) {
                char *value = next_word(&p);
                uint64_t number = 0;
                if (!*value) return list_usage(a);
                if (!strcmp(word, "--sort")) {
                    if (!strcmp(value, "channel") || !strcmp(value, "name")) show_sort = APP_LIST_SORT_CHANNEL;
                    else if (!strcmp(value, "users")) show_sort = APP_LIST_SORT_USERS;
                    else if (!strcmp(value, "topic")) show_sort = APP_LIST_SORT_TOPIC;
                    else return list_usage(a);
                } else {
                    if (!list_number(value, &number) || number > APP_LIST_MAX) return list_usage(a);
                    show_limit = (size_t)number;
                }
                continue;
            }
            return list_usage(a);
        }
        a->list.display_limit = show_limit;
        app_list_sort(a, (AppListSort)show_sort, show_descending);
        app_status(a, "%s", a->list.active ? irc_tr("Channel directory is still receiving results.") :
                   a->list.complete ? irc_tr("Showing the cached channel directory.") :
                   irc_tr("No completed channel directory is cached; use /list first."));
        return 6;
    }
    if (!strncmp(p, "clear", 5) && (!p[5] || p[5] == ' ')) {
        char *tail = p + 5;
        while (*tail == ' ') ++tail;
        if (*tail) return list_usage(a);
        if (a->list.active) { app_status(a, "%s", irc_tr("Wait for the current LIST request to finish before clearing it.")); return 0; }
        app_list_clear(a);
        app_status(a, "%s", irc_tr("Cached network channel directory cleared."));
        return 1;
    }
    while (*(word = next_word(&p))) {
        if (!strcmp(word, "--asc")) { descending = 0; continue; }
        if (!strcmp(word, "--desc")) { descending = 1; continue; }
        if (!strcmp(word, "--min-users") || !strcmp(word, "--max-users") ||
            !strcmp(word, "--name") || !strcmp(word, "--topic") ||
            !strcmp(word, "--sort") || !strcmp(word, "--limit")) {
            char *value = next_word(&p);
            if (!*value) return list_usage(a);
            if (!strcmp(word, "--min-users")) {
                if (!list_number(value, &min_users)) return list_usage(a);
                min_set = 1;
            } else if (!strcmp(word, "--max-users")) {
                if (!list_number(value, &max_users)) return list_usage(a);
                max_set = 1;
            } else if (!strcmp(word, "--limit")) {
                if (!list_number(value, &limit) || limit > APP_LIST_MAX) return list_usage(a);
            } else if (!strcmp(word, "--name")) {
                if (strlen(value) >= sizeof name_filter) return list_usage(a);
                copy_text(name_filter, sizeof name_filter, value);
            } else if (!strcmp(word, "--topic")) {
                if (strlen(value) >= sizeof topic_filter) return list_usage(a);
                copy_text(topic_filter, sizeof topic_filter, value);
            } else if (!strcmp(word, "--sort")) {
                if (!strcmp(value, "channel") || !strcmp(value, "name")) sort = APP_LIST_SORT_CHANNEL;
                else if (!strcmp(value, "users")) sort = APP_LIST_SORT_USERS;
                else if (!strcmp(value, "topic")) sort = APP_LIST_SORT_TOPIC;
                else return list_usage(a);
            }
            continue;
        }
        if (!strncmp(word, "--", 2) || mask_set || *word == ':' || strlen(word) > IRC_TARGET_MAX)
            return list_usage(a);
        copy_text(mask, sizeof mask, word);
        mask_set = 1;
    }
    if (min_set && max_set && min_users > max_users) {
        app_status(a, "%s", irc_tr("Minimum users cannot be greater than maximum users."));
        return 0;
    }
    if (!require_registered(a)) return 0;
    if (a->list.active) {
        app_status(a, "%s", irc_tr("A network channel list is already in progress."));
        return 0;
    }
    if (!(*mask ? app_outgoing(a, 0, "LIST %s", mask) : app_outgoing(a, 0, "LIST")))
        return 0;
    free(a->list.entries);
    memset(&a->list, 0, sizeof a->list);
    a->list.min_users = min_users; a->list.max_users = max_users;
    a->list.min_set = min_set; a->list.max_set = max_set;
    a->list.sort = (AppListSort)sort; a->list.descending = descending;
    a->list.display_limit = (size_t)limit;
    copy_text(a->list.server_mask, sizeof a->list.server_mask, mask);
    copy_text(a->list.name_filter, sizeof a->list.name_filter, name_filter);
    copy_text(a->list.topic_filter, sizeof a->list.topic_filter, topic_filter);
    list_reset_results(a);
    a->list.active = 1;
    app_status(a, "Network channel list requested%s; directory updates as replies arrive.",
               *a->list.server_mask ? " with a server mask" : "");
    return 6;
}

static int ignore_equal(const App *a, const char *one, const char *two)
{
    const char *bang_one = strchr(one, '!'), *bang_two = strchr(two, '!');
    char nick_one[CONFIG_IGNORE_SIZE], nick_two[CONFIG_IGNORE_SIZE];
    if (!bang_one || !bang_two)
        return !bang_one && !bang_two && irc_equal(one, two, a->casemap);
    size_t first = (size_t)(bang_one - one), second = (size_t)(bang_two - two);
    if (first >= sizeof(nick_one) || second >= sizeof(nick_two)) return 0;
    memcpy(nick_one, one, first); nick_one[first] = 0;
    memcpy(nick_two, two, second); nick_two[second] = 0;
    return irc_equal(nick_one, nick_two, a->casemap) &&
           irc_equal(bang_one + 1, bang_two + 1, IRC_CASE_ASCII);
}

static int ignore_command(App *a, char *rest, int remove)
{
    char *mask = next_word(&rest);
    size_t i;
    const char *persistence = a->test_mode ? irc_tr(" Test session only; /save is disabled.") :
                                           irc_tr(" /save preserves the list.");
    if (!remove && (!*mask || (!strcmp(mask, "list") && !*rest))) {
        app_log(a, 0, "Ignore masks: %zu/%d. Matching user PRIVMSG/NOTICE are hidden locally.",
                a->cfg.ignore_count, CONFIG_IGNORE_MAX);
        for (i = 0; i < a->cfg.ignore_count; ++i)
            app_log(a, 0, "  %s", a->cfg.ignores[i]);
        app_switch(a, 0);
        {
            char shown[512];
            const IrcDisplayField fields[] = {{"persistence", persistence}};
            if (irc_display_render(shown, sizeof shown,
                    "Ignore list shown in the server view.{persistence}", fields, 1))
                app_status(a, "%s", shown);
            else app_status(a, "Ignore list shown in the server view.%s", persistence);
        }
        return 1;
    }
    if (!remove && !strcmp(mask, "clear") && !*rest) {
        config_wipe(a->cfg.ignores, sizeof(a->cfg.ignores));
        a->cfg.ignore_count = 0;
        {
            char shown[512];
            const IrcDisplayField fields[] = {{"persistence", persistence}};
            if (irc_display_render(shown, sizeof shown,
                    "Ignore list cleared.{persistence}", fields, 1))
                app_status(a, "%s", shown);
            else app_status(a, "Ignore list cleared.%s", persistence);
        }
        return 1;
    }
    if (!remove && (!strcmp(mask, "add") || !strcmp(mask, "remove"))) {
        remove = !strcmp(mask, "remove");
        mask = next_word(&rest);
    }
    if (!*mask || *rest || !irc_valid_ignore_mask(mask)) {
        app_status(a, "%s", irc_tr("Usage: /ignore [list|clear|[add|remove] MASK], /unignore MASK; use a nickname or nick!user@host mask (127 bytes)."));
        return 0;
    }
    for (i = 0; i < a->cfg.ignore_count; ++i)
        if (ignore_equal(a, a->cfg.ignores[i], mask)) break;
    if (remove) {
        if (i == a->cfg.ignore_count) {
            app_status(a, "%s", irc_tr("That ignore mask is not in the list."));
            return 0;
        }
        if (i + 1 < a->cfg.ignore_count)
            memmove(a->cfg.ignores[i], a->cfg.ignores[i + 1],
                    (a->cfg.ignore_count - i - 1) * sizeof(a->cfg.ignores[0]));
        --a->cfg.ignore_count;
        memset(a->cfg.ignores[a->cfg.ignore_count], 0, sizeof(a->cfg.ignores[0]));
        {
            char shown[512];
            const IrcDisplayField fields[] = {{"persistence", persistence}};
            if (irc_display_render(shown, sizeof shown, "Ignore mask removed.{persistence}", fields, 1))
                app_status(a, "%s", shown);
            else app_status(a, "Ignore mask removed.%s", persistence);
        }
        return 1;
    }
    if (i < a->cfg.ignore_count) {
        app_status(a, "%s", irc_tr("That ignore mask is already present."));
        return 1;
    }
    if (a->cfg.ignore_count >= CONFIG_IGNORE_MAX) {
        {
        char shown[512], limit[32];
        (void)snprintf(limit, sizeof limit, "%d", CONFIG_IGNORE_MAX);
        const IrcDisplayField fields[] = {{"limit", limit}};
        if (irc_display_render(shown, sizeof shown, "Ignore limit is {limit} masks; remove one before adding another.", fields, 1))
            app_status(a, "%s", shown);
        else app_status(a, "Ignore limit is %d masks; remove one before adding another.", CONFIG_IGNORE_MAX);
    }
        return 0;
    }
    copy_text(a->cfg.ignores[a->cfg.ignore_count++], CONFIG_IGNORE_SIZE, mask);
    {
            char shown[512];
            const IrcDisplayField fields[] = {{"persistence", persistence}};
            if (irc_display_render(shown, sizeof shown, "Ignore mask added.{persistence}", fields, 1))
                app_status(a, "%s", shown);
            else app_status(a, "Ignore mask added.%s", persistence);
        }
    return 1;
}

static int join_command(App *a, char *rest)
{
    char *channel_name = next_word(&rest), *key = next_word(&rest);
    char previous[sizeof(a->cfg.channels)];
    size_t index, wire_limit = a->wire_limit ? a->wire_limit : 512u;
    if (!irc_valid_channel(channel_name) || *rest) {
        app_status(a, "%s", irc_tr("Usage: /join #channel [key] (one channel and one optional key)"));
        return 0;
    }
    if (*key && !safe_word(key)) {
        app_status(a, "%s", irc_tr("JOIN key must be one printable ASCII token without spaces, ':' or ','."));
        return 0;
    }
    if (!require_registered(a)) return 0;
    if (wire_limit > 512u) wire_limit = 512u;
    size_t overhead = strlen(channel_name) + 7u + (*key ? 1u : 0u);
    if (overhead > wire_limit || strlen(key) > wire_limit - overhead) {
        app_status(a, "%s", irc_tr("JOIN command exceeds the protocol byte limit; the key was not queued."));
        return 0;
    }
    index = app_buffer(a, channel_name, B_CHANNEL, 1);
    if (index == SIZE_MAX) return 0;
    copy_text(previous, sizeof(previous), session_channels(a));
    if (!channels_change(a, channel_name, 1)) {
        app_status(a, "%s", irc_tr("Configured channel list is full."));
        return 0;
    }
    int queued = *key ? app_outgoing(a, 0, "JOIN %s %s", channel_name, key) :
                        app_outgoing(a, 0, "JOIN %s", channel_name);
    if (!queued) {
        copy_text(a->test_mode ? a->test_channels : a->cfg.channels, sizeof(previous), previous);
        return 0;
    }
    a->buffers[index].background_join = 0;
    app_switch(a, index);
    app_status(a, "Joining %s%s.", channel_name, *key ? " with a supplied key" : "");
    return 1;
}

/* Return actions to the frontend instead of opening UI here. A send-plan
 * result still needs the frontend's review/acceptance path before queueing. */
int app_command(App *a, const char *text, SendPlan *plan)
{
    char command[APP_TEXT + 1], *rest, *name, *arg;
    Buffer *b = &a->buffers[a->current];
    size_t i, index;
    if (!text || !*text) return 1;
    if (strlen(text) > APP_TEXT || !irc_utf8_valid(text)) {
        app_status(a, "%s", irc_tr("Invalid or oversized input."));
        return 0;
    }
    for (i = 0; text[i]; ++i) {
        unsigned char c = (unsigned char)text[i];
        if ((c < 0x20 && c != '\n') || c == 0x7f) {
            app_status(a, "%s", irc_tr("Input cannot contain control characters except composed newlines."));
            return 0;
        }
    }
    if (text[0] != '/' || text[1] == '/') {
        if (b->kind != B_CHANNEL && b->kind != B_QUERY) {
            app_status(a, "%s", irc_tr("Choose a channel/query view or use /msg nick text."));
            return 0;
        }
        if (b->kind == B_CHANNEL && !b->joined) {
            {
            char translated[1024];
            const IrcDisplayField fields[] = {{"channel", b->name}};
            if (irc_display_render(translated, sizeof translated, "You are not in this channel; use /join {channel}.", fields, 1))
                app_status(a, "%s", translated);
            else app_status(a, "You are not in this channel; use /join %s.", b->name);
        }
            return 0;
        }
        if (bouncer_service(a, b->name))
            return bouncer_control_send(a, b->name, text[0] == '/' ? text + 1 : text);
        return app_plan(a, b->name, text[0] == '/' ? text + 1 : text, 0, plan) ? 2 : 0;
    }
    copy_text(command, sizeof(command), text + 1);
    rest = command;
    name = next_word(&rest);
    if (strchr(name, '\n')) {
        app_status(a, "%s", irc_tr("A command name cannot contain a newline."));
        config_wipe(command, sizeof(command));
        return 0;
    }
    for (i = 0; name[i]; ++i)
        if (name[i] >= 'A' && name[i] <= 'Z') name[i] = (char)(name[i] + 32);
    if (strcmp(name, "msg") && strcmp(name, "me") && strchr(rest, '\n')) {
        app_status(a, "%s", irc_tr("Only message text can contain composed newlines."));
        config_wipe(command, sizeof(command));
        return 0;
    }
    if (!strcmp(name, "znc") || !strcmp(name, "soju")) {
        int type = !strcmp(name, "znc") ? CONFIG_BOUNCER_ZNC : CONFIG_BOUNCER_SOJU;
        int sent = 0;
        if (a->cfg.bouncer_type != type)
            app_status(a, "%s", irc_tr("Select the matching ZNC or soju connection before using this command."));
        else
            sent = bouncer_control_send(a, type == CONFIG_BOUNCER_ZNC ? "*status" : "BouncerServ",
                                        *rest ? rest : "help");
        config_wipe(command, sizeof command);
        return sent;
    }
    if (!strcmp(name, "ignore") || !strcmp(name, "unignore"))
        return ignore_command(a, rest, !strcmp(name, "unignore"));
    if (!strcmp(name, "channels") || !strcmp(name, "cl")) {
        if (*rest) { app_status(a, "Usage: /%s", name); return 0; }
        return 3;
    }
    if (!strcmp(name, "ql") || !strcmp(name, "queries")) {
        if (*rest) { app_status(a, "Usage: /%s", name); return 0; }
        return 4;
    }
    if (!strcmp(name, "pm") || !strcmp(name, "query")) {
        if (*rest >= '0' && *rest <= '9') {
            char *end;
            errno = 0;
            unsigned long number = strtoul(rest, &end, 10);
            index = SIZE_MAX;
            if (!errno && !*end && number) {
                for (i = 3; i < a->buffer_count; ++i)
                    if (a->buffers[i].kind == B_QUERY && --number == 0) { index = i; break; }
            }
            if (index == SIZE_MAX) { app_status(a, "%s", irc_tr("No such query number; /ql lists queries starting at 1.")); return 0; }
        } else {
            if (!irc_valid_nick(rest)) { {
        char localized[1024];
        const IrcDisplayField fields[] = {{"command", name}};
        if (irc_display_render(localized, sizeof localized, "Usage: /{command} nickname|query-number", fields, 1))
            app_status(a, "%s", localized);
        else app_status(a, "Usage: /%s nickname|query-number", name);
    } return 0; }
            index = app_buffer(a, rest, B_QUERY, 1);
        }
        if (index == SIZE_MAX) return 0;
        app_switch(a, index);
        {
            char localized[1024];
            const IrcDisplayField fields[] = {{"nick", a->buffers[index].name}};
            if (irc_display_render(localized, sizeof localized, "Query with {nick}; type a message to send.", fields, 1))
                app_status(a, "%s", localized);
            else app_status(a, "Query with %s; type a message to send.", a->buffers[index].name);
        }
        return 1;
    }
    if (!strcmp(name, "channel")) {
        arg = next_word(&rest);
        if (!*arg || *rest) { app_status(a, "%s", irc_tr("Usage: /channel N|#name (0 is server)")); return 0; }
        index = named_or_numbered_view(a, arg, 1);
        if (index == SIZE_MAX) { app_status(a, "%s", irc_tr("No such channel; /channels lists channel numbers.")); return 0; }
        app_switch(a, index);
        return 1;
    }
    if (!strcmp(name, "close")) {
        arg = next_word(&rest);
        if (*rest) { app_status(a, "%s", irc_tr("Usage: /close [channel-number|view-name]")); return 0; }
        index = *arg ? named_or_numbered_view(a, arg, 0) : a->current;
        if (index == SIZE_MAX) { app_status(a, "%s", irc_tr("No such channel or query view.")); return 0; }
        return app_close_view(a, index);
    }
    if (!strcmp(name, "help") || !strcmp(name, "manual")) {
        if (*rest) { app_status(a, "Usage: /%s", name); return 0; }
        return 5;
    }
    if (!strcmp(name, "server")) { app_switch(a, 0); return 1; }
    if (!strcmp(name, "mentions")) { app_switch(a, 1); return 1; }
    if (!strcmp(name, "next")) { app_cycle(a, 1); return 1; }
    if (!strcmp(name, "prev")) { app_cycle(a, -1); return 1; }
    if (!strcmp(name, "users")) {
        if (!*rest || !strcmp(rest, "toggle")) a->cfg.user_list = !a->cfg.user_list;
        else if (!strcmp(rest, "on")) a->cfg.user_list = 1;
        else if (!strcmp(rest, "off")) a->cfg.user_list = 0;
        else { app_status(a, "%s", irc_tr("Usage: /users [on|off|toggle]")); return 0; }
        app_status(a, "User list %s; %s", a->cfg.user_list ? "on" : "off",
                   a->test_mode ? "test-session change only; /save is disabled." : "/save preserves this setting.");
        return 1;
    }
    if (!strcmp(name, "views")) {
        if (*rest) { app_status(a, "%s", irc_tr("Usage: /views (channel list)")); return 0; }
        return 3;
    }
    if (!strcmp(name, "view")) {
        char *end;
        unsigned long n;
        errno = 0;
        n = strtoul(rest, &end, 10);
        if (*rest && !*end && !errno) {
            index = channel_number(a, n);
            if (index != SIZE_MAX) {
                app_switch(a, index);
                return 1;
            }
        }
        for (i = 0; i < a->buffer_count; ++i)
            if (irc_equal(a->buffers[i].name, rest, a->casemap)) {
                if (a->buffers[i].kind == B_HELP) return 5;
                app_switch(a, i);
                return 1;
            }
        app_status(a, "%s", irc_tr("No such view; /channels and /ql list conversations."));
        return 0;
    }
    if (!strcmp(name, "clear")) { app_clear_buffer(a, a->current); return 1; }
    if (!strcmp(name, "connect")) return app_connect(a);
    if (!strcmp(name, "disconnect")) { app_disconnect(a, "Disconnected by user"); return 1; }
    if (!strcmp(name, "cancel")) {
        OutLine *p, *next;
        size_t automatic = app_autocommands_cancel(a);
        size_t cancelled = a->chat_count;
        for (p = a->chat_head; p; p = next) {
            next = p->next;
            outline_free(p);
        }
        a->chat_head = a->chat_tail = NULL;
        a->chat_count = 0;
        app_status(a, "Cancelled %zu queued messages and %zu automatic commands.%s", cancelled, automatic,
                   a->inflight && (a->inflight->chat || a->inflight->autocommand) ?
                   " One in-flight message will finish." : "");
        return 1;
    }
    if (!strcmp(name, "quit")) return app_request_quit(a, rest);
    if (!strcmp(name, "save")) {
        char error[512];
        Config effective;
        if (a->test_mode) {
            app_status(a, "%s", irc_tr("OFFLINE TEST MODE: /save is disabled; configuration was not written."));
            return 0;
        }
        app_config_effective(a, &effective);
        int saved = save_configuration(a, &effective, error, sizeof(error));
        config_wipe(&effective, sizeof effective);
        if (saved < 0) {
            app_status(a, "%s", error);
            return 0;
        }
        {
            char localized[1024];
            const IrcDisplayField fields[] = {{"path", a->config_path}};
            if (irc_display_render(localized, sizeof localized, "Configuration saved to {path}", fields, 1))
                app_status(a, "%s", localized);
            else app_status(a, "Configuration saved to %s", a->config_path);
        }
        return 1;
    }
    if (!strcmp(name, "set")) {
        arg = next_word(&rest);
        if (!strcmp(arg, "autoconnect") && (!strcmp(rest, "on") || !strcmp(rest, "off"))) {
            a->cfg.autoconnect = !strcmp(rest, "on");
            app_status(a, "Autoconnect %s; %s", rest,
                       a->test_mode ? "test-session setting only; simulator remains offline." : "/save preserves this setting.");
            return 1;
        }
        app_status(a, "%s", irc_tr("Usage: /set autoconnect on|off"));
        return 0;
    }
    if (!strcmp(name, "watch")) {
        arg = next_word(&rest);
        if (!strcmp(arg, "list") || !*arg) {
            app_log(a, 0, "Nick mentions always match %s; %zu additional literal strings:", a->nick, a->cfg.word_count);
            for (i = 0; i < a->cfg.word_count; ++i)
                app_log(a, 0, "  %s", a->cfg.words[i]);
            app_switch(a, 0);
            return 1;
        }
        if (!*rest || strlen(rest) >= CONFIG_WORD_SIZE || strchr(rest, '\n')) {
            app_status(a, "%s", irc_tr("Watch text must contain 1 to 127 UTF-8 bytes."));
            return 0;
        }
        for (i = 0; i < a->cfg.word_count; ++i)
            if (irc_equal(a->cfg.words[i], rest, a->casemap)) break;
        if (!strcmp(arg, "add")) {
            if (i < a->cfg.word_count) { app_status(a, "%s", irc_tr("That watch string already exists.")); return 1; }
            if (a->cfg.word_count == CONFIG_WORD_MAX) { app_status(a, "%s", irc_tr("Watch limit is 32 strings.")); return 0; }
            copy_text(a->cfg.words[a->cfg.word_count++], CONFIG_WORD_SIZE, rest);
            app_status(a, "Watch string added; %s",
                       a->test_mode ? "test-session change only; /save is disabled." : "/save preserves it.");
            return 1;
        }
        if (!strcmp(arg, "remove")) {
            if (i == a->cfg.word_count) { app_status(a, "%s", irc_tr("That watch string was not found.")); return 0; }
            if (i + 1 < a->cfg.word_count)
                memmove(a->cfg.words[i], a->cfg.words[i + 1],
                        (a->cfg.word_count - i - 1) * CONFIG_WORD_SIZE);
            --a->cfg.word_count;
            a->cfg.words[a->cfg.word_count][0] = 0;
            app_status(a, "Watch string removed; %s",
                       a->test_mode ? "test-session change only; /save is disabled." : "/save preserves it.");
            return 1;
        }
        app_status(a, "%s", irc_tr("Usage: /watch add|remove text, or /watch list"));
        return 0;
    }
    if (!strcmp(name, "nick")) {
        if (!irc_valid_nick(rest)) { app_status(a, "%s", irc_tr("Invalid nickname (maximum 63 ASCII bytes).")); return 0; }
        if (!io_has(a)) {
            copy_text(a->nick, sizeof(a->nick), rest);
            copy_text(a->cfg.nick, sizeof(a->cfg.nick), rest);
            {
            char localized[1024];
            const IrcDisplayField fields[] = {{"nick", rest}};
            if (irc_display_render(localized, sizeof localized, "Nickname set to {nick}; /connect to use it.", fields, 1))
                app_status(a, "%s", localized);
            else app_status(a, "Nickname set to %s; /connect to use it.", rest);
        }
            return 1;
        }
        return app_outgoing(a, 0, "NICK %s", rest);
    }
    if (!strcmp(name, "join")) {
        int result = join_command(a, rest);
        config_wipe(command, sizeof(command));
        return result;
    }
    if (!strcmp(name, "list")) {
        return list_command(a, rest);
    }
    if (!strcmp(name, "away") || !strcmp(name, "back")) {
        if (!strcmp(name, "back") && *rest) {
            app_status(a, "%s", irc_tr("Usage: /back (or /away with no message)"));
            return 0;
        }
        if (!require_registered(a)) return 0;
        if (!(*rest ? app_outgoing(a, 0, "AWAY :%s", rest) : app_outgoing(a, 0, "AWAY")))
            return 0;
        a->automatic_away = APP_AUTO_AWAY_NONE;
        a->manual_away_pending = *rest != 0;
        a->autocommand_presence = NULL;
        a->automatic_away_clear_pending = 0;
        a->automatic_away_retry_at = 0;
        app_status(a, "%s", *rest ? irc_tr("Setting away status; waiting for server confirmation.") :
                   irc_tr("Clearing away status; waiting for server confirmation."));
        return 1;
    }
    if ((!strcmp(name, "part") ||
         !strcmp(name, "msg") || !strcmp(name, "me") ||
         !strcmp(name, "whois") || !strcmp(name, "topic")) &&
        !require_registered(a)) return 0;
    if (!strcmp(name, "part")) {
        char arguments[APP_TEXT + 1], *tail, *first;
        copy_text(arguments, sizeof arguments, rest);
        tail = arguments;
        first = next_word(&tail);
        const char *channel = b->kind == B_CHANNEL ? b->name : "";
        const char *message = a->cfg.part_message;
        if (*first && irc_valid_channel(first)) {
            channel = first;
            if (*tail) message = tail;
        } else if (*rest) message = rest;
        if (!irc_valid_channel(channel)) {
            config_wipe(arguments, sizeof arguments);
            app_status(a, "%s", irc_tr("Usage: /part [#channel] [message] (select a channel when omitting #channel)"));
            return 0;
        }
        if (!presence_text_valid(message)) {
            config_wipe(arguments, sizeof arguments);
            app_status(a, "%s", irc_tr("PART message must be valid UTF-8 without control characters."));
            return 0;
        }
        size_t cancelled;
        int inflight;
        int queued = *message ? app_outgoing(a, 0, "PART %s :%s", channel, message) :
                                app_outgoing(a, 0, "PART %s", channel);
        if (!queued) {
            config_wipe(arguments, sizeof arguments);
            return 0;
        }
        (void)autocommands_cancel_target(a, channel);
        (void)channels_change(a, channel, 0);
        (void)drop_target_queue(a, 0, "JOIN", channel);
        cancelled = drop_target_queue(a, 1, "PRIVMSG", channel);
        inflight = a->inflight && a->inflight->chat &&
                   outline_target(a, a->inflight, "PRIVMSG", channel);
        app_status(a, "Leaving %s; history retained; %zu queued messages cancelled.%s", channel,
                   cancelled, inflight ? " One in-flight message will finish." : "");
        config_wipe(arguments, sizeof arguments);
        return 1;
    }
    if (!strcmp(name, "msg")) {
        arg = next_word(&rest);
        if (bouncer_service(a, arg)) {
            int sent = bouncer_control_send(a, arg, rest);
            config_wipe(command, sizeof command);
            return sent;
        }
        if (!irc_valid_target(arg) || !*rest) { app_status(a, "%s", irc_tr("Usage: /msg nick-or-channel text")); return 0; }
        index = app_buffer(a, arg, irc_valid_channel(arg) ? B_CHANNEL : B_QUERY, 1);
        if (index == SIZE_MAX) return 0;
        return app_plan(a, arg, rest, 0, plan) ? 2 : 0;
    }
    if (!strcmp(name, "me")) {
        if (b->kind != B_CHANNEL && b->kind != B_QUERY) { app_status(a, "%s", irc_tr("Choose a channel/query for /me.")); return 0; }
        if (b->kind == B_CHANNEL && !b->joined) { app_status(a, "%s", irc_tr("Join this channel first.")); return 0; }
        return app_plan(a, b->name, rest, 1, plan) ? 2 : 0;
    }
    if (!strcmp(name, "whois")) {
        if (!irc_valid_nick(rest)) { app_status(a, "%s", irc_tr("Usage: /whois nick")); return 0; }
        if (!app_outgoing(a, 0, "WHOIS %s", rest)) return 0;
        app_switch(a, 0);
        return 1;
    }
    if (!strcmp(name, "topic")) {
        if (b->kind != B_CHANNEL || !b->joined) { app_status(a, "%s", irc_tr("Select a joined channel first.")); return 0; }
        return *rest ? app_outgoing(a, 0, "TOPIC %s :%s", b->name, rest) :
                       app_outgoing(a, 0, "TOPIC %s", b->name);
    }
    {
            char translated[1024];
            const IrcDisplayField fields[] = {{"command", name}};
            if (irc_display_render(translated, sizeof translated, "Unknown command /{command}; /help lists supported commands.", fields, 1))
                app_status(a, "%s", translated);
            else app_status(a, "Unknown command /%s; /help lists supported commands.", name);
        }
    return 0;
}
