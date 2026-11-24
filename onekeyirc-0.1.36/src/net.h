/* Poll-driven byte transport. A single owning event loop supplies progress;
 * returned descriptors and error strings are borrowed, not transferred. */
#ifndef VIPIRC_NET_H
#define VIPIRC_NET_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct Net Net;
typedef enum NetState {
    NET_RESOLVING, NET_CONNECTING, NET_HANDSHAKE, NET_READY, NET_FAILED
} NetState;

/* Allocate a caller-owned transport; release it with net_close. TLS context
 * and trust-file loading happen synchronously, before asynchronous resolver,
 * connect and handshake progress. Filesystem access has no wall-clock bound.
 * NULL reports an immediate allocation/setup error. SIGCHLD must preserve
 * waitable children: callers must not reap the resolver or change that policy
 * while this transport owns it. Auto-reaping is rejected. */
Net *net_open(const char *host, int port, int tls, const char *ca_file,
              char *error, size_t error_cap);
/* Borrow the current resolver-pipe/socket descriptor, or -1. Re-query after
 * net_step: progressing to another address may replace the descriptor. */
int net_fd(const Net *net);
/* Poll exactly these events. want_write means application output is pending. */
short net_events(const Net *net, int want_write);
NetState net_state(const Net *net);
/* Call after poll, even with revents=0, so deadlines are enforced. Use
 * net_now_ms() for the monotonic clock; IRC registration and reconnect policy
 * belong to the caller, not to this transport state machine. Return 0 while
 * setup is pending or ready, -1 on failure; inspect net_state for readiness. */
int net_step(Net *net, short revents, int64_t now_ms);
/* >0 transferred, 0 orderly EOF, -1 failed/invalid call, -2 try again after
 * polling. Use net_error for a failed state; do not interpret -2 as errno. */
ssize_t net_read(Net *net, void *out, size_t cap);
/* After -2, retain and retry the same bytes and length until positive
 * progress or failure; do not rebuild, append to or release that buffer. A
 * TLS write waiting for socket output takes precedence over net_read. */
ssize_t net_write(Net *net, const void *data, size_t size);
/* Immediately readable decoded TLS bytes, even without socket readability.
 * Suppressed while a TLS write must be retried before any other I/O. Partial
 * encrypted records do not count: they still require descriptor readiness. */
int net_pending(const Net *net);
/* Collect a finished resolver without waiting. Safe to fork the connection
 * only when no resolver child remains owned by the current process. */
int net_detach_ready(Net *net);
/* Borrowed diagnostic, valid until this Net changes or is closed. */
const char *net_error(const Net *net);
/* True only for a failed transport with an identified temporary cause.
 * NULL, active connections, certificate/identity/protocol/configuration errors
 * and unclassified failures return false. Orderly net_read EOF is reported as
 * zero separately; callers decide whether to retry it. No retry is performed. */
int net_retryable(const Net *net);
int64_t net_now_ms(void);
/* Cancel/reap the owned resolver, close descriptors/TLS state and free Net.
 * This deliberately does not wait for a peer's TLS shutdown handshake. */
void net_close(Net *net);

#endif
