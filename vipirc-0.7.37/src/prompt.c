/* Separate command editing. Chat drafts are never borrowed or overwritten.
 *
 * Lifecycle/history helpers come first, then suggestion/recall navigation,
 * prompt_key()/prompt_paste(), and rendering/mouse hit tests. This module
 * returns PromptAction requests; ui.c owns execution and confirmation. */
#include "prompt.h"
#include "commands.h"
#include "focus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This is only a stack-workspace size, not a limit on visible or reachable
 * commands. Larger terminal pages are painted in successive batches. */
#define PROMPT_PAGE_BATCH 16u

struct CommandPrompt {
    Editor editor;
    CommandHistory history;
    char original[ED_MAX_BYTES + 1];
    size_t original_pos, history_at;
    int browsing, active, paste, paste_rejected;
    char completion_prefix[64];
    size_t completion_at;
    int completing;
    size_t suggestion_first, shown_first, shown_count;
    size_t selected;
    CommandContext context;
    int selecting, inserted, inserted_drawn;
    int first_row, rows, width, scroll, suggestion_row;
    int drawn, drawn_lines, drawn_cols;
};

CommandPrompt *prompt_create(void)
{
    CommandPrompt *p = calloc(1, sizeof *p);
    if (p) ed_init(&p->editor);
    return p;
}

void prompt_destroy(CommandPrompt *p)
{
    if (!p) return;
    commands_history_free(&p->history);
    config_wipe(p, sizeof *p);
    free(p);
}

int prompt_open(CommandPrompt *p, const char *prefix)
{
    if (!p || !prefix || strchr(prefix, '\n') || strchr(prefix, '\r') ||
        !ed_from_utf8(&p->editor, prefix)) return 0;
    p->active = 1;
    p->browsing = p->completing = p->paste = p->paste_rejected = 0;
    p->scroll = p->drawn = 0;
    p->suggestion_first = p->shown_first = p->shown_count = 0;
    p->selected = 0;
    p->selecting = p->inserted = p->inserted_drawn = 0;
    p->context = COMMAND_CONTEXT_CLIENT;
    config_wipe(p->original, sizeof p->original);
    return 1;
}

void prompt_close(CommandPrompt *p)
{
    if (!p) return;
    config_wipe(&p->editor, sizeof p->editor);
    ed_init(&p->editor);
    config_wipe(p->original, sizeof p->original);
    p->active = p->paste = p->browsing = p->completing = p->drawn = 0;
}

int prompt_active(const CommandPrompt *p) { return p && p->active; }
void prompt_set_context(CommandPrompt *p, CommandContext context) { if (p) p->context = context; }
int prompt_pasting(const CommandPrompt *p) { return p && p->paste; }
Editor *prompt_editor(CommandPrompt *p) { return p ? &p->editor : NULL; }
const Editor *prompt_const_editor(const CommandPrompt *p) { return p ? &p->editor : NULL; }

void prompt_remember(CommandPrompt *p, App *a, const SendPlan *plan)
{
    char line[ED_MAX_BYTES + 1];
    if (!p || ed_to_utf8(&p->editor, line, sizeof line) == SIZE_MAX) return;
    if ((!plan || app_plan_history_safe(plan)) && commands_history_add(&p->history, line) < 0)
        app_status(a, "Command completed; unable to retain its history entry.");
    commands_usage_record(&a->cfg, line);
    memcpy(a->config_pending.command_usage, a->cfg.command_usage, sizeof a->cfg.command_usage);
    a->config_pending.command_usage_count = a->cfg.command_usage_count;
    if (a->config_saved && !a->test_mode && a->config_path[0]) {
        char error[256];
        if (config_command_usage_save(&a->cfg, a->config_path, error, sizeof error) < 0)
            app_status(a, "Command completed; unable to save command usage counters.");
    }
    config_wipe(line, sizeof line);
}

static CommandContext live_context(const App *a)
{
    if (!a->registered) return COMMAND_CONTEXT_DISCONNECTED;
    BufferKind kind = a->buffers[a->current].kind;
    return kind == B_CHANNEL ? COMMAND_CONTEXT_CHANNEL : kind == B_QUERY ?
           COMMAND_CONTEXT_QUERY : COMMAND_CONTEXT_SERVER;
}

static size_t suggestions(const CommandPrompt *p, const App *a, const char *prefix,
                          CommandSuggestion *out, size_t cap, size_t offset, size_t *total)
{
    ConfigSuggestMode mode = (ConfigSuggestMode)a->cfg.command_suggestions;
    CommandContext context = mode == CONFIG_SUGGEST_CONTEXTUAL ? p->context : live_context(a);
    return commands_suggest_ranked(prefix, mode, context, &p->history, &a->cfg,
                                   out, cap, offset, total);
}

const CommandSpec *prompt_help_command(const CommandPrompt *p, const App *a)
{
    char line[ED_MAX_BYTES + 1];
    CommandSuggestion entry;
    if (!p || !a || !p->active || !p->selecting || p->paste ||
        ed_to_utf8(&p->editor, line, sizeof line) == SIZE_MAX) return NULL;
    size_t count = suggestions(p, a, line, &entry, 1, p->selected, NULL);
    config_wipe(line, sizeof line);
    return count ? entry.spec : NULL;
}

static void select_insert(CommandPrompt *p, App *a, size_t index)
{
    char line[ED_MAX_BYTES + 1], completed[96];
    CommandSuggestion entry;
    if (ed_to_utf8(&p->editor, line, sizeof line) == SIZE_MAX) return;
    if (!suggestions(p, a, line, &entry, 1, index, NULL)) { config_wipe(line, sizeof line); return; }
    if (entry.text) (void)ed_from_utf8(&p->editor, entry.text);
    else if (!strchr(line, ' ')) {
        snprintf(completed, sizeof completed, "/%s ", entry.spec->name);
        (void)ed_from_utf8(&p->editor, completed);
    }
    config_wipe(line, sizeof line);
    p->selecting = p->browsing = p->completing = 0;
    p->suggestion_first = p->selected = 0;
    p->inserted = 1;
    p->inserted_drawn = 0;
}

static void select_delete(CommandPrompt *p, App *a)
{
    char line[ED_MAX_BYTES + 1];
    CommandSuggestion entry;
    if (ed_to_utf8(&p->editor, line, sizeof line) == SIZE_MAX) return;
    size_t count = suggestions(p, a, line, &entry, 1, p->selected, NULL);
    config_wipe(line, sizeof line);
    if (!count) return;
    if (entry.history_index != SIZE_MAX) {
        (void)commands_history_remove(&p->history, entry.history_index);
        app_status(a, "Removed the selected command history entry.");
    } else if (a->cfg.command_suggestions == CONFIG_SUGGEST_FREQUENT) {
        (void)commands_usage_reset(&a->cfg, entry.spec->name);
        memcpy(a->config_pending.command_usage, a->cfg.command_usage, sizeof a->cfg.command_usage);
        a->config_pending.command_usage_count = a->cfg.command_usage_count;
        char error[256];
        if (a->config_saved && !a->test_mode && a->config_path[0] &&
            config_command_usage_save(&a->cfg, a->config_path, error, sizeof error) < 0)
            app_status(a, "Usage count reset for this session; unable to save the reset.");
        else app_status(a, "Reset this command's usage count; the command remains available.");
    } else app_status(a, "Delete is available for Recent history and Most used suggestions.");
}

/* Recall keeps the original prefix/caret stable throughout one browse. */
static void history_move(CommandPrompt *p, App *a, int older)
{
    const char *line;
    if (!p->browsing) {
        if (!older) return;
        if (ed_to_utf8(&p->editor, p->original, sizeof p->original) == SIZE_MAX) return;
        p->original_pos = p->editor.pos;
        p->history_at = 0;
        line = commands_history_match(&p->history, p->original, 0);
        if (!line) { app_status(a, "No matching command history."); return; }
        p->browsing = 1;
    } else if (older) {
        ++p->history_at;
        line = commands_history_match(&p->history, p->original, p->history_at);
        if (!line) {
            p->history_at = 0;
            line = commands_history_match(&p->history, p->original, 0);
        }
    } else if (!p->history_at) {
        if (ed_from_utf8(&p->editor, p->original)) p->editor.pos = p->original_pos;
        p->browsing = 0;
        return;
    } else {
        --p->history_at;
        line = commands_history_match(&p->history, p->original, p->history_at);
    }
    if (line) (void)ed_from_utf8(&p->editor, line);
}

static void complete_name(CommandPrompt *p, App *a)
{
    CommandSuggestion match;
    const CommandSpec *exact = NULL;
    char line[ED_MAX_BYTES + 1], completed[96];
    if (!p->completing) {
        if (ed_to_utf8(&p->editor, line, sizeof line) == SIZE_MAX) return;
        if (strchr(line, ' ') || strlen(line) >= sizeof p->completion_prefix) {
            config_wipe(line, sizeof line);
            return;
        }
        memcpy(p->completion_prefix, line, strlen(line) + 1);
        exact = commands_find(line);
        config_wipe(line, sizeof line);
        p->completion_at = 0;
    } else ++p->completion_at;
    size_t count = 0;
    (void)suggestions(p, a, p->completion_prefix, NULL, 0, 0, &count);
    if (exact) {
        /* Ranking may prefer a longer name, but a complete command starts
         * with itself. Continue cycling from its existing ranked position.
         * An exact command absent from Recent starts before that list. */
        p->completion_at = SIZE_MAX;
        for (size_t i = 0; i < count; ++i) {
            if (!suggestions(p, a, p->completion_prefix, &match, 1, i, NULL)) break;
            if (match.spec == exact) { p->completion_at = i; break; }
        }
        match.spec = exact;
    } else {
        if (!count) { app_status(a, "No matching command."); p->completing = 0; return; }
        p->completion_at %= count;
        if (!suggestions(p, a, p->completion_prefix, &match, 1, p->completion_at, NULL)) return;
    }
    snprintf(completed, sizeof completed, "/%s ", match.spec->name);
    if (ed_from_utf8(&p->editor, completed)) p->completing = 1;
    p->browsing = 0;
}

static size_t suggestion_room(void)
{
    int first = 4 + (LINES >= 14 ? 3 : 1) + 2;
    return LINES >= 9 && COLS >= 20 && LINES - 2 > first ? (size_t)(LINES - 2 - first) : 0;
}

static void page_suggestions(CommandPrompt *p, const App *a, int direction)
{
    char line[ED_MAX_BYTES + 1];
    size_t count = 0, step = suggestion_room();
    p->drawn = 0;
    if (!step || ed_to_utf8(&p->editor, line, sizeof line) == SIZE_MAX) return;
    (void)suggestions(p, a, line, NULL, 0, 0, &count);
    config_wipe(line, sizeof line);
    if (!count) { p->suggestion_first = 0; return; }
    size_t maximum = ((count - 1) / step) * step;
    if (p->suggestion_first > maximum) p->suggestion_first = maximum;
    if (direction < 0)
        p->suggestion_first = p->suggestion_first > step ? p->suggestion_first - step : 0;
    else
        p->suggestion_first += step < maximum - p->suggestion_first ? step : maximum - p->suggestion_first;
    if (p->selecting) p->selected = p->suggestion_first;
}

/* Paste and selected suggestions own Enter before ordinary command input. */
/* The command prompt owns its draft and recall
 * separately from chat. Key handling returns an action request; ui.c
 * must perform any network send or confirmation after this returns. */
PromptAction prompt_key(CommandPrompt *p, App *a, wint_t key, int special)
{
    if (!prompt_active(p)) return PROMPT_NONE;
    p->drawn = 0;
    if (p->paste) {
        if (!special) {
            if (key == '\r' || key == '\n' || key == '\t') key = ' ';
            if (!ed_insert(&p->editor, (wchar_t)key)) p->paste_rejected = 1;
            p->suggestion_first = 0;
        }
        return PROMPT_NONE;
    }
    if (special && (key == KEY_PPAGE || key == KEY_NPAGE)) {
        page_suggestions(p, a, key == KEY_PPAGE ? -1 : 1);
        return PROMPT_NONE;
    }
    int enter = (special && key == KEY_ENTER) || (!special && (key == '\r' || key == '\n'));
    if (p->selecting) {
        if (!special && key == '?') return PROMPT_HELP;
        if (enter) {
            select_insert(p, a, p->selected);
            return PROMPT_NONE;
        }
        if (special && key == KEY_DC) {
            select_delete(p, a);
            return PROMPT_NONE;
        }
        if (!special && key == 27) {
            p->selecting = 0;
            return PROMPT_NONE;
        }
    }
    if (enter && p->inserted && !p->inserted_drawn) return PROMPT_NONE;
    if (special && key == KEY_DOWN && !p->browsing) {
        if (!suggestion_room()) {
            app_status(a, "Resize terminal to browse suggestions.");
            return PROMPT_NONE;
        }
        char line[ED_MAX_BYTES + 1];
        size_t count = 0;
        if (ed_to_utf8(&p->editor, line, sizeof line) == SIZE_MAX) return PROMPT_NONE;
        (void)suggestions(p, a, line, NULL, 0, 0, &count);
        config_wipe(line, sizeof line);
        if (count) {
            if (!p->selecting) p->selected = p->suggestion_first < count ? p->suggestion_first : 0;
            else if (p->selected + 1u < count) ++p->selected;
            p->selecting = 1;
        }
        p->completing = 0;
        return PROMPT_NONE;
    }
    if (special && key == KEY_UP && p->selecting) {
        if (p->selected) --p->selected;
        else p->selecting = 0;
        return PROMPT_NONE;
    }
    p->selecting = 0;
    if (special && key == KEY_RIGHT && p->editor.pos == p->editor.len) {
        complete_name(p, a);
        p->suggestion_first = 0;
        return PROMPT_NONE;
    }
    p->completing = 0;
    if (special && (key == KEY_UP || key == KEY_DOWN)) {
        history_move(p, a, key == KEY_UP);
        p->suggestion_first = 0;
        return PROMPT_NONE;
    }
    p->browsing = 0;
    if (special) {
        switch (key) {
        case KEY_LEFT: ed_left(&p->editor); break;
        case KEY_RIGHT: ed_right(&p->editor); break;
        case KEY_HOME: ed_home(&p->editor); break;
        case KEY_END: ed_end(&p->editor); break;
        case KEY_BACKSPACE: if (ed_backspace(&p->editor)) p->suggestion_first = 0; break;
        case KEY_DC: if (ed_delete(&p->editor)) p->suggestion_first = 0; break;
        case KEY_ENTER: return PROMPT_RUN;
        default: break;
        }
    } else {
        switch (key) {
        case 27: return PROMPT_CANCEL;
        case '\r': case '\n': return PROMPT_RUN;
        case 8: case 127: if (ed_backspace(&p->editor)) p->suggestion_first = 0; break;
        default:
            if (key >= 32) {
                if (ed_insert(&p->editor, (wchar_t)key)) p->suggestion_first = 0;
                else app_status(a, "Command input limit reached or unsupported character.");
            }
            break;
        }
    }
    return PROMPT_NONE;
}

/* A bracketed paste is literal command text,
 * never a script or a sequence of independent submissions. Clearing
 * completion state prevents paste bytes activating a suggestion. */
void prompt_paste(CommandPrompt *p, App *a, int begin)
{
    if (!prompt_active(p)) return;
    p->drawn = 0;
    if (begin) {
        p->paste = 1;
        p->paste_rejected = p->browsing = p->completing = 0;
        p->selecting = 0;
    } else if (p->paste) {
        p->paste = 0;
        app_status(a, "%s", p->paste_rejected ?
                   "Paste contained rejected characters or exceeded the command limit; review before running." :
                   "Command paste captured. Review before running.");
    }
}

static void draw_editor(CommandPrompt *p)
{
    Editor *e = &p->editor;
    int total, row, col;
    ed_layout(e, p->width, &total, &row, &col);
    int maximum = total > p->rows ? total - p->rows : 0;
    if (p->scroll > maximum) p->scroll = maximum;
    if (row < p->scroll) p->scroll = row;
    if (row >= p->scroll + p->rows) p->scroll = row - p->rows + 1;
    int r = 0, c = 0;
    for (size_t i = 0; i < e->len;) {
        int advance = ed_cluster_width(e, i, p->width);
        if (advance && c + advance > p->width) { ++r; c = 0; }
        size_t end = ed_next(e, i);
        if (r >= p->scroll && r < p->scroll + p->rows) {
            move(p->first_row + r - p->scroll, c + 1);
            int width = wcwidth(e->data[i]);
            if (width < 0 || width > p->width) addch('?');
            else { if (!width) addch('?'); (void)addnwstr(e->data + i, (int)(end - i)); }
        }
        c += advance;
        if (c >= p->width) { ++r; c = 0; }
        if (r >= p->scroll + p->rows) break;
        i = end;
    }
    int y = p->first_row + row - p->scroll;
    int span = ed_cluster_width(e, e->pos, p->width);
    if (span < 1) span = 1;
    (void)mvchgat(y, col + 1, span, A_REVERSE, 0, NULL);
    move(y, col + 1);
    (void)curs_set(1);
}

/* Only a completed usable draw can release a
 * pending confirmation guard. Small-terminal warnings must not be
 * treated as an opportunity to accept buffered Enter. */
void prompt_draw(CommandPrompt *p, const App *a, PromptText text)
{
    char line[ED_MAX_BYTES + 1], label[512], detail[96];
    erase();
    p->drawn = 0;
    text(2, 0, COLS, "Command", A_BOLD);
    if (LINES < 9 || COLS < 20) {
        text(2, 0, COLS, "Resize; Esc returns.", A_NORMAL);
        (void)curs_set(1);
        move(LINES > 3 ? 3 : 0, 0);
        return;
    }
    p->first_row = 4;
    p->rows = LINES >= 14 ? 3 : 1;
    p->width = COLS - 2;
    attrset((int)vipirc_focus_attrs());
    mvhline(3, 1, ACS_HLINE, COLS - 2);
    mvhline(p->first_row + p->rows, 1, ACS_HLINE, COLS - 2);
    mvvline(p->first_row, 0, ACS_VLINE, p->rows);
    mvvline(p->first_row, COLS - 1, ACS_VLINE, p->rows);
    mvaddch(3, 0, ACS_ULCORNER);
    mvaddch(3, COLS - 1, ACS_URCORNER);
    mvaddch(p->first_row + p->rows, 0, ACS_LLCORNER);
    mvaddch(p->first_row + p->rows, COLS - 1, ACS_LRCORNER);
    attrset(A_NORMAL);
    snprintf(label, sizeof label, "On: %s", a->buffers[a->current].kind == B_SERVER ? a->cfg.server : a->buffers[a->current].name);
    text(3, 1, COLS - 2, label, vipirc_focus_attrs());
    if (ed_to_utf8(&p->editor, line, sizeof line) == SIZE_MAX) line[0] = 0;
    AppDraftMeasure measure;
    int chat = app_measure_draft(a, a->current, line, 0, &measure) && measure.budget;
    snprintf(label, sizeof label, "%zu bytes", chat ? measure.payload_bytes : p->editor.bytes);
    if (chat) {
        snprintf(detail, sizeof detail, " | text budget <=%zu/message", measure.budget);
        if (strlen(label) + strlen(detail) <= (size_t)p->width)
            strncat(label, detail, sizeof label - strlen(label) - 1);
    }
    if (p->paste && strlen(label) + 10 <= (size_t)p->width)
        strncat(label, " | PASTING", sizeof label - strlen(label) - 1);
    text(p->first_row + p->rows, 1, COLS - 2, label, vipirc_focus_attrs());
    p->suggestion_row = p->first_row + p->rows + 2;
    size_t room = suggestion_room(), total = 0;
    if (!room) p->selecting = 0;
    (void)suggestions(p, a, line, NULL, 0, 0, &total);
    if (p->selecting) {
        if (!total) { p->selecting = 0; p->selected = 0; }
        else {
            if (p->selected >= total) p->selected = total - 1u;
            if (p->selected < p->suggestion_first) p->suggestion_first = p->selected;
            if (room && p->selected >= p->suggestion_first + room)
                p->suggestion_first = p->selected - room + 1u;
        }
    }
    if (p->suggestion_first >= total) p->suggestion_first = 0;
    p->shown_first = p->suggestion_first;
    p->shown_count = 0;
    while (p->shown_count < room && p->shown_first + p->shown_count < total) {
        CommandSuggestion batch[PROMPT_PAGE_BATCH];
        size_t capacity = room - p->shown_count;
        if (capacity > PROMPT_PAGE_BATCH) capacity = PROMPT_PAGE_BATCH;
        size_t count = suggestions(p, a, line, batch, capacity,
                                  p->shown_first + p->shown_count, NULL);
        if (!count) break;
        for (size_t i = 0; i < count; ++i) {
            int row = p->suggestion_row + (int)(p->shown_count + i);
            int selected = p->selecting && p->selected == p->shown_first + p->shown_count + i;
            const char *usage = batch[i].text ? batch[i].text : batch[i].spec->usage;
            attr_t attrs = selected ? vipirc_focus_attrs() : A_BOLD;
            if (selected) text(row, 0, 1, "[", attrs);
            text(row, selected ? 1 : 0, COLS - (selected ? 2 : 0), usage, attrs);
            size_t offset = strlen(usage) + 2u;
            if (selected) {
                int end = offset < (size_t)COLS ? (int)offset - 1 : COLS - 1;
                text(row, end, 1, "]", attrs);
                ++offset;
            }
            if (!batch[i].text && offset < (size_t)COLS)
                text(row, (int)offset, COLS - (int)offset, batch[i].spec->summary, A_NORMAL);
        }
        p->shown_count += count;
    }
    if (p->shown_count)
        snprintf(label, sizeof label, "Commands %zu-%zu of %zu", p->shown_first + 1,
                 p->shown_first + p->shown_count, total);
    else if (total) snprintf(label, sizeof label, "Commands %zu; resize", total);
    else snprintf(label, sizeof label, "Commands (0 matches)");
    if (total > room && room && strlen(label) + 14 <= (size_t)COLS)
        strncat(label, " | PgUp/PgDn", sizeof label - strlen(label) - 1);
    const char *mode = commands_suggestion_mode_label(a->cfg.command_suggestions);
    if (strlen(label) + strlen(mode) + 3u <= (size_t)COLS) {
        strncat(label, " | ", sizeof label - strlen(label) - 1u);
        strncat(label, mode, sizeof label - strlen(label) - 1u);
    }
    text(p->suggestion_row - 1, 0, COLS, label, A_BOLD);
    if (!p->shown_count && room) text(p->suggestion_row, 0, COLS, "No matching command", A_DIM);
    text(LINES - 2, 0, 5, "[Run]", A_NORMAL);
    text(LINES - 2, 7, 8, "[Cancel]", A_NORMAL);
    if (a->cfg.hints && COLS >= 33) {
        const char *hint = p->selecting ? "Enter: insert | ?: help | Delete: forget" :
                           COLS >= 65 ? "Down: suggestions | Up: history | Right: complete" : "Down: suggestions";
        text(LINES - 2, 17, COLS - 17, hint, A_DIM);
    }
    text(LINES - 1, 0, COLS, a->status, A_NORMAL);
    draw_editor(p);
    if (p->selecting) { (void)curs_set(0); move(p->suggestion_row + (int)(p->selected - p->shown_first), 0); }
    p->drawn_lines = LINES;
    p->drawn_cols = COLS;
    p->drawn = 1;
    if (p->inserted) p->inserted_drawn = 1;
    config_wipe(line, sizeof line);
}

#if defined(KEY_MOUSE) && defined(BUTTON1_PRESSED)
PromptAction prompt_mouse(CommandPrompt *p, App *a, const MEVENT *event)
{
    (void)a;
    if (!prompt_active(p) || p->paste || !p->drawn || p->drawn_lines != LINES || p->drawn_cols != COLS)
        return PROMPT_NONE;
#ifdef BUTTON4_PRESSED
    if (event->bstate & BUTTON4_PRESSED) { page_suggestions(p, a, -1); return PROMPT_NONE; }
#endif
#ifdef BUTTON5_PRESSED
    if (event->bstate & BUTTON5_PRESSED) { page_suggestions(p, a, 1); return PROMPT_NONE; }
#endif
    if (!(event->bstate & BUTTON1_PRESSED)) return PROMPT_NONE;
    if (event->y == LINES - 2) {
        if (event->x >= 0 && event->x < 5) return PROMPT_RUN;
        if (event->x >= 7 && event->x < 15) return PROMPT_CANCEL;
    } else if (event->y >= p->first_row && event->y < p->first_row + p->rows &&
               event->x > 0 && event->x < COLS - 1) {
        ed_set_cell(&p->editor, p->scroll + event->y - p->first_row, event->x - 1, p->width);
        p->selecting = 0;
    } else if (event->y >= p->suggestion_row &&
               (size_t)(event->y - p->suggestion_row) < p->shown_count) {
        size_t offset = p->shown_first + (size_t)(event->y - p->suggestion_row);
        select_insert(p, a, offset);
    }
    p->browsing = p->completing = p->drawn = 0;
    return PROMPT_NONE;
}
#endif
