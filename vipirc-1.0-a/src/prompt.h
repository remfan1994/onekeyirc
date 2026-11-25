#ifndef VIPIRC_PROMPT_H
#define VIPIRC_PROMPT_H

#include "app.h"
#include "editor.h"
#include "commands.h"
#include <curses.h>
#include <wchar.h>

typedef struct CommandPrompt CommandPrompt;
typedef void (*PromptText)(int, int, int, const char *, attr_t);
typedef enum PromptAction { PROMPT_NONE, PROMPT_RUN, PROMPT_CANCEL, PROMPT_HELP } PromptAction;

/* A prompt owns its editor and private recall history. Open/close resets
 * working text while preserving recall; destruction wipes both. */
CommandPrompt *prompt_create(void);
void prompt_destroy(CommandPrompt *);
int prompt_open(CommandPrompt *, const char *prefix);
void prompt_set_context(CommandPrompt *, CommandContext);
/* Returns a selected suggestion only; text-entry '?' remains ordinary text. */
const CommandSpec *prompt_help_command(const CommandPrompt *, const App *);
void prompt_close(CommandPrompt *);
int prompt_active(const CommandPrompt *);
int prompt_pasting(const CommandPrompt *);
/* Borrowed views of the prompt editor; do not free or retain past destroy. */
Editor *prompt_editor(CommandPrompt *);
const Editor *prompt_const_editor(const CommandPrompt *);
/* Only record an accepted execution, after any send/close confirmation. */
/* A completed message supplies its exact plan so split credential commands
 * stay out of recall; non-message commands pass NULL. Usage counts are safe. */
void prompt_remember(CommandPrompt *, App *, const SendPlan *);
/* Decode an editing action, not command execution. PROMPT_RUN asks ui.c to
 * validate/execute the current text and own any resulting confirmation. */
PromptAction prompt_key(CommandPrompt *, App *, wint_t, int special);
void prompt_paste(CommandPrompt *, App *, int begin);
void prompt_draw(CommandPrompt *, const App *, PromptText);
#if defined(KEY_MOUSE) && defined(BUTTON1_PRESSED)
PromptAction prompt_mouse(CommandPrompt *, App *, const MEVENT *);
#endif

#endif
