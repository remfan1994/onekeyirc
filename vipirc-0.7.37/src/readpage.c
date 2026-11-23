/* Owned, bounded response text for the full frontend's scrollable reader.
 * Callback producers may supply transient lines; readpage_add sanitizes and
 * copies them. A failed page retains its owned prefix for readpage_free. */
#include "readpage.h"
#include "protocol.h"
#include <stdlib.h>
#include <string.h>

#define READPAGE_LINES 8192u
#define READPAGE_BYTES (4u * 1024u * 1024u)
#define READPAGE_LINE 4096u

void readpage_add(void *context, const char *text)
{
    ReadPage *page = context;
    char safe[READPAGE_LINE];
    if (!page || page->failed) return;
    size_t bytes = irc_sanitize(safe, sizeof safe, text ? text : "") + 1;
    if (page->count >= READPAGE_LINES || bytes > READPAGE_BYTES - page->bytes) {
        page->failed = 1;
        return;
    }
    if (page->count == page->capacity) {
        size_t capacity = page->capacity ? page->capacity * 2 : 32;
        char **lines = realloc(page->lines, capacity * sizeof *lines);
        if (!lines) {
            page->failed = 1;
            return;
        }
        page->lines = lines;
        page->capacity = capacity;
    }
    char *copy = malloc(bytes);
    if (!copy) {
        page->failed = 1;
        return;
    }
    memcpy(copy, safe, bytes);
    page->lines[page->count++] = copy;
    page->bytes += bytes;
}

/* Convert text to bounded owned lines before
 * presenting a modal reader. An overlong line marks the whole page
 * incomplete; do not silently split a protocol result into fake records. */
void readpage_text(ReadPage *page, const char *text)
{
    char line[READPAGE_LINE];
    while (text && *text && !page->failed) {
        const char *end = strchr(text, '\n');
        size_t length = end ? (size_t)(end - text) : strlen(text);
        if (length >= sizeof line) {
            page->failed = 1;
            return;
        }
        memcpy(line, text, length);
        line[length] = 0;
        readpage_add(page, line);
        if (!end) break;
        text = end + 1;
    }
}

void readpage_free(ReadPage *page)
{
    if (!page) return;
    for (size_t i = 0; i < page->count; ++i) free(page->lines[i]);
    free(page->lines);
    memset(page, 0, sizeof *page);
}
