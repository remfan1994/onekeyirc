#ifndef VIPIRC_FILEPICKER_H
#define VIPIRC_FILEPICKER_H

#include <stddef.h>
#include <stdint.h>

#define FILEPICKER_PATH_MAX 4096u
#define FILEPICKER_NAME_MAX 255u
#define FILEPICKER_MAX_ENTRIES 512u
#define FILEPICKER_MAX_SCANNED 4096u
#define FILEPICKER_SCAN_BATCH 64u

typedef struct FilePicker FilePicker;
typedef struct FilePickerEntry {
    char name[FILEPICKER_NAME_MAX + 1u];
    int directory;
    uint32_t size;
} FilePickerEntry;

FilePicker *filepicker_create(int offline);
void filepicker_free(FilePicker *);
/* NULL opens the launch working directory. Browsing never changes cwd.
 * Explicit paths must be absolute and contain no symlink or '..' component.
 * Offline mode refuses before reading the working directory or filesystem. */
int filepicker_open(FilePicker *, const char *directory);
/* Process at most FILEPICKER_SCAN_BATCH entries. Returns a display change. */
int filepicker_step(FilePicker *);
int filepicker_loading(const FilePicker *);
int filepicker_limited(const FilePicker *);
size_t filepicker_skipped(const FilePicker *);
size_t filepicker_count(const FilePicker *);
/* Entry and string results are borrowed from the picker. A new scan or
 * navigation can replace them; copy a chosen path before continuing. */
const FilePickerEntry *filepicker_at(const FilePicker *, size_t);
const char *filepicker_path(const FilePicker *);
const char *filepicker_error(const FilePicker *);
/* 1: chosen regular-file path; 2: directory opened; 0: error.
 * Selection rechecks the listed inode without opening any file contents. */
int filepicker_choose(FilePicker *, size_t, char *path, size_t cap);
int filepicker_parent(FilePicker *);

#endif
