/* Real private-file publication with deterministic competing creators at the
 * last syscall boundary. All objects belong to this isolated fixture. No
 * timing race, real config, secret, account, or network is involved. */
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int publication_link(int, const char *, int, const char *, int);
static int publication_rename(int, const char *, int, const char *);
static int publication_unlink(int, const char *, int);
static int publication_sync(int);
#define linkat publication_link
#define renameat publication_rename
#define unlinkat publication_unlink
#define fsync publication_sync
#include "config.c"
#undef linkat
#undef renameat
#undef unlinkat
#undef fsync

enum CompetingObject { NO_OBJECT, REGULAR_OBJECT, SYMLINK_OBJECT, FIFO_OBJECT };
static enum CompetingObject race;
static struct stat competitor;
static unsigned races, publication_calls;
static int fail_link, fail_cleanup, fail_sync;
static const char original[] = "another process created this file\n";
static const char target_data[] = "untouched symlink target\n";

static void write_at(int directory, const char *name, const char *data)
{
    int fd = openat(directory, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    assert(fd >= 0 && write(fd, data, strlen(data)) == (ssize_t)strlen(data));
    assert(!close(fd));
}

static void competing_object(int directory, const char *leaf, enum CompetingObject kind)
{
    if (kind == REGULAR_OBJECT) write_at(directory, leaf, original);
    else if (kind == SYMLINK_OBJECT) assert(!symlinkat("target", directory, leaf));
    else if (kind == FIFO_OBJECT) assert(!mkfifoat(directory, leaf, 0600));
    else abort();
    assert(!fstatat(directory, leaf, &competitor, AT_SYMLINK_NOFOLLOW));
}

static void compete(int directory, const char *leaf)
{
    ++publication_calls;
    if (race) {
        competing_object(directory, leaf, race);
        race = NO_OBJECT;
        ++races;
    }
}

static int publication_link(int from, const char *old, int to, const char *name, int flags)
{
    compete(to, name);
    if (fail_link) { errno = EIO; return -1; }
    return linkat(from, old, to, name, flags);
}

static int publication_rename(int from, const char *old, int to, const char *name)
{
    compete(to, name);
    return renameat(from, old, to, name);
}

static int publication_unlink(int directory, const char *name, int flags)
{
    if (fail_cleanup && !strncmp(name, ".vipirc-", 8)) { errno = EIO; return -1; }
    return unlinkat(directory, name, flags);
}

static int publication_sync(int fd)
{
    struct stat st;
    assert(!fstat(fd, &st));
    if (fail_sync && S_ISDIR(st.st_mode)) { errno = EIO; return -1; }
    return fsync(fd);
}

static void read_equals(int directory, const char *name, const char *expected)
{
    char bytes[128];
    int fd = openat(directory, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    assert(fd >= 0);
    ssize_t length = read(fd, bytes, sizeof bytes);
    assert(length == (ssize_t)strlen(expected) && !memcmp(bytes, expected, (size_t)length));
    assert(!close(fd));
}

static unsigned temporary_files(const char *directory, int remove_files)
{
    DIR *entries = opendir(directory);
    assert(entries);
    unsigned count = 0;
    struct dirent *entry;
    while ((entry = readdir(entries)) != NULL) {
        if (strncmp(entry->d_name, ".vipirc-", 8)) continue;
        ++count;
        if (remove_files) assert(!unlinkat(dirfd(entries), entry->d_name, 0));
    }
    assert(!closedir(entries));
    return count;
}

static void valid_created(const char *path, const Config *expected)
{
    struct stat st;
    assert(!lstat(path, &st) && S_ISREG(st.st_mode) && (st.st_mode & 07777) == 0600);
    Config loaded;
    config_defaults(&loaded);
    if (expected->minimal) config_minimal_defaults(&loaded);
    char error[512];
    assert(config_load(&loaded, path, error, sizeof error) == 1);
    assert(!memcmp(&loaded, expected, sizeof loaded));
    config_wipe(&loaded, sizeof loaded);
}

int main(void)
{
    char directory[] = "/tmp/irc-config-create-XXXXXX", path[512], error[512];
    assert(mkdtemp(directory));
    (void)umask(0077);
    int dir = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(dir >= 0);
    assert(snprintf(path, sizeof path, "%s/config.ini", directory) > 0);
    write_at(dir, "target", target_data);
    Config config;
    for (int minimal = 0; minimal < 2; ++minimal) {
        if (minimal) config_minimal_defaults(&config);
        else config_defaults(&config);
        assert(config_hotkey_set(&config, config_hotkey_key("F7"), CONFIG_ACTION_SUMMARY,
                                 error, sizeof error) == 1);
        assert(config_create(&config, path, error, sizeof error) == 1);
        valid_created(path, &config);
        assert(!temporary_files(directory, 0));
        assert(!unlinkat(dir, "config.ini", 0));

        for (int concurrent = 0; concurrent < 2; ++concurrent) {
            for (enum CompetingObject kind = REGULAR_OBJECT; kind <= FIFO_OBJECT; ++kind) {
                races = publication_calls = 0;
                if (concurrent) race = kind;
                else competing_object(dir, "config.ini", kind);
                assert(config_create(&config, path, error, sizeof error) == -1);
                assert(strstr(error, "already exists") && strstr(error, "not overwritten"));
                assert(races == (unsigned)concurrent && publication_calls == (unsigned)concurrent);
                struct stat after;
                assert(!fstatat(dir, "config.ini", &after, AT_SYMLINK_NOFOLLOW));
                assert(after.st_dev == competitor.st_dev && after.st_ino == competitor.st_ino &&
                       after.st_mode == competitor.st_mode && after.st_size == competitor.st_size);
                if (kind == REGULAR_OBJECT) read_equals(dir, "config.ini", original);
                read_equals(dir, "target", target_data);
                assert(!temporary_files(directory, 0));
                assert(!unlinkat(dir, "config.ini", 0));
            }
        }
    }
    puts("config create: full/minimal roundtrip and 12 existing/raced regular/symlink/FIFO cases passed");

    fail_link = 1;
    assert(config_create(&config, path, error, sizeof error) == -1);
    assert(faccessat(dir, "config.ini", F_OK, 0) < 0 && errno == ENOENT);
    assert(!temporary_files(directory, 0));
    fail_link = 0;

    fail_sync = 1;
    assert(config_create(&config, path, error, sizeof error) == -1);
    assert(strstr(error, "was created") && strstr(error, "synchronization failed"));
    valid_created(path, &config);
    assert(!temporary_files(directory, 0));
    fail_sync = 0;
    assert(!unlinkat(dir, "config.ini", 0));

    fail_cleanup = 1;
    assert(config_create(&config, path, error, sizeof error) == -1);
    assert(strstr(error, "was created") && strstr(error, "cleanup failed"));
    valid_created(path, &config);
    assert(temporary_files(directory, 0) == 1);
    fail_cleanup = 0;
    assert(temporary_files(directory, 1) == 1);
    assert(!unlinkat(dir, "config.ini", 0));
    puts("config create: failed publication cleans temporary data; post-publication failures retain reported completed file");

    write_at(dir, "config.ini", original);
    assert(config_save(&config, path, error, sizeof error) == 1);
    valid_created(path, &config);
    assert(!temporary_files(directory, 0));
    assert(!unlinkat(dir, "config.ini", 0));
    puts("config create: ordinary explicit Save still replaces an existing owned file");

    config.port = 0;
    publication_calls = 0;
    assert(config_create(&config, path, error, sizeof error) == -1 && !publication_calls);
    assert(faccessat(dir, "config.ini", F_OK, 0) < 0 && errno == ENOENT);
    assert(!temporary_files(directory, 0));
    assert(!unlinkat(dir, "target", 0) && !close(dir) && !rmdir(directory));
    config_wipe(&config, sizeof config);
    puts("config create: invalid configuration causes no publication");
    return 0;
}
