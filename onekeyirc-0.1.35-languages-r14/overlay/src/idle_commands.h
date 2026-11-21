#ifndef VIPIRC_IDLE_COMMANDS_H
#define VIPIRC_IDLE_COMMANDS_H
#include "client.h"
#include "dcc.h"
#include <stddef.h>
#include <stdio.h>
/* Callbacks run synchronously. Copy a line if it must outlive the callback;
 * it may point to a local scratch buffer. Keep frontend context alive until
 * the command/manual operation has returned. */
typedef void (*IdlePrint)(void *context, const char *line);
typedef struct IdleCommands {
    Client *client;
    const char *program;
    IdlePrint print;
    void *context;
    /* The curses caller commits navigation after its key handler returns.
     * Selection commands still report Client.current, but must not mark a
     * hidden conversation read or change its App view before that commit. */
    int defer_visibility;
    /* Native screens number servers from one; the curses lists retain their
     * existing zero-based numbers. Internal slots and saved IDs never change. */
    unsigned server_base;
    /* Borrow the current transfer manager; summary never creates one. */
    const DccManager *dcc;
    /* Display-only lookup. NULL keeps English for tools and generated manuals. */
    const char *(*translate)(const char *english);
} IdleCommands;
typedef struct IdleCommandSpec {
    const char *name, *usage, *description;
} IdleCommandSpec;
enum { IDLE_COMMAND_ERROR = 0, IDLE_COMMAND_DONE = 1,
       IDLE_COMMAND_SEND = 2, IDLE_COMMAND_QUIT = 3,
       IDLE_COMMAND_BACKGROUND = 4, IDLE_COMMAND_CLEAR = 5,
       IDLE_COMMAND_PANEL = 6, IDLE_COMMAND_QUICKSTART = 7,
       IDLE_COMMAND_DCC = 8, IDLE_COMMAND_MENTIONS_CLEAR = 9 };
/* Returns an IDLE_COMMAND_* result, not a generic success boolean. SEND
 * provides the plan for frontend review/queueing; PANEL/QUICKSTART/DCC and
 * other frontend actions are requests for the caller to handle. */
int idle_command(IdleCommands *, const char *text, SendPlan *);
size_t idle_command_count(void);
const IdleCommandSpec *idle_command_spec(size_t index);
/* Return 0 for any input that could contain a credential or join key. */
int idle_command_recordable(const char *text);
void idle_manual(FILE *out, const char *program);
/* Plain-text localized export. The caller chooses the lookup explicitly;
 * NULL gives exactly the English manual. Never applies visual RTL shaping. */
void idle_manual_translated(FILE *out, const char *program,
                            const char *(*translate)(const char *));
#endif
