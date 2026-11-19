#ifndef VIPIRC_IDLE_H
#define VIPIRC_IDLE_H
#include "client.h"
#include "background.h"
#include "dcc.h"
/* No curses dependency. Welcome: 1 acknowledged, 0 failure, negative signal. */
int idle_welcome(const char *program, int force_terminal_signals);
/* Returns 0 on normal exit or 128+signal after terminal restoration. */
int idle_run(Client *client, const char *program);
/* Prepare a resumable worker without a terminal, detach, then start network
 * jobs. The original parent exits only after the local worker handshake.
 * Successful startup does not imply an IRC connection or registration. */
int idle_run_background(Client *client, const char *program,
                        int primary_connect, int no_connect);
/* Shared-session runner. background and dcc remain owned by the caller;
 * next_frontend receives BACKGROUND_FRONTEND_FULL/IDLE when an attachment
 * requests a frontend switch. Return 1000 for that internal handoff. */
int idle_run_session(Client *, const char *, Background *, DccManager *,
                     int start_background, int primary_connect, int no_connect,
                     int *next_frontend);
#endif
