#define _POSIX_C_SOURCE 200809L
#include "filepicker.h"

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void join(char *out, size_t cap, const char *dir, const char *name)
{
    int n = snprintf(out, cap, "%s/%s", dir, name);
    assert(n > 0 && (size_t)n < cap);
}

static void make_file(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0);
    assert(write(fd, "sample", 6) == 6);
    assert(close(fd) == 0);
}

static void finish(FilePicker *p)
{
    size_t steps = 0;
    while (filepicker_loading(p)) {
        assert(!filepicker_count(p));
        (void)filepicker_step(p);
        assert(++steps <= FILEPICKER_MAX_SCANNED / FILEPICKER_SCAN_BATCH + 1u);
    }
}

static size_t named(const FilePicker *p, const char *name)
{
    for (size_t i = 0; i < filepicker_count(p); ++i)
        if (!strcmp(filepicker_at(p, i)->name, name)) return i;
    return SIZE_MAX;
}

static size_t fd_count(void)
{
    size_t n = 0;
    for (int fd = 0; fd < 256; ++fd) if (fcntl(fd, F_GETFD) >= 0) ++n;
    return n;
}

static void clear_directory(const char *path)
{
    DIR *dir = opendir(path);
    assert(dir);
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char full[4096];
        join(full, sizeof full, path, entry->d_name);
        struct stat st;
        assert(lstat(full, &st) == 0);
        if (S_ISDIR(st.st_mode)) clear_directory(full);
        else assert(unlink(full) == 0);
    }
    assert(closedir(dir) == 0);
    assert(rmdir(path) == 0);
}

static void browsing(void)
{
    char root[] = "/tmp/vipirc-picker-XXXXXX";
    assert(mkdtemp(root));
    char path[4096], chosen[4096], original[4096], after[4096];
    assert(getcwd(original, sizeof original));
    join(path, sizeof path, root, "folder"); assert(mkdir(path, 0700) == 0);
    join(path, sizeof path, root, ".cache"); assert(mkdir(path, 0700) == 0);
    const char *files[] = {"alpha.txt", "caf\xc3\xa9.txt", "file with spaces.bin", "literal$(not-a-command).txt"};
    for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
        join(path, sizeof path, root, files[i]); make_file(path);
    }
    join(path, sizeof path, root, ".hidden"); make_file(path);
    join(path, sizeof path, root, "quote\".txt"); make_file(path);
    join(path, sizeof path, root, "line\nname"); make_file(path);
    join(path, sizeof path, root, "bidi\xe2\x80\xae.txt"); make_file(path);
    join(path, sizeof path, root, "fifo"); assert(mkfifo(path, 0600) == 0);
    join(path, sizeof path, root, "file-link"); assert(symlink("alpha.txt", path) == 0);
    join(path, sizeof path, root, "dir-link"); assert(symlink("folder", path) == 0);
    join(path, sizeof path, root, "oversized");
    int large = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(large >= 0 && ftruncate(large, (off_t)UINT32_MAX + 1) == 0 && close(large) == 0);

    size_t before = fd_count();
    FilePicker *p = filepicker_create(0);
    assert(p && filepicker_open(p, root));
    assert(filepicker_loading(p) && !filepicker_count(p));
    finish(p);
    assert(filepicker_count(p) == 7 && filepicker_skipped(p) == 8 && !filepicker_limited(p));
    assert(!strcmp(filepicker_at(p, 0)->name, ".."));
    assert(!strcmp(filepicker_at(p, 1)->name, ".cache"));
    assert(!strcmp(filepicker_at(p, 2)->name, "folder"));
    assert(named(p, "file-link") == SIZE_MAX && named(p, "fifo") == SIZE_MAX && named(p, ".hidden") == SIZE_MAX);
    for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
        assert(filepicker_choose(p, named(p, files[i]), chosen, sizeof chosen) == 1);
        join(path, sizeof path, root, files[i]); assert(!strcmp(chosen, path));
    }
    assert(filepicker_choose(p, named(p, "alpha.txt"), chosen, 2) == 0 && !*chosen);
    join(path, sizeof path, root, "dir-link");
    assert(!filepicker_open(p, path) && !strcmp(filepicker_path(p), root));
    join(path, sizeof path, root, "../"); assert(!filepicker_open(p, path));
    assert(!strcmp(filepicker_path(p), root));

    assert(filepicker_choose(p, named(p, "folder"), chosen, sizeof chosen) == 2);
    finish(p);
    assert(filepicker_count(p) == 1 && !strcmp(filepicker_at(p, 0)->name, ".."));
    assert(filepicker_choose(p, 0, chosen, sizeof chosen) == 2);
    finish(p); assert(!strcmp(filepicker_path(p), root));
    join(path, sizeof path, root, "alpha.txt");
    char moved[4096]; join(moved, sizeof moved, root, "old-alpha");
    assert(rename(path, moved) == 0); make_file(path);
    assert(!filepicker_choose(p, named(p, "alpha.txt"), chosen, sizeof chosen));
    assert(strstr(filepicker_error(p), "changed"));
    assert(unlink(path) == 0 && symlink("old-alpha", path) == 0);
    assert(!filepicker_choose(p, named(p, "alpha.txt"), chosen, sizeof chosen));
    filepicker_free(p);
    assert(fd_count() == before);
    assert(getcwd(after, sizeof after) && !strcmp(original, after));
    clear_directory(root);
}

static void bounds_and_cancel(void)
{
    char root[] = "/tmp/vipirc-picker-bound-XXXXXX", path[4096];
    assert(mkdtemp(root));
    size_t before = fd_count();
    for (size_t i = 0; i < FILEPICKER_MAX_ENTRIES + 20u; ++i) {
        int n = snprintf(path, sizeof path, "%s/file-%04zu", root, i);
        assert(n > 0 && (size_t)n < sizeof path); make_file(path);
    }
    FilePicker *p = filepicker_create(0);
    assert(p && filepicker_open(p, root)); finish(p);
    assert(filepicker_limited(p) && filepicker_count(p) == FILEPICKER_MAX_ENTRIES);
    for (size_t i = 0; i < 20; ++i) {
        assert(filepicker_open(p, root));
        (void)filepicker_step(p);
    }
    filepicker_free(p);
    assert(fd_count() == before);
    clear_directory(root);

    char ignored[] = "/tmp/vipirc-picker-scan-XXXXXX";
    assert(mkdtemp(ignored));
    for (size_t i = 0; i < FILEPICKER_MAX_SCANNED + 8u; ++i) {
        int n = snprintf(path, sizeof path, "%s/.ignored-%04zu", ignored, i);
        assert(n > 0 && (size_t)n < sizeof path); make_file(path);
    }
    p = filepicker_create(0);
    assert(p && filepicker_open(p, ignored)); finish(p);
    assert(filepicker_limited(p) && filepicker_count(p) == 1);
    assert(filepicker_skipped(p) >= FILEPICKER_MAX_SCANNED - 2u);
    filepicker_free(p);
    assert(fd_count() == before);
    clear_directory(ignored);
}

static void initial_directory_and_offline(void)
{
    char root[] = "/tmp/vipirc-picker-cwd-XXXXXX", child[4096], path[4096];
    assert(mkdtemp(root)); join(child, sizeof child, root, "child"); assert(mkdir(child, 0700) == 0);
    int saved = open(".", O_RDONLY | O_DIRECTORY);
    assert(saved >= 0 && chdir(root) == 0);
    FilePicker *p = filepicker_create(0);
    assert(p && filepicker_open(p, NULL)); finish(p);
    assert(!strcmp(filepicker_path(p), root));
    assert(chdir(child) == 0 && filepicker_open(p, NULL)); finish(p);
    assert(!strcmp(filepicker_path(p), root));
    assert(getcwd(path, sizeof path) && !strcmp(path, child));
    filepicker_free(p);
    p = filepicker_create(1);
    assert(p && !filepicker_open(p, NULL) && strstr(filepicker_error(p), "OFFLINE TEST MODE"));
    assert(!filepicker_open(p, "/must-not-read-offline-picker"));
    assert(!filepicker_loading(p) && !filepicker_count(p) && !filepicker_step(p));
    assert(!filepicker_choose(p, 0, path, sizeof path));
    filepicker_free(p);
    assert(fchdir(saved) == 0 && close(saved) == 0);
    clear_directory(root);
}

static void renamed_directory(void)
{
    char root[] = "/tmp/vipirc-picker-directory-XXXXXX";
    char shown[4096], moved[4096], path[4096], chosen[4096];
    assert(mkdtemp(root));
    join(shown, sizeof shown, root, "shown");
    join(moved, sizeof moved, root, "original-directory");
    assert(mkdir(shown, 0700) == 0);
    join(path, sizeof path, shown, "selected.txt"); make_file(path);
    FilePicker *p = filepicker_create(0);
    assert(p && filepicker_open(p, shown)); finish(p);
    size_t selected = named(p, "selected.txt");
    assert(selected != SIZE_MAX);
    assert(rename(shown, moved) == 0 && mkdir(shown, 0700) == 0);
    join(path, sizeof path, shown, "selected.txt"); make_file(path);
    /* The cached directory still contains the original inode. Its displayed
     * path now names a different directory and must not select that file. */
    assert(!filepicker_choose(p, selected, chosen, sizeof chosen) && !*chosen);
    assert(strstr(filepicker_error(p), "changed"));
    filepicker_free(p);
    clear_directory(root);
}

int main(void)
{
    browsing(); bounds_and_cancel(); initial_directory_and_offline(); renamed_directory();
    puts("file picker: incremental bounded scans, ordering, parent navigation, literal paths, unchanged cwd, unsafe-entry and changed-inode refusal, offline guards and descriptor cleanup passed");
    return 0;
}
