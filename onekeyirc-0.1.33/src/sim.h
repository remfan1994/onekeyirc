#ifndef VIPIRC_SIM_H
#define VIPIRC_SIM_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
/* In-process IRC peer. No sockets, resolver, TLS, credentials or threads.
 * Replies are byte-queued; they never call application state synchronously. */
typedef struct Sim Sim;
Sim *sim_open(char *error, size_t error_cap);
void sim_close(Sim *);
int sim_step(Sim *, int64_t now_ms, const char *watch_text);
ssize_t sim_read(Sim *, void *out, size_t cap);
ssize_t sim_write(Sim *, const void *data, size_t size);
size_t sim_watch_index(const Sim *);
int sim_pending(const Sim *);
int sim_failed(const Sim *);
int sim_timeout(const Sim *, int64_t now_ms);
const char *sim_error(const Sim *);
int64_t sim_now_ms(void);
#endif
