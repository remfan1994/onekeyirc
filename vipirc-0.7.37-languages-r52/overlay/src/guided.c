/* Curses adapter for IdleSetup, not a second configuration implementation.
 * question()/submit() translate one visible answer; guided_tick() reaps async
 * work while retaining modal input ownership; guided_draw() paints a snapshot.
 * Commit, merge, credential publication and cancellation live in idle_setup.c. */
#include "guided.h"
#include "editor_curses.h"
#include "l10n.h"
#include "idle_setup.h"
#include "editor.h"
#include "focus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int irc_label_cells(const char *s)
{
    int cells = 0;
    mbstate_t state = {0};
    while (*s && cells < 512) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, s, MB_CUR_MAX, &state);
        if (n == (size_t)-1 || n == (size_t)-2 || n == 0) {
            ++s; ++cells;
            memset(&state, 0, sizeof state);
            continue;
        }
        int w = wcwidth(wc);
        cells += w < 0 ? 1 : w;
        s += n;
    }
    return cells;
}

#define GUIDED_BODY_MAX IDLE_SETUP_TEXT_MAX
#define GUIDED_ENTER_DELAY_MS 100

/* Owns setup/editor/presentation buffers, but borrows Client. A finished
 * helper may set deferred while this object still owns paste/repeated keys. */
struct Guided {
    Client *client;
    IdleSetup *setup;
    IdleSetupView view;
    Editor editor;
    char message[768], deferred_message[768], body[GUIDED_BODY_MAX];
    int result, deferred, focus, paste, paste_accept, paste_rejected;
    int question_drawn, rows, cols, edit_top, edit_rows, edit_width, edit_scroll;
    int body_top, body_rows, body_scroll, body_count;
    int action_x[3], action_width[3];
    unsigned revision, drawn_revision;
    int64_t last_enter, settle_at;
};

static void changed(Guided *g)
{
    g->revision = (g->revision + 1u) & 0x3fffffffu;
}

static int64_t now_ms(const Guided *g)
{
    App *app = g->client->slots[g->client->current].app;
    return app ? app_now_ms(app) : 0;
}

static void notice(Guided *g, const char *message)
{
    irc_sanitize(g->message, sizeof g->message, message);
}

static void reset_editor(Guided *g)
{
    config_wipe(&g->editor, sizeof g->editor);
    ed_init(&g->editor);
    g->edit_scroll = 0;
}

static void question(Guided *g, int64_t now)
{
    reset_editor(g);
    idle_setup_view(g->setup, &g->view);
    g->focus = -1;
    g->body_scroll = 0;
    g->question_drawn = 0;
    g->last_enter = now;
    g->rows = g->cols = 0;
    notice(g, "");
    changed(g);
}

Guided *guided_create(Client *client)
{
    if (!client) return NULL;
    Guided *g = calloc(1, sizeof *g);
    if (g) { g->client = client; g->focus = -1; ed_init(&g->editor); }
    return g;
}

void guided_close(Guided *g)
{
    if (!g) return;
    idle_setup_free(g->setup); g->setup = NULL;
    reset_editor(g);
    config_wipe(&g->view, sizeof g->view);
    config_wipe(g->body, sizeof g->body);
    config_wipe(g->deferred_message, sizeof g->deferred_message);
    g->paste = g->paste_accept = g->paste_rejected = g->question_drawn = 0;
    g->result = g->deferred = g->rows = g->cols = 0;
    notice(g, "");
    changed(g);
}

void guided_free(Guided *g)
{
    if (!g) return;
    guided_close(g);
    config_wipe(g, sizeof *g);
    free(g);
}

int guided_open(Guided *g, int quick, char *error, size_t cap)
{
    if (!g) {
        if (error && cap) (void)snprintf(error, cap, "Cannot allocate the configuration panel.");
        return 0;
    }
    IdleSetup *setup = idle_setup_new(g->client, quick, error, cap);
    if (!setup) return 0;
    guided_close(g);
    g->setup = setup;
    question(g, now_ms(g));
    return 1;
}

int guided_active(const Guided *g) { return g && g->setup != NULL; }
int guided_pasting(const Guided *g) { return guided_active(g) && g->paste; }
int guided_context_help(const Guided *g, char *screen, size_t screen_cap,
                        char *item, size_t item_cap, char *detail, size_t detail_cap)
{
    if (!guided_active(g) || g->paste || g->focus < 0) return 0;
    const char *action = g->focus == 1 ? "Back" : g->focus == 2 ? "Cancel" :
                         g->view.final ? "Finish" : "Next";
    snprintf(screen, screen_cap, "%s", g->view.title);
    snprintf(item, item_cap, "%s", irc_tr(action));
    snprintf(detail, detail_cap, "%s\n\nCurrent question: %s\n%s",
        g->focus == 1 ? "Return to the previous question in this settings draft." :
        g->focus == 2 ? "Close this settings draft without applying its unfinished edits." :
        g->view.final ? "Apply the reviewed choices shown on this page. Saving and connection actions follow the choices you made." :
        "Validate the current answer and continue to the next question. Tab returns to the answer field.",
        g->view.question, g->view.detail);
    return 1;
}
unsigned guided_mouse_state(const Guided *g)
{
    return guided_active(g) ? 1u | (g->revision << 1) : 0u;
}

static void finished(Guided *g, int result, const char *message)
{
    char copy[sizeof g->message];
    irc_sanitize(copy, sizeof copy, message);
    guided_close(g);
    g->result = result;
    notice(g, copy);
    config_wipe(copy, sizeof copy);
}

int guided_take_result(Guided *g, char *message, size_t cap)
{
    if (!g || !g->result) return 0;
    int result = g->result;
    if (message && cap) (void)snprintf(message, cap, "%s", g->message);
    g->result = 0;
    notice(g, "");
    return result;
}

static void back(Guided *g, int64_t now)
{
    if (idle_setup_back(g->setup)) question(g, now);
    else finished(g, 1, "Setup cancelled.");
}

static void submit(Guided *g, int64_t now)
{
    int fresh = g->question_drawn && now - g->last_enter >= GUIDED_ENTER_DELAY_MS;
    g->last_enter = now;
    if (!fresh || idle_setup_pending(g->setup) || LINES < 14 || COLS < 32) return;
    char answer[CONFIG_PATH_MAX + 1], message[768];
    if (ed_to_utf8(&g->editor, answer, sizeof answer) == SIZE_MAX) {
        notice(g, "This answer exceeds the field byte limit.");
        return;
    }
    int result = idle_setup_answer(g->setup, answer, message, sizeof message);
    config_wipe(answer, sizeof answer);
    if (result == 1) question(g, now);
    else if (result == 2) {
        result = idle_setup_commit(g->setup, message, sizeof message);
        if (result == 1) finished(g, 2, message);
        else {
            question(g, now);
            notice(g, message);
        }
    } else notice(g, message);
    config_wipe(message, sizeof message);
}

/* Async credential completion is deliberately
 * deferred across paste/repeated-input boundaries. Finishing setup must
 * not turn trailing buffered Enter into acceptance of the next screen. */
int guided_tick(Guided *g, int64_t now)
{
    if (!guided_active(g)) return 0;
    if (g->deferred) {
        if (g->paste || now < g->settle_at) return 0;
        char message[sizeof g->deferred_message];
        int result = g->deferred;
        memcpy(message, g->deferred_message, sizeof message);
        g->deferred = 0;
        config_wipe(g->deferred_message, sizeof g->deferred_message);
        if (result > 0) finished(g, 2, message);
        else {
            question(g, now);
            notice(g, message);
        }
        config_wipe(message, sizeof message);
        return 1;
    }
    if (!idle_setup_pending(g->setup)) return 0;
    char message[768];
    int result = idle_setup_poll(g->setup, now, message, sizeof message);
    if (result) {
        /* Finish polling/reaping promptly, but retain this modal input owner
         * through a split paste and its queued suffix. Changing screens here
         * would reinterpret pasted slash/Enter bytes as new commands. */
        g->deferred = result;
        irc_sanitize(g->deferred_message, sizeof g->deferred_message, message);
        g->paste_accept = 0;
        g->settle_at = now + GUIDED_ENTER_DELAY_MS;
        reset_editor(g);
        changed(g);
    }
    config_wipe(message, sizeof message);
    return result != 0;
}

int guided_timeout(const Guided *g)
{
    if (!guided_active(g)) return -1;
    if (idle_setup_pending(g->setup)) return 50;
    if (g->deferred && !g->paste) {
        int64_t remaining = g->settle_at - now_ms(g);
        return remaining <= 0 ? 0 : remaining < 50 ? (int)remaining : 50;
    }
    return -1;
}

static int insert(Guided *g, wchar_t wc)
{
    uint32_t value = (uint32_t)wc;
    size_t bytes = value < 0x80u ? 1u : value < 0x800u ? 2u : value < 0x10000u ? 3u : 4u;
    size_t prior = g->editor.selection_all ? 0u : g->editor.bytes;
    if (value < 32u || value == 127u || prior > CONFIG_PATH_MAX - bytes) return 0;
    return ed_insert(&g->editor, wc);
}

void guided_paste(Guided *g, int begin)
{
    if (!guided_active(g)) return;
    if (begin) {
        g->paste = 1;
        g->paste_accept = !idle_setup_pending(g->setup) && !g->deferred && g->focus < 0 && g->question_drawn;
        g->paste_rejected = 0;
    } else {
        g->paste = g->paste_accept = 0;
        if (g->paste_rejected) notice(g, "Unsupported controls or excess text were rejected; review this answer.");
        g->question_drawn = 0;
        g->last_enter = now_ms(g);
        if (g->deferred) g->settle_at = g->last_enter + GUIDED_ENTER_DELAY_MS;
    }
    changed(g);
}

static void scroll_body(Guided *g, int forward)
{
    int page = g->body_rows > 0 ? g->body_rows : 1;
    int last = g->body_count > page ? g->body_count - page : 0;
    if (forward) {
        g->body_scroll += page;
        if (g->body_scroll > last) g->body_scroll = last;
    } else g->body_scroll = g->body_scroll > page ? g->body_scroll - page : 0;
}

void guided_input_discarded(Guided *g, int64_t now)
{
    if (guided_active(g) && g->deferred) g->settle_at = now + GUIDED_ENTER_DELAY_MS;
}

void guided_key(Guided *g, App *a, wint_t key, int special)
{
    if (!guided_active(g)) return;
    a->dirty = 1;
    changed(g);
    if (g->paste) {
        if (g->paste_accept && (special || !insert(g, (wchar_t)key))) g->paste_rejected = 1;
        return;
    }
    if (g->deferred) {
        g->settle_at = app_now_ms(a) + GUIDED_ENTER_DELAY_MS;
        return;
    }
    if (!special && key == 27) {
        back(g, app_now_ms(a));
        return;
    }
    if (idle_setup_pending(g->setup)) return;
    if (special && (key == KEY_PPAGE || key == KEY_NPAGE)) {
        scroll_body(g, key == KEY_NPAGE);
        return;
    }
    if ((!special && key == '\t') || (special && key == KEY_BTAB)) {
        g->focus = special ? (g->focus + 4) % 4 - 1 : (g->focus + 2) % 4 - 1;
        return;
    }
    int enter = (special && key == KEY_ENTER) || (!special && (key == '\r' || key == '\n'));
    if (enter) {
        if (g->focus == 1) back(g, app_now_ms(a));
        else if (g->focus == 2) finished(g, 1, "Setup cancelled.");
        else submit(g, app_now_ms(a));
        return;
    }
    if (g->focus >= 0) {
        if (special && (key == KEY_LEFT || key == KEY_UP)) g->focus = g->focus ? g->focus - 1 : -1;
        else if (special && (key == KEY_RIGHT || key == KEY_DOWN)) g->focus = g->focus < 2 ? g->focus + 1 : -1;
        return;
    }
    if (!g->question_drawn) return;
    if (special) switch (key) {
    case KEY_LEFT:
        if (g->view.secret) ed_left(&g->editor);
        else ed_visual_left(&g->editor, g->edit_width);
        break;
    case KEY_RIGHT:
        if (g->view.secret) ed_right(&g->editor);
        else ed_visual_right(&g->editor, g->edit_width);
        break;
    case KEY_UP:
        if (g->view.secret) ed_move_vertical(&g->editor, -1, g->edit_width);
        else ed_visual_move_vertical(&g->editor, -1, g->edit_width);
        break;
    case KEY_DOWN:
        if (g->view.secret) ed_move_vertical(&g->editor, 1, g->edit_width);
        else ed_visual_move_vertical(&g->editor, 1, g->edit_width);
        break;
    case KEY_HOME:
        if (g->view.secret) ed_home(&g->editor);
        else ed_visual_home(&g->editor, g->edit_width);
        break;
    case KEY_END:
        if (g->view.secret) ed_end(&g->editor);
        else ed_visual_end(&g->editor, g->edit_width);
        break;
    case KEY_BACKSPACE: (void)ed_backspace(&g->editor); break;
    case KEY_DC: (void)ed_delete(&g->editor); break;
    default: break;
    }
    else if (key == 8 || key == 127) (void)ed_backspace(&g->editor);
    else if (key >= 32 && !insert(g, (wchar_t)key)) notice(g, "Answer limit reached or unsupported control character.");
}

/* Keep model-supplied line boundaries but sanitize each displayed line; no
 * secret answer is ever copied into this presentation buffer. */
static size_t append_lines(char *out, size_t cap, size_t used, const char *text)
{
    char line[CONFIG_PATH_MAX + 1], safe[CONFIG_PATH_MAX + 1];
    while (*text && used + 1u < cap) {
        size_t length = strcspn(text, "\n");
        if (length >= sizeof line) length = sizeof line - 1u;
        memcpy(line, text, length);
        line[length] = 0;
        irc_sanitize(safe, sizeof safe, line);
        size_t bytes = strlen(safe);
        if (bytes >= cap - used) bytes = cap - used - 1u;
        memcpy(out + used, safe, bytes);
        used += bytes;
        text += length;
        if (*text == '\n') {
            if (used + 1u < cap) out[used++] = '\n';
            ++text;
        }
        else if (*text) break;
    }
    out[used] = 0;
    return used;
}

static void make_body(Guided *g)
{
    size_t used = append_lines(g->body, sizeof g->body, 0, g->view.detail);
    if (*g->view.summary) {
        used = append_lines(g->body, sizeof g->body, used, "\n\n");
        (void)append_lines(g->body, sizeof g->body, used, g->view.summary);
    }
}

/* Count/wrap at terminal character boundaries, keeping work bounded by the
 * fixed view buffers. Scrolling moves the explanation, never the editor. */
static int body_lines(Guided *g, SettingsText text, int paint)
{
    const char *at = g->body;
    int row = 0;
    while (*at) {
        size_t paragraph_bytes = strcspn(at, "\n");
        IrcArabicLayout *visual = irc_arabic_layout_utf8(at, paragraph_bytes, COLS, 0);
        if (visual) {
            if (paint) {
                int skip = g->body_scroll > row ? g->body_scroll - row : 0;
                int py = g->body_top + row + skip - g->body_scroll;
                int visible = g->body_top + g->body_rows - py;
                if (visible > 0 && skip < visual->rows) {
                    (void)attrset(A_NORMAL);
                    irc_curses_arabic_paint(visual, py, 0, COLS, skip, visible);
                }
            }
            row += visual->rows;
            irc_arabic_layout_free(visual);
            at += paragraph_bytes;
            if (*at == '\n') ++at;
            continue;
        }
        const char *begin = at;
        int width = 0;
        mbstate_t state;
        memset(&state, 0, sizeof state);
        while (*at && *at != '\n') {
            wchar_t wc;
            size_t n = mbrtowc(&wc, at, MB_CUR_MAX, &state);
            int cells;
            if (n == (size_t)-1 || n == (size_t)-2 || !n) {
                memset(&state, 0, sizeof state); n = 1; cells = 1;
            } else { cells = wcwidth(wc); if (cells < 0) cells = 1; }
            if (width + cells > COLS) break;
            width += cells; at += n;
        }
        if (paint && row >= g->body_scroll && row < g->body_scroll + g->body_rows) {
            char line[GUIDED_BODY_MAX];
            size_t bytes = (size_t)(at - begin);
            if (bytes >= sizeof line) bytes = sizeof line - 1u;
            memcpy(line, begin, bytes); line[bytes] = 0;
            text(g->body_top + row - g->body_scroll, 0, COLS, line, A_NORMAL);
        }
        ++row;
        if (*at == '\n') ++at;
        else if (at == begin) ++at;
    }
    return row;
}

static void draw_actions(Guided *g, SettingsText text, int pending)
{
    const char *labels[] = {irc_tr(pending ? "Working..." : g->view.final ? "Finish" : "Next"), irc_tr("Back"), irc_tr("Cancel")};
    int x = 0;
    for (size_t i = 0; i < 3; ++i) {
        g->action_x[i] = x;
        g->action_width[i] = irc_label_cells(labels[i]) + 2;
        char label[320];
        (void)snprintf(label, sizeof label, "[%s]", labels[i]);
        text(3, x, g->action_width[i], label, g->focus == (int)i ? vipirc_focus_attrs() : A_NORMAL);
        x += g->action_width[i] + 1;
    }
}

static void draw_editor(Guided *g, SettingsText text, int pending)
{
    attr_t attrs = g->focus < 0 && !pending ? vipirc_focus_attrs() : A_NORMAL;
    attrset((int)attrs);
    int top = g->edit_top - 1, bottom = g->edit_top + g->edit_rows;
    mvhline(top, 1, ACS_HLINE, COLS - 2); mvhline(bottom, 1, ACS_HLINE, COLS - 2);
    mvvline(g->edit_top, 0, ACS_VLINE, g->edit_rows); mvvline(g->edit_top, COLS - 1, ACS_VLINE, g->edit_rows);
    mvaddch(top, 0, ACS_ULCORNER); mvaddch(top, COLS - 1, ACS_URCORNER);
    mvaddch(bottom, 0, ACS_LLCORNER); mvaddch(bottom, COLS - 1, ACS_LRCORNER);
    attrset(A_NORMAL);
    text(top, 2, COLS - 4, irc_tr(pending ? "Encrypting credentials" : g->view.final ? "Choice" : "Answer"), attrs);
    IrcCursesEditorPosition visual;
    if (!g->view.secret && irc_curses_editor_visual(&g->editor, g->edit_width,
            g->edit_top, 1, g->edit_rows, &g->edit_scroll,
            g->editor.selection_all ? A_REVERSE : A_NORMAL, &visual)) {
        if (g->focus < 0 && !pending) {
            int y = g->edit_top + visual.row - visual.first, x = visual.col + 1;
            (void)mvchgat(y, x, visual.span, A_REVERSE | A_UNDERLINE, 0, NULL);
            (void)move(y, x); (void)curs_set(1);
        } else {
            (void)move(3, g->focus >= 0 ? g->action_x[g->focus] : 0);
            (void)curs_set(0);
        }
        return;
    }
    int total, caret_row, caret_col;
    ed_layout(&g->editor, g->edit_width, &total, &caret_row, &caret_col);
    if (g->edit_scroll >= total) g->edit_scroll = total - 1;
    if (g->edit_scroll < 0) g->edit_scroll = 0;
    if (caret_row < g->edit_scroll) g->edit_scroll = caret_row;
    if (caret_row >= g->edit_scroll + g->edit_rows) g->edit_scroll = caret_row - g->edit_rows + 1;
    int row = 0, column = 0;
    for (size_t i = 0; i < g->editor.len;) {
        int width = ed_cluster_width(&g->editor, i, g->edit_width);
        if (width && column + width > g->edit_width) { ++row; column = 0; }
        size_t end = ed_next(&g->editor, i);
        if (row >= g->edit_scroll && row < g->edit_scroll + g->edit_rows) {
            int y = g->edit_top + row - g->edit_scroll;
            if (g->view.secret) {
                for (int cell = 0; cell < width; ++cell) mvaddch(y, column + cell + 1, '*');
            } else {
                move(y, column + 1);
                wchar_t wc = g->editor.data[i];
                int cells = wcwidth(wc);
                if (cells < 0 || cells > g->edit_width) (void)addch('?');
                else (void)addnwstr(g->editor.data + i, (int)(end - i));
            }
        }
        column += width;
        if (column >= g->edit_width) { ++row; column = 0; }
        if (row >= g->edit_scroll + g->edit_rows) break;
        i = end;
    }
    if (g->focus < 0 && !pending) {
        int y = g->edit_top + caret_row - g->edit_scroll, x = caret_col + 1;
        (void)mvchgat(y, x, 1, A_REVERSE | A_UNDERLINE, 0, NULL);
        move(y, x); (void)curs_set(1);
    } else { move(3, g->focus >= 0 ? g->action_x[g->focus] : 0); (void)curs_set(0); }
}

/* Paint a snapshot of IdleSetup; it owns the actual
 * draft and credential jobs. A cramped display is not approval to advance
 * an unseen question, and secret answers remain masked. */
void guided_draw(Guided *g, App *a, SettingsText text)
{
    (void)a;
    if (!guided_active(g)) return;
    erase();
    g->rows = g->cols = 0;
    idle_setup_view(g->setup, &g->view);
    if (g->deferred) {
        (void)snprintf(g->view.question, sizeof g->view.question, "%s", irc_tr("Finishing setup"));
        (void)snprintf(g->view.current, sizeof g->view.current, "%s", irc_tr("Discarding pending input"));
        (void)snprintf(g->view.detail, sizeof g->view.detail, "The setup result appears after the current paste and queued input finish.");
        g->view.summary[0] = 0;
        g->view.secret = 0;
    }
    char safe[CONFIG_PATH_MAX + 160];
    irc_sanitize(safe, sizeof safe, g->view.title);
    text(2, 0, COLS, safe, A_BOLD);
    if (LINES < 14 || COLS < 32) {
        text(3, 0, COLS, irc_tr("Resize to 14 rows by 32 columns."), A_NORMAL);
        g->question_drawn = 0;
        (void)curs_set(0); move(2, 0);
        return;
    }
    int pending = idle_setup_pending(g->setup) || g->deferred;
    draw_actions(g, text, pending);
    irc_sanitize(safe, sizeof safe, g->view.question);
    text(5, 0, COLS, safe, A_BOLD);
    char current[sizeof safe];
    (void)snprintf(current, sizeof current, "%s: %s", irc_tr("Current"), g->view.current);
    irc_sanitize(safe, sizeof safe, current);
    text(6, 0, COLS, safe, A_NORMAL);
    g->edit_rows = LINES >= 18 ? 3 : 1;
    g->edit_top = LINES - g->edit_rows - 2;
    g->edit_width = COLS - 2;
    g->body_top = 7; g->body_rows = g->edit_top - g->body_top - 1;
    make_body(g);
    g->body_count = body_lines(g, text, 0);
    int last = g->body_count > g->body_rows ? g->body_count - g->body_rows : 0;
    if (g->body_scroll > last) g->body_scroll = last;
    (void)body_lines(g, text, 1);
    text(LINES - 1, 0, COLS, g->message, A_BOLD);
    draw_editor(g, text, pending);
    g->rows = LINES; g->cols = COLS; g->drawn_revision = g->revision;
    g->question_drawn = 1;
}

#if defined(KEY_MOUSE) && defined(BUTTON1_PRESSED)
void guided_mouse(Guided *g, App *a, const MEVENT *event)
{
    if (!guided_active(g) || g->paste || g->rows != LINES || g->cols != COLS ||
        g->drawn_revision != g->revision) return;
    if (g->deferred) {
        g->settle_at = app_now_ms(a) + GUIDED_ENTER_DELAY_MS;
        changed(g); a->dirty = 1; return;
    }
    if (event->bstate & BUTTON1_PRESSED) {
        if (event->y == 3) {
            for (size_t i = 0; i < 3; ++i) if (event->x >= g->action_x[i] && event->x < g->action_x[i] + g->action_width[i]) {
                g->focus = (int)i;
                if (i == 1) back(g, app_now_ms(a));
                else if (i == 2) finished(g, 1, "Setup cancelled.");
                else submit(g, app_now_ms(a));
                break;
            }
        } else if (!idle_setup_pending(g->setup) && event->y >= g->edit_top &&
                   event->y < g->edit_top + g->edit_rows && event->x >= 1 && event->x < COLS - 1) {
            g->focus = -1;
            if (g->view.secret)
                ed_set_cell(&g->editor, g->edit_scroll + event->y - g->edit_top, event->x - 1, g->edit_width);
            else
                ed_visual_set_cell(&g->editor, g->edit_scroll + event->y - g->edit_top, event->x - 1, g->edit_width);
        }
    }
#ifdef BUTTON4_PRESSED
    else if (event->bstate & BUTTON4_PRESSED) scroll_body(g, 0);
#endif
#ifdef BUTTON5_PRESSED
    else if (event->bstate & BUTTON5_PRESSED) scroll_body(g, 1);
#endif
    changed(g); a->dirty = 1;
}
#endif
