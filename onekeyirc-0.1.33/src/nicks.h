#ifndef VIPIRC_NICKS_H
#define VIPIRC_NICKS_H

#include "app.h"
#include "editor.h"

/* A completion cycle stores only its original prefix and replacement range.
 * Candidates are read from the current server, never another session. */
typedef struct NickCompletion {
    int active;
    size_t view, first, end;
    char prefix[64], previous[64], suffix[3];
} NickCompletion;

void nick_completion_reset(NickCompletion *);
/* 1=replaced, 0=no match/unsupported word, -1=editor limit. No I/O. */
int nick_complete(const App *, Editor *, NickCompletion *, int direction);

#endif
