#ifndef VIPIRC_READPAGE_H
#define VIPIRC_READPAGE_H
#include <stddef.h>

/* An independent, bounded snapshot; never a conversation or its scrollback. */
typedef struct ReadPage {
    char **lines;
    size_t count, capacity, bytes;
    int failed;
} ReadPage;
void readpage_add(void *, const char *);
void readpage_text(ReadPage *, const char *);
void readpage_free(ReadPage *);
#endif
