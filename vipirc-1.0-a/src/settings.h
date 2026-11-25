#ifndef VIPIRC_SETTINGS_H
#define VIPIRC_SETTINGS_H

#include "app.h"
#include "client.h"
#include <curses.h>
#include <wchar.h>

typedef struct Settings Settings;
typedef void (*SettingsText)(int, int, int, const char *, attr_t);

/* Forms own private base/work configurations and an editor. A Client/App
 * passed to an open operation is borrowed; the form never owns the session. */
Settings *settings_create(void);
void settings_destroy(Settings *);
void settings_open(Settings *, const App *);
/* Application-level settings home. Server forms keep their own stable target
 * and never switch the active chat session. */
void settings_open_client(Settings *, Client *);
/* Open one server directly without switching the active conversation. */
int settings_open_server(Settings *, Client *, size_t, int nickname);
void settings_open_new(Settings *, const Config *);
/* Consume a completed new-server form: 0 none, 1 Add, 2 Connect. */
int settings_take_new(Settings *, Config *);
void settings_focus_ignores(Settings *);
int settings_active(const Settings *);
/* Up from the first nonediting field can enter the application header. */
int settings_at_top(const Settings *);
/* Global navigation may discard the form, but never interrupt a paste. */
void settings_close(Settings *, App *);
int settings_pasting(const Settings *);
/* Describe the focused navigation item without disclosing field values.
 * Text and secret editors keep '?' as ordinary input and return zero. */
int settings_context_help(const Settings *, char *screen, size_t screen_cap,
                          char *item, size_t item_cap, char *detail, size_t detail_cap);
/* Identifies the visible list/editor/choice layout for mouse-batch routing.
 * Coordinates queued for one layout must not act on a newly opened one. */
unsigned settings_mouse_state(const Settings *);
/* Editors stage field values. Explicit Apply/Save merges edited fields into
 * the current effective configuration after rechecking the target identity. */
void settings_key(Settings *, App *, wint_t key, int special);
void settings_begin_paste(Settings *);
void settings_end_paste(Settings *);
#if defined(KEY_MOUSE) && defined(BUTTON1_PRESSED)
void settings_mouse(Settings *, App *, const MEVENT *);
#endif
void settings_draw(Settings *, const App *, SettingsText);

#endif
