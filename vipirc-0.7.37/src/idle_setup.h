/* Shared setup model used by idle.c and the curses adapter guided.c.
 * The model owns its drafts and credential-creation jobs, but borrows Client.
 * Presentation code owns the visible-question/fresh-input confirmation gate. */
#ifndef VIPIRC_IDLE_SETUP_H
#define VIPIRC_IDLE_SETUP_H

#include "client.h"

typedef struct IdleSetup IdleSetup;
#define IDLE_SETUP_SUMMARY_MAX (2u * CONFIG_PATH_MAX + 3072u)
#define IDLE_SETUP_TEXT_MAX (3u * CONFIG_PATH_MAX + 5120u)
typedef struct IdleSetupView {
    char title[384];
    char question[384];
    char current[CONFIG_PATH_MAX + 1];
    char detail[768];
    char summary[IDLE_SETUP_SUMMARY_MAX];
    int secret, final, step;
} IdleSetupView;

/* A private, bounded draft. Panel 0 edits native application preferences;
 * displayed panels 1..8 address stable internal server slots 0..7. Quickstart
 * edits the current server directly. Accepting a new credential destination
 * performs read-only preflight; accepting an offered default may prepare its
 * private directory. No credential contents are read or created, no connection
 * or config write occurs, and runtime preferences do not change before Finish. */
IdleSetup *idle_setup_new(Client *, int quick, char *error, size_t error_cap);
/* Cancels remaining jobs, applies their cleanup rules, and wipes the draft. */
void idle_setup_free(IdleSetup *);
/* Copy a bounded presentation snapshot into caller storage; secret values
 * are represented by status text rather than copied into the view. */
void idle_setup_view(const IdleSetup *, IdleSetupView *);
/* Empty keeps the shown value, except password confirmation must be retyped.
 * Returns 0=invalid, 1=next, 2=ready to commit.
 * The caller must require a freshly displayed question and fresh Enter. */
int idle_setup_answer(IdleSetup *, const char *, char *error, size_t error_cap);
/* Back also cancels any pending GPG creation before moving one question back. */
int idle_setup_back(IdleSetup *);
/* 1=finished, 0=error (remain on the final question), 2=GPG creation pending.
 * Application Save/Apply
 * merges only edited preferences into the latest primary configuration and
 * never changes the selected conversation or connects. A server connection
 * failure after Apply/Save is reported explicitly, never rolled back. */
int idle_setup_commit(IdleSetup *, char *message, size_t message_cap);
/* Drive pending work at intervals no longer than 50 ms; allow cancellation.
 * now_ms uses the monotonic clock shared with credential helper deadlines.
 * 0=pending/idle, 1=committed, -1=failed and ready for correction or retry.
 * No configuration changes occur until all requested encrypted files finish. */
int idle_setup_pending(const IdleSetup *);
int idle_setup_poll(IdleSetup *, int64_t now_ms, char *message, size_t message_cap);

#endif
