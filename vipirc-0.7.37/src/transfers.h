#ifndef VIPIRC_TRANSFERS_H
#define VIPIRC_TRANSFERS_H
#include "client.h"
#include "dcc.h"
#include "settings.h"
typedef struct Transfers Transfers;
Transfers *transfers_create(Client *, DccManager *);
void transfers_free(Transfers *);
void transfers_open(Transfers *, size_t server_slot);
int transfers_active(const Transfers *);
/* Up from the first list row may enter the application header. */
int transfers_at_top(const Transfers *);
int transfers_pasting(const Transfers *);
void transfers_close(Transfers *);
/* Distinguishes the list, each review form, and its field editor so queued
 * coordinates cannot approve a newly opened transfer review. */
unsigned transfers_mouse_state(const Transfers *);
int transfers_context_help(const Transfers *, char *, size_t, char *, size_t, char *, size_t);
/* Incremental file-browser work, serviced by the ordinary IRC event loop. */
int transfers_tick(Transfers *);
int transfers_timeout(const Transfers *);
void transfers_draw(Transfers *, App *, SettingsText);
void transfers_key(Transfers *, App *, wint_t, int special);
void transfers_paste(Transfers *, int begin);
#if defined(KEY_MOUSE) && defined(BUTTON1_PRESSED)
void transfers_mouse(Transfers *, App *, const MEVENT *);
#endif
#endif
