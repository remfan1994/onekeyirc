/* Bounded, descriptor-relative file selection. No cwd changes or file reads.
 * Opening stages a directory and scan before replacing the previous view.
 * filepicker_step scans a bounded batch; callers display entries after the
 * scan finishes. Choosing rechecks both directory identity and listed inode.
 * This returns a path, not a trusted open file: dcc_send validates it again. */
#include "filepicker.h"
#include "protocol.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct ListedFile {
    FilePickerEntry item;
    dev_t device;
    ino_t inode;
} ListedFile;

struct FilePicker {
    int offline, directory_fd, limited;
    DIR *scan;
    size_t count, scanned, skipped;
    char launch[FILEPICKER_PATH_MAX], path[FILEPICKER_PATH_MAX];
    char error[256];
    ListedFile entries[FILEPICKER_MAX_ENTRIES];
};

static int fail(FilePicker *p, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    (void)vsnprintf(p->error, sizeof p->error, format, ap);
    va_end(ap);
    return 0;
}

static int safe_text(const char *text, size_t limit)
{
    char clean[FILEPICKER_PATH_MAX];
    if (!text || !*text || strlen(text) >= limit || !irc_utf8_valid(text)) return 0;
    irc_sanitize(clean, sizeof clean, text);
    return !strcmp(clean, text);
}

static int sendable_name(const char *name)
{
    size_t length = strlen(name);
    return safe_text(name, FILEPICKER_NAME_MAX + 1u) && name[0] != '.' &&
        name[0] != ' ' && name[length - 1u] != ' ' &&
        !strchr(name, '/') && !strchr(name, '\\') && !strchr(name, '"');
}

static int open_directory(FilePicker *p, const char *path)
{
    char pieces[FILEPICKER_PATH_MAX];
    if (!safe_text(path, sizeof pieces) || path[0] != '/') {
        fail(p, "Choose an absolute directory path without terminal controls.");
        return -1;
    }
    memcpy(pieces, path, strlen(path) + 1u);
    int fd = open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        fail(p, "Cannot open directory: %s", strerror(errno));
        return -1;
    }
    char *at = pieces;
    while (*at) {
        while (*at == '/') ++at;
        if (!*at) break;
        char *end = strchr(at, '/');
        if (end) *end = 0;
        if (!strcmp(at, "..")) {
            close(fd);
            fail(p, "Use the parent-directory entry instead of '..' in a path.");
            return -1;
        }
        if (strcmp(at, ".")) {
            int next = openat(fd, at, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0) {
                int saved = errno;
                close(fd);
                fail(p, "Cannot open directory (symlinks refused): %s", strerror(saved));
                return -1;
            }
            close(fd);
            fd = next;
        }
        if (!end) break;
        at = end + 1;
    }
    return fd;
}

static void stop_scan(FilePicker *p)
{
    if (p->scan) { (void)closedir(p->scan); p->scan = NULL; }
}

FilePicker *filepicker_create(int offline)
{
    FilePicker *p = calloc(1, sizeof *p);
    if (p) { p->offline = offline; p->directory_fd = -1; }
    return p;
}

void filepicker_free(FilePicker *p)
{
    if (!p) return;
    stop_scan(p);
    if (p->directory_fd >= 0) close(p->directory_fd);
    free(p);
}

/* Opening a directory stages a replacement scan
 * before discarding the prior browser state. Failure leaves a usable
 * selection context instead of an empty partially opened view. */
int filepicker_open(FilePicker *p, const char *directory)
{
    char path[FILEPICKER_PATH_MAX];
    if (!p) return 0;
    if (p->offline) return fail(p, "OFFLINE TEST MODE: file browsing is disabled; Type path can edit a sample.");
    if (!directory) {
        if (!*p->launch && !getcwd(p->launch, sizeof p->launch))
            return fail(p, "Cannot read the launch directory: %s. Use Type path.", strerror(errno));
        directory = p->launch;
    }
    if (!safe_text(directory, sizeof path)) return fail(p, "Directory path is too long or contains unsupported text.");
    memcpy(path, directory, strlen(directory) + 1u);
    /* Normalize slash and '.' components without following symlinks. */
    if (*path != '/') return fail(p, "Choose an absolute directory path.");
    char normalized[FILEPICKER_PATH_MAX];
    size_t used = 1;
    normalized[0] = '/'; normalized[1] = 0;
    for (char *at = path; *at;) {
        while (*at == '/') ++at;
        if (!*at) break;
        char *end = strchr(at, '/');
        if (end) *end = 0;
        if (!strcmp(at, "..")) return fail(p, "Use the parent-directory entry instead of '..' in a path.");
        if (strcmp(at, ".")) {
            size_t length = strlen(at);
            if (used > 1u) normalized[used++] = '/';
            memcpy(normalized + used, at, length + 1u); used += length;
        }
        if (!end) break;
        at = end + 1;
    }
    int fd = open_directory(p, normalized);
    if (fd < 0) return 0;
    int scan_fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (scan_fd < 0) {
        int saved = errno;
        close(fd);
        return fail(p, "Cannot read directory: %s", strerror(saved));
    }
    DIR *scan = fdopendir(scan_fd);
    if (!scan) {
        int saved = errno;
        close(scan_fd);
        close(fd);
        return fail(p, "Cannot read directory: %s", strerror(saved));
    }
    stop_scan(p);
    if (p->directory_fd >= 0) close(p->directory_fd);
    p->directory_fd = fd;
    p->scan = scan;
    p->count = p->scanned = p->skipped = 0;
    p->limited = 0;
    p->error[0] = 0;
    memcpy(p->path, normalized, used + 1u);
    if (used > 1u) {
        ListedFile *up = &p->entries[p->count++];
        memset(up, 0, sizeof *up);
        memcpy(up->item.name, "..", 3); up->item.directory = 1;
    }
    return 1;
}

static int compare(const void *left, const void *right)
{
    const ListedFile *a = left, *b = right;
    if (!strcmp(a->item.name, "..")) return !strcmp(b->item.name, "..") ? 0 : -1;
    if (!strcmp(b->item.name, "..")) return 1;
    if (a->item.directory != b->item.directory) return a->item.directory ? -1 : 1;
    return strcmp(a->item.name, b->item.name);
}

/* Process a bounded number of directory entries
 * per UI tick. The incomplete scan is not a stable list for selection;
 * large directories are intentionally truncated at fixed limits. */
int filepicker_step(FilePicker *p)
{
    if (!p || !p->scan) return 0;
    for (size_t n = 0; n < FILEPICKER_SCAN_BATCH; ++n) {
        if (p->count >= FILEPICKER_MAX_ENTRIES || p->scanned >= FILEPICKER_MAX_SCANNED) {
            p->limited = 1;
            stop_scan(p);
            break;
        }
        errno = 0;
        struct dirent *entry = readdir(p->scan);
        if (!entry) {
            int saved = errno;
            stop_scan(p);
            if (saved) fail(p, "Directory listing stopped: %s", strerror(saved));
            break;
        }
        ++p->scanned;
        const char *name = entry->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        struct stat st;
        if (!safe_text(name, FILEPICKER_NAME_MAX + 1u) || strchr(name, '/') ||
            fstatat(p->directory_fd, name, &st, AT_SYMLINK_NOFOLLOW) < 0 ||
            (!S_ISDIR(st.st_mode) && (!S_ISREG(st.st_mode) || !sendable_name(name) ||
             st.st_size < 0 || (uintmax_t)st.st_size > UINT32_MAX))) {
            ++p->skipped;
            continue;
        }
        ListedFile *listed = &p->entries[p->count++];
        memset(listed, 0, sizeof *listed);
        memcpy(listed->item.name, name, strlen(name) + 1u);
        listed->item.directory = S_ISDIR(st.st_mode);
        listed->item.size = S_ISREG(st.st_mode) ? (uint32_t)st.st_size : 0;
        listed->device = st.st_dev;
        listed->inode = st.st_ino;
    }
    if (!p->scan) qsort(p->entries, p->count, sizeof p->entries[0], compare);
    return p->scan ? 0 : 1;
}

int filepicker_loading(const FilePicker *p) { return p && p->scan != NULL; }
int filepicker_limited(const FilePicker *p) { return p && p->limited; }
size_t filepicker_skipped(const FilePicker *p) { return p ? p->skipped : 0; }
size_t filepicker_count(const FilePicker *p) { return p && !p->scan ? p->count : 0; }
const FilePickerEntry *filepicker_at(const FilePicker *p, size_t i)
{
    return i < filepicker_count(p) ? &p->entries[i].item : NULL;
}
const char *filepicker_path(const FilePicker *p) { return p ? p->path : ""; }
const char *filepicker_error(const FilePicker *p) { return p ? p->error : "File browser unavailable."; }

int filepicker_parent(FilePicker *p)
{
    if (!p || !*p->path) return 0;
    char parent[FILEPICKER_PATH_MAX];
    memcpy(parent, p->path, strlen(p->path) + 1u);
    char *slash = strrchr(parent, '/');
    if (!slash || slash == parent) parent[1] = 0;
    else *slash = 0;
    return filepicker_open(p, parent);
}

/* Revalidate the chosen inode against the saved
 * directory descriptor. The returned path is only a suggestion to the
 * transfer manager, which must reopen and validate it independently. */
int filepicker_choose(FilePicker *p, size_t index, char *path, size_t cap)
{
    if (path && cap) *path = 0;
    if (!p || !path || !cap) return 0;
    if (p->offline) return fail(p, "OFFLINE TEST MODE: file selection is disabled.");
    const FilePickerEntry *entry = filepicker_at(p, index);
    if (!entry) return fail(p, "Select a file or directory after the listing finishes.");
    if (!strcmp(entry->name, "..")) return filepicker_parent(p) ? 2 : 0;
    struct stat st, original_directory, current_directory;
    const ListedFile *listed = &p->entries[index];
    /* A stable descriptor alone is insufficient when the result is a path:
     * a renamed directory may still contain the cached inode while that path
     * now names a different tree. Check the path again before returning it. */
    int current = open_directory(p, p->path);
    if (current < 0) return 0;
    int unchanged = fstat(p->directory_fd, &original_directory) == 0 &&
        fstat(current, &current_directory) == 0 &&
        original_directory.st_dev == current_directory.st_dev &&
        original_directory.st_ino == current_directory.st_ino;
    if (!unchanged) {
        close(current);
        return fail(p, "That directory changed. Reopen it before selecting a file.");
    }
    int found = fstatat(current, entry->name, &st, AT_SYMLINK_NOFOLLOW) == 0;
    close(current);
    if (!found ||
        st.st_dev != listed->device || st.st_ino != listed->inode ||
        (entry->directory ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)) ||
        (!entry->directory && (st.st_size < 0 || (uintmax_t)st.st_size > UINT32_MAX)))
        return fail(p, "That entry changed. Reopen its directory before selecting it.");
    char full[FILEPICKER_PATH_MAX];
    int n = snprintf(full, sizeof full, "%s%s%s", p->path, !strcmp(p->path, "/") ? "" : "/", entry->name);
    if (n < 0 || (size_t)n >= sizeof full) return fail(p, "The selected path is too long. Use Type path.");
    if (entry->directory) return filepicker_open(p, full) ? 2 : 0;
    if ((size_t)n >= cap) return fail(p, "The selected path does not fit the source field.");
    memcpy(path, full, (size_t)n + 1u);
    p->error[0] = 0;
    return 1;
}
