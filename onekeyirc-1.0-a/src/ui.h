/* Full curses frontend API. The runner borrows Client; UI editors and
 * screens are frontend-owned. ui_run_session() preserves caller-owned worker
 * and transfer state when handing control to the minimalist frontend. */
#ifndef VIPIRC_UI_H
#define VIPIRC_UI_H
#include "client.h"
#include "background.h"
#include "dcc.h"

/* 1: acknowledged; 0: terminal error; -signal: interrupted after cleanup. */
int ui_welcome(int use_signals);
/* 0=normal exit, 1=local failure, 128+signal=interrupted after cleanup. */
int ui_run(Client *);
/* Start detached before connecting; the first resume creates the full screen. */
int ui_run_background(Client *, int primary_connect, int no_connect);
/* Borrow both managers. Return 1000 for an interface handoff and set
 * next_frontend; otherwise use the same return convention as ui_run(). */
int ui_run_session(Client *, Background *, DccManager *, int start_background,
                   int primary_connect, int no_connect, int *next_frontend);
#endif
