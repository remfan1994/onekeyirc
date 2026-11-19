#ifndef VIPIRC_BACKGROUND_H
#define VIPIRC_BACKGROUND_H

#include "client.h"
#include <poll.h>
#include <signal.h>
#include <stddef.h>

typedef struct Background Background;

#define BACKGROUND_POLL_CAP 3u

enum BackgroundFrontend { BACKGROUND_FRONTEND_CURRENT = 0, BACKGROUND_FRONTEND_FULL = 1, BACKGROUND_FRONTEND_IDLE = 2 };

enum BackgroundEvent {
    BACKGROUND_ATTACH = 1,
    BACKGROUND_DETACH = 2,
    BACKGROUND_RESIZE = 4,
    BACKGROUND_SUSPEND = 8,
    BACKGROUND_CONTINUE = 16,
    BACKGROUND_STOP = 32
};

/* The ordinary foreground path allocates no socket and starts no process.
 * Prepare must run before restoring the terminal; first detach refuses live
 * child jobs and requires a waitable SIGCHLD policy. Callers must not reap
 * its startup child. Detach runs AFTER terminal restoration. On success, the
 * original parent exits 0 without destroying the worker's copied state.
 * The continuing worker receives 1; 0 means the caller must restore its UI.
 * A managed worker detaches again without forking. Keep this object across
 * frontend replacements. */
Background *background_create(void);
void background_set_program(Background *, const char *program);
int background_prepare(Background *, const Client *, char *, size_t);
int background_detach(Background *, const volatile sig_atomic_t *cancel, char *, size_t);
int background_managed(const Background *);
int background_detached(const Background *);
const char *background_path(const Background *);

/* At most BACKGROUND_POLL_CAP descriptors. Pass the matching pollfds
 * snapshot to process: descriptor numbers alone can be reused between turns.
 * Handshake work is bounded and times out. ATTACH has installed validated
 * terminal descriptors/TERM/LC_CTYPE; rebuild frontend presentation.
 * DETACH/SUSPEND require terminal restoration before detach/suspend. */
size_t background_pollfds(const Background *, struct pollfd *, size_t);
int background_timeout(const Background *, int);
unsigned background_process(Background *, const struct pollfd *, size_t);
int background_signal(const Background *);
int background_frontend(const Background *);
void background_suspend(Background *);
void background_finish(Background *, int);
void background_free(Background *);

/* List verified local jobs without a terminal. Resume accepts NULL for the
 * sole available compatible job, a stable job number, or an explicit legacy
 * absolute socket path. Ambiguous or unverifiable selection fails closed. */
int background_sessions(void);
int background_resume(const char *, int frontend);
/* Cooperatively close one verified numbered job without attaching a terminal.
 * Return 0 only after worker teardown is acknowledged, 1 on operational or
 * legacy-worker failure, and 2 for an invalid number. Never signal a PID. */
int background_close_session(const char *job);
/* Non-destructively detach the terminal/controller from one verified numbered
 * job. IRC connections and worker state remain alive for a later --resume. */
int background_detach_session(const char *job);

#endif
