#ifndef VIPIRC_DCC_COMMANDS_H
#define VIPIRC_DCC_COMMANDS_H
#include "client.h"
#include "dcc.h"
typedef void (*DccPrint)(void *, const char *);
/* Shared, curses-free command implementation. A NULL manager is allocated
 * only for an explicit operation that needs one. Paths are literal and
 * relative to the unchanged process working directory. No shell is used. */
int dcc_command(DccManager **, Client *, App *, const char *arguments,
                DccPrint, void *context);
#endif
