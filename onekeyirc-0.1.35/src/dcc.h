#ifndef VIPIRC_DCC_H
#define VIPIRC_DCC_H

#include <stddef.h>
#include <stdint.h>
#include <poll.h>

#define DCC_MAX_OFFERS 8
#define DCC_MAX_ACTIVE 4
#define DCC_MAX_ITEMS (DCC_MAX_OFFERS + DCC_MAX_ACTIVE)
#define DCC_NAME_MAX 255
#define DCC_PATH_MAX 4096

typedef enum DccState {
    DCC_EMPTY, DCC_OFFERED, DCC_LISTENING, DCC_CONNECTING,
    DCC_SENDING, DCC_RECEIVING, DCC_DONE, DCC_REJECTED,
    DCC_CANCELLED, DCC_FAILED
} DccState;

typedef struct DccInfo {
    uint64_t id, server_token;
    DccState state;
    int outgoing;
    char nick[64], filename[DCC_NAME_MAX + 1], address[16];
    char path[DCC_PATH_MAX], error[160];
    uint16_t port;
    uint32_t size, transferred, acknowledged;
} DccInfo;

typedef struct DccManager DccManager;

/* Classic active IPv4 DCC SEND only. Returns 1/0; input is the CTCP body
 * without its 0x01 delimiters. Parsing performs no filesystem/network I/O and
 * commits output only after full validation. A valid offer is not consent to
 * accept it; the caller must obtain explicit approval before dcc_accept. */
int dcc_parse_send(const char *, DccInfo *, char *error, size_t error_cap);
/* The session driver owns this manager across frontend replacements.
 * Freeing it cancels active transfers and removes unchanged partial receives. */
DccManager *dcc_create(int offline);
void dcc_free(DccManager *);
/* Offer, accept, send, reject and cancel return 1 on success, 0 on failure.
 * Recording an offer does not accept it or create a transfer socket/file. */
int dcc_offer(DccManager *, uint64_t server_token, const char *nick,
              const char *ctcp_body, uint64_t *id, char *error, size_t error_cap);
/* Accept creates missing directory components with mode 0700; the final
 * existing directory must be owned by the user and private. No symlinks or
 * '..' components are followed. filename NULL/empty uses the offered name. */
int dcc_accept(DccManager *, uint64_t id, const char *directory,
               const char *filename, char *error, size_t error_cap);
/* Source is an explicitly selected regular file. listen_port=0 requests an
 * ephemeral listener. advertised_ipv4 is explicit: no discovery or DNS.
 * Caller queues returned CTCP body on the originating server, or cancels id
 * if that queueing fails. Offline send/accept fail before any file/socket I/O. */
int dcc_send(DccManager *, uint64_t server_token, const char *nick,
             const char *source, const char *advertised_ipv4,
             uint16_t listen_port, uint64_t *id, char *ctcp_body,
             size_t ctcp_cap, char *error, size_t error_cap);
int dcc_reject(DccManager *, uint64_t id, char *error, size_t error_cap);
int dcc_cancel(DccManager *, uint64_t id, char *error, size_t error_cap);
void dcc_cancel_server(DccManager *, uint64_t server_token);
size_t dcc_count(const DccManager *);
/* Borrowed views into the manager. Copy ids for deferred actions rather than
 * holding a pointer/index across mutations: completed entries can be recycled. */
const DccInfo *dcc_at(const DccManager *, size_t index);
const DccInfo *dcc_get(const DccManager *, uint64_t id);
const char *dcc_state_name(DccState);
/* Snapshot descriptors plus their stable transfer ids. Process every loop,
 * including timeout-only loops; now_ms is CLOCK_MONOTONIC milliseconds. */
size_t dcc_pollfds(const DccManager *, struct pollfd *, uint64_t *ids, size_t cap);
void dcc_process(DccManager *, const struct pollfd *, const uint64_t *ids,
                 size_t count, int64_t now_ms);
int dcc_timeout(const DccManager *, int64_t now_ms);
/* Return and clear the display-change flag. */
int dcc_changed(DccManager *);

#endif
