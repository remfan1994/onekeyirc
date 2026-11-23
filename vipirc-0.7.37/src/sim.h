/* Private offline peer used by App's test-mode transport adapter. */
#ifndef VIPIRC_SIM_H
#define VIPIRC_SIM_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
/* In-process IRC peer. No sockets, resolver, TLS, credentials or threads.
 * Replies are byte-queued; they never call application state synchronously. */
typedef struct Sim Sim;
/* Caller owns the returned simulator; NULL reports allocation failure. */
Sim *sim_open(char *error, size_t error_cap);
void sim_close(Sim *);
/* Use sim_now_ms for deadlines. A healthy step returns 1, including when
 * no event is due; -1 reports failed input state. watch_text is borrowed
 * during the call and sanitized before any demonstration message uses it. */
int sim_step(Sim *, int64_t now_ms, const char *watch_text);
/* Read: >0 bytes, -2 empty/zero capacity, -1 failed. Write consumes the
 * supplied bytes and returns size, or -1 for malformed/failed input.
 * Both require a live Sim and valid caller-owned buffers. */
ssize_t sim_read(Sim *, void *out, size_t cap);
ssize_t sim_write(Sim *, const void *data, size_t size);
size_t sim_watch_index(const Sim *);
int sim_pending(const Sim *);
int sim_failed(const Sim *);
/* Milliseconds until useful work, capped at one second; 0 means due now. */
int sim_timeout(const Sim *, int64_t now_ms);
/* Borrowed diagnostic, valid until the simulator changes or is closed. */
const char *sim_error(const Sim *);
int64_t sim_now_ms(void);
#endif
