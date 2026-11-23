#ifndef VIPIRC_GUIDED_H
#define VIPIRC_GUIDED_H

#include "settings.h"

typedef struct Guided Guided;

/* A curses view of the shared configuration draft. Configuration validation,
 * credentials, persistence and connection choices belong to IdleSetup.
 * The adapter owns IdleSetup and its editor, and borrows Client. Freeing or
 * closing the adapter cancels its model; no server is freed by this layer. */
Guided *guided_create(Client *);
void guided_free(Guided *);
/* 1=opened, 0=error with a diagnostic; quick selects the short model flow. */
int guided_open(Guided *, int quick, char *error, size_t error_cap);
void guided_close(Guided *);
int guided_active(const Guided *);
int guided_pasting(const Guided *);
unsigned guided_mouse_state(const Guided *);
/* Selected controls only; text and credential editors keep '?' literal. */
int guided_context_help(const Guided *, char *, size_t, char *, size_t, char *, size_t);
void guided_paste(Guided *, int begin);
void guided_key(Guided *, App *, wint_t key, int special);
/* A frontend-discarded suffix still delays a completed asynchronous setup
 * result; it must not reveal a new input owner while queued input continues. */
void guided_input_discarded(Guided *, int64_t now_ms);
#if defined(KEY_MOUSE) && defined(BUTTON1_PRESSED)
void guided_mouse(Guided *, App *, const MEVENT *);
#endif
void guided_draw(Guided *, App *, SettingsText);
/* Call from the main loop using monotonic milliseconds. A nonzero tick
 * result requests a redraw; helper completion can remain deferred until the
 * paste owner and queued-input quiet interval have both been released. */
int guided_tick(Guided *, int64_t now_ms);
/* Milliseconds until work is due, or -1 when this adapter needs no timer. */
int guided_timeout(const Guided *);
/* 0=no completion, 1=cancelled, 2=applied. Consumes the retained message. */
int guided_take_result(Guided *, char *message, size_t message_cap);

#endif
