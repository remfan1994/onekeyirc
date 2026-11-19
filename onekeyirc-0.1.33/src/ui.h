#ifndef VIPIRC_UI_H
#define VIPIRC_UI_H
#include "client.h"
#include "background.h"
#include "dcc.h"
/* 1: acknowledged; 0: terminal error; -signal: interrupted after cleanup. */
int ui_welcome(int force_terminal_signals);
int ui_run(Client *);
/* Start detached before connecting; the first resume creates the full screen. */
int ui_run_background(Client *, int primary_connect, int no_connect);
int ui_run_session(Client *, Background *, DccManager *, int start_background,
                   int primary_connect, int no_connect, int *next_frontend);
#endif
