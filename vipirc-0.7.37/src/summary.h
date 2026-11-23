#ifndef VIPIRC_SUMMARY_H
#define VIPIRC_SUMMARY_H

#include "client.h"
#include "dcc.h"

/* Each callback receives one sanitized, NUL-terminated display line shorter
 * than this bound. The pointer is valid only until the callback returns.
 * The callback must not modify or reenter the client/transfer manager. */
#define SUMMARY_LINE_MAX 2048
typedef void (*SummaryPrint)(void *context, const char *line);

/* A local snapshot of every runtime session. Does not create sessions, change
 * selection/activity, query the network, open files, or consume DCC changes.
 * server_base follows the frontend's existing server-number convention.
 * NULL client/manager and an empty client are supported. */
void summary_print(const Client *, const DccManager *, unsigned server_base,
                   SummaryPrint, void *context);

#endif
