/* Original bounded implementation of the documented classic DCC SEND wire
 * protocol. No client code, automatic acceptance, shell or executable data. */
#define _POSIX_C_SOURCE 200809L
#include "dcc.h"
#include "protocol.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DCC_BLOCK 16384u
#define DCC_BATCH 4u
#define DCC_IDLE_MS 120000
#define DCC_OFFER_MS 300000

typedef struct DccEntry {
    DccInfo info;
    int64_t created;
} DccEntry;

typedef struct DccTransfer {
    int used, fd, file_fd, dir_fd, created_file;
    size_t entry, length, offset, ack_length;
    int64_t last_progress;
    dev_t device;
    ino_t inode;
    char leaf[DCC_NAME_MAX + 1];
    unsigned char buffer[DCC_BLOCK], ack[4];
} DccTransfer;

struct DccManager {
    int offline, dirty, progress;
    uint64_t next_id;
    int64_t next_report;
    DccEntry entries[DCC_MAX_ITEMS];
    DccTransfer transfers[DCC_MAX_ACTIVE];
};

static int problem(char *error, size_t cap, const char *format, ...)
{
    va_list ap;
    if (error && cap) {
        va_start(ap, format);
        (void)vsnprintf(error, cap, format, ap);
        va_end(ap);
    }
    return 0;
}

static int64_t now_ms(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) < 0) return 0;
    return (int64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

static void wipe(void *memory, size_t count)
{
    volatile unsigned char *p = memory;
    while (count--) *p++ = 0;
}

static int safe_name(const char *name)
{
    char safe[DCC_NAME_MAX + 1];
    size_t length;
    if (!name || !(length = strlen(name)) || length > DCC_NAME_MAX ||
        name[0] == '.' || name[0] == ' ' || name[length - 1] == ' ' ||
        strchr(name, '/') || strchr(name, '\\') || strchr(name, '"') ||
        !irc_utf8_valid(name)) return 0;
    irc_sanitize(safe, sizeof(safe), name);
    return !strcmp(safe, name);
}

static int safe_path(const char *path)
{
    char safe[DCC_PATH_MAX];
    if (!path || !*path || strlen(path) >= sizeof(safe) || !irc_utf8_valid(path)) return 0;
    irc_sanitize(safe, sizeof(safe), path);
    return !strcmp(safe, path);
}

static int decimal(const char **cursor, uint32_t limit, uint32_t *value)
{
    const char *p = *cursor;
    uint32_t n = 0;
    if (*p < '0' || *p > '9') return 0;
    do {
        unsigned digit = (unsigned)(*p++ - '0');
        if (n > limit / 10u || (n == limit / 10u && digit > limit % 10u)) return 0;
        n = n * 10u + digit;
    } while (*p >= '0' && *p <= '9');
    if (*p && *p != ' ') return 0;
    while (*p == ' ') ++p;
    *cursor = p;
    *value = n;
    return 1;
}

static int unicast(uint32_t address)
{
    return address && (address >> 24) != 0 && (address >> 24) < 224u;
}

int dcc_parse_send(const char *body, DccInfo *out, char *error, size_t cap)
{
    DccInfo parsed;
    const char *p, *start;
    uint32_t address, port;
    size_t length;
    struct in_addr ipv4;
    if (!body || !out || strlen(body) > IRC_BODY_MAX || strncmp(body, "DCC SEND ", 9))
        return problem(error, cap, "Only active DCC SEND with filename, IPv4, port and size is supported.");
    memset(&parsed, 0, sizeof(parsed));
    p = body + 9;
    while (*p == ' ') ++p;
    if (*p == '"') {
        start = ++p;
        while (*p && *p != '"') ++p;
        if (*p != '"') return problem(error, cap, "DCC filename has an unmatched quote.");
        length = (size_t)(p - start);
        ++p;
        if (*p != ' ') return problem(error, cap, "DCC filename must be followed by an address.");
    } else {
        start = p;
        while (*p && *p != ' ') ++p;
        length = (size_t)(p - start);
    }
    if (!length || length > DCC_NAME_MAX)
        return problem(error, cap, "DCC filename is empty or too long.");
    memcpy(parsed.filename, start, length);
    parsed.filename[length] = 0;
    if (!safe_name(parsed.filename))
        return problem(error, cap, "DCC filename must be a visible safe basename without paths or controls.");
    while (*p == ' ') ++p;
    if (!decimal(&p, UINT32_MAX, &address) || !unicast(address) ||
        !decimal(&p, UINT16_MAX, &port) || !port ||
        !decimal(&p, UINT32_MAX, &parsed.size) || *p)
        return problem(error, cap, "DCC requires a decimal IPv4 address, port 1..65535 and size 0..4294967295; extensions are unsupported.");
    ipv4.s_addr = htonl(address);
    if (!inet_ntop(AF_INET, &ipv4, parsed.address, sizeof(parsed.address)))
        return problem(error, cap, "Cannot display the DCC IPv4 address.");
    parsed.port = (uint16_t)port;
    parsed.state = DCC_OFFERED;
    *out = parsed;
    if (error && cap) error[0] = 0;
    return 1;
}

static int terminal(DccState state)
{
    return state == DCC_DONE || state == DCC_REJECTED ||
           state == DCC_CANCELLED || state == DCC_FAILED;
}

static DccEntry *find_entry(DccManager *manager, uint64_t id)
{
    if (!manager || !id) return NULL;
    for (size_t i = 0; i < DCC_MAX_ITEMS; ++i)
        if (manager->entries[i].info.id == id) return &manager->entries[i];
    return NULL;
}

static DccEntry *available_entry(DccManager *manager)
{
    DccEntry *oldest = NULL;
    for (size_t i = 0; i < DCC_MAX_ITEMS; ++i) {
        DccEntry *entry = &manager->entries[i];
        if (!entry->info.id) return entry;
        if (terminal(entry->info.state) && (!oldest || entry->info.id < oldest->info.id)) oldest = entry;
    }
    return oldest;
}

static DccTransfer *available_transfer(DccManager *manager)
{
    for (size_t i = 0; i < DCC_MAX_ACTIVE; ++i)
        if (!manager->transfers[i].used) return &manager->transfers[i];
    return NULL;
}

static void reset_transfer(DccTransfer *transfer)
{
    wipe(transfer, sizeof(*transfer));
    transfer->fd = transfer->file_fd = transfer->dir_fd = -1;
}

/* A receive owns the descriptor and the exact inode it created, not any
 * later file at the same pathname. Keep the directory pinned and compare
 * identity before removing an incomplete receive; never unlink a replacement. */
static void cleanup_transfer(DccTransfer *transfer, int complete)
{
    if (transfer->fd >= 0) close(transfer->fd);
    if (!complete && transfer->created_file && transfer->dir_fd >= 0) {
        struct stat current;
        if (fstatat(transfer->dir_fd, transfer->leaf, &current, AT_SYMLINK_NOFOLLOW) == 0 &&
            S_ISREG(current.st_mode) && current.st_dev == transfer->device && current.st_ino == transfer->inode)
            (void)unlinkat(transfer->dir_fd, transfer->leaf, 0);
    }
    if (transfer->file_fd >= 0) close(transfer->file_fd);
    if (transfer->dir_fd >= 0) close(transfer->dir_fd);
    reset_transfer(transfer);
}

static void finish(DccManager *manager, DccTransfer *transfer, DccState state, const char *reason)
{
    DccInfo *info = &manager->entries[transfer->entry].info;
    info->state = state;
    if (reason) irc_sanitize(info->error, sizeof(info->error), reason);
    else info->error[0] = 0;
    cleanup_transfer(transfer, state == DCC_DONE);
    manager->dirty = 1;
}

DccManager *dcc_create(int offline)
{
    DccManager *manager = calloc(1, sizeof(*manager));
    if (!manager) return NULL;
    manager->offline = !!offline;
    manager->next_id = 1;
    for (size_t i = 0; i < DCC_MAX_ACTIVE; ++i) reset_transfer(&manager->transfers[i]);
    return manager;
}

void dcc_free(DccManager *manager)
{
    if (!manager) return;
    for (size_t i = 0; i < DCC_MAX_ACTIVE; ++i)
        if (manager->transfers[i].used) cleanup_transfer(&manager->transfers[i], 0);
    wipe(manager, sizeof(*manager));
    free(manager);
}

int dcc_offer(DccManager *manager, uint64_t server_token, const char *nick,
               const char *body, uint64_t *id, char *error, size_t cap)
{
    DccInfo info;
    DccEntry *entry;
    size_t offered = 0;
    if (id) *id = 0;
    if (!manager || !irc_valid_nick(nick)) return problem(error, cap, "Invalid DCC sender.");
    if (!dcc_parse_send(body, &info, error, cap)) return 0;
    for (size_t i = 0; i < DCC_MAX_ITEMS; ++i) {
        const DccInfo *old = &manager->entries[i].info;
        if (old->state != DCC_OFFERED) continue;
        ++offered;
        if (old->server_token == server_token && !strcmp(old->nick, nick) &&
            !strcmp(old->filename, info.filename) && !strcmp(old->address, info.address) &&
            old->port == info.port && old->size == info.size) {
            if (id) *id = old->id;
            return 1;
        }
    }
    if (offered >= DCC_MAX_OFFERS || !(entry = available_entry(manager)) || !manager->next_id)
        return problem(error, cap, "DCC offer limit reached; reject an offer before accepting more.");
    memset(entry, 0, sizeof(*entry));
    entry->info = info;
    entry->info.id = manager->next_id++;
    entry->info.server_token = server_token;
    (void)snprintf(entry->info.nick, sizeof(entry->info.nick), "%s", nick);
    entry->created = now_ms();
    manager->dirty = 1;
    if (id) *id = entry->info.id;
    return 1;
}

/* Walk stable directory descriptors. Only Accept requests creation; neither
 * incoming offers nor source-file selection create directories. */
static int directory_open(const char *path, int create, int private_final,
                           char *error, size_t cap)
{
    char parts[DCC_PATH_MAX], *p, *end;
    int fd, next;
    struct stat st;
    if (!safe_path(path)) {
        problem(error, cap, "DCC directory must be bounded UTF-8 without terminal controls."); return -1;
    }
    (void)snprintf(parts, sizeof(parts), "%s", path);
    fd = open(path[0] == '/' ? "/" : ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) { problem(error, cap, "Cannot open the DCC directory base: %s", strerror(errno)); return -1; }
    p = parts;
    while (*p) {
        while (*p == '/') ++p;
        if (!*p) break;
        end = strchr(p, '/');
        if (end) *end = 0;
        if (!strcmp(p, "..")) {
            close(fd); problem(error, cap, "DCC paths cannot contain '..' components."); return -1;
        }
        if (strcmp(p, ".")) {
            next = openat(fd, p, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0 && errno == ENOENT && create) {
                if (mkdirat(fd, p, 0700) == 0 || errno == EEXIST)
                    next = openat(fd, p, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            }
            if (next < 0) {
                int saved = errno;
                close(fd); problem(error, cap, "Cannot open DCC directory (symlinks refused): %s", strerror(saved)); return -1;
            }
            close(fd); fd = next;
        }
        if (!end) break;
        p = end + 1;
    }
    if (fstat(fd, &st) < 0 || !S_ISDIR(st.st_mode) ||
        (private_final && (st.st_uid != getuid() || (st.st_mode & 077)))) {
        close(fd); problem(error, cap, "DCC receive directory must be owned by you with no group/other permissions."); return -1;
    }
    return fd;
}

static int nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) return 0;
#ifdef SO_NOSIGPIPE
    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) < 0) return 0;
#endif
    return 1;
}

static int new_socket(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0 && !nonblocking(fd)) { close(fd); return -1; }
    return fd;
}

int dcc_accept(DccManager *manager, uint64_t id, const char *directory,
                const char *filename, char *error, size_t cap)
{
    DccEntry *entry;
    DccTransfer *transfer, prepared;
    struct sockaddr_in peer;
    struct stat st;
    const char *name;
    char path[DCC_PATH_MAX];
    int result;
    if (!manager || manager->offline) return problem(error, cap, "OFFLINE TEST MODE: DCC receive is disabled.");
    entry = find_entry(manager, id);
    if (!entry || entry->info.state != DCC_OFFERED) return problem(error, cap, "Select a pending DCC offer to accept.");
    if (!(transfer = available_transfer(manager))) return problem(error, cap, "DCC active-transfer limit is 4; finish or cancel one first.");
    name = filename && *filename ? filename : entry->info.filename;
    if (!safe_name(name)) return problem(error, cap, "Choose a visible safe filename without paths or controls.");
    if (!directory || !*directory) return problem(error, cap, "Choose a private receive directory.");
    result = snprintf(path, sizeof(path), "%s%s%s", directory, directory[strlen(directory) - 1] == '/' ? "" : "/", name);
    if (result < 0 || (size_t)result >= sizeof(path)) return problem(error, cap, "DCC destination path is too long.");
    reset_transfer(&prepared);
    prepared.dir_fd = directory_open(directory, 1, 1, error, cap);
    if (prepared.dir_fd < 0) return 0;
    prepared.file_fd = openat(prepared.dir_fd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    if (prepared.file_fd < 0) {
        int saved = errno;
        cleanup_transfer(&prepared, 0);
        return problem(error, cap, "Cannot create a new DCC file (no overwrite): %s", strerror(saved));
    }
    if (fstat(prepared.file_fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid()) {
        cleanup_transfer(&prepared, 0); return problem(error, cap, "DCC destination is not a new regular file.");
    }
    prepared.created_file = 1; prepared.device = st.st_dev; prepared.inode = st.st_ino;
    (void)snprintf(prepared.leaf, sizeof(prepared.leaf), "%s", name);
    if (fchmod(prepared.file_fd, 0600) < 0) {
        cleanup_transfer(&prepared, 0); return problem(error, cap, "Cannot secure DCC destination permissions.");
    }
    memset(&peer, 0, sizeof(peer));
    peer.sin_family = AF_INET; peer.sin_port = htons(entry->info.port);
    (void)inet_pton(AF_INET, entry->info.address, &peer.sin_addr);
    prepared.fd = new_socket();
    if (prepared.fd < 0) {
        int saved = errno;
        cleanup_transfer(&prepared, 0); return problem(error, cap, "Cannot open DCC connection: %s", strerror(saved));
    }
    result = connect(prepared.fd, (struct sockaddr *)&peer, sizeof(peer));
    if (result < 0 && errno != EINPROGRESS) {
        int saved = errno;
        cleanup_transfer(&prepared, 0); return problem(error, cap, "Cannot connect to offered DCC endpoint: %s", strerror(saved));
    }
    prepared.used = 1; prepared.entry = (size_t)(entry - manager->entries);
    prepared.last_progress = now_ms();
    *transfer = prepared;
    entry->info.state = result == 0 ? DCC_RECEIVING : DCC_CONNECTING;
    (void)snprintf(entry->info.path, sizeof(entry->info.path), "%s", path);
    entry->info.error[0] = 0;
    manager->dirty = 1;
    if (error && cap) error[0] = 0;
    return 1;
}

int dcc_send(DccManager *manager, uint64_t server_token, const char *nick,
              const char *source, const char *advertised_ipv4, uint16_t listen_port,
              uint64_t *id, char *body, size_t body_cap, char *error, size_t cap)
{
    char parent[DCC_PATH_MAX], leaf[DCC_NAME_MAX + 1], message[IRC_BODY_MAX + 1];
    const char *slash, *name;
    struct sockaddr_in address;
    socklen_t address_length = sizeof(address);
    struct in_addr advertised;
    struct stat st, selected;
    DccEntry *entry;
    DccTransfer *transfer, prepared;
    int dir, result, reuse = 1;
    if (id) *id = 0;
    if (body && body_cap) body[0] = 0;
    if (!manager || manager->offline) return problem(error, cap, "OFFLINE TEST MODE: DCC send is disabled.");
    if (!irc_valid_nick(nick) || !safe_path(source) ||
        !advertised_ipv4 || inet_pton(AF_INET, advertised_ipv4, &advertised) != 1 || !unicast(ntohl(advertised.s_addr)))
        return problem(error, cap, "DCC send requires a nickname, source path and explicit unicast IPv4 address.");
    if (!(transfer = available_transfer(manager))) return problem(error, cap, "DCC active-transfer limit is 4; finish or cancel one first.");
    if (!(entry = available_entry(manager)) || !manager->next_id) return problem(error, cap, "DCC transfer list is full.");
    slash = strrchr(source, '/'); name = slash ? slash + 1 : source;
    if (!safe_name(name)) return problem(error, cap, "DCC source needs a visible safe basename without quotes or controls.");
    (void)snprintf(leaf, sizeof(leaf), "%s", name);
    if (!slash) (void)snprintf(parent, sizeof(parent), ".");
    else if (slash == source) (void)snprintf(parent, sizeof(parent), "/");
    else { size_t length = (size_t)(slash - source); memcpy(parent, source, length); parent[length] = 0; }
    dir = directory_open(parent, 0, 0, error, cap);
    if (dir < 0) return 0;
    if (fstatat(dir, leaf, &selected, AT_SYMLINK_NOFOLLOW) < 0 || !S_ISREG(selected.st_mode) ||
        selected.st_size < 0 || (uintmax_t)selected.st_size > UINT32_MAX) {
        close(dir); return problem(error, cap, "DCC source must be a regular file no larger than 4294967295 bytes.");
    }
    reset_transfer(&prepared);
    prepared.file_fd = openat(dir, leaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    close(dir);
    if (prepared.file_fd < 0) return problem(error, cap, "Cannot open DCC source (symlinks refused): %s", strerror(errno));
    if (fstat(prepared.file_fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (uintmax_t)st.st_size > UINT32_MAX) {
        cleanup_transfer(&prepared, 0); return problem(error, cap, "DCC source must be a regular file no larger than 4294967295 bytes.");
    }
    if (st.st_dev != selected.st_dev || st.st_ino != selected.st_ino) {
        cleanup_transfer(&prepared, 0); return problem(error, cap, "DCC source changed before opening. Select the file again.");
    }
    prepared.fd = new_socket();
    if (prepared.fd < 0) {
        int saved = errno; cleanup_transfer(&prepared, 0); return problem(error, cap, "Cannot create DCC listener: %s", strerror(saved));
    }
    (void)setsockopt(prepared.fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_ANY); address.sin_port = htons(listen_port);
    if (bind(prepared.fd, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(prepared.fd, 1) < 0 ||
        getsockname(prepared.fd, (struct sockaddr *)&address, &address_length) < 0) {
        int saved = errno; cleanup_transfer(&prepared, 0); return problem(error, cap, "Cannot bind/listen on the DCC port: %s", strerror(saved));
    }
    result = snprintf(message, sizeof(message), strchr(leaf, ' ') ? "DCC SEND \"%s\" %" PRIu32 " %u %" PRIu32 :
                                                                "DCC SEND %s %" PRIu32 " %u %" PRIu32,
                      leaf, ntohl(advertised.s_addr), (unsigned)ntohs(address.sin_port), (uint32_t)st.st_size);
    if (result < 0 || (size_t)result >= sizeof(message) || !body || (size_t)result >= body_cap) {
        cleanup_transfer(&prepared, 0); return problem(error, cap, "DCC offer does not fit the command buffer.");
    }
    memset(entry, 0, sizeof(*entry));
    entry->info.id = manager->next_id++;
    entry->info.server_token = server_token;
    entry->info.outgoing = 1; entry->info.state = DCC_LISTENING;
    entry->info.size = (uint32_t)st.st_size; entry->info.port = ntohs(address.sin_port);
    (void)snprintf(entry->info.nick, sizeof(entry->info.nick), "%s", nick);
    (void)snprintf(entry->info.filename, sizeof(entry->info.filename), "%s", leaf);
    (void)inet_ntop(AF_INET, &advertised, entry->info.address, sizeof(entry->info.address));
    (void)snprintf(entry->info.path, sizeof(entry->info.path), "%s", source);
    entry->created = now_ms();
    prepared.used = 1; prepared.entry = (size_t)(entry - manager->entries); prepared.last_progress = entry->created;
    *transfer = prepared;
    memcpy(body, message, (size_t)result + 1);
    manager->dirty = 1;
    if (id) *id = entry->info.id;
    if (error && cap) error[0] = 0;
    return 1;
}

int dcc_reject(DccManager *manager, uint64_t id, char *error, size_t cap)
{
    DccEntry *entry = find_entry(manager, id);
    if (!entry || entry->info.state != DCC_OFFERED) return problem(error, cap, "Select a pending offer to reject.");
    entry->info.state = DCC_REJECTED; manager->dirty = 1;
    if (error && cap) error[0] = 0;
    return 1;
}

int dcc_cancel(DccManager *manager, uint64_t id, char *error, size_t cap)
{
    DccEntry *entry = find_entry(manager, id);
    if (!entry || terminal(entry->info.state)) return problem(error, cap, "That DCC item is no longer active.");
    for (size_t i = 0; i < DCC_MAX_ACTIVE; ++i)
        if (manager->transfers[i].used && manager->entries[manager->transfers[i].entry].info.id == id) {
            finish(manager, &manager->transfers[i], DCC_CANCELLED, "Cancelled by user.");
            if (error && cap) error[0] = 0;
            return 1;
        }
    entry->info.state = DCC_CANCELLED; manager->dirty = 1;
    if (error && cap) error[0] = 0;
    return 1;
}

void dcc_cancel_server(DccManager *manager, uint64_t token)
{
    if (!manager) return;
    for (size_t i = 0; i < DCC_MAX_ITEMS; ++i)
        if (manager->entries[i].info.id && manager->entries[i].info.server_token == token &&
            !terminal(manager->entries[i].info.state))
            (void)dcc_cancel(manager, manager->entries[i].info.id, NULL, 0);
}

size_t dcc_count(const DccManager *manager)
{
    size_t count = 0;
    if (manager) for (size_t i = 0; i < DCC_MAX_ITEMS; ++i) if (manager->entries[i].info.id) ++count;
    return count;
}

const DccInfo *dcc_at(const DccManager *manager, size_t index)
{
    if (manager) for (size_t i = 0; i < DCC_MAX_ITEMS; ++i)
        if (manager->entries[i].info.id && !index--) return &manager->entries[i].info;
    return NULL;
}

const DccInfo *dcc_get(const DccManager *manager, uint64_t id)
{
    if (manager && id) for (size_t i = 0; i < DCC_MAX_ITEMS; ++i)
        if (manager->entries[i].info.id == id) return &manager->entries[i].info;
    return NULL;
}

const char *dcc_state_name(DccState state)
{
    switch (state) {
    case DCC_OFFERED: return "offered";
    case DCC_LISTENING: return "waiting for peer";
    case DCC_CONNECTING: return "connecting";
    case DCC_SENDING: return "sending";
    case DCC_RECEIVING: return "receiving";
    case DCC_DONE: return "complete";
    case DCC_REJECTED: return "rejected";
    case DCC_CANCELLED: return "cancelled";
    case DCC_FAILED: return "failed";
    case DCC_EMPTY: return "empty";
    }
    return "unknown";
}

size_t dcc_pollfds(const DccManager *manager, struct pollfd *fds, uint64_t *ids, size_t cap)
{
    size_t count = 0;
    if (!manager || !fds || !ids) return 0;
    for (size_t i = 0; i < DCC_MAX_ACTIVE && count < cap; ++i) {
        const DccTransfer *transfer = &manager->transfers[i];
        const DccInfo *info;
        short events = POLLIN;
        if (!transfer->used) continue;
        info = &manager->entries[transfer->entry].info;
        if (info->state == DCC_CONNECTING) events = POLLOUT;
        else if (info->state == DCC_SENDING &&
                 (transfer->offset < transfer->length ||
                  (info->acknowledged == info->transferred && info->transferred < info->size)))
            events |= POLLOUT;
        else if (info->state == DCC_RECEIVING &&
                 (transfer->ack_length || !info->size || info->acknowledged < info->transferred))
            /* receive_step flushes the acknowledgement before reading more.
             * Input readiness cannot make progress while that write blocks. */
            events = POLLOUT;
        fds[count].fd = transfer->fd; fds[count].events = events; fds[count].revents = 0;
        ids[count++] = info->id;
    }
    return count;
}

static ssize_t socket_write(int fd, const void *bytes, size_t size)
{
#ifdef MSG_NOSIGNAL
    return send(fd, bytes, size, MSG_NOSIGNAL);
#else
    return send(fd, bytes, size, 0);
#endif
}

static int would_block(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

static int write_file(int fd, const unsigned char *bytes, size_t count)
{
    size_t offset = 0;
    while (offset < count) {
        ssize_t n = write(fd, bytes + offset, count - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        offset += (size_t)n;
    }
    return 1;
}

static void receive_step(DccManager *manager, DccTransfer *transfer, short events, int64_t now)
{
    DccInfo *info = &manager->entries[transfer->entry].info;
    for (size_t batch = 0; batch < DCC_BATCH; ++batch) {
        if (transfer->ack_length || info->acknowledged < info->transferred || !info->size) {
            if (!transfer->ack_length) {
                uint32_t value = htonl(info->transferred);
                memcpy(transfer->ack, &value, sizeof(value));
                transfer->ack_length = 4; transfer->offset = 0;
            }
            ssize_t n = socket_write(transfer->fd, transfer->ack + transfer->offset, transfer->ack_length - transfer->offset);
            if (n < 0 && would_block()) return;
            if (n <= 0) { finish(manager, transfer, DCC_FAILED, "DCC acknowledgement could not be sent."); return; }
            transfer->offset += (size_t)n;
            if (transfer->offset != transfer->ack_length) return;
            transfer->ack_length = transfer->offset = 0;
            info->acknowledged = info->transferred;
            if (info->transferred == info->size) {
                if (close(transfer->file_fd) < 0) {
                    transfer->file_fd = -1;
                    finish(manager, transfer, DCC_FAILED, "DCC destination close failed."); return;
                }
                transfer->file_fd = -1;
                finish(manager, transfer, DCC_DONE, NULL); return;
            }
        }
        if (!(events & (POLLIN | POLLHUP))) return;
        uint32_t remaining = info->size - info->transferred;
        size_t capacity = remaining < DCC_BLOCK ? (size_t)remaining + 1u : DCC_BLOCK;
        ssize_t n = recv(transfer->fd, transfer->buffer, capacity, 0);
        if (n < 0 && would_block()) return;
        if (n <= 0) { finish(manager, transfer, DCC_FAILED, "DCC peer closed before the advertised file size arrived."); return; }
        if ((uint64_t)n > remaining) { finish(manager, transfer, DCC_FAILED, "DCC peer sent more than the advertised size."); return; }
        if (!write_file(transfer->file_fd, transfer->buffer, (size_t)n)) {
            finish(manager, transfer, DCC_FAILED, "DCC destination write failed."); return;
        }
        info->transferred += (uint32_t)n;
        transfer->last_progress = now; manager->progress = 1;
    }
}

/* The accepted file-size limit is UINT32_MAX, so cumulative ACKs never
 * wrap. Decode all four network-order bytes before comparing unsigned values;
 * an ACK may repeat, but cannot go backward or acknowledge unsent data. */
static int read_acknowledgements(DccManager *manager, DccTransfer *transfer, int64_t now)
{
    DccInfo *info = &manager->entries[transfer->entry].info;
    for (size_t batch = 0; batch < 16; ++batch) {
        ssize_t n = recv(transfer->fd, transfer->ack + transfer->ack_length, 4 - transfer->ack_length, 0);
        if (n < 0 && would_block()) return 1;
        if (n <= 0) {
            finish(manager, transfer, DCC_FAILED, "DCC peer closed before acknowledging the complete file."); return 0;
        }
        transfer->ack_length += (size_t)n;
        if (transfer->ack_length == 4) {
            uint32_t acknowledged;
            memcpy(&acknowledged, transfer->ack, 4); acknowledged = ntohl(acknowledged);
            transfer->ack_length = 0;
            if (acknowledged < info->acknowledged || acknowledged > info->transferred) {
                finish(manager, transfer, DCC_FAILED, "DCC peer sent an invalid byte acknowledgement."); return 0;
            }
            if (acknowledged > info->acknowledged) transfer->last_progress = now;
            info->acknowledged = acknowledged; manager->progress = 1;
            if (acknowledged == info->size && info->transferred == info->size) {
                finish(manager, transfer, DCC_DONE, NULL); return 0;
            }
        }
    }
    return 1;
}

static void send_step(DccManager *manager, DccTransfer *transfer, short events, int64_t now)
{
    DccInfo *info = &manager->entries[transfer->entry].info;
    if ((events & (POLLIN | POLLHUP)) && !read_acknowledgements(manager, transfer, now)) return;
    if (!(events & POLLOUT)) return;
    for (size_t batch = 0; batch < DCC_BATCH; ++batch) {
        if (transfer->offset == transfer->length) {
            if (info->acknowledged != info->transferred || info->transferred == info->size) return;
            size_t count = info->size - info->transferred;
            if (count > sizeof(transfer->buffer)) count = sizeof(transfer->buffer);
            ssize_t n = read(transfer->file_fd, transfer->buffer, count);
            if (n < 0 && errno == EINTR) return;
            if (n <= 0) { finish(manager, transfer, DCC_FAILED, "DCC source ended or became unreadable before its advertised size."); return; }
            transfer->length = (size_t)n; transfer->offset = 0;
        }
        ssize_t n = socket_write(transfer->fd, transfer->buffer + transfer->offset, transfer->length - transfer->offset);
        if (n < 0 && would_block()) return;
        if (n <= 0) { finish(manager, transfer, DCC_FAILED, "DCC source could not be sent to the peer."); return; }
        transfer->offset += (size_t)n; info->transferred += (uint32_t)n;
        transfer->last_progress = now; manager->progress = 1;
    }
}

void dcc_process(DccManager *manager, const struct pollfd *fds, const uint64_t *ids,
                  size_t count, int64_t now)
{
    if (!manager) return;
    if (count > DCC_MAX_ACTIVE) count = DCC_MAX_ACTIVE;
    for (size_t i = 0; i < DCC_MAX_ACTIVE; ++i) {
        DccTransfer *transfer = &manager->transfers[i];
        DccInfo *info;
        short events = 0;
        if (!transfer->used) continue;
        info = &manager->entries[transfer->entry].info;
        if (fds && ids) for (size_t j = 0; j < count; ++j)
            if (ids[j] == info->id && fds[j].fd == transfer->fd) { events = fds[j].revents; break; }
        if (events & (POLLERR | POLLNVAL)) {
            finish(manager, transfer, DCC_FAILED, "DCC socket failed."); continue;
        }
        if (info->state == DCC_LISTENING && (events & (POLLIN | POLLHUP))) {
            int connected = accept(transfer->fd, NULL, NULL);
            if (connected >= 0) {
                if (!nonblocking(connected)) { close(connected); finish(manager, transfer, DCC_FAILED, "Cannot configure DCC peer socket."); continue; }
                close(transfer->fd); transfer->fd = connected;
                info->state = DCC_SENDING; transfer->last_progress = now;
                manager->dirty = 1; events = 0;
            } else if (!would_block()) { finish(manager, transfer, DCC_FAILED, "Cannot accept DCC peer connection."); continue; }
        } else if (info->state == DCC_CONNECTING && (events & (POLLOUT | POLLHUP))) {
            int result = 0;
            socklen_t length = sizeof(result);
            if (getsockopt(transfer->fd, SOL_SOCKET, SO_ERROR, &result, &length) < 0 || result) {
                finish(manager, transfer, DCC_FAILED, "Cannot connect to the offered DCC endpoint."); continue;
            }
            info->state = DCC_RECEIVING; transfer->last_progress = now; manager->dirty = 1;
        }
        if (info->state == DCC_RECEIVING) receive_step(manager, transfer, events, now);
        else if (info->state == DCC_SENDING) send_step(manager, transfer, events, now);
        if (transfer->used && now - transfer->last_progress >= DCC_IDLE_MS)
            finish(manager, transfer, DCC_FAILED, "DCC transfer timed out without progress.");
    }
    for (size_t i = 0; i < DCC_MAX_ITEMS; ++i) {
        DccEntry *entry = &manager->entries[i];
        if (entry->info.state == DCC_OFFERED && now - entry->created >= DCC_OFFER_MS) {
            entry->info.state = DCC_FAILED;
            (void)snprintf(entry->info.error, sizeof(entry->info.error), "DCC offer expired without acceptance.");
            manager->dirty = 1;
        }
    }
}

int dcc_timeout(const DccManager *manager, int64_t now)
{
    int64_t remaining = 1000, next;
    if (!manager) return 1000;
    for (size_t i = 0; i < DCC_MAX_ACTIVE; ++i)
        if (manager->transfers[i].used) {
            next = manager->transfers[i].last_progress + DCC_IDLE_MS - now;
            if (next < remaining) remaining = next;
        }
    for (size_t i = 0; i < DCC_MAX_ITEMS; ++i)
        if (manager->entries[i].info.state == DCC_OFFERED) {
            next = manager->entries[i].created + DCC_OFFER_MS - now;
            if (next < remaining) remaining = next;
        }
    if (manager->progress && manager->next_report - now < remaining) remaining = manager->next_report - now;
    return remaining < 0 ? 0 : (int)remaining;
}

int dcc_changed(DccManager *manager)
{
    int64_t now;
    if (!manager) return 0;
    now = now_ms();
    if (manager->dirty || (manager->progress && now >= manager->next_report)) {
        manager->dirty = manager->progress = 0;
        manager->next_report = now + 100;
        return 1;
    }
    return 0;
}
