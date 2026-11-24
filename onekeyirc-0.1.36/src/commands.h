#ifndef VIPIRC_COMMANDS_H
#define VIPIRC_COMMANDS_H

#include <stddef.h>
#include "config.h"

#define COMMAND_HISTORY_MAX_ENTRIES 64u
#define COMMAND_HISTORY_MAX_BYTES (64u * 1024u)

typedef struct CommandSpec {
    const char *name;       /* Canonical lowercase name, without '/'. */
    const char *usage;      /* Full syntax, including '/'. */
    const char *summary;
} CommandSpec;

/* Metadata only: arguments are neither executed nor validated here. The
 * first command token is ASCII case-insensitive, with optional leading '/'.
 * Unknown, incomplete, empty and malformed command names return NULL. */
const CommandSpec *commands_find(const char *line);

/* Exact, case-insensitive alternative spellings. Abbreviations are suggested
 * but never executed implicitly. An unknown name returns NULL. Expansion
 * changes only the first slash-command word and preserves all argument bytes.
 * Return 1=expanded, 0=not an alias, -1=insufficient space/oversized input. */
const char *commands_alias_name(const char *name, size_t length);
int commands_expand_alias(const char *line, char *out, size_t capacity);
/* Match a canonical name or one of its aliases, without duplicate rows. */
int commands_name_matches(const char *canonical, const char *prefix,
                          size_t length, int exact);

/* Fill up to cap entries in stable alphabetical order and return the number
 * stored. Empty prefix or "/" lists all commands. Before the first space,
 * match command-name prefixes; after a space, return only an exact command's
 * usage. No argument values are predicted. NULL output/zero cap stores none. */
size_t commands_suggest(const char *prefix, const CommandSpec **out, size_t cap);

/* The same ordered matches, starting at zero-based offset. Return the number
 * stored and, when total is non-NULL, report the entire matching count even
 * if no entries fit. NULL output or zero capacity performs a count-only
 * query. Offset past the final match returns zero without wrapping. */
size_t commands_suggest_page(const char *prefix, const CommandSpec **out,
                             size_t cap, size_t offset, size_t *total);

/* A separate, zero-initializable object belongs to each server session.
 * Storage is allocated only for recorded commands: at most 64 entries and
 * 64 KiB of strings including their NULs, plus this fixed-size object.
 * The application never persists this history. Treat fields as read-only. */
typedef struct CommandHistory {
    char *entries[COMMAND_HISTORY_MAX_ENTRIES]; /* Newest first. */
    size_t count;
    size_t bytes;
} CommandHistory;

void commands_history_init(CommandHistory *history);
void commands_history_free(CommandHistory *history);

/* Call only after the application accepts a command. This additionally
 * requires a known slash command, valid UTF-8, no control characters, and
 * at most ED_MAX_BYTES bytes. Keyed joins and credential/service messages
 * are deliberately omitted. Arguments otherwise remain byte-for-byte.
 * Exact consecutive duplicates are not added. Oldest entries are evicted
 * at either bound. Return 1=added, 0=omitted/duplicate, -1=allocation failed.
 * Allocation failure leaves the existing history untouched. */
int commands_history_add(CommandHistory *history, const char *line);

/* Nth prefix match, newest first (ordinal 0 is the first). Prefix accepts
 * an optional leading '/'. Command-name matching folds ASCII case; argument
 * bytes match exactly. Empty prefix or "/" matches all entries. The UI owns
 * browsing position and restores its original typed line after the newest
 * match. Returned text is borrowed until the next add/free. */
const char *commands_history_match(const CommandHistory *history,
                                   const char *prefix, size_t ordinal);

typedef enum CommandContext {
    COMMAND_CONTEXT_CLIENT, COMMAND_CONTEXT_SERVER,
    COMMAND_CONTEXT_CHANNEL, COMMAND_CONTEXT_QUERY,
    COMMAND_CONTEXT_DISCONNECTED
} CommandContext;

typedef struct CommandSuggestion {
    const CommandSpec *spec;
    /* Exact command text for a history row; NULL for a catalog row. */
    const char *text;
    size_t history_index; /* SIZE_MAX for a catalog row. */
} CommandSuggestion;

/* Ranking never removes matching catalog commands. History mode contains
 * only retained history entries, newest first. Returned pointers are borrowed
 * until history mutation. Counts and pages have the same rules as above. */
size_t commands_suggest_ranked(const char *prefix, ConfigSuggestMode mode,
                               CommandContext context, const CommandHistory *,
                               const Config *, CommandSuggestion *out,
                               size_t cap, size_t offset, size_t *total);
unsigned commands_suggestion_score(const char *name, ConfigSuggestMode,
                                   CommandContext, const Config *);
const char *commands_suggestion_mode_name(int mode);
const char *commands_suggestion_mode_label(int mode);
/* Accepted executions only. This stores a canonical command name and count,
 * never arguments; counts saturate and the collection has a fixed bound. */
void commands_usage_record(Config *, const char *line);
int commands_usage_reset(Config *, const char *name);
/* Remove and wipe one retained entry. Catalog commands are immutable. */
int commands_history_remove(CommandHistory *, size_t index);

#endif
