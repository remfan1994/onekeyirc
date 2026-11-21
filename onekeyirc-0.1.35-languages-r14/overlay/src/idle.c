/* Curses-free terminal frontend. Network text is data, never terminal syntax.
 *
 * Reading order: terminal_start()/output_flush() own terminal I/O;
 * line_event()/stream_output() maintain the presentation cache; draw_*()
 * build bounded frames; key_event()/input_byte() route modal input; and
 * idle_run_mode() ties timers, network, transfers and worker control together.
 * Client/App retain conversation state independently of this frontend. */
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
/* OpenBSD hides SIGWINCH under strict POSIX/XSI feature settings. */
#define _BSD_SOURCE 1
#endif
#define _XOPEN_SOURCE 700
#include "idle.h"
#include "idle_commands.h"
#include "idle_setup.h"
#include "commands.h"
#include "background.h"
#include "dcc_commands.h"
#include "editor.h"
#include "nicks.h"

#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include "l10n.h"
#include "arabic_visual.h"
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define STREAM_BYTES (512u * 1024u)
#define STREAM_LINES 2048u
#define OUTPUT_BYTES (64u * 1024u)
#define HISTORY_LINES 64u
#define HISTORY_BYTES (64u * 1024u)
#define RESPONSE_BYTES (512u * 1024u)
#define CATCHUP_POINTS 512u
#define CATCHUP_POINT_BYTES (APP_PERSISTENT_LOG_MAX / CATCHUP_POINTS + 1u)
#define SETUP_TEXT_BYTES IDLE_SETUP_TEXT_MAX
#define CONTEXT_HELP_BYTES (24u * 1024u)
#define SUGGESTION_ITEMS 128u
#define ESCAPE_MS 500
#define ESCAPE_CONTINUATION_MS 1000
#define REVIEW_DELAY_MS 250
#define SETUP_SETTLE_MS 100

typedef enum IdleMode { WATCH, SCROLLBACK, CATCHUP, COMPOSE, COMMAND, REVIEW, READING, SETUP, CONTEXT_HELP } IdleMode;
typedef enum IdleKey { IK_LEFT = 0x110000, IK_RIGHT, IK_UP, IK_DOWN,
    IK_HOME, IK_END, IK_INSERT, IK_DELETE, IK_SHIFT_TAB, IK_ESCAPE, IK_PAGE_UP, IK_PAGE_DOWN } IdleKey;
typedef struct StreamLine {
    char *text;
    size_t bytes, body_offset;
    uint64_t sequence, token;
    BufferKind kind;
    char target[201];
    int mention, status, source_hidden, mine;
} StreamLine;
typedef struct HistoryLine { char *text; uint64_t token; } HistoryLine;
typedef struct CatchupPoint { int64_t offset; size_t row; } CatchupPoint;
typedef struct Terminal {
    struct termios saved;
    int active, input_flags, output_flags, rows, cols, utf8;
} Terminal;
typedef struct Idle Idle;
static void scrollback_record_added(Idle *idle, StreamLine *line);
typedef struct IdleOffer { Idle *owner; size_t slot; uint64_t token; } IdleOffer;
typedef struct TransferSeen { uint64_t id; DccState state; } TransferSeen;
typedef struct NativeSuggestion {
    const IdleCommandSpec *spec;
    const char *text;
    size_t history_index;
    unsigned score;
} NativeSuggestion;
/* Per-frontend state. compose_token/pending_token pin the server incarnation;
 * stream sequence numbers track presentation only. A queued frame is not yet
 * visible: the event loop grants *_drawn credit only after output drains. */
struct Idle {
    Client *client;
    const char *program;
    IdleCommands commands;
    Background *background;
    DccManager *dcc;
    IdleOffer offers[CLIENT_MAX_SERVERS];
    TransferSeen transfers[DCC_MAX_ITEMS];
    Terminal tty;
    IdleMode mode, editor_return, reader_return;
    IdleSetup *setup;
    size_t setup_scroll;
    size_t scrollback_rows;
    size_t catchup_row;
    int catchup_fd;
    int64_t catchup_start, catchup_end;
    uint64_t catchup_token;
    BufferKind catchup_kind;
    char catchup_target[201];
    CatchupPoint catchup_points[CATCHUP_POINTS];
    size_t catchup_point_count, catchup_total;
    int catchup_index_valid, catchup_cols, catchup_utf8, catchup_full;
    char catchup_locale[128];
    off_t catchup_file_size;
    struct timespec catchup_mtime, catchup_ctime;
    size_t review_scroll_rows;
    uint64_t setup_batch;
    int64_t setup_enter_time;
    int setup_drawn, setup_help_focus;
    int setup_result;
    int64_t setup_settle_at;
    char setup_message[512];
    Editor editor, history_original;
    NickCompletion nick_cycle;
    StreamLine lines[STREAM_LINES];
    size_t first, count, bytes;
    uint64_t next_sequence, shown_sequence;
    uint64_t display_token, compose_token;
    BufferKind display_kind, compose_kind;
    char display_target[201], compose_target[201];
    App *attached[CLIENT_MAX_SERVERS];
    uint64_t attached_token[CLIENT_MAX_SERVERS];
    char output[OUTPUT_BYTES];
    size_t output_start, output_length;
    size_t session_notice_remaining;
    char *response;
    size_t response_length, response_offset, response_capacity, response_lines;
    int response_overflow, response_active, response_help, response_roster, response_summary;
    int response_keeps_draft;
    CommandContext response_context, suggestion_context;
    size_t suggestion_index;
    int suggestion_selected, suggestion_inserted, suggestion_drawn;
    uint64_t suggestion_batch;
    int limit_pending, limit_confirmed;
    IdleMode help_return;
    int help_parent;
    size_t help_offset;
    char help_screen[384], help_item[384], help_text[CONTEXT_HELP_BYTES], help_screen_text[4096];
    size_t help_length;
    HistoryLine history[HISTORY_LINES];
    size_t history_count, history_bytes, history_index;
    int browsing, completing;
    char completion_prefix[64];
    size_t completion_index;
    SendPlan pending;
    uint64_t pending_token;
    BufferKind pending_kind;
    int pending_requires_view, pending_from_command;
    char notice[512];
    int notice_pending;
    int redraw, alternate, pending_review_draw, review_drawn;
    int paste, paste_rejected, paste_cr;
    char escape[32];
    size_t escape_length;
    int64_t escape_time, review_time, watch_escape_after;
    int escape_background_allowed;
    uint64_t input_batch, review_batch, transition_batch, paste_batch, hotkey_batch;
    int paste_completed, paste_drawn, setup_frame;
    int64_t paste_review_at;
    int hotkey_return_guard, editor_frame, hotkey_return_drawn;
    int64_t hotkey_return_at;
    uint32_t utf8_value, utf8_minimum;
    unsigned utf8_remaining;
    int cr, quit, failed, background_requested;
};

/* Signal handlers only record intent; the main loop performs cleanup. */
static volatile sig_atomic_t stop_signal, hangup_signal, suspend_signal, resize_signal;
static const int handled_signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP, SIGWINCH};

static int64_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void signal_handler(int n)
{
    if (n == SIGTSTP) suspend_signal = 1;
    else if (n == SIGWINCH) resize_signal = 1;
    else if (n == SIGHUP) hangup_signal = 1;
    else stop_signal = n;
}

static size_t install_signals(struct sigaction *previous)
{
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    stop_signal = hangup_signal = suspend_signal = resize_signal = 0;
    size_t i;
    for (i = 0; i < sizeof handled_signals / sizeof handled_signals[0]; ++i)
        if (sigaction(handled_signals[i], &action, &previous[i]) < 0) break;
    return i;
}

static void restore_signals(struct sigaction *previous, size_t count)
{
    while (count) {
        --count;
        (void)sigaction(handled_signals[count], &previous[count], NULL);
    }
}

static void geometry(Terminal *tty)
{
    struct winsize size;
    memset(&size, 0, sizeof size);
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) < 0) memset(&size, 0, sizeof size);
    tty->rows = size.ws_row ? (int)size.ws_row : 24;
    tty->cols = size.ws_col ? (int)size.ws_col : 80;
    /* Work and output bounds do not depend on a peer-supplied window size. */
    if (tty->rows > 200) tty->rows = 200;
    if (tty->cols > 500) tty->cols = 500;
}

static int utf8_locale(void)
{
    const char *name = nl_langinfo(CODESET);
    char compact[16];
    size_t n = 0;
    for (; *name && n + 1 < sizeof compact; ++name) {
        if (*name == '-' || *name == '_') continue;
        compact[n++] = *name >= 'a' && *name <= 'z' ? (char)(*name - 'a' + 'A') : *name;
    }
    compact[n] = 0;
    return !*name && !strcmp(compact, "UTF8");
}

/* Retain the exact previous termios and fd flags
 * for teardown. This frontend is intentionally not a readline shell:
 * signal key mappings remain owned by the launching terminal settings. */
static int terminal_start(Terminal *tty, int force_terminal_signals)
{
    const char *term = getenv("TERM");
    if (!term || !*term || !strcmp(term, "dumb") || !strcmp(term, "unknown") ||
        !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return 0;
    if (tcgetattr(STDIN_FILENO, &tty->saved) < 0) return 0;
    tty->input_flags = fcntl(STDIN_FILENO, F_GETFL);
    tty->output_flags = fcntl(STDOUT_FILENO, F_GETFL);
    if (tty->input_flags < 0 || tty->output_flags < 0) return 0;
    struct termios mode = tty->saved;
    mode.c_lflag &= (tcflag_t)~(ICANON | ECHO | IEXTEN);
    /* Keep the terminal's signal policy and character assignments unless
     * the user explicitly requests enabling signals. Never bind key bytes. */
    if (force_terminal_signals) mode.c_lflag |= ISIG;
    mode.c_iflag &= (tcflag_t)~(ICRNL | INLCR | IGNCR);
    mode.c_oflag &= (tcflag_t)~OPOST;
    mode.c_cc[VMIN] = 1;
    mode.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &mode) < 0) return 0;
    if (fcntl(STDIN_FILENO, F_SETFL, tty->input_flags | O_NONBLOCK) < 0 ||
        fcntl(STDOUT_FILENO, F_SETFL, tty->output_flags | O_NONBLOCK) < 0) {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &tty->saved);
        (void)fcntl(STDIN_FILENO, F_SETFL, tty->input_flags);
        (void)fcntl(STDOUT_FILENO, F_SETFL, tty->output_flags);
        return 0;
    }
    tty->active = 1;
    tty->utf8 = utf8_locale();
    geometry(tty);
    return 1;
}

/* Only terminal restoration may wait briefly: ordinary output never blocks
 * the network loop. A stalled terminal cannot hold exit/suspend indefinitely. */
static void restoration_write(const char *text)
{
    size_t left = strlen(text);
    int64_t deadline = now_ms() + 250;
    while (left && now_ms() < deadline) {
        ssize_t n = write(STDOUT_FILENO, text, left);
        if (n > 0) {
            text += n;
            left -= (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd p = {STDOUT_FILENO, POLLOUT, 0};
            (void)poll(&p, 1, 20);
            continue;
        }
        break;
    }
}

static void terminal_stop(Terminal *tty, int alternate)
{
    if (!tty->active) return;
    restoration_write(alternate ? "\033[?1049l\033[0m\033[?25h\033[?2004l\r\n" :
                                  "\033[0m\033[?25h\033[?2004l\r\n");
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &tty->saved);
    (void)fcntl(STDIN_FILENO, F_SETFL, tty->input_flags);
    (void)fcntl(STDOUT_FILENO, F_SETFL, tty->output_flags);
    tty->active = 0;
}

static void suspend_process(void)
{
    struct sigaction action, previous;
    memset(&action, 0, sizeof action);
    sigemptyset(&action.sa_mask);
    action.sa_handler = SIG_DFL;
    (void)sigaction(SIGTSTP, &action, &previous);
    (void)raise(SIGTSTP);
    (void)sigaction(SIGTSTP, &previous, NULL);
}

/* Output queue: append complete fragments or leave the queue unchanged.
 * output_flush() has its own per-turn budget so a writable tty cannot starve
 * network timers. Its success result includes deferred EAGAIN/EINTR work. */
static int output_add(Idle *idle, const char *text, size_t n)
{
    if (n > sizeof idle->output - idle->output_length) return 0;
    if (idle->output_start + idle->output_length + n > sizeof idle->output) {
        memmove(idle->output, idle->output + idle->output_start, idle->output_length);
        idle->output_start = 0;
    }
    memcpy(idle->output + idle->output_start + idle->output_length, text, n);
    idle->output_length += n;
    return 1;
}

static int output_literal(Idle *idle, const char *text)
{
    return output_add(idle, text, strlen(text));
}

/* Advance only after write confirms bytes accepted.
 * A blocked or short write keeps the unsent suffix and cannot mark a
 * session notice visible; poll for progress instead of spinning. */
static int output_flush(Idle *idle)
{
    size_t allowance = 16384;
    while (idle->output_length && allowance) {
        size_t amount = idle->output_length < allowance ? idle->output_length : allowance;
        ssize_t n = write(STDOUT_FILENO, idle->output + idle->output_start, amount);
        if (n > 0) {
            if (idle->session_notice_remaining) {
                if ((size_t)n >= idle->session_notice_remaining) {
                    idle->session_notice_remaining = 0;
                    idle->client->session_notice_shown = 1;
                } else idle->session_notice_remaining -= (size_t)n;
            }
            idle->output_start += (size_t)n;
            idle->output_length -= (size_t)n;
            allowance -= (size_t)n;
        } else if (n < 0 && errno == EINTR) break;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        else return 0;
    }
    if (!idle->output_length) idle->output_start = 0;
    return 1;
}

static size_t encode(uint32_t c, char out[4])
{
    if (c < 128) { out[0] = (char)c; return 1; }
    if (c < 2048) { out[0] = (char)(0xc0u | (c >> 6)); out[1] = (char)(0x80u | (c & 63u)); return 2; }
    if (c < 65536) {
        out[0] = (char)(0xe0u | (c >> 12)); out[1] = (char)(0x80u | ((c >> 6) & 63u));
        out[2] = (char)(0x80u | (c & 63u)); return 3;
    }
    out[0] = (char)(0xf0u | (c >> 18)); out[1] = (char)(0x80u | ((c >> 12) & 63u));
    out[2] = (char)(0x80u | ((c >> 6) & 63u)); out[3] = (char)(0x80u | (c & 63u)); return 4;
}

/* Input is sanitized valid UTF-8. ASCII display consumes complete scalars. */
static size_t display_text(const Idle *idle, char *out, size_t cap, const char *text)
{
    size_t used = 0;
    for (size_t i = 0; text[i] && used + 1 < cap;) {
        unsigned char c = (unsigned char)text[i];
        size_t n = c < 128 ? 1u : c < 0xe0 ? 2u : c < 0xf0 ? 3u : 4u;
        if (!idle->tty.utf8 && c >= 128) { out[used++] = '?'; i += n; }
        else {
            if (n > cap - used - 1) break;
            memcpy(out + used, text + i, n); used += n; i += n;
        }
    }
    out[used] = 0;
    return used;
}

static App *current_app(Idle *idle)
{
    if (idle->client->current >= CLIENT_MAX_SERVERS) return NULL;
    return idle->client->slots[idle->client->current].app;
}

static ClientSlot *slot_for_app(Idle *idle, const App *app)
{
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (idle->client->slots[i].used && idle->client->slots[i].app == app)
            return &idle->client->slots[i];
    return NULL;
}

static App *app_for_token(const Idle *idle, uint64_t token)
{
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (idle->client->slots[i].used && idle->client->slots[i].token == token)
            return idle->client->slots[i].app;
    return NULL;
}

static size_t named_view(const App *app, BufferKind kind, const char *name)
{
    if (!app) return SIZE_MAX;
    for (size_t i = 0; i < app->buffer_count; ++i)
        if (app->buffers[i].kind == kind && irc_equal(app->buffers[i].name, name, app->casemap)) return i;
    return SIZE_MAX;
}

static void notice(Idle *idle, const char *message)
{
    irc_sanitize(idle->notice, sizeof idle->notice, message);
    idle->notice_pending = 1;
    if (idle->mode == REVIEW) {
        idle->pending_review_draw = 1;
        idle->review_drawn = 0;
    }
    idle->redraw = 1;
}

/* Retained presentation records and App event adapters. */
static void stream_pop(Idle *idle)
{
    StreamLine *line = &idle->lines[idle->first];
    idle->bytes -= line->bytes;
    free(line->text);
    memset(line, 0, sizeof *line);
    idle->first = (idle->first + 1) % STREAM_LINES;
    --idle->count;
}

static StreamLine *stream_store(Idle *idle, uint64_t token, BufferKind kind, const char *target,
                               int mention, int status, int mine, const char *text)
{
    char clean[APP_TEXT + 2048];
    irc_sanitize(clean, sizeof clean, text);
    size_t bytes = strlen(clean) + 1;
    char *copy = malloc(bytes);
    if (!copy) { notice(idle, "A stream line could not be retained (allocation failed)."); return NULL; }
    memcpy(copy, clean, bytes);
    const ClientSlot *primary = &idle->client->slots[0];
    int configured = primary->app ? primary->app->cfg.history_lines : primary->seed.history_lines;
    size_t limit = configured > 0 ? (size_t)configured : 100u;
    if (limit > STREAM_LINES) limit = STREAM_LINES;
    while (idle->count && (idle->count >= limit || idle->bytes > STREAM_BYTES - bytes)) stream_pop(idle);
    StreamLine *line = &idle->lines[(idle->first + idle->count) % STREAM_LINES];
    memset(line, 0, sizeof *line);
    line->text = copy; line->bytes = bytes;
    line->sequence = idle->next_sequence++;
    line->token = token; line->kind = kind; line->mention = mention; line->status = status; line->mine = mine;
    (void)snprintf(line->target, sizeof line->target, "%s", target ? target : "");
    ++idle->count; idle->bytes += bytes;
    if (idle->mode == COMPOSE || idle->mode == COMMAND) idle->redraw = 1;
    return line;
}

static void line_event(void *context, const App *app, size_t view, const char *text, int mention, int mine)
{
    Idle *idle = context;
    if (view >= app->buffer_count || app->buffers[view].kind == B_MENTIONS || app->buffers[view].kind == B_HELP) return;
    ClientSlot *slot = slot_for_app(idle, app);
    if (!slot) return;
    char line[APP_TEXT + 2048];
    int prefix = snprintf(line, sizeof line, "[S%zu %s %s] ", slot->index + 1u, app->cfg.server,
                          app->buffers[view].name);
    if (prefix < 0 || (size_t)prefix >= sizeof line) return;
    (void)snprintf(line + prefix, sizeof line - (size_t)prefix, "%s", text);
    StreamLine *stored = stream_store(idle, slot->token, app->buffers[view].kind,
                                     app->buffers[view].name, mention, 0, mine, line);
    if (stored) {
        stored->body_offset = (size_t)prefix;
        scrollback_record_added(idle, stored);
    }
}

static void status_event(void *context, const App *app, const char *text)
{
    Idle *idle = context;
    if (app->status_kind == APP_STATUS_QUEUE_NOTICE && !app->cfg.queue_notices) return;
    /* Review mode already supplied an explicit send confirmation.  A routine
     * queue acknowledgement after that confirmation is redundant in the
     * minimalist transcript and used to become another permanent log line.
     * Keep queue notices for direct sends, where they are still useful. */
    if (app->status_kind == APP_STATUS_QUEUE_NOTICE && idle->mode == REVIEW) return;
    if (app->status_kind == APP_STATUS_PROGRAM_NOTICE && !app->cfg.program_messages) return;
    ClientSlot *slot = slot_for_app(idle, app);
    if (!slot) return;
    char line[1024];
    (void)snprintf(line, sizeof line, "[S%zu] %s", slot->index + 1u, text);
    StreamLine *stored = stream_store(idle, slot->token, B_SERVER, "server", 0, 1, 0, line);
    if (stored) scrollback_record_added(idle, stored);
    if (idle->mode == COMPOSE || idle->mode == COMMAND) notice(idle, line);
}

static void command_print(void *context, const char *text)
{
    Idle *idle = context;
    if (idle->mode == COMMAND) {
        char safe[APP_TEXT + 2048];
        irc_sanitize(safe, sizeof safe, text);
        size_t length = strlen(safe);
        if (length + 1 <= RESPONSE_BYTES - idle->response_length) {
            size_t wanted = idle->response_length + length + 1;
            if (wanted > idle->response_capacity) {
                size_t capacity = idle->response_capacity ? idle->response_capacity : 4096;
                while (capacity < wanted && capacity < RESPONSE_BYTES) capacity *= 2;
                if (capacity > RESPONSE_BYTES) capacity = RESPONSE_BYTES;
                char *grown = realloc(idle->response, capacity);
                if (grown) { idle->response = grown; idle->response_capacity = capacity; }
            }
            if (wanted <= idle->response_capacity) {
                memcpy(idle->response + idle->response_length, safe, length);
                idle->response_length += length;
                idle->response[idle->response_length++] = '\n';
                idle->response_active = 1;
                ++idle->response_lines;
            } else idle->response_overflow = 1;
        } else idle->response_overflow = 1;
        if (idle->response_overflow) idle->response_active = 1;
        notice(idle, safe);
        return;
    }
    App *app = current_app(idle);
    ClientSlot *slot = app ? slot_for_app(idle, app) : NULL;
    const Buffer *view = app && app->current < app->buffer_count ? &app->buffers[app->current] : NULL;
    StreamLine *stored = stream_store(idle, slot ? slot->token : 0, view ? view->kind : B_SERVER,
                                      view ? view->name : "server", 0, 1, 0, text);
    if (stored) scrollback_record_added(idle, stored);
}

static void program_print(Idle *idle, const char *text)
{
    App *app = current_app(idle);
    if (app && !app->cfg.program_messages) return;
    command_print(idle, text);
}

static void response_reset(Idle *idle)
{
    if (idle->response) config_wipe(idle->response, idle->response_capacity);
    free(idle->response); idle->response = NULL;
    idle->response_length = idle->response_offset = idle->response_capacity = 0;
    idle->response_lines = 0;
    idle->response_active = idle->response_overflow = idle->response_help = idle->response_roster = idle->response_summary = 0;
    idle->response_keeps_draft = 0;
    idle->response_context = COMMAND_CONTEXT_CLIENT;
}

static void response_notice(Idle *idle)
{
    char text[sizeof idle->notice];
    size_t n = 0;
    while (n < idle->response_length && n + 1 < sizeof text && idle->response[n] != '\n') ++n;
    if (n) memcpy(text, idle->response, n);
    text[n] = 0;
    response_reset(idle);
    if (n) notice(idle, text);
}

static int incoming_file(void *context, const char *nick, const char *body)
{
    IdleOffer *offer = context;
    Idle *idle = offer->owner;
    ClientSlot *slot = &idle->client->slots[offer->slot];
    if (!slot->used || !slot->app || slot->token != offer->token) return 1;
    if (!idle->dcc) idle->dcc = dcc_create(idle->client->test_mode);
    if (!idle->dcc) { app_status(slot->app, "Cannot retain the file offer: out of memory."); return 1; }
    uint64_t id;
    char error[256];
    if (!dcc_offer(idle->dcc, slot->token, nick, body, &id, error, sizeof error)) {
        app_status(slot->app, "File offer from %s rejected: %s", nick, error); return 1;
    }
    const DccInfo *info = dcc_get(idle->dcc, id);
    app_status(slot->app, "File offer #%llu from %s: %s (%u bytes), peer %s:%u. /dcc list shows details; acceptance is explicit.",
               (unsigned long long)id, nick, info ? info->filename : "file", info ? info->size : 0,
               info ? info->address : "?", info ? (unsigned)info->port : 0u);
    return 1;
}

static void transfer_changes(Idle *idle, int announce)
{
    if (!idle->dcc) return;
    TransferSeen next[DCC_MAX_ITEMS];
    memset(next, 0, sizeof next);
    for (size_t i = 0; i < dcc_count(idle->dcc) && i < DCC_MAX_ITEMS; ++i) {
        const DccInfo *info = dcc_at(idle->dcc, i);
        next[i].id = info->id; next[i].state = info->state;
        int changed = 0;
        for (size_t j = 0; j < DCC_MAX_ITEMS; ++j)
            if (idle->transfers[j].id == info->id) { changed = idle->transfers[j].state != info->state; break; }
        App *app = app_for_token(idle, info->server_token);
        if (info->outgoing && info->state >= DCC_DONE && app)
            (void)app_cancel_dcc_offer(app, info->id);
        if (announce && changed && info->state >= DCC_DONE && app)
            app_status(app, "File transfer #%llu %s: %s%s%s", (unsigned long long)info->id,
                       dcc_state_name(info->state), info->filename, *info->error ? ": " : "", info->error);
    }
    memcpy(idle->transfers, next, sizeof next);
    (void)dcc_changed(idle->dcc);
}

static void attach_apps(Idle *idle)
{
    for (size_t slot = 0; slot < CLIENT_MAX_SERVERS; ++slot) {
        ClientSlot *entry = &idle->client->slots[slot];
        App *app = entry->app;
        if (idle->attached_token[slot] && (!entry->used || entry->token != idle->attached_token[slot])) {
            if (idle->dcc) dcc_cancel_server(idle->dcc, idle->attached_token[slot]);
            idle->attached[slot] = NULL; idle->attached_token[slot] = 0;
        }
        if (!app || (idle->attached[slot] == app && idle->attached_token[slot] == entry->token)) continue;
        idle->attached[slot] = app; idle->attached_token[slot] = entry->token;
        app->line_event = line_event; app->status_event = status_event; app->event_context = idle;
        idle->offers[slot] = (IdleOffer){idle, slot, entry->token};
        app->dcc_offer = incoming_file; app->dcc_context = &idle->offers[slot];
        for (size_t view = 0; view < app->buffer_count; ++view) {
            Buffer *buffer = &app->buffers[view];
            for (size_t n = 0; n < buffer->line_count; ++n) {
                int mine = buffer->last_own_retained &&
                           n + buffer->last_own_from_end + 1u == buffer->line_count;
                line_event(idle, app, view, buffer->lines[(buffer->line_start + n) % buffer->line_cap], 0, mine);
            }
        }
        /* The App owns canonical bounded scrollback. The minimalist stream is
         * a presentation cache only, so a later ncurses attachment sees the
         * same retained session history. */
        app->stream_only = 0;
        if (*app_status_text(app)) status_event(idle, app, app_status_text(app));
    }
}

static int record_matches(const Idle *idle, const StreamLine *line)
{
    if (!line->text) return 0;
    if (line->status && !line->source_hidden) return 1;
    if (line->token != idle->display_token) return 0;
    if (idle->display_kind == B_MENTIONS) return line->mention;
    return !line->source_hidden && line->kind == idle->display_kind && !strcmp(line->target, idle->display_target);
}

/* Compact the same timestamp/history format in retained and disk-backed
 * views. Server timestamps stay explicitly UTC; local timestamps keep their
 * existing form. Leave unfamiliar records intact. */
static int compact_record(const char *body, BufferKind kind, const char *target,
                          char *out, size_t cap)
{
    size_t length = strlen(body), prefix;
    if (length < 22 || body[0] != '[' || body[5] != '-' || body[8] != '-' ||
        body[11] != ' ' || body[14] != ':' || body[17] != ':') return 0;
    if (body[20] == ']' && body[21] == ' ') prefix = 22;
    else if (length >= 26 && !memcmp(body + 20, " UTC] ", 6)) prefix = 26;
    else return 0;
    for (size_t i = 1; i < 20; ++i) {
        if (i == 5 || i == 8 || i == 11 || i == 14 || i == 17) continue;
        if (body[i] < '0' || body[i] > '9') return 0;
    }
    const char *message = body + prefix, *history = "";
    if (!strncmp(message, "[history] ", 10)) {
        history = "[history] ";
        message += 10;
    }
    if ((kind == B_CHANNEL || kind == B_QUERY) && *message == '[') {
        const char *target_end = strstr(message, "] ");
        if (target_end && (target_end[2] == '<' || target_end[2] == '-' ||
                           (target_end[2] == '*' && target_end[3] == ' '))) message = target_end + 2;
    }
    const char *zone = prefix == 26 ? " UTC" : "";
    if (target) (void)snprintf(out, cap, "[%.8s%s] %s[%s] %s", body + 12, zone, history, target, message);
    else (void)snprintf(out, cap, "[%.8s%s] %s%s", body + 12, zone, history, message);
    return 1;
}

/* Retain the complete record once. Presentation can change without rewriting
 * history, and a later full-format replay still has its original origin/date. */
static const char *stream_text(const Idle *idle, const StreamLine *line, char *out, size_t cap)
{
    const ClientSlot *primary = &idle->client->slots[0];
    int full = primary->app ? primary->app->cfg.output_full : primary->seed.output_full;
    if (full || !line->body_offset || line->body_offset >= line->bytes) return line->text;
    const char *target = idle->display_kind == B_MENTIONS ? line->target : NULL;
    return compact_record(line->text + line->body_offset, line->kind, target, out, cap) ? out : line->text;
}

static void context_update(Idle *idle, int force)
{
    App *app = current_app(idle);
    ClientSlot *slot = app ? slot_for_app(idle, app) : NULL;
    if (!app || !slot || app->current >= app->buffer_count) return;
    Buffer *buffer = &app->buffers[app->current];
    if (!force && idle->display_token == slot->token && idle->display_kind == buffer->kind &&
        !strcmp(idle->display_target, buffer->name)) return;
    idle->display_token = slot->token; idle->display_kind = buffer->kind;
    (void)snprintf(idle->display_target, sizeof idle->display_target, "%s", buffer->name);
    /* Replay only a recent bounded tail. Already printed context need not
     * be copied into another history allocation. */
    size_t matches = 0;
    idle->shown_sequence = idle->next_sequence;
    for (size_t n = idle->count; n; --n) {
        StreamLine *line = &idle->lines[(idle->first + n - 1) % STREAM_LINES];
        if (record_matches(idle, line)) {
            idle->shown_sequence = line->sequence;
            if (++matches == 40) break;
        }
    }
    char heading[512];
    (void)snprintf(heading, sizeof heading, "-- S%zu %s | %s --", slot->index + 1u,
                   *app->cfg.server ? app->cfg.server : "disconnected", buffer->name);
    command_print(idle, heading);
}

static int stream_display_output(Idle *idle, const char *text);

static void stream_output(Idle *idle)
{
    /* Alternate-screen interfaces own the display while they are active.
     * Incoming records remain retained and are emitted when WATCH resumes. */
    if (idle->mode != WATCH || background_detached(idle->background)) return;
    if (idle->notice_pending && *idle->notice) {
        if (!stream_display_output(idle, idle->notice)) return;
        idle->notice_pending = 0;
    }
    /* A single local transcript line needs neither a modal prompt nor space
     * in the input field. Operational notices retain priority. Count bytes
     * ahead of this line too; only a completed write consumes the reminder. */
    if (!idle->client->session_notice_shown && !idle->session_notice_remaining) {
        if (!output_literal(idle, CLIENT_SESSION_NOTICE "\r\n")) return;
        idle->session_notice_remaining = idle->output_length;
    }
    if (idle->count && idle->shown_sequence < idle->lines[idle->first].sequence) {
        char lost[128];
        (void)snprintf(lost, sizeof lost, "[stream: %llu earlier records omitted at the bounded history limit]\r\n",
                       (unsigned long long)(idle->lines[idle->first].sequence - idle->shown_sequence));
        if (!output_literal(idle, lost)) return;
        idle->shown_sequence = idle->lines[idle->first].sequence;
    }
    /* Stored sequences are consecutive even when a cleared record has no
     * text. Jump over the already displayed prefix instead of rescanning
     * retained history at every timer/network wakeup. Keep the old bounded
     * walk if the counter wraps across the current ring. */
    size_t start = 0;
    if (idle->count && idle->next_sequence >= idle->lines[idle->first].sequence &&
        idle->shown_sequence >= idle->lines[idle->first].sequence) {
        uint64_t skipped = idle->shown_sequence - idle->lines[idle->first].sequence;
        if (skipped >= idle->count) return;
        start = (size_t)skipped;
    }
    size_t emitted = 0;
    for (size_t n = start; n < idle->count && emitted < 64; ++n) {
        StreamLine *line = &idle->lines[(idle->first + n) % STREAM_LINES];
        if (line->sequence < idle->shown_sequence) continue;
        if (record_matches(idle, line)) {
            char compact[APP_TEXT + 2048];
            if (!stream_display_output(idle,
                    stream_text(idle, line, compact, sizeof compact))) return;
            ++emitted;
        }
        idle->shown_sequence = line->sequence + 1;
    }
}

static void clear_stream(Idle *idle)
{
    for (size_t n = 0; n < idle->count; ++n) {
        StreamLine *line = &idle->lines[(idle->first + n) % STREAM_LINES];
        if (line->token != idle->display_token || !line->text) continue;
        if (idle->display_kind == B_MENTIONS) line->mention = 0;
        else if (line->kind == idle->display_kind && !strcmp(line->target, idle->display_target))
            line->source_hidden = 1;
        if (line->source_hidden && !line->mention) {
            idle->bytes -= line->bytes; free(line->text); line->text = NULL; line->bytes = 0;
        }
    }
    idle->shown_sequence = idle->next_sequence;
    program_print(idle, "Retained stream records for this conversation cleared.");
}

static void clear_stream_mentions(Idle *idle)
{
    for (size_t n = 0; n < idle->count; ++n) {
        StreamLine *line = &idle->lines[(idle->first + n) % STREAM_LINES];
        if (!line->text) continue;
        line->mention = 0;
        /* A source hidden by an earlier /clear was retained only for its
         * mention copy. Other conversation records stay available. */
        if (line->source_hidden) {
            idle->bytes -= line->bytes; free(line->text);
            line->text = NULL; line->bytes = 0;
        }
    }
}

typedef struct Frame { char data[OUTPUT_BYTES]; size_t used; int overflow; } Frame;

/* Bounded screen construction and readers; these helpers queue presentation. */
static void frame_add(Frame *frame, const char *text, size_t n)
{
    if (n > sizeof frame->data - frame->used) { frame->overflow = 1; return; }
    memcpy(frame->data + frame->used, text, n); frame->used += n;
}

static void frame_printf(Frame *frame, const char *format, ...)
{
    char part[1024];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(part, sizeof part, format, args);
    va_end(args);
    if (n < 0) return;
    frame_add(frame, part, (size_t)n < sizeof part ? (size_t)n : sizeof part - 1);
}

/* Paint a shared bidi line left-to-right into the terminal's physical cells.
 * Combining marks immediately follow their base; the map retains the logical
 * source offsets used by readers and the input editor. */
static void frame_arabic_line(Frame *frame, const IrcArabicLayout *map, int line, int screen_row)
{
    const IrcArabicLine *range = &map->lines[line];
    int col = 0;
    for (size_t i = range->glyph_first; i < range->glyph_past; ++i) {
        const IrcArabicGlyph *glyph = &map->glyphs[i];
        if (screen_row > 0 && col < glyph->col) {
            frame_printf(frame, "\033[%d;%dH", screen_row, glyph->col + 1);
            col = glyph->col;
        } else while (col < glyph->col) { frame_add(frame, " ", 1); ++col; }
        char encoded[4]; size_t bytes = encode(glyph->codepoint, encoded);
        frame_add(frame, encoded, bytes);
        if (glyph->cells) col = glyph->col + glyph->cells;
    }
}

/* Draw short labels inside their existing rectangle. The same bounded layout
 * fits a logical prefix before reordering, so RTL clipping keeps its start. */
static void frame_text(Idle *idle, Frame *frame, int row, const char *text)
{
    char safe[2048], shown[2048];
    irc_sanitize(safe, sizeof safe, text);
    size_t n = display_text(idle, shown, sizeof shown, safe);
    int width = idle->tty.cols > 1 ? idle->tty.cols - 1 : 1;
    frame_printf(frame, "\033[%d;1H", row);
    if (idle->tty.utf8) {
        IrcArabicLayout *map = irc_arabic_layout_utf8(shown, n, width, IRC_ARABIC_SINGLE_LINE);
        if (map) {
            if (map->rows) frame_arabic_line(frame, map, 0, row);
            irc_arabic_layout_free(map);
            return;
        }
    }
    size_t stop = 0;
    int cells = 0;
    while (stop < n) {
        uint32_t cp; size_t bytes = irc_arabic_decode(shown + stop, n - stop, &cp);
        if (!bytes) break;
        int span = idle->tty.utf8 ? wcwidth((wchar_t)cp) : 1;
        if (span < 0) span = 1;
        if (span > width || cells + span > width) break;
        cells += span; stop += bytes;
    }
    frame_add(frame, shown, stop);
}

/* A scoped iterator keeps one immutable paragraph map while its rows are
 * counted, painted or traversed backwards. A failed map is also remembered
 * for this paragraph, so a frame cannot switch layout midway through it.
 * Never retain this object across events or reuse it after editing its source;
 * clear it before a scratch buffer is refilled and on every exit path. */
typedef struct TextRows {
    const char *text;
    size_t length, first, past;
    int width, active, valid;
    IrcArabicLayout *map;
} TextRows;

static void text_rows_clear(TextRows *rows)
{
    irc_arabic_layout_free(rows->map);
    memset(rows, 0, sizeof *rows);
}

static IrcArabicLayout *text_rows_prepare(Idle *idle, TextRows *rows,
        const char *text, size_t length, size_t start)
{
    int width = idle->tty.cols > 1 ? idle->tty.cols - 1 : 1;
    int active = idle->tty.utf8 && irc_arabic_visual_selected() && irc_arabic_utf8_locale();
    if (start >= length) { text_rows_clear(rows); return NULL; }
    if (rows->valid && rows->text == text && rows->length == length &&
        rows->width == width && rows->active == active &&
        start >= rows->first && start <= rows->past) return rows->map;
    text_rows_clear(rows);
    if (!active) return NULL;
    rows->text = text; rows->length = length; rows->width = width;
    rows->active = active; rows->valid = 1;
    rows->first = rows->past = start;
    while (rows->first && text[rows->first - 1] != '\n') --rows->first;
    while (rows->past < length && text[rows->past] != '\n') ++rows->past;
    rows->map = irc_arabic_layout_utf8(text + rows->first,
                    rows->past - rows->first, width, 0);
    return rows->map;
}

static int text_rows_line(const IrcArabicLayout *map, size_t offset)
{
    int lower = 0, upper = map->rows;
    while (lower < upper) {
        int middle = lower + (upper - lower) / 2;
        if (map->lines[middle].byte_past <= offset) lower = middle + 1;
        else upper = middle;
    }
    return lower < map->rows ? lower : map->rows - 1;
}

/* Paint one wrapped row from sanitized logical text without allocating a
 * second transcript. The iterator owns only one bounded paragraph map. */
static size_t text_rows_next(Idle *idle, TextRows *rows,
        const char *text, size_t length, size_t start, Frame *frame, int row)
{
    if (start > length) start = length;
    size_t at = start;
    int width = idle->tty.cols > 1 ? idle->tty.cols - 1 : 1, cells = 0;
    /* row zero is an append-only WATCH line, with no cursor addressing. */
    if (frame && row > 0) frame_printf(frame, "\033[%d;1H", row);
    IrcArabicLayout *map = text_rows_prepare(idle, rows, text, length, start);
    if (map) {
        int line = text_rows_line(map, start - rows->first);
        size_t next = rows->first + map->lines[line].byte_past;
        if (next > start || next == rows->past) {
            if (frame) frame_arabic_line(frame, map, line, row);
            if (next == rows->past && next < length && text[next] == '\n') ++next;
            return next;
        }
    }
    while (at < length && text[at] != '\n') {
        const unsigned char *s = (const unsigned char *)text + at;
        size_t n = *s < 128 ? 1u : *s < 0xe0 ? 2u : *s < 0xf0 ? 3u : 4u;
        if (n > length - at) n = 1;
        uint32_t value = n == 1 ? *s : (uint32_t)(*s & (n == 2 ? 31u : n == 3 ? 15u : 7u));
        for (size_t i = 1; i < n; ++i) value = (value << 6) | (s[i] & 63u);
        int substitute = !idle->tty.utf8 && value >= 128;
        int span = substitute ? 1 : wcwidth((wchar_t)value);
        if (span < 0 || span > width) { span = 1; substitute = 1; }
        if (span && cells + span > width) break;
        if (frame) {
            const char *piece = substitute ? "?" : text + at;
            size_t part = substitute ? 1u : n;
            frame_add(frame, piece, part);
        }
        cells += span; at += n;
    }
    if (at < length && text[at] == '\n') ++at;
    return at;
}

/* Single-row adapter retained for callers that do not traverse a paragraph. */
static inline size_t text_row(Idle *idle, const char *text, size_t length,
                              size_t start, Frame *frame, int row)
{
    TextRows rows = {0};
    size_t next = text_rows_next(idle, &rows, text, length, start, frame, row);
    text_rows_clear(&rows);
    return next;
}

static size_t text_rows_previous(Idle *idle, TextRows *rows,
        const char *text, size_t length, size_t offset)
{
    if (offset > length) offset = length;
    if (!offset) return 0;
    IrcArabicLayout *map = text_rows_prepare(idle, rows, text, length, offset - 1);
    if (map) {
        int line = text_rows_line(map, offset - 1 - rows->first);
        return rows->first + map->lines[line].byte_first;
    }
    size_t at = offset - 1;
    while (at && text[at - 1] != '\n') --at;
    size_t previous = at;
    while (at < offset) {
        previous = at;
        size_t next = text_rows_next(idle, rows, text, length, at, NULL, 0);
        if (next <= at || next >= offset) break;
        at = next;
    }
    return previous;
}

/* WATCH uses one complete temporary paragraph map for a live Arabic record.
 * Missing paragraph APIs or a failed map preserve the exact logical fallback,
 * including terminal auto-wrapping. Queue the entire result atomically:
 * backpressure must not duplicate a partially written record or advance its
 * retained-history sequence. Missing FriBidi, ordinary text and disabled mode
 * keep their existing logical UTF-8 output, including terminal auto-wrapping. */
static int stream_display_output(Idle *idle, const char *text)
{
    char shown[APP_TEXT + 2048];
    size_t length = display_text(idle, shown, sizeof shown - 2, text);
    if (idle->tty.utf8) {
        int width = idle->tty.cols > 1 ? idle->tty.cols - 1 : 1;
        IrcArabicLayout *map = irc_arabic_layout_utf8(shown, length, width, 0);
        if (map) {
            Frame frame = {{0}, 0, 0};
            for (int row = 0; row < map->rows && !frame.overflow; ++row) {
                if (map->lines[row].byte_first >= length) break;
                frame_arabic_line(&frame, map, row, 0);
                frame_add(&frame, "\r\n", 2);
            }
            irc_arabic_layout_free(map);
            if (!frame.overflow) return output_add(idle, frame.data, frame.used);
            /* An extreme frame still has a complete logical fallback. */
        }
    }
    shown[length++] = '\r'; shown[length++] = '\n';
    return output_add(idle, shown, length);
}

static inline size_t reader_row(Idle *idle, size_t start, Frame *frame, int row)
{
    return text_row(idle, idle->response, idle->response_length, start, frame, row);
}

static inline size_t reader_previous(Idle *idle, size_t offset)
{
    TextRows rows = {0};
    size_t previous = text_rows_previous(idle, &rows, idle->response,
                                         idle->response_length, offset);
    text_rows_clear(&rows);
    return previous;
}

static void reader_move(Idle *idle, uint32_t key)
{
    TextRows rows = {0};
    size_t page = idle->tty.rows > 2 ? (size_t)(idle->tty.rows - 2) : 1u;
    size_t offset = idle->response_offset;
    if (key == IK_HOME || key == 'g') offset = 0;
    else if (key == IK_END || key == 'G') {
        offset = idle->response_length;
        for (size_t i = 0; i < page && offset; ++i)
            offset = text_rows_previous(idle, &rows, idle->response, idle->response_length, offset);
    } else if (key == IK_UP || key == 'k' || key == IK_PAGE_UP || key == 'b') {
        size_t amount = key == IK_PAGE_UP || key == 'b' ? page : 1u;
        while (amount-- && offset)
            offset = text_rows_previous(idle, &rows, idle->response, idle->response_length, offset);
    } else if (key == IK_DOWN || key == 'j' || key == IK_PAGE_DOWN || key == ' ') {
        size_t amount = key == IK_PAGE_DOWN || key == ' ' ? page : 1u;
        size_t next = offset;
        while (amount-- && next < idle->response_length)
            next = text_rows_next(idle, &rows, idle->response, idle->response_length, next, NULL, 0);
        if (next < idle->response_length) offset = next;
    }
    text_rows_clear(&rows);
    if (offset != idle->response_offset) { idle->response_offset = offset; idle->redraw = 1; }
}

static void draw_reader(Idle *idle)
{
    if (!idle->redraw || idle->output_length || background_detached(idle->background)) return;
    Frame frame = {{0}, 0, 0};
    int entering = !idle->alternate;
    if (entering) { frame_add(&frame, "\033[?1049h", 8); idle->alternate = 1; }
    frame_add(&frame, "\033[0m\033[2J\033[H\033[?25l", sizeof "\033[0m\033[2J\033[H\033[?25l" - 1);
    if (idle->tty.rows < 5 || idle->tty.cols < 12) {
        frame_text(idle, &frame, 1, irc_tr("Terminal too small to display this response."));
    } else {
        frame_text(idle, &frame, 1, idle->response_help ? irc_tr("Help") : irc_tr("Command response"));
        char rule[500];
        size_t width = (size_t)(idle->tty.cols - 1);
        memset(rule, '-', width);
        frame_printf(&frame, "\033[2;1H"); frame_add(&frame, rule, width);
        if (idle->response_summary && *idle->notice) frame_text(idle, &frame, 2, idle->notice);
        size_t at = idle->response_offset;
        TextRows rows = {0};
        int row = 3;
        for (; row <= idle->tty.rows && at < idle->response_length; ++row)
            at = text_rows_next(idle, &rows, idle->response, idle->response_length, at, &frame, row);
        text_rows_clear(&rows);
        if (idle->response_overflow && at == idle->response_length && row <= idle->tty.rows)
            frame_text(idle, &frame, row, "Response reached its 512 KiB limit; request a narrower topic.");
    }
    if (frame.overflow) {
        frame.used = 0; frame.overflow = 0;
        if (entering) frame_add(&frame, "\033[?1049h", 8);
        frame_add(&frame, "\033[2J\033[H\033[?25l", sizeof "\033[2J\033[H\033[?25l" - 1);
        frame_text(idle, &frame, 1, irc_tr("Display capacity reached; response retained."));
    }
    (void)output_add(idle, frame.data, frame.used);
    idle->redraw = 0;
}

typedef struct EditorPosition { int row, col, screen_row; } EditorPosition;

typedef struct VisualEditorFrame {
    Frame *frame;
    int screen_first, viewport_first, selected, style_active;
    int painted_row, painted_col;
} VisualEditorFrame;

static void frame_editor_cluster(void *context, int row, int col,
                                  const wchar_t *text, size_t count, int cells,
                                  size_t first, size_t past)
{
    VisualEditorFrame *paint = context;
    (void)first; (void)past;
    if (paint->painted_row != row || paint->painted_col != col)
        frame_printf(paint->frame, "\033[%d;%dH", paint->screen_first + row - paint->viewport_first, col + 1);
    if (paint->selected && !paint->style_active) {
        frame_add(paint->frame, "\033[7m", sizeof "\033[7m" - 1);
        paint->style_active = 1;
    }
    for (size_t i = 0; i < count; ++i) {
        char encoded[4]; size_t bytes = encode((uint32_t)text[i], encoded);
        frame_add(paint->frame, encoded, bytes);
    }
    paint->painted_row = row;
    paint->painted_col = col + cells;
}

static EditorPosition frame_editor(Idle *idle, Frame *frame, int first_row,
                                   int height, int secret)
{
    int width = idle->tty.cols - 1;
    EditorPosition caret;
    int rows;
    if (!secret && idle->tty.utf8 && irc_arabic_visual_selected()) {
        EdVisualSnapshot *snapshot = ed_visual_snapshot(&idle->editor, width);
        if (snapshot) {
            ed_visual_snapshot_layout(snapshot, &rows, &caret.row, &caret.col);
            int top = caret.row >= height ? caret.row - height + 1 : 0;
            VisualEditorFrame paint = {frame, first_row, top, idle->editor.selection_all, 0, -1, -1};
            ed_visual_snapshot_render(snapshot, top, height, frame_editor_cluster, &paint);
            ed_visual_snapshot_free(snapshot);
            if (paint.style_active) frame_add(frame, "\033[0m", sizeof "\033[0m" - 1);
            caret.screen_row = first_row + caret.row - top;
            return caret;
        }
    }
    ed_layout(&idle->editor, width, &rows, &caret.row, &caret.col);
    int top = caret.row >= height ? caret.row - height + 1 : 0;
    int row = 0, col = 0, wrapped = 0, painted_row = -1, painted_col = -1;
    for (size_t i = 0; i < idle->editor.len;) {
        if (!secret && idle->editor.data[i] == L'\n') {
            /* Match ed_position(): an exact-width wrap already advanced
             * to the row selected by the following explicit newline. */
            if (!wrapped) ++row;
            col = 0; wrapped = 0; ++i; continue;
        }
        int span = ed_cluster_width(&idle->editor, i, width);
        if (span && col + span > width) { ++row; col = 0; }
        size_t end = ed_next(&idle->editor, i);
        if (row >= top && row < top + height) {
            if (painted_row != row || painted_col != col)
                frame_printf(frame, "\033[%d;%dH", first_row + row - top, 1 + col);
            int actual = secret ? span : wcwidth(idle->editor.data[i]);
            /* Mask every occupied cell, retaining the same cluster boundaries
             * and wrapping as the editor's arrows and vertical navigation. */
            if (secret) for (int cell = 0; cell < span; ++cell) frame_add(frame, "*", 1);
            else if (actual < 0 || actual > width || (!idle->tty.utf8 && idle->editor.data[i] >= 128))
                frame_add(frame, "?", 1);
            else {
                if (!actual) frame_add(frame, "?", 1);
                for (size_t k = i; k < end; ++k) {
                    char encoded[4]; size_t n = encode((uint32_t)idle->editor.data[k], encoded);
                    frame_add(frame, encoded, n);
                }
            }
            painted_row = row; painted_col = col + span;
        }
        col += span;
        wrapped = col >= width;
        if (wrapped) { ++row; col = 0; }
        i = end;
        if (row >= top + height) break;
    }
    caret.screen_row = first_row + caret.row - top;
    return caret;
}

static const Config *suggestion_config(const Idle *idle)
{
    const App *app = app_for_token(idle, idle->compose_token);
    if (app) return &app->cfg;
    const ClientSlot *primary = &idle->client->slots[0];
    return primary->app ? &primary->app->cfg : &primary->seed;
}

typedef struct EditorLayout {
    int tail_first, tail_rows;
    int suggestion_first;
    size_t suggestion_room;
    int editor_first, editor_height;
    int status_row, hint_row;
} EditorLayout;

static int composer_fullscreen(const Idle *idle)
{
    return suggestion_config(idle)->composer_fullscreen;
}

static EditorLayout editor_layout(const Idle *idle)
{
    EditorLayout l; memset(&l, 0, sizeof l);
    int rows = idle->tty.rows;
    l.tail_first = 2;
    if (rows < 6) {
        l.editor_first = rows > 1 ? rows - 1 : 1;
        l.editor_height = 1;
        l.hint_row = rows;
        return l;
    }
    if (!composer_fullscreen(idle)) {
        l.editor_first = rows;
        l.editor_height = 1;
        l.hint_row = rows - 1;
        int suggestion_rows = 0;
        if (idle->mode == COMMAND && rows >= 9) {
            suggestion_rows = rows - 6;
            if (suggestion_rows > 4) suggestion_rows = 4;
            if (suggestion_rows < 2) suggestion_rows = 0;
        }
        l.tail_rows = rows - 3 - suggestion_rows;
        if (l.tail_rows < 1) l.tail_rows = 1;
        if (suggestion_rows) {
            l.suggestion_first = l.tail_first + l.tail_rows;
            l.suggestion_room = (size_t)(suggestion_rows - 1);
        }
        return l;
    }
    l.status_row = rows - 1;
    l.hint_row = rows;
    if (idle->mode == COMMAND) {
        l.editor_height = rows >= 14 ? 3 : 1;
        int suggestion_rows = rows >= 10 ? rows - (1 + 3 + l.editor_height + 2) : 0;
        if (suggestion_rows > 6) suggestion_rows = 6;
        if (suggestion_rows < 2) suggestion_rows = 0;
        l.tail_rows = rows - 1 - l.editor_height - 2 - suggestion_rows;
        if (l.tail_rows < 1) l.tail_rows = 1;
        if (suggestion_rows) {
            l.suggestion_first = l.tail_first + l.tail_rows;
            l.suggestion_room = (size_t)(suggestion_rows - 1);
        }
        l.editor_first = l.tail_first + l.tail_rows + suggestion_rows;
    } else {
        l.tail_rows = rows >= 10 ? 3 : 1;
        l.editor_first = l.tail_first + l.tail_rows;
        l.editor_height = rows - l.editor_first - 1;
        if (l.editor_height < 1) l.editor_height = 1;
    }
    return l;
}

static const IdleCommandSpec *native_command(const char *line)
{
    if (*line == '/') ++line;
    size_t length = strcspn(line, " ");
    if (!length) return NULL;
    for (size_t i = 0; i < idle_command_count(); ++i) {
        const IdleCommandSpec *spec = idle_command_spec(i);
        if (commands_name_matches(spec->name, line, length, 1)) return spec;
    }
    return NULL;
}

static int native_rank(const void *left, const void *right)
{
    const NativeSuggestion *a = left, *b = right;
    if (a->score != b->score) return a->score > b->score ? -1 : 1;
    return strcmp(a->spec->name, b->spec->name);
}

static size_t native_suggestions(const Idle *idle, NativeSuggestion *out, size_t cap)
{
    char input[ED_MAX_BYTES + 1];
    if (!cap || ed_to_utf8(&idle->editor, input, sizeof input) == SIZE_MAX) return 0;
    const char *prefix = input + (input[0] == '/');
    size_t length = strcspn(prefix, " "), count = 0;
    int exact = prefix[length] == ' ';
    const Config *cfg = suggestion_config(idle);
    if (cfg->command_suggestions == CONFIG_SUGGEST_HISTORY) {
        for (size_t i = 0; i < idle->history_count && count < cap; ++i) {
            const HistoryLine *entry = &idle->history[i];
            if (entry->token != idle->compose_token) continue;
            const IdleCommandSpec *spec = native_command(entry->text);
            if (!spec || !commands_name_matches(spec->name, prefix, length, exact)) continue;
            const char *argument = strchr(entry->text, ' ');
            if (exact && (!argument || strncmp(argument, prefix + length, strlen(prefix + length)))) continue;
            out[count++] = (NativeSuggestion){spec, entry->text, i, 0};
        }
    } else {
        CommandContext context = idle->suggestion_context;
        const App *app = app_for_token(idle, idle->compose_token);
        if (cfg->command_suggestions == CONFIG_SUGGEST_COMMON && (!app || !app->registered))
            context = COMMAND_CONTEXT_DISCONNECTED;
        for (size_t i = 0; i < idle_command_count() && count < cap; ++i) {
            const IdleCommandSpec *spec = idle_command_spec(i);
            if (!commands_name_matches(spec->name, prefix, length, exact)) continue;
            out[count++] = (NativeSuggestion){spec, NULL, SIZE_MAX,
                commands_suggestion_score(spec->name, cfg->command_suggestions, context, cfg)};
        }
        qsort(out, count, sizeof *out, native_rank);
    }
    config_wipe(input, sizeof input);
    return count;
}

static size_t native_suggestion_room(const Idle *idle)
{
    if (idle->mode != COMMAND || idle->tty.cols < 12) return 0;
    return editor_layout(idle).suggestion_room;
}

static void draw_suggestions(Idle *idle, Frame *frame, const EditorLayout *layout)
{
    if (idle->mode != COMMAND) return;
    if (!native_suggestion_room(idle)) { idle->suggestion_selected = 0; return; }
    NativeSuggestion items[SUGGESTION_ITEMS];
    size_t count = native_suggestions(idle, items, SUGGESTION_ITEMS);
    if (idle->suggestion_index >= count) idle->suggestion_index = count ? count - 1 : 0;
    if (!count) idle->suggestion_selected = 0;
    size_t room = native_suggestion_room(idle);
    size_t first = idle->suggestion_selected && room ? idle->suggestion_index / room * room : 0;
    int row = layout->suggestion_first;
    char label[1024];
    int mode = suggestion_config(idle)->command_suggestions;
    const char *context = idle->suggestion_context == COMMAND_CONTEXT_CHANNEL ? irc_tr("Channel") :
        idle->suggestion_context == COMMAND_CONTEXT_QUERY ? irc_tr("Query") :
        idle->suggestion_context == COMMAND_CONTEXT_SERVER ? irc_tr("Server") : irc_tr("Client");
    if (mode == CONFIG_SUGGEST_CONTEXTUAL)
        (void)snprintf(label, sizeof label, "%s: %s, %s (%zu)",
            irc_tr("Suggestions"), irc_tr(commands_suggestion_mode_label(mode)), context, count);
    else (void)snprintf(label, sizeof label, "%s: %s (%zu)",
            irc_tr("Suggestions"), irc_tr(commands_suggestion_mode_label(mode)), count);
    frame_text(idle, frame, row++, label);
    if (!count) frame_text(idle, frame, row, irc_tr("No matching suggestions."));
    for (size_t i = first; i < count && i - first < room; ++i, ++row) {
        NativeSuggestion *entry = &items[i];
        int selected = idle->suggestion_selected && i == idle->suggestion_index;
        if (selected) frame_add(frame, "\033[7m", sizeof "\033[7m" - 1);
        if (entry->text) (void)snprintf(label, sizeof label, "%c %s", selected ? '>' : ' ', entry->text);
        else (void)snprintf(label, sizeof label, "%c /%s  %s", selected ? '>' : ' ', entry->spec->name, entry->spec->description);
        frame_text(idle, frame, row, label);
        if (selected) frame_add(frame, "\033[0m", sizeof "\033[0m" - 1);
    }
}

static void suggestion_move(Idle *idle, uint32_t key)
{
    if (!native_suggestion_room(idle)) {
        idle->suggestion_selected = 0;
        notice(idle, irc_tr("Resize the terminal to display suggestions.")); return;
    }
    NativeSuggestion items[SUGGESTION_ITEMS];
    size_t count = native_suggestions(idle, items, SUGGESTION_ITEMS);
    if (!count) { notice(idle, "No matching suggestions."); return; }
    size_t step = key == IK_PAGE_UP || key == IK_PAGE_DOWN ? native_suggestion_room(idle) : 1;
    if (!step) step = 1;
    if (!idle->suggestion_selected) {
        idle->suggestion_selected = 1; idle->suggestion_index = 0;
    } else if (key == IK_UP || key == IK_PAGE_UP) {
        if (!idle->suggestion_index) idle->suggestion_selected = 0;
        else idle->suggestion_index = idle->suggestion_index > step ? idle->suggestion_index - step : 0;
    } else {
        if (idle->suggestion_index >= count) idle->suggestion_index = count - 1;
        size_t remaining = count - 1 - idle->suggestion_index;
        idle->suggestion_index += step < remaining ? step : remaining;
    }
    idle->browsing = idle->completing = 0;
    idle->redraw = 1;
}

static int selected_suggestion(Idle *idle, NativeSuggestion *selected)
{
    NativeSuggestion items[SUGGESTION_ITEMS];
    size_t count = native_suggestions(idle, items, SUGGESTION_ITEMS);
    if (!idle->suggestion_selected || !count) return 0;
    if (idle->suggestion_index >= count) idle->suggestion_index = count - 1;
    *selected = items[idle->suggestion_index];
    return 1;
}

static void suggestion_insert(Idle *idle)
{
    NativeSuggestion entry;
    if (!selected_suggestion(idle, &entry)) return;
    char line[ED_MAX_BYTES + 1], original[ED_MAX_BYTES + 1];
    if (entry.text) (void)snprintf(line, sizeof line, "%s", entry.text);
    else {
        if (ed_to_utf8(&idle->editor, original, sizeof original) == SIZE_MAX) return;
        const char *tail = strchr(original, ' ');
        int length = snprintf(line, sizeof line, "/%s%s", entry.spec->name,
                              tail ? tail : strchr(entry.spec->usage, ' ') ? " " : "");
        config_wipe(original, sizeof original);
        if (length < 0 || (size_t)length >= sizeof line) {
            config_wipe(line, sizeof line);
            notice(idle, "Completed command would exceed the input limit; draft retained."); return;
        }
    }
    if (ed_from_utf8(&idle->editor, line)) {
        idle->suggestion_selected = 0;
        idle->suggestion_inserted = 1; idle->suggestion_drawn = 0;
        idle->suggestion_batch = idle->input_batch;
        idle->browsing = idle->completing = 0;
        idle->notice[0] = 0; idle->notice_pending = 0;
        idle->redraw = 1;
    }
    config_wipe(line, sizeof line);
}

static void save_usage(Idle *idle, App *app)
{
    if (!app || !app->config_saved || app->test_mode || idle->client->test_mode) return;
    char error[512];
    if (config_command_usage_save(&app->cfg, app->config_path, error, sizeof error) < 0)
        notice(idle, "Command frequency is available this session; its file could not be saved.");
    config_wipe(error, sizeof error);
}

static void suggestion_delete(Idle *idle)
{
    NativeSuggestion entry;
    if (!selected_suggestion(idle, &entry)) return;
    const Config *cfg = suggestion_config(idle);
    if (cfg->command_suggestions == CONFIG_SUGGEST_HISTORY && entry.history_index < idle->history_count) {
        size_t index = entry.history_index, length = strlen(idle->history[index].text);
        config_wipe(idle->history[index].text, length); free(idle->history[index].text);
        idle->history_bytes -= length + 1;
        --idle->history_count;
        memmove(idle->history + index, idle->history + index + 1,
                (idle->history_count - index) * sizeof idle->history[0]);
        memset(&idle->history[idle->history_count], 0, sizeof idle->history[0]);
        notice(idle, "Removed the selected command from recent history.");
    } else if (cfg->command_suggestions == CONFIG_SUGGEST_FREQUENT) {
        App *app = app_for_token(idle, idle->compose_token);
        if (app) {
            commands_usage_reset(&app->cfg, entry.spec->name);
            notice(idle, "Reset the selected command's usage count.");
            save_usage(idle, app);
        }
    } else notice(idle, "This suggestion comes from the command catalog.");
    idle->redraw = 1;
}

static void editor_measure(Idle *idle, AppDraftMeasure *measure)
{
    memset(measure, 0, sizeof *measure);
    App *app = app_for_token(idle, idle->compose_token);
    if (!app) return;
    size_t view = named_view(app, idle->compose_kind, idle->compose_target);
    if (view == SIZE_MAX) view = app->current;
    char text[ED_MAX_BYTES + 1];
    if (ed_to_utf8(&idle->editor, text, sizeof text) != SIZE_MAX)
        (void)app_measure_draft(app, view, text, idle->mode == COMPOSE, measure);
    config_wipe(text, sizeof text);
}

static void editor_limit_update(Idle *idle)
{
    if (idle->mode != COMPOSE && idle->mode != COMMAND) return;
    AppDraftMeasure measure; editor_measure(idle, &measure);
    if (!measure.budget || measure.longest_line_bytes < measure.budget) {
        idle->limit_pending = idle->limit_confirmed = 0;
        return;
    }
    if (!idle->limit_confirmed) {
        idle->limit_pending = 1;
        idle->redraw = 1;
    }
}

static void leave_editor(Idle *idle);
static void reset_input(Idle *idle);
static size_t scrollback_page_rows(const Idle *idle);

static size_t stream_visual_rows(Idle *idle, const char *text)
{
    size_t length = strlen(text), at = 0, rows = 0;
    TextRows layout = {0};
    if (!length) return 1;
    while (at < length) {
        size_t next = text_rows_next(idle, &layout, text, length, at, NULL, 0);
        ++rows;
        if (next <= at) break;
        at = next;
    }
    text_rows_clear(&layout);
    return rows ? rows : 1;
}

/* Catch-up snapshots and scrollback preserve canonical App history. */
static void catchup_close(Idle *idle)
{
    if (idle->catchup_fd >= 0) close(idle->catchup_fd);
    idle->catchup_fd = -1;
    idle->catchup_start = idle->catchup_end = 0;
    idle->catchup_row = 0;
    idle->catchup_token = 0;
    idle->catchup_kind = B_SERVER;
    idle->catchup_target[0] = 0;
    idle->catchup_index_valid = 0;
    idle->catchup_point_count = idle->catchup_total = 0;
}

static const char *catchup_display_text(Idle *idle, const char *record, char *out, size_t cap)
{
    char clean[APP_TEXT + 2048];
    irc_sanitize(clean, sizeof clean, record);
    const ClientSlot *primary = &idle->client->slots[0];
    int full = primary->app ? primary->app->cfg.output_full : primary->seed.output_full;
    if (full || !compact_record(clean, idle->catchup_kind, NULL, out, cap))
        (void)snprintf(out, cap, "%s", clean);
    config_wipe(clean, sizeof clean);
    return out;
}

/* Count the pinned snapshot once and retain only sparse record offsets. A
 * painted page starts at its nearest checkpoint and stops when it is full;
 * both paths still sanitize and wrap one bounded record at a time. */
static size_t catchup_scan(Idle *idle, size_t start_row, size_t room, Frame *frame, int first_row)
{
    if (!frame) idle->catchup_point_count = 0;
    if (idle->catchup_fd < 0 || idle->catchup_end <= idle->catchup_start) return 0;
    if (frame && (!room || start_row >= idle->catchup_total)) return idle->catchup_total;
    int64_t position = idle->catchup_start;
    size_t absolute = 0, rendered = 0;
    if (frame && idle->catchup_point_count) {
        size_t lower = 0, upper = idle->catchup_point_count;
        while (lower < upper) {
            size_t middle = lower + (upper - lower) / 2;
            if (idle->catchup_points[middle].row <= start_row) lower = middle + 1;
            else upper = middle;
        }
        if (lower) {
            const CatchupPoint *point = &idle->catchup_points[lower - 1];
            position = point->offset; absolute = point->row;
        }
    }
    int copy = dup(idle->catchup_fd);
    if (copy < 0) return SIZE_MAX;
    FILE *file = fdopen(copy, "r");
    if (!file) { close(copy); return SIZE_MAX; }
    if (fseeko(file, (off_t)position, SEEK_SET) != 0) { fclose(file); return SIZE_MAX; }
    char prefix[256];
    int p = snprintf(prefix, sizeof prefix, "[%s] ", idle->catchup_target);
    if (p <= 0 || (size_t)p >= sizeof prefix) { fclose(file); return SIZE_MAX; }
    char raw[APP_TEXT + 4096], shown[APP_TEXT + 2048];
    int64_t next_point = idle->catchup_start;
    int discarding = 0, failed = 0;
    while (position < idle->catchup_end &&
           (!frame || (rendered < room && absolute < idle->catchup_total))) {
        if (!frame && !discarding && position >= next_point &&
            idle->catchup_point_count < CATCHUP_POINTS) {
            idle->catchup_points[idle->catchup_point_count++] = (CatchupPoint){position, absolute};
            next_point = position + CATCHUP_POINT_BYTES;
        }
        size_t capacity = sizeof raw;
        int64_t remaining = idle->catchup_end - position;
        if ((uint64_t)remaining < capacity - 1) capacity = (size_t)remaining + 1;
        /* A later append must not change the last record of this snapshot,
         * even if its captured final record did not end with a newline. */
        if (!fgets(raw, (int)capacity, file)) { failed = ferror(file); break; }
        off_t after = ftello(file);
        if (after < 0 || (int64_t)after <= position || (int64_t)after > idle->catchup_end) {
            failed = 1; break;
        }
        position = (int64_t)after;
        size_t length = strlen(raw);
        int complete = length && raw[length - 1] == '\n';
        if (!complete && position < idle->catchup_end && !feof(file)) { discarding = 1; continue; }
        if (discarding) { discarding = 0; continue; }
        while (length && (raw[length - 1] == '\n' || raw[length - 1] == '\r')) raw[--length] = 0;
        if (strncmp(raw, prefix, (size_t)p)) continue;
        const char *text = catchup_display_text(idle, raw + p, shown, sizeof shown);
        size_t text_length = strlen(text), at = 0;
        if (!text_length) {
            if (absolute >= start_row && rendered < room && frame) ++rendered;
            ++absolute;
            continue;
        }
        TextRows layout = {0};
        while (at < text_length) {
            int visible = frame && absolute >= start_row && rendered < room;
            size_t next = text_rows_next(idle, &layout, text, text_length, at, visible ? frame : NULL,
                                   first_row + (int)rendered);
            if (visible) ++rendered;
            ++absolute;
            if (next <= at || (frame && rendered >= room)) break;
            at = next;
        }
        text_rows_clear(&layout);
    }
    config_wipe(raw, sizeof raw);
    config_wipe(shown, sizeof shown);
    fclose(file);
    return failed ? SIZE_MAX : absolute;
}

static size_t catchup_prepare(Idle *idle)
{
    struct stat st;
    if (idle->catchup_fd < 0 || fstat(idle->catchup_fd, &st) < 0 || st.st_size < 0) {
        idle->catchup_index_valid = 0;
        idle->catchup_point_count = idle->catchup_total = 0;
        return 0;
    }
    const ClientSlot *primary = &idle->client->slots[0];
    int full = primary->app ? primary->app->cfg.output_full : primary->seed.output_full;
    const char *locale = setlocale(LC_CTYPE, NULL);
    int locale_fits = locale && strlen(locale) < sizeof idle->catchup_locale;
    if (idle->catchup_index_valid &&
        (idle->catchup_cols != idle->tty.cols || idle->catchup_utf8 != idle->tty.utf8 ||
         idle->catchup_full != full || !locale_fits || strcmp(idle->catchup_locale, locale) ||
         st.st_size < idle->catchup_file_size ||
         (st.st_size == idle->catchup_file_size &&
          (st.st_mtim.tv_sec != idle->catchup_mtime.tv_sec || st.st_mtim.tv_nsec != idle->catchup_mtime.tv_nsec ||
           st.st_ctim.tv_sec != idle->catchup_ctime.tv_sec || st.st_ctim.tv_nsec != idle->catchup_ctime.tv_nsec))))
        idle->catchup_index_valid = 0;
    /* The application only appends. Growth beyond the captured end leaves
     * the snapshot intact; a known rewrite or shrink invalidates its index.
     * Never extend a truncated snapshot when the file subsequently regrows. */
    if ((int64_t)st.st_size < idle->catchup_end) {
        idle->catchup_end = (int64_t)st.st_size;
        idle->catchup_index_valid = 0;
    }
    idle->catchup_file_size = st.st_size;
    idle->catchup_mtime = st.st_mtim; idle->catchup_ctime = st.st_ctim;
    if (!idle->catchup_index_valid) {
        size_t total = catchup_scan(idle, 0, 0, NULL, 0);
        if (total == SIZE_MAX) {
            idle->catchup_point_count = idle->catchup_total = 0;
            return 0;
        }
        idle->catchup_total = total;
        idle->catchup_cols = idle->tty.cols; idle->catchup_utf8 = idle->tty.utf8;
        idle->catchup_full = full;
        if (locale_fits) {
            memcpy(idle->catchup_locale, locale, strlen(locale) + 1);
            idle->catchup_index_valid = 1;
        }
    }
    return idle->catchup_total;
}

static size_t catchup_page_rows(const Idle *idle)
{
    return idle->tty.rows > 4 ? (size_t)(idle->tty.rows - 2) : 1u;
}

static void draw_catchup(Idle *idle)
{
    if (!idle->redraw || idle->output_length || background_detached(idle->background)) return;
    Frame frame = {{0}, 0, 0};
    int entering = !idle->alternate;
    if (entering) { frame_add(&frame, "\033[?1049h", 8); idle->alternate = 1; }
    frame_add(&frame, "\033[0m\033[2J\033[H\033[?25l", sizeof "\033[0m\033[2J\033[H\033[?25l" - 1);
    if (idle->tty.rows < 5 || idle->tty.cols < 12) {
        frame_text(idle, &frame, 1, irc_tr("Terminal too small for catch-up history."));
    } else {
        size_t page = catchup_page_rows(idle);
        size_t total = catchup_prepare(idle);
        size_t maximum = total > page ? total - page : 0;
        if (idle->catchup_row > maximum) idle->catchup_row = maximum;
        char heading[512], footer[256];
        App *app = app_for_token(idle, idle->catchup_token);
        ClientSlot *slot = app ? slot_for_app(idle, app) : NULL;
        if (slot) (void)snprintf(heading, sizeof heading, "Catch-up from your most recent sent message | S%zu %s",
                                 slot->index + 1u, idle->catchup_target);
        else (void)snprintf(heading, sizeof heading, "Catch-up from your most recent sent message | %s", idle->catchup_target);
        frame_text(idle, &frame, 1, heading);
        (void)catchup_scan(idle, idle->catchup_row, page, &frame, 2);
        (void)snprintf(footer, sizeof footer,
                       "Up/Down row | PgUp/PgDn page | Home anchor | End newest | Esc live | row %zu/%zu",
                       total ? idle->catchup_row + 1u : 0u, total);
        frame_text(idle, &frame, idle->tty.rows, footer);
    }
    if (frame.overflow) {
        frame.used = 0; frame.overflow = 0;
        if (entering) frame_add(&frame, "\033[?1049h", 8);
        frame_add(&frame, "\033[2J\033[H\033[?25l", sizeof "\033[2J\033[H\033[?25l" - 1);
        frame_text(idle, &frame, 1, irc_tr("Display capacity reached; catch-up position retained."));
    }
    (void)output_add(idle, frame.data, frame.used);
    idle->redraw = 0;
}

static int begin_memory_catchup(Idle *idle, uint64_t token, BufferKind kind, const char *target)
{
    size_t total = 0, anchor = SIZE_MAX;
    idle->display_token = token;
    idle->display_kind = kind;
    (void)snprintf(idle->display_target, sizeof idle->display_target, "%s", target);
    for (size_t n = 0; n < idle->count; ++n) {
        StreamLine *line = &idle->lines[(idle->first + n) % STREAM_LINES];
        if (!record_matches(idle, line)) continue;
        char compact[APP_TEXT + 2048];
        const char *text = stream_text(idle, line, compact, sizeof compact);
        size_t rows = stream_visual_rows(idle, text);
        if (line->mine) anchor = total;
        if (SIZE_MAX - total < rows) return 0;
        total += rows;
    }
    if (anchor == SIZE_MAX) return 0;
    size_t page = scrollback_page_rows(idle);
    size_t end = anchor + page < total ? anchor + page : total;
    idle->scrollback_rows = total - end;
    reset_input(idle);
    idle->mode = SCROLLBACK;
    idle->redraw = 1;
    idle->transition_batch = idle->input_batch;
    return 1;
}

static int begin_catchup(Idle *idle)
{
    App *app = app_for_token(idle, idle->compose_token);
    size_t view = named_view(app, idle->compose_kind, idle->compose_target);
    if (!app || view == SIZE_MAX || (idle->compose_kind != B_CHANNEL && idle->compose_kind != B_QUERY)) {
        notice(idle, "Catch-up is available in channel and query conversations.");
        return 0;
    }
    Buffer *buffer = &app->buffers[view];
    if (app->cfg.persistent_logging && !app->persistent_log_failed && buffer->last_own_log_valid && *app->config_path) {
        char error[256];
        int fd = config_open_log_read(app->config_path, error, sizeof error);
        if (fd >= 0) {
            int64_t end = app_catchup_log_end(buffer, fd);
            char prefix[256];
            int p = snprintf(prefix, sizeof prefix, "[%s] ", buffer->name);
            int valid = p > 0 && (size_t)p < sizeof prefix && end >= 0;
            if (valid) {
                char check[256];
                ssize_t got = pread(fd, check, (size_t)p, (off_t)buffer->last_own_log_offset);
                valid = got == p && !memcmp(check, prefix, (size_t)p);
            }
            if (valid) {
                catchup_close(idle);
                idle->catchup_fd = fd;
                idle->catchup_start = buffer->last_own_log_offset;
                idle->catchup_end = end;
                idle->catchup_token = idle->compose_token;
                idle->catchup_kind = idle->compose_kind;
                (void)snprintf(idle->catchup_target, sizeof idle->catchup_target, "%s", idle->compose_target);
                idle->catchup_row = 0;
                reset_input(idle);
                idle->mode = CATCHUP;
                idle->redraw = 1;
                idle->transition_batch = idle->input_batch;
                return 1;
            }
            close(fd);
        }
    }
    if (buffer->last_own_retained && begin_memory_catchup(idle, idle->compose_token, idle->compose_kind, idle->compose_target))
        return 1;
    if (app->cfg.persistent_logging && app->persistent_log_failed)
        notice(idle, "Catch-up cannot guarantee the absence: persistent logging stopped after an error, and the sent-message anchor is no longer retained in memory.");
    else if (app->cfg.persistent_logging)
        notice(idle, "The durable sent-message anchor is unavailable (for example, logging began after that message or the log was replaced). The retained-memory anchor is also gone.");
    else
        notice(idle, "Your most recent sent message is older than retained memory. Enable persistent_logging before the next absence for durable /catchup.");
    return 0;
}

static size_t scrollback_total_rows(Idle *idle)
{
    size_t total = 0;
    for (size_t n = 0; n < idle->count; ++n) {
        StreamLine *line = &idle->lines[(idle->first + n) % STREAM_LINES];
        if (!record_matches(idle, line)) continue;
        char compact[APP_TEXT + 2048];
        const char *text = stream_text(idle, line, compact, sizeof compact);
        size_t rows = stream_visual_rows(idle, text);
        if (SIZE_MAX - total < rows) return SIZE_MAX;
        total += rows;
    }
    return total;
}

static void scrollback_record_added(Idle *idle, StreamLine *line)
{
    if (idle->mode != SCROLLBACK || !record_matches(idle, line)) return;
    char compact[APP_TEXT + 2048];
    const char *text = stream_text(idle, line, compact, sizeof compact);
    size_t rows = stream_visual_rows(idle, text);
    if (SIZE_MAX - idle->scrollback_rows < rows) idle->scrollback_rows = SIZE_MAX;
    else idle->scrollback_rows += rows;
}

static size_t scrollback_page_rows(const Idle *idle)
{
    return idle->tty.rows > 4 ? (size_t)(idle->tty.rows - 2) : 1u;
}

static size_t scrollback_max_offset(Idle *idle)
{
    size_t total = scrollback_total_rows(idle), page = scrollback_page_rows(idle);
    return total > page ? total - page : 0;
}

static void leave_scrollback(Idle *idle)
{
    idle->scrollback_rows = 0;
    leave_editor(idle);
    idle->mode = WATCH;
    idle->watch_escape_after = now_ms() + ESCAPE_MS;
}

static void draw_scrollback(Idle *idle)
{
    if (!idle->redraw || idle->output_length || background_detached(idle->background)) return;
    Frame frame = {{0}, 0, 0};
    int entering = !idle->alternate;
    if (entering) { frame_add(&frame, "\033[?1049h", 8); idle->alternate = 1; }
    frame_add(&frame, "\033[0m\033[2J\033[H\033[?25l", sizeof "\033[0m\033[2J\033[H\033[?25l" - 1);
    if (idle->tty.rows < 5 || idle->tty.cols < 12) {
        frame_text(idle, &frame, 1, irc_tr("Terminal too small for scrollback."));
    } else {
        char heading[512], footer[256];
        (void)snprintf(heading, sizeof heading, "Scrollback | %s", *idle->display_target ? idle->display_target : "chat");
        frame_text(idle, &frame, 1, heading);
        size_t page = scrollback_page_rows(idle);
        size_t total = scrollback_total_rows(idle);
        size_t maximum = total > page ? total - page : 0;
        if (idle->scrollback_rows > maximum) idle->scrollback_rows = maximum;
        size_t end = total > idle->scrollback_rows ? total - idle->scrollback_rows : 0;
        size_t start = end > page ? end - page : 0;
        size_t absolute = 0;
        int row = 2;
        for (size_t n = 0; n < idle->count && row < idle->tty.rows; ++n) {
            StreamLine *line = &idle->lines[(idle->first + n) % STREAM_LINES];
            if (!record_matches(idle, line)) continue;
            char compact[APP_TEXT + 2048];
            const char *text = stream_text(idle, line, compact, sizeof compact);
            size_t length = strlen(text), at = 0;
            if (!length) {
                if (absolute >= start && absolute < end) ++row;
                ++absolute;
                continue;
            }
            TextRows layout = {0};
            while (at < length) {
                int visible = absolute >= start && absolute < end && row < idle->tty.rows;
                size_t next = text_rows_next(idle, &layout, text, length, at,
                                             visible ? &frame : NULL, row);
                if (visible) ++row;
                ++absolute;
                if (next <= at) break;
                at = next;
                if (absolute >= end) break;
            }
            text_rows_clear(&layout);
            if (absolute >= end) break;
        }
        (void)snprintf(footer, sizeof footer,
                       "Up/Down row | PgUp/PgDn page | Home oldest | End/Esc live | %zu back",
                       idle->scrollback_rows);
        frame_text(idle, &frame, idle->tty.rows, footer);
    }
    if (frame.overflow) {
        frame.used = 0; frame.overflow = 0;
        if (entering) frame_add(&frame, "\033[?1049h", 8);
        frame_add(&frame, "\033[2J\033[H\033[?25l", sizeof "\033[2J\033[H\033[?25l" - 1);
        frame_text(idle, &frame, 1, irc_tr("Display capacity reached; scrollback retained."));
    }
    (void)output_add(idle, frame.data, frame.used);
    idle->redraw = 0;
}

static void draw_live_tail(Idle *idle, Frame *frame, int first_row, int rows)
{
    if (rows <= 0) return;
    /* Geometry is capped at 200 rows. Walk newest records first and stop
     * once their visual rows fill the tail; old history need not be wrapped
     * on every keystroke. Retain only row starts for the partial oldest one. */
    size_t starts[200], first = idle->count, first_offset = 0;
    if (rows > (int)(sizeof starts / sizeof starts[0]))
        rows = (int)(sizeof starts / sizeof starts[0]);
    size_t remaining = (size_t)rows;
    for (size_t n = idle->count; n; --n) {
        StreamLine *line = &idle->lines[(idle->first + n - 1) % STREAM_LINES];
        if (!record_matches(idle, line)) continue;
        first = n - 1;
        char compact[APP_TEXT + 2048];
        const char *text = stream_text(idle, line, compact, sizeof compact);
        size_t length = strlen(text), at = 0, count = 0;
        TextRows layout = {0};
        do {
            starts[count++ % remaining] = at;
            at = text_rows_next(idle, &layout, text, length, at, NULL, 0);
        } while (at < length);
        text_rows_clear(&layout);
        if (count >= remaining) {
            first_offset = starts[(count - remaining) % remaining];
            break;
        }
        remaining -= count;
    }
    int row = first_row;
    for (size_t n = first; n < idle->count && row < first_row + rows; ++n) {
        StreamLine *line = &idle->lines[(idle->first + n) % STREAM_LINES];
        if (!record_matches(idle, line)) continue;
        char compact[APP_TEXT + 2048];
        const char *text = stream_text(idle, line, compact, sizeof compact);
        size_t length = strlen(text), at = n == first ? first_offset : 0;
        TextRows layout = {0};
        do {
            at = text_rows_next(idle, &layout, text, length, at, frame, row++);
        } while (at < length && row < first_row + rows);
        text_rows_clear(&layout);
    }
}

/* Rebuild the composer from retained records and logical draft text.
 * editor_layout reserves editor/status rows before draw_live_tail wraps its
 * records, so adding an input row must not overwrite the last chat row. This
 * frame is presentation only; replacing it never consumes transcript text. */
static void draw_editor(Idle *idle)
{
    if (!idle->redraw || idle->output_length || background_detached(idle->background)) return;
    idle->editor_frame = 0;
    Frame frame = {{0}, 0, 0};
    int entering = !idle->alternate;
    if (entering) { frame_add(&frame, "\033[?1049h", 8); idle->alternate = 1; }
    frame_add(&frame, "\033[0m\033[2J\033[H\033[?25h", sizeof "\033[0m\033[2J\033[H\033[?25h" - 1);
    if (idle->tty.rows < 6 || idle->tty.cols < 12) {
        idle->suggestion_selected = 0;
        frame_text(idle, &frame, 1, irc_tr("Terminal too small; draft retained."));
        frame_printf(&frame, "\033[2;1H");
        (void)output_add(idle, frame.data, frame.used); idle->redraw = 0; return;
    }
    AppDraftMeasure measure;
    editor_measure(idle, &measure);
    if (!measure.budget || measure.longest_line_bytes < measure.budget)
        idle->limit_pending = idle->limit_confirmed = 0;
    else if (!idle->limit_confirmed) idle->limit_pending = 1;
    EditorLayout layout = editor_layout(idle);
    char label[1024];
    App *app = app_for_token(idle, idle->compose_token);
    ClientSlot *slot = app ? slot_for_app(idle, app) : NULL;
    const char *presentation = composer_fullscreen(idle) ? irc_tr("fullscreen") : irc_tr("line");
    if (slot) {
        if (measure.budget)
            (void)snprintf(label, sizeof label, "%s | S%zu %s | %s | %s | %zu/%zu bytes",
                           idle->mode == COMMAND ? irc_tr("Command") : irc_tr("Compose"), slot->index + 1u, app->cfg.server,
                           idle->compose_target, presentation, idle->editor.bytes, measure.budget);
        else
            (void)snprintf(label, sizeof label, "%s | S%zu %s | %s | %s | %zu bytes",
                           idle->mode == COMMAND ? irc_tr("Command") : irc_tr("Compose"), slot->index + 1u, app->cfg.server,
                           idle->compose_target, presentation, idle->editor.bytes);
    } else
        (void)snprintf(label, sizeof label, "%s | unavailable | %s | %s | %zu bytes",
                       idle->mode == COMMAND ? irc_tr("Command") : irc_tr("Compose"), idle->compose_target, presentation, idle->editor.bytes);
    frame_text(idle, &frame, 1, label);
    draw_live_tail(idle, &frame, layout.tail_first, layout.tail_rows);
    draw_suggestions(idle, &frame, &layout);
    EditorPosition caret = frame_editor(idle, &frame, layout.editor_first, layout.editor_height, 0);
    if (layout.status_row) {
        (void)snprintf(label, sizeof label, "%zu UTF-8 bytes | %zu/message | row %d col %d",
                       idle->editor.bytes, measure.budget, caret.row + 1, caret.col + 1);
        frame_text(idle, &frame, layout.status_row, label);
    }
    const char *hint = idle->notice;
    char limit_hint[512];
    if (idle->limit_pending && measure.budget) {
        (void)snprintf(limit_hint, sizeof limit_hint,
                       "Message limit reached (%zu bytes). Enter confirms continued editing; Backspace/Delete can reduce; Esc cancels.",
                       measure.budget);
        hint = limit_hint;
    } else if (measure.messages > 1) {
        (void)snprintf(limit_hint, sizeof limit_hint,
                       "This draft needs %zu IRC messages. Sending still requires explicit confirmation.", measure.messages);
        hint = limit_hint;
    } else if (idle->mode == COMMAND && !*idle->notice) {
        char text[ED_MAX_BYTES + 1];
        if (ed_to_utf8(&idle->editor, text, sizeof text) != SIZE_MAX) {
            for (size_t n = 0; n < idle_command_count(); ++n) {
                const IdleCommandSpec *spec = idle_command_spec(n);
                size_t length = strcspn(text + (text[0] == '/'), " ");
                if (text[0] == '/' && commands_name_matches(spec->name, text + 1, length, 1)) {
                    hint = spec->usage; break;
                }
            }
        }
        config_wipe(text, sizeof text);
    }
    frame_text(idle, &frame, layout.hint_row, hint);
    frame_printf(&frame, "\033[%d;%dH%s", caret.screen_row, 1 + caret.col,
                 idle->suggestion_selected && idle->mode == COMMAND ? "\033[?25l" : "\033[?25h");
    int complete_editor_frame = !frame.overflow;
    if (frame.overflow) {
        frame.used = 0; frame.overflow = 0;
        if (entering) frame_add(&frame, "\033[?1049h", 8);
        frame_add(&frame, "\033[2J\033[H", sizeof "\033[2J\033[H" - 1);
        frame_text(idle, &frame, 1, irc_tr("Display capacity reached; draft retained."));
        frame_add(&frame, "\033[2;1H\033[?25h", sizeof "\033[2;1H\033[?25h" - 1);
    }
    if (output_add(idle, frame.data, frame.used) && complete_editor_frame)
        idle->editor_frame = 1;
    idle->redraw = 0;
}

static size_t setup_text_add(char *out, size_t cap, size_t used, const char *text)
{
    char line[CONFIG_PATH_MAX + 1];
    while (*text && used + 1 < cap) {
        const char *end = strchr(text, '\n');
        size_t length = end ? (size_t)(end - text) : strlen(text);
        if (length >= sizeof line) length = sizeof line - 1;
        memcpy(line, text, length); line[length] = 0;
        used += irc_sanitize(out + used, cap - used, line);
        text += length;
        if (*text == '\n' && used + 1 < cap) { out[used++] = '\n'; ++text; }
    }
    out[used] = 0;
    return used;
}

static size_t setup_body(const IdleSetupView *view, char *out, size_t cap)
{
    size_t used = setup_text_add(out, cap, 0, irc_tr("Current"));
    used = setup_text_add(out, cap, used, ": ");
    used = setup_text_add(out, cap, used, view->current);
    used = setup_text_add(out, cap, used, "\n\n");
    used = setup_text_add(out, cap, used, view->detail);
    if (*view->summary) {
        used = setup_text_add(out, cap, used, "\n\n");
        used = setup_text_add(out, cap, used, view->summary);
    }
    return used;
}

static size_t setup_last_page(Idle *idle, const char *body, size_t length)
{
    size_t rows = 0, at = 0;
    size_t page = idle->tty.rows > 9 ? (size_t)(idle->tty.rows - 9) : 1u;
    TextRows layout = {0};
    while (at < length) {
        at = text_rows_next(idle, &layout, body, length, at, NULL, 0);
        ++rows;
    }
    text_rows_clear(&layout);
    return rows > page ? rows - page : 0;
}

static void setup_view(const Idle *idle, IdleSetupView *view)
{
    idle_setup_view(idle->setup, view);
    if (idle->setup_result) {
        (void)snprintf(view->question, sizeof view->question, "Finishing setup");
        (void)snprintf(view->current, sizeof view->current, "Discarding pending input");
        (void)snprintf(view->detail, sizeof view->detail, "The setup result appears after the current paste and queued input finish.");
        view->summary[0] = 0;
        view->secret = 0;
    }
}

static void setup_scroll(Idle *idle, int forward)
{
    IdleSetupView view;
    char body[SETUP_TEXT_BYTES];
    setup_view(idle, &view);
    size_t length = setup_body(&view, body, sizeof body);
    size_t page = idle->tty.rows > 9 ? (size_t)(idle->tty.rows - 9) : 1u;
    size_t last = setup_last_page(idle, body, length);
    if (forward) {
        idle->setup_scroll += page;
        if (idle->setup_scroll > last) idle->setup_scroll = last;
    } else idle->setup_scroll = idle->setup_scroll > page ? idle->setup_scroll - page : 0;
    idle->redraw = 1;
}

static void draw_setup(Idle *idle)
{
    if (!idle->setup || !idle->redraw || idle->output_length || background_detached(idle->background)) return;
    idle->setup_frame = 0;
    IdleSetupView view;
    setup_view(idle, &view);
    Frame frame = {{0}, 0, 0};
    int entering = !idle->alternate;
    if (entering) { frame_add(&frame, "\033[?1049h", 8); idle->alternate = 1; }
    frame_add(&frame, "\033[0m\033[2J\033[H\033[?25h", sizeof "\033[0m\033[2J\033[H\033[?25h" - 1);
    char title[512];
    (void)snprintf(title, sizeof title, "%s - %s", idle->program, view.title);
    frame_text(idle, &frame, 1, title);
    if (idle->tty.rows < 10 || idle->tty.cols < 12) {
        frame_text(idle, &frame, 2, irc_tr("Terminal too small to display these fields."));
        idle->setup_drawn = 0;
    } else {
        char rule[500], body[SETUP_TEXT_BYTES];
        size_t width = (size_t)(idle->tty.cols - 1);
        memset(rule, '-', width);
        frame_printf(&frame, "\033[2;1H"); frame_add(&frame, rule, width);
        if (idle->setup_help_focus) frame_add(&frame, "\033[7m", sizeof "\033[7m" - 1);
        frame_text(idle, &frame, 3, view.question);
        if (idle->setup_help_focus) frame_add(&frame, "\033[0m", sizeof "\033[0m" - 1);
        size_t length = setup_body(&view, body, sizeof body);
        size_t last = setup_last_page(idle, body, length);
        if (idle->setup_scroll > last) idle->setup_scroll = last;
        size_t at = 0;
        TextRows rows = {0};
        for (size_t n = 0; n < idle->setup_scroll && at < length; ++n)
            at = text_rows_next(idle, &rows, body, length, at, NULL, 0);
        for (int row = 4; row <= idle->tty.rows - 6 && at < length; ++row)
            at = text_rows_next(idle, &rows, body, length, at, &frame, row);
        text_rows_clear(&rows);
        frame_text(idle, &frame, idle->tty.rows - 5,
                   irc_tr(view.final ? "Choice:" : "Answer:"));
        EditorPosition caret = frame_editor(idle, &frame, idle->tty.rows - 4, 2, view.secret);
        if (*idle->notice) frame_text(idle, &frame, idle->tty.rows - 2, idle->notice);
        frame_printf(&frame, "\033[%d;1H", idle->tty.rows - 1); frame_add(&frame, rule, width);
        frame_printf(&frame, "\033[%d;%dH%s", caret.screen_row, caret.col + 1,
                     idle->setup_help_focus ? "\033[?25l" : "\033[?25h");
    }
    int complete_setup_frame = !frame.overflow && idle->tty.rows >= 10 && idle->tty.cols >= 12;
    if (frame.overflow) {
        frame.used = 0; frame.overflow = 0;
        if (entering) frame_add(&frame, "\033[?1049h", 8);
        frame_add(&frame, "\033[2J\033[H\033[?25h", sizeof "\033[2J\033[H\033[?25h" - 1);
        frame_text(idle, &frame, 1, irc_tr("Display capacity reached; settings draft retained."));
        idle->setup_drawn = 0;
    }
    if (output_add(idle, frame.data, frame.used) && complete_setup_frame) idle->setup_frame = 1;
    idle->redraw = 0;
}

/* Editor/setup transitions retire presentation without sending draft text. */
static void leave_editor(Idle *idle)
{
    if (idle->alternate) {
        /* Only an alternate-screen frame can be pending here. Discard its
         * unsent remainder so Escape is not trapped behind terminal output. */
        idle->output_start = idle->output_length = 0;
        idle->session_notice_remaining = 0;
        if (!output_literal(idle, "\033[?1049l\033[0m\033[?25h")) {
            idle->failed = 1;
            return;
        }
        idle->alternate = 0;
    }
    idle->redraw = 0;
}

static void reset_input(Idle *idle)
{
    config_wipe(&idle->editor, sizeof idle->editor);
    ed_init(&idle->editor);
    config_wipe(&idle->pending, sizeof idle->pending);
    config_wipe(&idle->history_original, sizeof idle->history_original);
    idle->browsing = idle->completing = idle->paste = idle->paste_rejected = 0;
    idle->paste_completed = idle->paste_drawn = 0;
    idle->pending_review_draw = idle->review_drawn = 0;
    idle->pending_token = 0;
    idle->suggestion_index = 0;
    idle->suggestion_selected = idle->suggestion_inserted = idle->suggestion_drawn = 0;
    idle->limit_pending = idle->limit_confirmed = 0;
    idle->hotkey_return_guard = idle->editor_frame = idle->hotkey_return_drawn = 0;
    idle->notice[0] = 0;
    idle->notice_pending = 0;
    nick_completion_reset(&idle->nick_cycle);
}

static void guard_restored_editor(Idle *idle)
{
    idle->hotkey_batch = idle->input_batch;
    idle->hotkey_return_guard = 1;
    idle->editor_frame = idle->hotkey_return_drawn = 0;
    idle->hotkey_return_at = now_ms();
}

static void setup_question(Idle *idle)
{
    reset_input(idle);
    idle->setup_scroll = 0;
    idle->setup_batch = idle->input_batch;
    idle->setup_enter_time = now_ms();
    idle->setup_drawn = 0;
    idle->setup_help_focus = 0;
    idle->transition_batch = idle->input_batch;
    idle->redraw = 1;
}

static int begin_setup(Idle *idle, int quick)
{
    char error[512];
    IdleSetup *setup = idle_setup_new(idle->client, quick, error, sizeof error);
    if (!setup) {
        notice(idle, error);
        return 0;
    }
    response_reset(idle);
    idle->setup = setup;
    idle->mode = SETUP;
    setup_question(idle);
    return 1;
}

static void finish_setup(Idle *idle, const char *message)
{
    idle_setup_free(idle->setup);
    idle->setup = NULL;
    leave_editor(idle);
    idle->mode = WATCH;
    reset_input(idle);
    idle->transition_batch = idle->input_batch;
    idle->watch_escape_after = now_ms() + ESCAPE_MS;
    attach_apps(idle);
    context_update(idle, 0);
    notice(idle, message);
}

static void submit_setup(Idle *idle)
{
    char answer[ED_MAX_BYTES + 1], message[512];
    if (ed_to_utf8(&idle->editor, answer, sizeof answer) == SIZE_MAX) return;
    int result = idle_setup_answer(idle->setup, answer, message, sizeof message);
    config_wipe(answer, sizeof answer);
    if (result == 1) setup_question(idle);
    else if (result == 2) {
        int committed = idle_setup_commit(idle->setup, message, sizeof message);
        if (committed == 1) finish_setup(idle, message);
        else {
            idle->setup_batch = idle->input_batch;
            idle->setup_drawn = 0;
            if (committed == 2) {
                config_wipe(&idle->editor, sizeof idle->editor);
                ed_init(&idle->editor);
            }
            notice(idle, message);
        }
    } else notice(idle, message);
    config_wipe(message, sizeof message);
    idle->redraw = 1;
}

static void poll_setup(Idle *idle)
{
    if (!idle->setup) return;
    if (idle->setup_result) {
        if (idle->paste || now_ms() < idle->setup_settle_at) return;
        char message[sizeof idle->setup_message];
        int result = idle->setup_result;
        int guarded_paste = idle->paste_completed;
        memcpy(message, idle->setup_message, sizeof message);
        idle->setup_result = 0;
        config_wipe(idle->setup_message, sizeof idle->setup_message);
        if (result > 0) finish_setup(idle, message);
        else {
            setup_question(idle);
            notice(idle, message);
        }
        /* Resetting an editor must not erase the input boundary protecting
         * an asynchronous result. The new log/question needs its own visible
         * output and quiet interval before any suffix key can act. */
        if (guarded_paste) {
            idle->paste_completed = 1;
            idle->paste_drawn = 0;
            idle->paste_batch = idle->input_batch;
            idle->paste_review_at = now_ms();
        }
        config_wipe(message, sizeof message);
        return;
    }
    if (!idle_setup_pending(idle->setup)) return;
    char message[512];
    int result = idle_setup_poll(idle->setup, now_ms(), message, sizeof message);
    if (result) {
        /* The helper is already finished/reaped. Keep consuming a fragmented
         * paste as inert input until its exact closing marker, then let queued
         * key repeats settle before a success/failure changes the screen. */
        idle->setup_result = result;
        irc_sanitize(idle->setup_message, sizeof idle->setup_message, message);
        idle->setup_settle_at = now_ms() + SETUP_SETTLE_MS;
        config_wipe(&idle->editor, sizeof idle->editor);
        ed_init(&idle->editor);
        idle->redraw = 1;
    }
    config_wipe(message, sizeof message);
}

static void begin_editor(Idle *idle, IdleMode mode)
{
    App *app = current_app(idle);
    ClientSlot *slot = app ? slot_for_app(idle, app) : NULL;
    if (!app || !slot) return;
    CommandContext context = idle->mode == READING ? idle->response_context :
        app->buffers[app->current].kind == B_CHANNEL ? COMMAND_CONTEXT_CHANNEL :
        app->buffers[app->current].kind == B_QUERY ? COMMAND_CONTEXT_QUERY :
        app->buffers[app->current].kind == B_SERVER ? COMMAND_CONTEXT_SERVER : COMMAND_CONTEXT_CLIENT;
    idle->editor_return = idle->mode == READING ? READING : WATCH;
    reset_input(idle);
    idle->mode = mode;
    idle->compose_token = slot->token;
    idle->compose_kind = app->buffers[app->current].kind;
    idle->suggestion_context = context;
    (void)snprintf(idle->compose_target, sizeof idle->compose_target, "%s", app->buffers[app->current].name);
    if (mode == COMMAND) (void)ed_insert(&idle->editor, L'/');
    idle->redraw = 1;
}

static void open_reader(Idle *idle, IdleMode return_mode)
{
    idle->mode = READING;
    idle->reader_return = return_mode;
    idle->response_offset = 0;
    idle->notice[0] = 0; idle->notice_pending = 0;
    idle->redraw = 1;
    idle->transition_batch = idle->input_batch;
}

static void context_help_line(void *context, const char *line)
{
    Idle *idle = context;
    if (idle->help_length + 2 >= sizeof idle->help_text) return;
    size_t remaining = sizeof idle->help_text - idle->help_length - 2;
    size_t added = irc_sanitize(idle->help_text + idle->help_length, remaining + 1, line);
    idle->help_length += added;
    idle->help_text[idle->help_length++] = '\n';
    idle->help_text[idle->help_length] = 0;
}

static void context_help_open(Idle *idle)
{
    IdleMode origin = idle->mode;
    NativeSuggestion selected;
    if (origin == COMMAND && !selected_suggestion(idle, &selected)) return;
    idle->help_return = origin;
    idle->help_parent = 0; idle->help_offset = idle->help_length = 0;
    idle->help_text[0] = idle->help_screen_text[0] = 0;
    char intro[1024];
    if (origin == COMMAND) {
        (void)snprintf(idle->help_screen, sizeof idle->help_screen, "command screen");
        (void)snprintf(idle->help_item, sizeof idle->help_item, "/%s", selected.spec->name);
        (void)snprintf(intro, sizeof intro, "%s is a suggestion on the command screen.", idle->help_item);
        context_help_line(idle, intro); context_help_line(idle, "");
        IdleCommands help = idle->commands;
        help.print = context_help_line; help.context = idle; help.dcc = idle->dcc;
        char request[96]; SendPlan unused;
        (void)snprintf(request, sizeof request, "/help %s", selected.spec->name);
        (void)idle_command(&help, request, &unused);
        config_wipe(&unused, sizeof unused);
        (void)snprintf(idle->help_screen_text, sizeof idle->help_screen_text,
            "Command screen\n\nCommands are edited separately from chat messages.\n"
            "Down selects a suggestion; Up moves toward the input. Enter inserts the selected suggestion without executing it.\n"
            "Read or edit the inserted command, then press Enter again to run it.\n"
            "Tab completes command names. Up from the input recalls recent commands.\n"
            "Delete removes a selected history entry, or resets a selected Frequent command's usage count.\n"
            "Contextual ordering remembers the screen that opened command entry. Other matching commands remain available.\n"
            "A question mark while typing is ordinary text. On a selected suggestion it opens this help.\n"
            "Escape returns to the originating log or response.\n");
    } else if (origin == SETUP) {
        IdleSetupView view; idle_setup_view(idle->setup, &view);
        (void)snprintf(idle->help_screen, sizeof idle->help_screen, "%s", view.title);
        (void)snprintf(idle->help_item, sizeof idle->help_item, "%s", view.question);
        (void)snprintf(intro, sizeof intro, "%s is an option on the %s screen.", view.question, view.title);
        context_help_line(idle, intro); context_help_line(idle, "");
        context_help_line(idle, view.question); context_help_line(idle, view.detail);
        (void)snprintf(idle->help_screen_text, sizeof idle->help_screen_text,
            "Settings and Quickstart\n\nThe selected question edits a private configuration draft.\n"
            "Type an answer and press Enter to continue. An empty answer keeps the displayed value unless the question requests confirmation.\n"
            "Escape returns to the previous question; Escape from the first question cancels the draft.\n"
            "Page Up and Page Down scroll long question details.\n"
            "Tab selects the question heading; ? then opens its help. Tab or Enter returns to the answer.\n"
            "Question marks in the answer field are ordinary text, including the first character.\n"
            "Changes take effect only after the final Save or Apply action. Connection changes apply on the next connection.\n"
            "Application preferences apply across the client; server settings belong to the selected server profile.\n");
    } else if (origin == READING) {
        (void)snprintf(idle->help_screen, sizeof idle->help_screen, "command response reader");
        (void)snprintf(idle->help_item, sizeof idle->help_item, "%s",
            idle->response_summary ? "/summary report" : idle->response_help ? irc_tr("Help text") : irc_tr("Command response"));
        (void)snprintf(intro, sizeof intro, "%s is displayed in the command response reader.", idle->help_item);
        context_help_line(idle, intro); context_help_line(idle, "");
        if (idle->response_summary) {
            context_help_line(idle, "The summary is a snapshot of all configured server sessions, channel and query views, activity, pending work, and file transfers.");
            context_help_line(idle, "Mentions can also be unread messages; their counts are not added together.");
            context_help_line(idle, "Press r in the summary reader to refresh from current state. Reading or refreshing does not send an IRC command.");
        } else context_help_line(idle, "This reader retains the result while you inspect it. The underlying conversation and unfinished command remain available.");
        (void)snprintf(idle->help_screen_text, sizeof idle->help_screen_text,
            "Command response reader\n\nUp and Down scroll one line. Page Up, Page Down, Space, and b scroll a page.\n"
            "Home or g goes to the beginning; End or G goes to the end.\n"
            "Escape or q returns to the log or the command that opened this response.\n"
            "Slash opens command entry from this reader; cancelling returns here.\n"
            "The summary reader accepts r to refresh its snapshot. ? explains the response and this reader.\n");
    } else {
        App *app = current_app(idle);
        const Buffer *view = app && app->current < app->buffer_count ? &app->buffers[app->current] : NULL;
        const char *kind = view && view->kind == B_CHANNEL ? irc_tr("Channel") :
            view && view->kind == B_QUERY ? irc_tr("Query") : view && view->kind == B_MENTIONS ? irc_tr("Mentions") : irc_tr("Server");
        (void)snprintf(idle->help_screen, sizeof idle->help_screen, "watch log");
        (void)snprintf(idle->help_item, sizeof idle->help_item, "%s %s", kind, view ? view->name : "(none selected)");
        (void)snprintf(intro, sizeof intro, "%s is selected in the watch log.", idle->help_item);
        context_help_line(idle, intro); context_help_line(idle, "");
        if (view && view->kind == B_CHANNEL)
            context_help_line(idle, "A channel is a shared conversation on its server. /me sends an action, /topic reads or edits its topic, /users lists its users, and /part leaves it.");
        else if (view && view->kind == B_QUERY)
            context_help_line(idle, "A query is a private conversation on its server. /me sends an action, /whois requests information, and /close closes its local view.");
        else if (view && view->kind == B_MENTIONS)
            context_help_line(idle, "Mentions collect messages that matched your nickname or watch terms. Their original conversations remain separate.");
        else context_help_line(idle, "The server log contains connection information and replies. /join opens a channel; /list searches the network's channels; /disconnect closes this connection.");
        (void)snprintf(idle->help_screen_text, sizeof idle->help_screen_text,
            "Watch log\n\nThe log follows the selected server and conversation. A channel or query receives composed messages.\n"
            "Slash opens command entry. An ordinary character starts composition. ? opens help for the selected conversation.\n"
            "Use /servers, /channels, /queries, /unread, or /summary to inspect client activity.\n"
            "Select conversations with /channel, /query, or /view.\n"
            "Escape backgrounds from the log. Inside composition or a reader, Escape first returns to the previous screen.\n");
    }
    idle->mode = CONTEXT_HELP; idle->redraw = 1;
    idle->transition_batch = idle->input_batch;
}

static const char *context_help_text(const Idle *idle, size_t *length)
{
    const char *text = idle->help_parent ? idle->help_screen_text : idle->help_text;
    *length = strlen(text);
    return text;
}

static void context_help_key(Idle *idle, uint32_t key)
{
    if (idle->input_batch == idle->transition_batch) return;
    if (key == IK_ESCAPE || key == 'q') {
        if (idle->help_parent) { idle->help_parent = 0; idle->help_offset = 0; }
        else {
            idle->mode = idle->help_return;
            if (idle->mode == WATCH) {
                leave_editor(idle);
                idle->watch_escape_after = now_ms() + ESCAPE_MS;
            } else if (idle->mode == SETUP) {
                idle->setup_drawn = 0;
                idle->setup_enter_time = now_ms();
            } else if (idle->mode == COMPOSE) {
                guard_restored_editor(idle);
            }
            config_wipe(idle->help_text, sizeof idle->help_text);
            config_wipe(idle->help_screen_text, sizeof idle->help_screen_text);
        }
        idle->transition_batch = idle->input_batch;
    } else if (!idle->help_parent && (key == '\r' || key == '\n' || key == IK_RIGHT)) {
        idle->help_parent = 1; idle->help_offset = 0;
        idle->transition_batch = idle->input_batch;
    } else {
        size_t length; const char *text = context_help_text(idle, &length);
        size_t page = idle->tty.rows > 4 ? (size_t)(idle->tty.rows - 4) : 1u;
        TextRows rows = {0};
        if (key == IK_HOME || key == 'g') idle->help_offset = 0;
        else if (key == IK_END || key == 'G') {
            idle->help_offset = length;
            for (size_t i = 0; i < page && idle->help_offset; ++i)
                idle->help_offset = text_rows_previous(idle, &rows, text, length, idle->help_offset);
        } else if (key == IK_UP || key == 'k' || key == IK_PAGE_UP || key == 'b') {
            size_t amount = key == IK_PAGE_UP || key == 'b' ? page : 1;
            while (amount-- && idle->help_offset)
                idle->help_offset = text_rows_previous(idle, &rows, text, length, idle->help_offset);
        } else if (key == IK_DOWN || key == 'j' || key == IK_PAGE_DOWN || key == ' ') {
            size_t amount = key == IK_PAGE_DOWN || key == ' ' ? page : 1;
            while (amount-- && idle->help_offset < length) {
                size_t next = text_rows_next(idle, &rows, text, length, idle->help_offset, NULL, 0);
                if (next >= length) break;
                idle->help_offset = next;
            }
        }
        text_rows_clear(&rows);
    }
    idle->redraw = 1;
}

static void draw_context_help(Idle *idle)
{
    if (!idle->redraw || idle->output_length || background_detached(idle->background)) return;
    Frame frame = {{0}, 0, 0};
    if (!idle->alternate) { frame_add(&frame, "\033[?1049h", 8); idle->alternate = 1; }
    frame_add(&frame, "\033[0m\033[2J\033[H\033[?25l", sizeof "\033[0m\033[2J\033[H\033[?25l" - 1);
    frame_text(idle, &frame, 1, irc_tr("Context help"));
    if (idle->tty.rows >= 5 && idle->tty.cols >= 12) {
        char label[512];
        (void)snprintf(label, sizeof label, idle->help_parent ? "About %s" : "[About %s]", idle->help_screen);
        if (!idle->help_parent) frame_add(&frame, "\033[7m", sizeof "\033[7m" - 1);
        frame_text(idle, &frame, 2, label);
        frame_add(&frame, "\033[0m", sizeof "\033[0m" - 1);
        size_t length; const char *text = context_help_text(idle, &length);
        size_t at = idle->help_offset;
        TextRows rows = {0};
        for (int row = 4; row <= idle->tty.rows && at < length; ++row)
            at = text_rows_next(idle, &rows, text, length, at, &frame, row);
        text_rows_clear(&rows);
    } else frame_text(idle, &frame, 2, irc_tr("Terminal too small; help retained."));
    if (!frame.overflow) (void)output_add(idle, frame.data, frame.used);
    idle->redraw = 0;
}

static void refresh_summary(Idle *idle)
{
    if (!idle->response_summary) return;
    char *previous = idle->response;
    size_t old_length = idle->response_length, old_capacity = idle->response_capacity;
    size_t old_offset = idle->response_offset, old_lines = idle->response_lines;
    int old_overflow = idle->response_overflow;
    idle->response = NULL;
    idle->response_length = idle->response_capacity = idle->response_offset = idle->response_lines = 0;
    idle->response_active = idle->response_overflow = 0;
    idle->mode = COMMAND;
    idle->commands.dcc = idle->dcc;
    SendPlan unused;
    int result = idle_command(&idle->commands, "/summary", &unused);
    config_wipe(&unused, sizeof unused);
    idle->mode = READING;
    if (result == IDLE_COMMAND_DONE && idle->response_active && !idle->response_overflow) {
        if (previous) config_wipe(previous, old_capacity);
        free(previous);
        idle->notice[0] = 0; idle->notice_pending = 0;
    } else {
        if (idle->response) config_wipe(idle->response, idle->response_capacity);
        free(idle->response);
        idle->response = previous; idle->response_length = old_length;
        idle->response_capacity = old_capacity; idle->response_offset = old_offset;
        idle->response_lines = old_lines; idle->response_active = 1;
        idle->response_overflow = old_overflow;
        notice(idle, "Refresh could not complete; the previous summary is retained.");
    }
    idle->response_help = idle->response_roster = 0;
    idle->response_context = COMMAND_CONTEXT_CLIENT;
    idle->redraw = 1;
}

/* Input recall is separate from chat scrollback and persistent logging.
 * Retain accepted, non-sensitive command text under its runtime server token;
 * usage persistence above the filter records command names/counts only. */
static void history_add(Idle *idle, uint64_t token, const char *text, const SendPlan *plan)
{
    App *app = app_for_token(idle, token);
    if (app) { commands_usage_record(&app->cfg, text); save_usage(idle, app); }
    if ((plan && !app_plan_history_safe(plan)) || !idle_command_recordable(text)) return;
    size_t bytes = strlen(text) + 1;
    if (bytes > ED_MAX_BYTES + 1u) return;
    if (idle->history_count && idle->history[0].token == token && !strcmp(idle->history[0].text, text)) return;
    char *copy = malloc(bytes);
    if (!copy) return;
    memcpy(copy, text, bytes);
    while (idle->history_count && (idle->history_count == HISTORY_LINES ||
           idle->history_bytes > HISTORY_BYTES - bytes)) {
        char *old = idle->history[--idle->history_count].text;
        idle->history_bytes -= strlen(old) + 1; config_wipe(old, strlen(old)); free(old);
    }
    memmove(idle->history + 1, idle->history, idle->history_count * sizeof idle->history[0]);
    idle->history[0].text = copy; idle->history[0].token = token;
    ++idle->history_count; idle->history_bytes += bytes;
}

/* The original draft is the stable prefix for the whole browse cycle.
 * Searching with the previously recalled row instead would narrow matches
 * on every repeated Up event and strand the user in one history entry. */
static void history_move(Idle *idle, int older)
{
    if (!idle->browsing) {
        if (!older) return;
        idle->history_original = idle->editor; idle->history_index = 0;
    } else if (older) ++idle->history_index;
    else if (!idle->history_index) {
        idle->editor = idle->history_original; idle->browsing = 0; return;
    } else --idle->history_index;
    char prefix[ED_MAX_BYTES + 1];
    if (ed_to_utf8(&idle->history_original, prefix, sizeof prefix) == SIZE_MAX) return;
    size_t found = 0;
    const char *first = NULL, *selected = NULL;
    for (size_t i = 0; i < idle->history_count; ++i) {
        if (idle->history[i].token != idle->compose_token ||
            strncmp(idle->history[i].text, prefix, strlen(prefix))) continue;
        if (!first) first = idle->history[i].text;
        if (found++ == idle->history_index) { selected = idle->history[i].text; break; }
    }
    if (!selected) { selected = first; idle->history_index = 0; }
    if (selected) { (void)ed_from_utf8(&idle->editor, selected); idle->browsing = 1; }
    else notice(idle, irc_tr("No matching command history."));
    config_wipe(prefix, sizeof prefix);
}

static void complete_command(Idle *idle, int reverse)
{
    char text[ED_MAX_BYTES + 1];
    if (!idle->completing) {
        if (ed_to_utf8(&idle->editor, text, sizeof text) == SIZE_MAX ||
            text[0] != '/' || strchr(text, ' ') || strlen(text) >= sizeof idle->completion_prefix) {
            config_wipe(text, sizeof text); return;
        }
        memcpy(idle->completion_prefix, text + 1, strlen(text));
        idle->completion_index = reverse ? SIZE_MAX : 0;
    } else if (reverse) --idle->completion_index;
    else ++idle->completion_index;
    const IdleCommandSpec *matches[128];
    size_t count = 0;
    for (size_t i = 0; i < idle_command_count() && count < 128; ++i) {
        const IdleCommandSpec *spec = idle_command_spec(i);
        if (commands_name_matches(spec->name, idle->completion_prefix,
                                  strlen(idle->completion_prefix), 0)) matches[count++] = spec;
    }
    if (count) {
        if (idle->completion_index == SIZE_MAX) idle->completion_index = count - 1;
        idle->completion_index %= count;
        (void)snprintf(text, sizeof text, "/%s ", matches[idle->completion_index]->name);
        (void)ed_from_utf8(&idle->editor, text); idle->completing = 1;
    } else notice(idle, irc_tr("No matching command."));
    config_wipe(text, sizeof text);
}

static int review_enabled(const Idle *idle)
{
    const ClientSlot *primary = &idle->client->slots[0];
    return primary->app ? primary->app->cfg.send_review : primary->seed.send_review;
}

static int destination_valid(Idle *idle, App *app, const char *target, BufferKind kind, int required)
{
    if (!app || !app->registered) { notice(idle, "Destination disconnected; draft retained."); return 0; }
    size_t view = named_view(app, kind, target);
    if ((required && view == SIZE_MAX) ||
        (kind == B_CHANNEL && (view == SIZE_MAX || !app->buffers[view].joined))) {
        notice(idle, "Destination changed or channel was left; draft retained."); return 0;
    }
    return 1;
}

/* Review pins the measured plan and origin. Confirmation rechecks that
 * identity; changing the visible server is never permission to retarget it. */
static void begin_review(Idle *idle, App *app, int command)
{
    ClientSlot *slot = slot_for_app(idle, app);
    if (!slot) return;
    idle->pending_token = slot->token;
    idle->pending_kind = irc_valid_channel(idle->pending.target) ? B_CHANNEL : B_QUERY;
    idle->pending_requires_view = named_view(app, idle->pending_kind, idle->pending.target) != SIZE_MAX;
    idle->pending_from_command = command;
    idle->review_batch = idle->input_batch;
    idle->review_time = now_ms();
    idle->review_scroll_rows = 0;
    idle->notice[0] = 0;
    idle->notice_pending = 0;
    idle->pending_review_draw = 1;
    idle->review_drawn = 0;
    idle->mode = REVIEW;
}

static size_t review_page_rows(const Idle *idle)
{
    /* title + rule + status + question consume four rows */
    return idle->tty.rows > 5 ? (size_t)(idle->tty.rows - 4) : 1u;
}

static size_t review_total_rows(Idle *idle, char *preview, size_t cap)
{
    irc_sanitize(preview, cap, idle->pending.text);
    return stream_visual_rows(idle, preview);
}

static size_t review_max_scroll(Idle *idle)
{
    char preview[APP_TEXT + 256];
    size_t total = review_total_rows(idle, preview, sizeof preview);
    size_t page = review_page_rows(idle);
    config_wipe(preview, sizeof preview);
    return total > page ? total - page : 0;
}

static void review_move(Idle *idle, uint32_t key)
{
    size_t maximum = review_max_scroll(idle), page = review_page_rows(idle);
    size_t offset = idle->review_scroll_rows;
    if (offset > maximum) offset = maximum;
    if (key == IK_HOME) offset = 0;
    else if (key == IK_END) offset = maximum;
    else if (key == IK_UP) offset = offset ? offset - 1 : 0;
    else if (key == IK_DOWN) offset = offset < maximum ? offset + 1 : maximum;
    else if (key == IK_PAGE_UP) offset = offset > page ? offset - page : 0;
    else if (key == IK_PAGE_DOWN)
        offset = maximum - offset < page ? maximum : offset + page;
    if (offset != idle->review_scroll_rows) {
        idle->review_scroll_rows = offset;
        idle->pending_review_draw = 1;
        idle->review_drawn = 0;
    }
}

static void draw_review(Idle *idle)
{
    if ((!idle->pending_review_draw && !idle->redraw) || idle->output_length ||
        background_detached(idle->background)) return;
    Frame frame = {{0}, 0, 0};
    int entering = !idle->alternate;
    if (entering) { frame_add(&frame, "\033[?1049h", 8); idle->alternate = 1; }
    frame_add(&frame, "\033[0m\033[2J\033[H\033[?25l", sizeof "\033[0m\033[2J\033[H\033[?25l" - 1);
    char preview[APP_TEXT + 256];
    char heading[512];
    App *app = app_for_token(idle, idle->pending_token);
    ClientSlot *slot = app ? slot_for_app(idle, app) : NULL;
    if (slot)
        (void)snprintf(heading, sizeof heading, "Review S%zu %s: %zu message%s (%zu UTF-8 bytes).\r\n",
                       slot->index + 1u, idle->pending.target, idle->pending.count,
                       idle->pending.count == 1 ? "" : "s", strlen(idle->pending.text));
    else
        (void)snprintf(heading, sizeof heading, "Review unavailable conversation %s: %zu message%s (%zu UTF-8 bytes).\r\n",
                       idle->pending.target, idle->pending.count,
                       idle->pending.count == 1 ? "" : "s", strlen(idle->pending.text));
    /* Strip the CRLF used by the old streaming renderer; this is now a fixed
     * alternate-screen title. */
    heading[strcspn(heading, "\r\n")] = 0;
    if (idle->tty.rows < 6 || idle->tty.cols < 12) {
        frame_text(idle, &frame, 1, irc_tr("Terminal too small for message review; draft retained."));
    } else {
        frame_text(idle, &frame, 1, heading);
        char rule[500];
        size_t width = (size_t)(idle->tty.cols - 1);
        memset(rule, '-', width);
        frame_printf(&frame, "\033[2;1H"); frame_add(&frame, rule, width);

        size_t total = review_total_rows(idle, preview, sizeof preview);
        size_t page = review_page_rows(idle);
        size_t maximum = total > page ? total - page : 0;
        if (idle->review_scroll_rows > maximum) idle->review_scroll_rows = maximum;
        size_t at = 0, length = strlen(preview);
        TextRows rows = {0};
        for (size_t n = 0; n < idle->review_scroll_rows && at < length; ++n)
            at = text_rows_next(idle, &rows, preview, length, at, NULL, 0);
        int row = 3;
        for (size_t n = 0; n < page && row <= idle->tty.rows - 2 && at < length; ++n, ++row)
            at = text_rows_next(idle, &rows, preview, length, at, &frame, row);
        text_rows_clear(&rows);

        char status[512];
        if (*idle->notice) (void)snprintf(status, sizeof status, "%s", idle->notice);
        else if (maximum) {
            size_t last = idle->review_scroll_rows + page;
            if (last > total) last = total;
            (void)snprintf(status, sizeof status, "Review rows %zu-%zu of %zu | Up/Down | PgUp/PgDn | Home/End",
                           idle->review_scroll_rows + 1, last, total);
        }
        else status[0] = 0;
        frame_text(idle, &frame, idle->tty.rows - 1, status);
        frame_text(idle, &frame, idle->tty.rows,
                   idle->pending.count == 1 ? irc_tr("Send this message?") : irc_tr("Send these messages?"));
    }
    if (frame.overflow) {
        frame.used = 0; frame.overflow = 0;
        if (entering) frame_add(&frame, "\033[?1049h", 8);
        frame_add(&frame, "\033[2J\033[H\033[?25l", sizeof "\033[2J\033[H\033[?25l" - 1);
        frame_text(idle, &frame, 1, irc_tr("Display capacity reached; review retained."));
    }
    (void)output_add(idle, frame.data, frame.used);
    idle->pending_review_draw = 0;
    idle->redraw = 0;
    idle->review_time = now_ms();
    config_wipe(preview, sizeof preview);
}

static void confirm_send(Idle *idle)
{
    App *app = app_for_token(idle, idle->pending_token);
    if (!destination_valid(idle, app, idle->pending.target, idle->pending_kind, idle->pending_requires_view)) {
        idle->pending_review_draw = 1;
        idle->review_drawn = 0;
        return;
    }
    if (idle->pending.connection_id != app->connection_id) {
        notice(idle, "Connection changed after this draft was prepared. Return to editing and review it again; nothing sent.");
        return;
    }
    SendPlan revised;
    if (!app_plan(app, idle->pending.target, idle->pending.text, idle->pending.action, &revised)) return;
    if (revised.count != idle->pending.count || revised.budget != idle->pending.budget) {
        idle->pending = revised;
        idle->pending_review_draw = 1;
        idle->review_drawn = 0;
        idle->review_batch = idle->input_batch;
        idle->review_scroll_rows = 0;
        notice(idle, "The message limit changed. Review the updated message count before sending.");
    } else if (app_send_plan(app, &revised)) {
        if (idle->pending_from_command) {
            char text[ED_MAX_BYTES + 1];
            if (ed_to_utf8(&idle->editor, text, sizeof text) != SIZE_MAX)
                history_add(idle, idle->pending_token, text, &revised);
            config_wipe(text, sizeof text);
        }
        leave_editor(idle);
        idle->mode = WATCH; reset_input(idle); idle->transition_batch = idle->input_batch;
    }
    config_wipe(&revised, sizeof revised);
}

static void submit_editor(Idle *idle)
{
    if (!idle->editor.len) return;
    char text[ED_MAX_BYTES + 1];
    if (ed_to_utf8(&idle->editor, text, sizeof text) == SIZE_MAX) return;
    int command = idle->mode == COMMAND;
    App *app = app_for_token(idle, idle->compose_token);
    int result = IDLE_COMMAND_ERROR;
    if (command) {
        const IdleCommandSpec *execution = native_command(text);
        if (execution && !strcmp(execution->name, "catchup")) {
            const char *tail = text + 1 + strlen(execution->name);
            while (*tail == ' ') ++tail;
            if (*tail) notice(idle, "Usage: /catchup");
            else {
                uint64_t token = idle->compose_token;
                if (begin_catchup(idle)) history_add(idle, token, text, NULL);
            }
            config_wipe(text, sizeof text);
            return;
        }
        int summary = execution && !strcmp(execution->name, "summary");
        size_t view = named_view(app, idle->compose_kind, idle->compose_target);
        ClientSlot *slot = app ? slot_for_app(idle, app) : NULL;
        if ((!slot || view == SIZE_MAX) && !summary) {
            notice(idle, "The command's original conversation is no longer available.");
            config_wipe(text, sizeof text); return;
        }
        if (!summary) {
            idle->client->current = slot->index;
            app_switch(app, view);
        }
        /* The old reader stays available while a command is being edited.
         * Executing the next command replaces its response as one unit. */
        response_reset(idle);
        idle->editor_return = WATCH;
        idle->response_help = (!strncasecmp(text, "/help", 5) && (!text[5] || text[5] == ' ')) ||
                              (!strncasecmp(text, "/manual", 7) && (!text[7] || text[7] == ' '));
        idle->response_context = idle->suggestion_context;
        if (summary || idle->response_help || (execution &&
            (!strcmp(execution->name, "servers") || !strcmp(execution->name, "sl"))))
            idle->response_context = COMMAND_CONTEXT_CLIENT;
        else if (execution && (!strcmp(execution->name, "channels") || !strcmp(execution->name, "cl") ||
                 !strcmp(execution->name, "queries") || !strcmp(execution->name, "ql") || !strcmp(execution->name, "ls")))
            idle->response_context = COMMAND_CONTEXT_SERVER;
        if (execution && !strcmp(execution->name, "ls") && strstr(text, "servers"))
            idle->response_context = COMMAND_CONTEXT_CLIENT;
        idle->commands.dcc = idle->dcc;
        result = idle_command(&idle->commands, text, &idle->pending);
        idle->response_summary = summary && result == IDLE_COMMAND_DONE;
        if (result == IDLE_COMMAND_DCC) {
            const char *arguments = text + 1;
            while (*arguments && *arguments != ' ') ++arguments;
            while (*arguments == ' ') ++arguments;
            result = dcc_command(&idle->dcc, idle->client, app, arguments, command_print, idle) ?
                     IDLE_COMMAND_DONE : IDLE_COMMAND_ERROR;
            transfer_changes(idle, 0);
        }
        /* Classify only an accepted roster response. Usage errors keep the
         * ordinary response reader and its return to the command editor. */
        const char *name = text[0] == '/' ? text + 1 : text;
        while (*name == ' ') ++name;
        idle->response_roster = result == IDLE_COMMAND_DONE &&
                                commands_name_matches("users", name, strcspn(name, " "), 1);
        attach_apps(idle);
        app = current_app(idle);
    } else if (destination_valid(idle, app, idle->compose_target, idle->compose_kind, 1)) {
        if (idle->compose_kind != B_CHANNEL && idle->compose_kind != B_QUERY)
            notice(idle, "Choose a channel or query with a command before composing.");
        else if (app_plan(app, idle->compose_target, text, 0, &idle->pending)) result = IDLE_COMMAND_SEND;
    }
    if (result == IDLE_COMMAND_PANEL || result == IDLE_COMMAND_QUICKSTART) {
        if (begin_setup(idle, result == IDLE_COMMAND_QUICKSTART))
            history_add(idle, idle->compose_token, text, NULL);
    } else if (result == IDLE_COMMAND_SEND && app) {
        BufferKind kind = irc_valid_channel(idle->pending.target) ? B_CHANNEL : B_QUERY;
        if (!destination_valid(idle, app, idle->pending.target, kind, !command)) {
            idle->redraw = 1;
        } else if (review_enabled(idle) || idle->pending.count > 1) begin_review(idle, app, command);
        else if (app_send_plan(app, &idle->pending)) {
            if (command) history_add(idle, idle->compose_token, text, &idle->pending);
            leave_editor(idle); idle->mode = WATCH; reset_input(idle); idle->transition_batch = idle->input_batch;
        }
    } else if (result != IDLE_COMMAND_ERROR) {
        if (command) history_add(idle, idle->compose_token, text, NULL);
        int reader = idle->response_active &&
                     (idle->response_help || idle->response_roster || idle->response_lines > 1 || idle->response_overflow);
        reset_input(idle); idle->transition_batch = idle->input_batch;
        if (reader && result == IDLE_COMMAND_DONE) open_reader(idle, WATCH);
        else {
            leave_editor(idle); idle->mode = WATCH;
            context_update(idle, 0);
            if (result == IDLE_COMMAND_QUIT) idle->quit = 1;
            else if (result == IDLE_COMMAND_BACKGROUND) idle->background_requested = 1;
            else if (result == IDLE_COMMAND_CLEAR) clear_stream(idle);
            else if (result == IDLE_COMMAND_MENTIONS_CLEAR) clear_stream_mentions(idle);
            if (idle->response_active) response_notice(idle);
        }
    } else if (command && idle->response_active) {
        if (idle->response_help || idle->response_lines > 1 || idle->response_overflow)
            open_reader(idle, COMMAND);
        else response_notice(idle);
    }
    idle->redraw = 1;
    config_wipe(text, sizeof text);
}

/* Translate only terminal key identities. Printable text never participates
 * in binding lookup, and the paste decoder calls paste_character directly. */
static unsigned hotkey_key(uint32_t key)
{
    switch (key) {
    case IK_UP: return CONFIG_KEY_UP;
    case IK_DOWN: return CONFIG_KEY_DOWN;
    case IK_LEFT: return CONFIG_KEY_LEFT;
    case IK_RIGHT: return CONFIG_KEY_RIGHT;
    case IK_HOME: return CONFIG_KEY_HOME;
    case IK_END: return CONFIG_KEY_END;
    case IK_PAGE_UP: return CONFIG_KEY_PAGE_UP;
    case IK_PAGE_DOWN: return CONFIG_KEY_PAGE_DOWN;
    case IK_INSERT: return CONFIG_KEY_INSERT;
    case IK_DELETE: return CONFIG_KEY_DELETE;
    case 8: case 127: return CONFIG_KEY_BACKSPACE;
    case '\t': return CONFIG_KEY_TAB;
    case IK_SHIFT_TAB: return CONFIG_KEY_SHIFT_TAB;
    case '\r': case '\n': return CONFIG_KEY_ENTER;
    case IK_ESCAPE: return CONFIG_KEY_ESCAPE;
    default:
        if ((key >= 1 && key <= 26) ||
            (key >= CONFIG_KEY_F1 && key < CONFIG_KEY_F1 + 12) ||
            (key >= CONFIG_KEY_ALT + 'a' && key <= CONFIG_KEY_ALT + 'z') ||
            (key >= CONFIG_KEY_ALT + '0' && key <= CONFIG_KEY_ALT + '9')) return key;
        return 0;
    }
}

static int hotkey_has_draft(const Idle *idle)
{
    if (idle->mode == COMPOSE) return idle->editor.len != 0;
    if (idle->mode != COMMAND) return 0;
    return idle->editor.len != 0 &&
           !(idle->editor.len == 1 && idle->editor.data[0] == '/');
}

/* A binding selects an allowlisted local action, never user-supplied command
 * text. Reader actions borrow the existing editor without rewriting it.
 * Operations that need the sole editor or change its destination wait for an
 * explicit finish/cancel, so a shortcut cannot redirect an unfinished draft. */
static int hotkey_event(Idle *idle, uint32_t key)
{
    if (idle->mode != WATCH && idle->mode != COMPOSE && idle->mode != COMMAND) return 0;
    unsigned identity = hotkey_key(key);
    if (!identity) return 0;
    const ClientSlot *primary = &idle->client->slots[0];
    const Config *cfg = primary->app ? &primary->app->cfg : &primary->seed;
    int action = config_hotkey_lookup(cfg, identity);
    if (!action) return 0;
    if (idle->paste || idle->limit_pending ||
        idle->input_batch == idle->transition_batch ||
        (idle->hotkey_return_guard && !idle->hotkey_return_drawn) ||
        (idle->paste_completed && (!idle->paste_drawn || idle->input_batch == idle->paste_batch))) return 1;
    const ConfigHotkeyAction *spec = config_hotkey_action(action);
    if (!spec) return 1;
    idle->hotkey_batch = idle->input_batch;
    IdleMode origin = idle->mode;
    if (action == CONFIG_ACTION_COMPOSE || action == CONFIG_ACTION_COMMAND) {
        IdleMode next = action == CONFIG_ACTION_COMPOSE ? COMPOSE : COMMAND;
        if (next == origin) return 1;
        if (hotkey_has_draft(idle)) {
            notice(idle, "Finish or cancel the current draft before changing editors.");
            return 1;
        }
        begin_editor(idle, next);
        idle->transition_batch = idle->input_batch;
        return 1;
    }
    if (action == CONFIG_ACTION_CLEARINPUT) {
        if (origin != WATCH) {
            reset_input(idle);
            if (origin == COMMAND) (void)ed_insert(&idle->editor, L'/');
            notice(idle, irc_tr("Input cleared; nothing sent."));
        }
        idle->transition_batch = idle->input_batch;
        return 1;
    }
    if ((action == CONFIG_ACTION_NEXT || action == CONFIG_ACTION_PREV ||
         action == CONFIG_ACTION_MENTIONS || action == CONFIG_ACTION_SETTINGS ||
         action == CONFIG_ACTION_QUICKSTART) && hotkey_has_draft(idle)) {
        notice(idle, "Finish or cancel the current draft before changing its conversation or opening settings.");
        return 1;
    }
    if (action == CONFIG_ACTION_HELP && origin != COMMAND) {
        context_help_open(idle);
        return 1;
    }
    const char *command = action == CONFIG_ACTION_HELP ? "/help" : spec->command;
    if (!command) return 1;
    response_reset(idle);
    idle->mode = COMMAND; /* Collect the fixed action's response in its reader. */
    idle->commands.dcc = idle->dcc;
    SendPlan unused;
    int result = idle_command(&idle->commands, command, &unused);
    if (result == IDLE_COMMAND_DCC) {
        result = dcc_command(&idle->dcc, idle->client, current_app(idle), "", command_print, idle) ?
                 IDLE_COMMAND_DONE : IDLE_COMMAND_ERROR;
        transfer_changes(idle, 0);
    }
    config_wipe(&unused, sizeof unused);
    idle->mode = origin;
    attach_apps(idle);
    idle->transition_batch = idle->input_batch;
    if (result == IDLE_COMMAND_PANEL || result == IDLE_COMMAND_QUICKSTART) {
        (void)begin_setup(idle, result == IDLE_COMMAND_QUICKSTART);
    } else if (result == IDLE_COMMAND_QUIT) idle->quit = 1;
    else if (result == IDLE_COMMAND_BACKGROUND) idle->background_requested = 1;
    else if (result == IDLE_COMMAND_DONE || result == IDLE_COMMAND_ERROR) {
        idle->response_help = action == CONFIG_ACTION_HELP || action == CONFIG_ACTION_MANUAL;
        idle->response_summary = action == CONFIG_ACTION_SUMMARY && result == IDLE_COMMAND_DONE;
        idle->response_context = action == CONFIG_ACTION_CHANNELS || action == CONFIG_ACTION_QUERIES ?
                                 COMMAND_CONTEXT_SERVER : COMMAND_CONTEXT_CLIENT;
        if (idle->response_active && (idle->response_help || idle->response_lines > 1 || idle->response_overflow)) {
            open_reader(idle, origin);
            idle->response_keeps_draft = origin != WATCH;
        } else if (idle->response_active) response_notice(idle);
        if (result == IDLE_COMMAND_DONE && (action == CONFIG_ACTION_NEXT ||
            action == CONFIG_ACTION_PREV || action == CONFIG_ACTION_MENTIONS)) {
            if (origin != WATCH) begin_editor(idle, origin);
            else context_update(idle, 0);
        }
    } else notice(idle, "This shortcut action is unavailable here; draft retained.");
    idle->redraw = 1;
    return 1;
}

/* Escape, navigation and shortcuts can change the input owner just as Enter
 * can execute it. Keep the whole suffix of a completed paste inert until a
 * later batch, a quiet interval, and (for editors) a complete visible frame.
 * Reader pastes are ignored; their existing return/confirmation gates still
 * protect the editor or send review exposed by a later deliberate key. */
static int paste_review_ready(Idle *idle)
{
    if (!idle->paste_completed) return 1;
    int64_t when = now_ms();
    int needs_display = idle->mode == COMPOSE || idle->mode == COMMAND ||
                        idle->mode == SETUP || idle->mode == WATCH;
    if (idle->input_batch == idle->paste_batch || (needs_display && !idle->paste_drawn) ||
        when - idle->paste_review_at < REVIEW_DELAY_MS) {
        idle->paste_review_at = when;
        if (idle->setup_result) idle->setup_settle_at = when + SETUP_SETTLE_MS;
        return 0;
    }
    idle->paste_completed = 0;
    return 1;
}

/* Paste and hotkey review barriers precede any
 * dispatch, including Escape and navigation. A newly drawn screen is
 * required before buffered keys may activate a different control. */
static void key_event(Idle *idle, uint32_t key)
{
    if (!paste_review_ready(idle)) return;
    /* Do not interpret buffered Enter/repeats against a shortcut's new state
     * before its effect can be drawn. Literal paste has a separate consumer. */
    if (idle->hotkey_batch && idle->input_batch == idle->hotkey_batch) return;
    if ((idle->mode == COMPOSE || idle->mode == COMMAND) && idle->hotkey_return_guard &&
        (key == '\r' || key == '\n')) {
        int64_t when = now_ms();
        int fresh = idle->hotkey_return_drawn &&
                    when - idle->hotkey_return_at >= REVIEW_DELAY_MS;
        idle->hotkey_return_at = when;
        if (!fresh) return;
        idle->hotkey_return_guard = 0;
    }
    if (hotkey_event(idle, key)) return;
    /* New function/Alt identities have no implicit action when unbound. */
    if (key >= CONFIG_KEY_F1) return;
    if (key == 4 || key == 0) return;
    if (key == IK_ESCAPE && idle->mode != SETUP && idle->input_batch == idle->transition_batch) return;
    if (idle->mode == CONTEXT_HELP) { context_help_key(idle, key); return; }
    if (idle->mode == SETUP) {
        if (idle->setup_result) { idle->setup_settle_at = now_ms() + SETUP_SETTLE_MS; return; }
        if (key == IK_ESCAPE) {
            if (idle_setup_back(idle->setup)) setup_question(idle);
            else finish_setup(idle, "Setup cancelled.");
            return;
        }
        if (idle_setup_pending(idle->setup)) return;
        if (idle->input_batch == idle->setup_batch) return;
        if (key == '\t' || key == IK_SHIFT_TAB) {
            idle->setup_help_focus = !idle->setup_help_focus;
            idle->redraw = 1; return;
        }
        if (idle->setup_help_focus && key == '?') { context_help_open(idle); return; }
        if (idle->setup_help_focus && (key == '\r' || key == '\n')) {
            idle->setup_help_focus = 0;
            idle->setup_enter_time = now_ms(); idle->redraw = 1; return;
        }
        if (key == IK_PAGE_UP || key == IK_PAGE_DOWN) {
            setup_scroll(idle, key == IK_PAGE_DOWN); return;
        }
        if (key == '\r' || key == '\n') {
            int64_t when = now_ms();
            int fresh = idle->setup_drawn && when - idle->setup_enter_time >= REVIEW_DELAY_MS;
            idle->setup_enter_time = when;
            if (fresh && (!idle->paste_completed ||
                (idle->paste_drawn && idle->input_batch != idle->paste_batch))) submit_setup(idle);
            return;
        }
        idle->setup_help_focus = 0;
    }
    if (idle->mode == READING) {
        if (idle->input_batch == idle->transition_batch) return;
        if (key == IK_ESCAPE || key == 'q' ||
            (idle->response_roster && (key == '\r' || key == '\n'))) {
            IdleMode back = idle->reader_return;
            int keeps_draft = idle->response_keeps_draft;
            response_reset(idle);
            idle->mode = back;
            idle->transition_batch = idle->input_batch;
            if (back == COMMAND || back == COMPOSE) {
                if (keeps_draft) guard_restored_editor(idle);
                idle->redraw = 1;
            }
            else {
                leave_editor(idle); reset_input(idle);
                idle->watch_escape_after = now_ms() + ESCAPE_MS;
            }
        } else if (key == '?') context_help_open(idle);
        else if (key == 'r' && idle->response_summary) refresh_summary(idle);
        else if (key == '/') {
            if (idle->response_keeps_draft) {
                notice(idle, "Return to the retained draft before opening another command.");
                return;
            }
            idle->reader_return = WATCH;
            begin_editor(idle, COMMAND);
        } else reader_move(idle, key == '\r' || key == '\n' ? IK_DOWN : key);
        return;
    }
    if (idle->mode == SCROLLBACK) {
        size_t page = scrollback_page_rows(idle);
        size_t maximum = scrollback_max_offset(idle);
        if (key == IK_ESCAPE || key == IK_END || key == 'q') {
            leave_scrollback(idle); return;
        }
        if (key == IK_PAGE_UP) {
            if (idle->scrollback_rows < maximum) {
                size_t add = page;
                idle->scrollback_rows = maximum - idle->scrollback_rows < add ? maximum : idle->scrollback_rows + add;
            }
            idle->redraw = 1; return;
        }
        if (key == IK_PAGE_DOWN) {
            if (idle->scrollback_rows <= page) { leave_scrollback(idle); return; }
            idle->scrollback_rows -= page; idle->redraw = 1; return;
        }
        if (key == IK_HOME) {
            idle->scrollback_rows = maximum; idle->redraw = 1; return;
        }
        if (key == IK_UP) {
            if (idle->scrollback_rows < maximum) ++idle->scrollback_rows;
            idle->redraw = 1; return;
        }
        if (key == IK_DOWN) {
            if (idle->scrollback_rows <= 1) { leave_scrollback(idle); return; }
            --idle->scrollback_rows; idle->redraw = 1; return;
        }
        return;
    }
    if (idle->mode == CATCHUP) {
        size_t page = catchup_page_rows(idle);
        if (key == IK_ESCAPE) {
            catchup_close(idle);
            leave_editor(idle);
            idle->mode = WATCH; idle->transition_batch = idle->input_batch;
            idle->watch_escape_after = now_ms() + ESCAPE_MS;
            return;
        }
        if (key != IK_HOME && key != IK_UP && key != IK_PAGE_UP &&
            key != IK_DOWN && key != IK_PAGE_DOWN && key != IK_END) return;
        size_t total = catchup_prepare(idle);
        size_t maximum = total > page ? total - page : 0;
        if (idle->catchup_row > maximum) idle->catchup_row = maximum;
        if (key == IK_HOME) idle->catchup_row = 0;
        else if (key == IK_UP) idle->catchup_row = idle->catchup_row ? idle->catchup_row - 1u : 0u;
        else if (key == IK_PAGE_UP) idle->catchup_row = idle->catchup_row > page ? idle->catchup_row - page : 0u;
        else if (key == IK_DOWN) {
            if (idle->catchup_row < maximum) ++idle->catchup_row;
        } else if (key == IK_PAGE_DOWN)
            idle->catchup_row += maximum - idle->catchup_row < page ? maximum - idle->catchup_row : page;
        else if (key == IK_END) idle->catchup_row = maximum;
        idle->redraw = 1;
        return;
    }
    if (idle->mode == WATCH) {
        if (idle->input_batch == idle->transition_batch) return;
        if (key == IK_ESCAPE) {
            /* Escape from the main log follows the same safe detach path as
             * /background. Editors, readers and setup screens consume Escape
             * before reaching WATCH, so only the log view backgrounds. */
            idle->background_requested = 1;
            return;
        }
        if (key == IK_PAGE_UP || key == IK_HOME || key == IK_UP) {
            size_t maximum = scrollback_max_offset(idle);
            if (maximum) {
                size_t amount = key == IK_HOME ? maximum :
                                key == IK_PAGE_UP ? scrollback_page_rows(idle) : 1u;
                idle->scrollback_rows = maximum < amount ? maximum : amount;
                idle->mode = SCROLLBACK; idle->redraw = 1;
            }
            return;
        }
        if (key == IK_PAGE_DOWN || key == IK_END || key == IK_DOWN ||
            key == IK_LEFT || key == IK_RIGHT || key == IK_DELETE || key == 8 || key == 127)
            return;
        if (key == IK_INSERT) { begin_editor(idle, COMPOSE); return; }
        if (key == '\r' || key == '\n') { begin_editor(idle, COMPOSE); return; }
        if (key == '\t' || key == IK_SHIFT_TAB) { begin_editor(idle, COMPOSE); return; }
        if (key < 32) return;
        if (key == '?') { context_help_open(idle); return; }
        if (key == '/') { begin_editor(idle, COMMAND); return; }
        begin_editor(idle, COMPOSE);
    } else if (idle->mode == REVIEW) {
        if (key == IK_UP || key == IK_DOWN || key == IK_PAGE_UP || key == IK_PAGE_DOWN ||
            key == IK_HOME || key == IK_END) {
            review_move(idle, key);
            return;
        }
        if (key == IK_ESCAPE) {
            /* Review is a temporary confirmation screen. Escape means edit,
             * not discard: return to the existing composer/command editor
             * with the exact draft and caret state intact. */
            idle->pending_review_draw = idle->review_drawn = 0;
            config_wipe(&idle->pending, sizeof idle->pending);
            idle->pending_token = 0;
            idle->mode = idle->pending_from_command ? COMMAND : COMPOSE;
            idle->notice[0] = 0; idle->notice_pending = 0;
            idle->redraw = 1; idle->transition_batch = idle->input_batch;
            return;
        }
        if (key == '\r' || key == '\n') {
            int64_t when = now_ms();
            int fresh = idle->review_drawn && idle->input_batch != idle->review_batch &&
                        when - idle->review_time >= REVIEW_DELAY_MS;
            idle->review_time = when;
            if (fresh) confirm_send(idle);
            return;
        }
        if (key < 32 && key != 8 && key != '\t') return;
        if (idle->input_batch == idle->review_batch) return;
        /* Editing retires this confirmation even if terminal backpressure
         * prevented its frame from being queued. Otherwise its pending draw
         * would keep the poll loop awake after the editor has settled. */
        idle->pending_review_draw = idle->review_drawn = 0;
        config_wipe(&idle->pending, sizeof idle->pending);
        idle->pending_token = 0;
        idle->mode = idle->pending_from_command ? COMMAND : COMPOSE;
        idle->redraw = 1;
    }
    if ((idle->mode == COMPOSE || idle->mode == COMMAND) && idle->limit_pending) {
        if (key == '\r' || key == '\n') {
            idle->limit_pending = 0; idle->limit_confirmed = 1;
            notice(idle, "Message limit acknowledged. A split send will still require explicit confirmation.");
            return;
        }
        if (key != IK_ESCAPE && key != 8 && key != 127 && key != IK_DELETE &&
            key != IK_LEFT && key != IK_RIGHT && key != IK_HOME && key != IK_END &&
            key != IK_UP && key != IK_DOWN) {
            notice(idle, "Confirm the message-limit warning with Enter before adding more text.");
            return;
        }
        if (key == 8 || key == 127 || key == IK_DELETE) idle->limit_pending = 0;
    }
    if (key == IK_ESCAPE) {
        IdleMode back = idle->editor_return;
        reset_input(idle); idle->transition_batch = idle->input_batch;
        if (back == READING && idle->response_active) {
            idle->mode = READING; idle->redraw = 1; return;
        }
        leave_editor(idle); idle->mode = WATCH;
        idle->watch_escape_after = now_ms() + ESCAPE_MS;
        response_reset(idle);
        program_print(idle, "Draft cancelled; nothing sent."); return;
    }
    if (key == '\r' || key == '\n') {
        if (idle->paste_completed && (!idle->paste_drawn || idle->input_batch == idle->paste_batch)) return;
        if (idle->mode == COMMAND && idle->suggestion_selected) { suggestion_insert(idle); return; }
        if (idle->suggestion_inserted && (!idle->suggestion_drawn || idle->input_batch == idle->suggestion_batch)) return;
        submit_editor(idle); return;
    }
    if (idle->mode == COMMAND) {
        if (key == '?' && idle->suggestion_selected) { context_help_open(idle); return; }
        if (key == IK_DELETE && idle->suggestion_selected) { suggestion_delete(idle); return; }
        if (key == IK_DOWN && idle->browsing && !idle->suggestion_selected) {
            history_move(idle, 0); idle->redraw = 1; return;
        }
        if (key == IK_DOWN || key == IK_PAGE_UP || key == IK_PAGE_DOWN ||
            (key == IK_UP && idle->suggestion_selected)) {
            suggestion_move(idle, key); return;
        }
    }
    if (key == '\t' || key == IK_SHIFT_TAB) {
        idle->suggestion_selected = 0;
        if (idle->mode == COMMAND) complete_command(idle, key == IK_SHIFT_TAB);
        else {
            App *app = app_for_token(idle, idle->compose_token);
            size_t view = named_view(app, idle->compose_kind, idle->compose_target);
            if (app && view != SIZE_MAX) {
                size_t saved = app->current; app->current = view;
                (void)nick_complete(app, &idle->editor, &idle->nick_cycle, key == IK_SHIFT_TAB ? -1 : 1);
                app->current = saved;
            }
        }
        idle->redraw = 1; return;
    }
    idle->completing = 0; nick_completion_reset(&idle->nick_cycle);
    if (idle->mode == COMMAND && key == IK_UP) history_move(idle, 1);
    else {
        idle->suggestion_selected = 0; idle->suggestion_index = 0;
        idle->browsing = 0;
        int visual = idle->tty.utf8;
        if (idle->mode == SETUP) {
            IdleSetupView view; setup_view(idle, &view);
            if (view.secret) visual = 0;
        }
        int width = idle->tty.cols - 1;
        switch (key) {
        case IK_LEFT:
            if (visual) ed_visual_left(&idle->editor, width); else ed_left(&idle->editor);
            break;
        case IK_RIGHT:
            if (visual) ed_visual_right(&idle->editor, width); else ed_right(&idle->editor);
            break;
        case IK_HOME:
            if (visual) ed_visual_home(&idle->editor, width); else ed_home(&idle->editor);
            break;
        case IK_END:
            if (visual) ed_visual_end(&idle->editor, width); else ed_end(&idle->editor);
            break;
        case IK_UP:
            if (visual) ed_visual_move_vertical(&idle->editor, -1, width);
            else ed_move_vertical(&idle->editor, -1, width);
            break;
        case IK_DOWN:
            if (visual) ed_visual_move_vertical(&idle->editor, 1, width);
            else ed_move_vertical(&idle->editor, 1, width);
            break;
        case IK_DELETE: (void)ed_delete(&idle->editor); break;
        case 8: case 127: (void)ed_backspace(&idle->editor); break;
        default:
            if (key >= 32 && key < 0x110000 && !ed_insert(&idle->editor, (wchar_t)key))
                notice(idle, "Unsupported character or composition capacity reached; existing draft preserved.");
            break;
        }
    }
    idle->redraw = 1;
}

static void paste_character(Idle *idle, uint32_t key)
{
    if (idle->mode == REVIEW || idle->mode == READING || idle->mode == CONTEXT_HELP) return;
    idle->suggestion_selected = 0;
    idle->setup_help_focus = 0;
    if (idle->mode == SETUP && (idle_setup_pending(idle->setup) || idle->setup_result || idle->input_batch == idle->setup_batch)) {
        idle->paste_rejected = 1; return;
    }
    if (idle->mode == WATCH) begin_editor(idle, COMPOSE);
    if (key == '\r') { idle->paste_cr = 1; key = '\n'; }
    else {
        if (key == '\n' && idle->paste_cr) { idle->paste_cr = 0; return; }
        idle->paste_cr = 0;
    }
    if (key == '\t' || ((idle->mode == COMMAND || idle->mode == SETUP) && key == '\n')) key = ' ';
    if (!ed_insert(&idle->editor, (wchar_t)key)) idle->paste_rejected = 1;
    idle->redraw = 1;
}

static void scalar(Idle *idle, uint32_t key)
{
    if (idle->paste) paste_character(idle, key);
    else {
        key_event(idle, key);
        editor_limit_update(idle);
    }
}

static void text_byte(Idle *idle, unsigned char c)
{
    if (c == 4) return;
    if (!idle->tty.utf8 && c >= 128) {
        notice(idle, "ASCII terminal: non-ASCII input rejected; use a UTF-8 LC_CTYPE.");
        if (idle->paste) idle->paste_rejected = 1;
        return;
    }
    if (idle->utf8_remaining) {
        if ((c & 0xc0u) == 0x80u) {
            idle->utf8_value = (idle->utf8_value << 6) | (uint32_t)(c & 0x3fu);
            if (--idle->utf8_remaining == 0) {
                uint32_t value = idle->utf8_value;
                if (value >= idle->utf8_minimum && value <= 0x10ffffu && !(value >= 0xd800u && value <= 0xdfffu))
                    scalar(idle, value);
                else notice(idle, "Invalid UTF-8 input ignored.");
            }
            return;
        }
        idle->utf8_remaining = 0;
        notice(idle, "Incomplete UTF-8 input ignored.");
    }
    if (c < 128) scalar(idle, c);
    else if (c >= 0xc2u && c <= 0xdfu) {
        idle->utf8_value = c & 0x1fu; idle->utf8_minimum = 0x80u; idle->utf8_remaining = 1;
    } else if (c >= 0xe0u && c <= 0xefu) {
        idle->utf8_value = c & 0x0fu; idle->utf8_minimum = 0x800u; idle->utf8_remaining = 2;
    } else if (c >= 0xf0u && c <= 0xf4u) {
        idle->utf8_value = c & 0x07u; idle->utf8_minimum = 0x10000u; idle->utf8_remaining = 3;
    } else notice(idle, "Invalid UTF-8 input ignored.");
}

static void escape_complete(Idle *idle)
{
    static const struct { const char *sequence; unsigned offset; } functions[] = {
        {"\033OP", 0}, {"\033OQ", 1}, {"\033OR", 2}, {"\033OS", 3},
        {"\033[11~", 0}, {"\033[12~", 1}, {"\033[13~", 2}, {"\033[14~", 3},
        {"\033[15~", 4}, {"\033[17~", 5}, {"\033[18~", 6}, {"\033[19~", 7},
        {"\033[20~", 8}, {"\033[21~", 9}, {"\033[23~", 10}, {"\033[24~", 11},
        {"\033[[A", 0}, {"\033[[B", 1}, {"\033[[C", 2}, {"\033[[D", 3}, {"\033[[E", 4}
    };
    const char *s = idle->escape;
    uint32_t key = 0;
    if (!strcmp(s, "\033[200~")) {
        if (idle->mode == WATCH) begin_editor(idle, COMPOSE);
        idle->paste = 1; idle->paste_rejected = idle->paste_cr = 0;
    } else if (!strcmp(s, "\033[A") || !strcmp(s, "\033OA")) key = IK_UP;
    else if (!strcmp(s, "\033[B") || !strcmp(s, "\033OB")) key = IK_DOWN;
    else if (!strcmp(s, "\033[C") || !strcmp(s, "\033OC")) key = IK_RIGHT;
    else if (!strcmp(s, "\033[D") || !strcmp(s, "\033OD")) key = IK_LEFT;
    else if (!strcmp(s, "\033[H") || !strcmp(s, "\033OH") || !strcmp(s, "\033[1~") || !strcmp(s, "\033[7~")) key = IK_HOME;
    else if (!strcmp(s, "\033[F") || !strcmp(s, "\033OF") || !strcmp(s, "\033[4~") || !strcmp(s, "\033[8~")) key = IK_END;
    else if (!strcmp(s, "\033[2~")) key = IK_INSERT;
    else if (!strcmp(s, "\033[3~")) key = IK_DELETE;
    else if (!strcmp(s, "\033[Z")) key = IK_SHIFT_TAB;
    else if (!strcmp(s, "\033[5~")) key = IK_PAGE_UP;
    else if (!strcmp(s, "\033[6~")) key = IK_PAGE_DOWN;
    else for (size_t i = 0; i < sizeof functions / sizeof functions[0]; ++i) {
        if (!strcmp(s, functions[i].sequence)) { key = CONFIG_KEY_F1 + functions[i].offset; break; }
    }
    idle->escape_length = 0;
    if (key) key_event(idle, key);
}

/* Byte decoder owns fragmented UTF-8/escape/paste prefixes. Only completed
 * keys reach key_event(); paste content keeps its literal input owner. */
static void input_byte(Idle *idle, unsigned char c)
{
    static const char paste_end[] = "\033[201~";
    if (c == 4) {
        /* An explicitly bound Ctrl-D can act only as a standalone key. Keep
         * its historical inert behavior within pasted or fragmented input. */
        if (!idle->paste && !idle->escape_length && !idle->utf8_remaining) key_event(idle, 4);
        return;
    }
    if (idle->paste) {
        if (c == 27 && idle->utf8_remaining) {
            idle->utf8_remaining = 0;
            idle->paste_rejected = 1;
        }
        if (idle->escape_length || c == 27) {
            if (idle->escape_length < sizeof paste_end - 1 && c == (unsigned char)paste_end[idle->escape_length]) {
                idle->escape[idle->escape_length++] = (char)c;
                if (idle->escape_length == sizeof paste_end - 1) {
                    idle->escape_length = 0; idle->paste = 0;
                    idle->paste_completed = 1;
                    idle->paste_drawn = 0;
                    idle->paste_batch = idle->input_batch;
                    idle->paste_review_at = now_ms();
                    if (idle->setup_result) idle->setup_settle_at = now_ms() + SETUP_SETTLE_MS;
                    notice(idle, idle->paste_rejected ? irc_tr("Paste captured with rejected characters.") : irc_tr("Paste captured."));
                    editor_limit_update(idle);
                }
                return;
            }
            for (size_t i = 0; i < idle->escape_length; ++i) text_byte(idle, (unsigned char)idle->escape[i]);
            idle->escape_length = 0;
            if (c == 27) { idle->escape[idle->escape_length++] = (char)c; return; }
        }
        text_byte(idle, c); return;
    }
    if (idle->escape_length) {
        if (c == 27) {
            int standalone = idle->escape_length == 1;
            int allowed = idle->escape_background_allowed;
            idle->escape_length = 0;
            if (standalone) {
                if (idle->mode != WATCH || allowed) key_event(idle, IK_ESCAPE);
                if (idle->mode == WATCH && now_ms() < idle->watch_escape_after)
                    idle->watch_escape_after = now_ms() + ESCAPE_MS;
                return;
            }
            /* A new Escape cancels an incomplete CSI/SS3 prefix. It still
             * receives the normal standalone delay rather than becoming text. */
            idle->escape[0] = 27; idle->escape[1] = 0; idle->escape_length = 1;
            idle->escape_time = now_ms();
            idle->escape_background_allowed = idle->mode != WATCH || idle->escape_time >= idle->watch_escape_after;
            return;
        }
        if (idle->escape_length == 1 && c != '[' && c != 'O') {
            /* Lowercase Alt letters and digits have an unambiguous two-byte
             * encoding. Other prefixes remain inert, never standalone Esc. */
            idle->escape_length = 0;
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                key_event(idle, CONFIG_KEY_ALT + c);
            return;
        }
        if (idle->escape_length + 1 >= sizeof idle->escape) { idle->escape_length = 0; return; }
        idle->escape[idle->escape_length++] = (char)c;
        idle->escape[idle->escape_length] = 0;
        idle->escape_time = now_ms();
        if (idle->escape_length >= 3 && c >= 0x40u && c <= 0x7eu &&
            !(idle->escape_length == 3 && idle->escape[1] == '[' && c == '[')) escape_complete(idle);
        return;
    }
    if (c == 27) {
        idle->escape[0] = 27; idle->escape[1] = 0; idle->escape_length = 1; idle->escape_time = now_ms();
        idle->escape_background_allowed = idle->mode != WATCH || idle->escape_time >= idle->watch_escape_after;
        if (!idle->escape_background_allowed) idle->watch_escape_after = idle->escape_time + ESCAPE_MS;
        return;
    }
    if (c == '\n' && idle->cr) { idle->cr = 0; return; }
    idle->cr = c == '\r';
    text_byte(idle, c);
}

static void escape_timeout(Idle *idle)
{
    int grace = idle->escape_length == 1 ? ESCAPE_MS : ESCAPE_CONTINUATION_MS;
    if (!idle->paste && idle->escape_length && now_ms() - idle->escape_time >= grace) {
        int standalone = idle->escape_length == 1;
        int allowed = idle->escape_background_allowed;
        idle->escape_length = 0;
        if (standalone && (idle->mode != WATCH || allowed)) key_event(idle, IK_ESCAPE);
    }
}

static void paste_frame_drawn(Idle *idle)
{
    int frame = ((idle->mode == COMPOSE || idle->mode == COMMAND) && idle->editor_frame) ||
                (idle->mode == SETUP && idle->setup_frame);
    /* WATCH streams its result notice rather than painting an alternate
     * screen; its redraw flag is not a frame-completion signal. */
    int watch = idle->mode == WATCH && !idle->notice_pending;
    if (idle->paste_completed && !idle->paste_drawn && (watch || (frame && !idle->redraw)) &&
        !idle->output_length && !background_detached(idle->background)) {
        idle->paste_drawn = 1;
        idle->paste_review_at = now_ms();
    }
}

/* Terminal attachment changes invalidate prior frame/input approval. */
static int restore_terminal(Idle *idle)
{
    const ClientSlot *primary = &idle->client->slots[0];
    const Config *cfg = primary->app ? &primary->app->cfg : &primary->seed;
    if (!terminal_start(&idle->tty, cfg->force_terminal_signals)) return 0;
    idle->output_start = idle->output_length = 0;
    idle->session_notice_remaining = 0;
    idle->alternate = 0;
    idle->escape_length = idle->utf8_remaining = 0;
    idle->editor_frame = idle->hotkey_return_drawn = idle->setup_frame = 0;
    if (idle->paste || idle->paste_completed) {
        if (idle->paste)
            notice(idle, "Paste interrupted by terminal change; review the retained draft before submitting.");
        idle->paste = idle->paste_cr = 0;
        idle->paste_completed = 1;
        idle->paste_drawn = 0;
        idle->paste_batch = idle->input_batch;
        idle->paste_review_at = now_ms();
    }
    (void)output_literal(idle, "\033[?2004h\033[?25h");
    idle->redraw = 1;
    if (idle->mode == SETUP) idle->setup_drawn = 0;
    if (idle->mode == REVIEW) {
        idle->pending_review_draw = 1;
        idle->review_drawn = 0;
    }
    return 1;
}

static void release_terminal(Idle *idle)
{
    idle->output_start = idle->output_length = 0;
    idle->session_notice_remaining = 0;
    /* A queued leave-editor sequence may not have reached a slow terminal
     * yet (notably /quit). An idempotent leave is required on every release. */
    terminal_stop(&idle->tty, 1);
    idle->alternate = 0;
}

static int detach_lost_terminal(Idle *idle)
{
    char error[512];
    /* The managed worker owns its IRC state independently of this tty.
     * Restoration is bounded and also restores the shared descriptor flags
     * before the background layer closes the old terminal and controller. */
    idle->background_requested = 0;
    idle->escape_length = idle->utf8_remaining = 0;
    suspend_signal = 0;
    release_terminal(idle);
    if (!background_detach(idle->background, &stop_signal, error, sizeof error)) {
        idle->failed = 1;
        return 0;
    }
    client_set_backgrounded(idle->client, 1);
    return 1;
}

static int process_hangup(Idle *idle)
{
    if (!hangup_signal) return 1;
    hangup_signal = 0;
    /* An explicit stop wins over an unrelated terminal hangup. Ordinary
     * foreground sessions still exit; only an existing worker can detach. */
    if (stop_signal) return 0;
    if (!background_managed(idle->background)) {
        stop_signal = SIGHUP;
        return 0;
    }
    return detach_lost_terminal(idle) ? 2 : 0;
}

static void process_background_request(Idle *idle)
{
    if (!idle->background_requested) return;
    idle->background_requested = 0;
    /* An explicit detach must not depend on a slow terminal draining while
     * peers add more traffic. Prepare before discarding display bytes: if it
     * fails, a queued leave-alternate-screen sequence must still be emitted. */
    char error[512] = "Cannot prepare background mode.";
    if (!background_prepare(idle->background, idle->client, error, sizeof error)) {
        /* Prioritize this error before adding more chat once output resumes. */
        notice(idle, error);
        return;
    }
    /* Only transient presentation is discarded. Canonical App history,
     * the stream cache, and all editor/draft state remain untouched. */
    response_reset(idle);
    idle->notice_pending = 0;
    idle->notice[0] = 0;
    release_terminal(idle);
    if (!background_detach(idle->background, &stop_signal, error, sizeof error)) {
        if (!stop_signal && !restore_terminal(idle)) idle->failed = 1;
        notice(idle, error);
    } else client_set_backgrounded(idle->client, 1);
}

int idle_welcome(const char *program, int force_terminal_signals)
{
    Terminal tty;
    struct sigaction previous[sizeof handled_signals / sizeof handled_signals[0]];
    memset(&tty, 0, sizeof tty);
    size_t installed = install_signals(previous);
    if (installed != sizeof handled_signals / sizeof handled_signals[0] ||
        !terminal_start(&tty, force_terminal_signals)) {
        restore_signals(previous, installed);
        fprintf(stderr, "%s: an ANSI/VT-compatible interactive terminal is required\n", program);
        return 0;
    }
    const char *warning = "Not vegetarian?  www.bloodguiltcurse.net will change your mind\r\nPress a key to start.\r\n";
    restoration_write("\033[?2004h"); restoration_write(warning);
    int accepted = 0, paste = 0;
    char escape[16] = {0}; size_t length = 0;
    int64_t escape_at = 0;
    while (!accepted && !stop_signal && !hangup_signal) {
        if (suspend_signal) {
            suspend_signal = 0; terminal_stop(&tty, 0); suspend_process();
            if (!terminal_start(&tty, force_terminal_signals)) break;
            paste = 0; length = 0;
            restoration_write("\033[?2004h"); restoration_write(warning);
        }
        struct pollfd p = {STDIN_FILENO, POLLIN, 0};
        int r = poll(&p, 1, 100);
        if (r < 0 && errno != EINTR) break;
        if (p.revents & (POLLHUP | POLLERR | POLLNVAL)) break;
        if (!(p.revents & POLLIN)) {
            if (!paste && length && now_ms() - escape_at >= (length == 1 ? ESCAPE_MS : ESCAPE_CONTINUATION_MS)) {
                if (length == 1) accepted = 1;
                length = 0;
            }
            continue;
        }
        unsigned char data[256]; ssize_t n = read(STDIN_FILENO, data, sizeof data);
        if (n <= 0) continue;
        for (ssize_t i = 0; i < n && !accepted; ++i) {
            unsigned char c = data[i];
            if (c == 4) continue;
            if (length || c == 27) {
                if (length + 1 >= sizeof escape) { length = 0; continue; }
                escape[length++] = (char)c; escape[length] = 0;
                escape_at = now_ms();
                if (!strcmp(escape, "\033[200~")) { paste = 1; length = 0; }
                else if (!strcmp(escape, "\033[201~")) { paste = 0; length = 0; }
                else if (length >= 3 && c >= 0x40u && c <= 0x7eu) { length = 0; if (!paste) accepted = 1; }
                else if (length == 2 && c != '[' && c != 'O') { length = 0; }
                continue;
            }
            if (!paste && (c >= 32 || c == '\r' || c == '\n' || c == '\t')) accepted = 1;
        }
    }
    int interrupted = stop_signal ? stop_signal : hangup_signal ? SIGHUP : 0;
    terminal_stop(&tty, 0); restore_signals(previous, installed);
    return interrupted ? -interrupted : accepted;
}

/* Shared orchestration for foreground, detached startup and frontend handoff.
 * The loop orders control/terminal ownership before App/DCC progress and
 * ordinary input. Keep timeout-only turns active: jobs need no ready fd to
 * expire. Cleanup must remove callbacks before freeing their Idle context. */
static int idle_run_mode(Client *client, const char *program,
                         int start_background, int primary_connect, int no_connect,
                         Background *shared_background, DccManager *shared_dcc,
                         int *next_frontend)
{
    int own_background = shared_background == NULL;
    int own_dcc = shared_dcc == NULL;
    int switch_frontend = 0;
    if (next_frontend) *next_frontend = BACKGROUND_FRONTEND_CURRENT;
    Idle *idle = calloc(1, sizeof *idle);
    if (!idle) return 1;
    idle->catchup_fd = -1;
    idle->client = client;
    idle->program = program;
    idle->commands = (IdleCommands){client, program, command_print, idle, 0, 1, NULL, irc_tr};
    idle->next_sequence = idle->shown_sequence = 1;
    idle->background = shared_background ? shared_background : background_create();
    idle->dcc = shared_dcc;
    if (!idle->background) {
        free(idle);
        return 1;
    }
    background_set_program(idle->background, program);
    struct sigaction previous[sizeof handled_signals / sizeof handled_signals[0]];
    size_t installed = install_signals(previous);
    char error[512] = "Cannot initialize signal handlers.";
    if (!current_app(idle) && !client_get_app(client, client->current, error, sizeof error)) idle->failed = 1;
    if (installed != sizeof handled_signals / sizeof handled_signals[0] ||
        (!start_background && !restore_terminal(idle))) idle->failed = 1;
    if (!start_background && !idle->failed && !background_detached(idle->background))
        client_set_backgrounded(client, 0);
    if (start_background) {
        /* No terminal, resolver, credential helper or IRC socket exists yet.
         * Forking here cannot duplicate an active helper or connection job. */
        if (!idle->failed && !stop_signal && !hangup_signal) {
            fputs("Not vegetarian?  www.bloodguiltcurse.net will change your mind\n", stderr);
            if (!background_prepare(idle->background, client, error, sizeof error) ||
                !background_detach(idle->background, &stop_signal, error, sizeof error)) idle->failed = 1;
            else {
                client_set_backgrounded(client, 1);
                (void)client_startup(client, primary_connect, no_connect, error, sizeof error);
            }
        }
        if (idle->failed) fprintf(stderr, "%s: background startup failed: %s\n", program, error);
    }
    attach_apps(idle);
    if (*client->catalog_error) {
        char diagnostic[640];
        (void)snprintf(diagnostic, sizeof diagnostic, "Saved-server catalog: %s", client->catalog_error);
        command_print(idle, diagnostic);
    }
    for (size_t i = 1; i < CLIENT_MAX_SERVERS; ++i) {
        ClientSlot *slot = &client->slots[i];
        if (slot->used && (slot->load_failed || (!slot->app && *slot->error))) {
            char diagnostic[640];
            (void)snprintf(diagnostic, sizeof diagnostic, "Saved server %zu: %s", i + 1u,
                           *slot->error ? slot->error : "profile could not be loaded");
            command_print(idle, diagnostic);
        }
    }
    context_update(idle, 1);
    while (!idle->quit && !idle->failed && !stop_signal) {
        if (!process_hangup(idle)) break;
        poll_setup(idle);
        process_background_request(idle);
        if (suspend_signal) {
            suspend_signal = 0;
            release_terminal(idle);
            if (background_managed(idle->background)) background_suspend(idle->background);
            else {
                suspend_process();
                if (!restore_terminal(idle)) idle->failed = 1;
            }
        }
        if (resize_signal) {
            resize_signal = 0;
            geometry(&idle->tty);
            idle->redraw = 1;
            if (idle->mode == SETUP) idle->setup_drawn = 0;
            idle->editor_frame = idle->hotkey_return_drawn = idle->setup_frame = idle->paste_drawn = 0;
        }
        attach_apps(idle);
        if (idle->mode == WATCH) context_update(idle, 0);
        stream_output(idle);
        if (idle->mode == COMPOSE || idle->mode == COMMAND) draw_editor(idle);
        else if (idle->mode == SCROLLBACK) draw_scrollback(idle);
        else if (idle->mode == CATCHUP) draw_catchup(idle);
        else if (idle->mode == REVIEW) draw_review(idle);
        else if (idle->mode == READING) draw_reader(idle);
        else if (idle->mode == SETUP) draw_setup(idle);
        else if (idle->mode == CONTEXT_HELP) draw_context_help(idle);
        if (!background_detached(idle->background) && !output_flush(idle)) {
            if (!background_managed(idle->background)) {
                idle->failed = 1;
                break;
            }
            if (!detach_lost_terminal(idle)) break;
        }
        if (idle->mode == REVIEW && !idle->pending_review_draw && !idle->output_length &&
            idle->tty.rows >= 6 && idle->tty.cols >= 12) idle->review_drawn = 1;
        paste_frame_drawn(idle);
        if (idle->mode == COMMAND && !idle->redraw && !idle->output_length)
            idle->suggestion_drawn = 1;
        if ((idle->mode == COMPOSE || idle->mode == COMMAND) && idle->hotkey_return_guard &&
            !idle->hotkey_return_drawn && idle->editor_frame &&
            !idle->redraw && !idle->output_length && !background_detached(idle->background) &&
            idle->tty.rows >= 6 && idle->tty.cols >= 12) {
            idle->hotkey_return_drawn = 1;
            idle->hotkey_return_at = now_ms();
        }
        if (idle->mode == SETUP && !idle->setup_drawn && idle->setup_frame && !idle->redraw && !idle->output_length &&
            idle->tty.rows >= 10 && idle->tty.cols >= 12) {
            idle->setup_drawn = 1;
            idle->setup_enter_time = now_ms();
        }
        /* Assemble readiness interests and the earliest component deadline. */
        struct pollfd descriptors[2 + BACKGROUND_POLL_CAP + CLIENT_MAX_SERVERS + DCC_MAX_ACTIVE];
        size_t indexes[CLIENT_MAX_SERVERS];
        uint64_t transfer_ids[DCC_MAX_ACTIVE];
        memset(descriptors, 0, sizeof descriptors);
        int detached = background_detached(idle->background);
        descriptors[0] = (struct pollfd){detached ? -1 : STDIN_FILENO, POLLIN, 0};
        descriptors[1] = (struct pollfd){detached ? -1 : STDOUT_FILENO,
                                       idle->output_length ? POLLOUT : 0, 0};
        size_t used = 2;
        int timeout = 1000;
        if (idle->setup && idle_setup_pending(idle->setup)) timeout = 50;
        if (idle->setup_result && !idle->paste) {
            int64_t remaining = idle->setup_settle_at - now_ms();
            timeout = remaining <= 0 ? 0 : remaining < 50 ? (int)remaining : 50;
        }
        for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
            App *app = client->slots[i].app;
            indexes[i] = SIZE_MAX;
            if (!app) continue;
            app_set_visible(app, !detached && app == current_app(idle) &&
                            (idle->mode == WATCH || idle->mode == REVIEW));
            int fd = app_fd(app);
            if (fd >= 0) {
                indexes[i] = used;
                descriptors[used++] = (struct pollfd){fd, app_events(app), 0};
            }
            int next = app_timeout(app);
            if (next < timeout) timeout = next;
        }
        size_t transfer_offset = used;
        size_t transfer_count = idle->dcc ? dcc_pollfds(idle->dcc, descriptors + used, transfer_ids, DCC_MAX_ACTIVE) : 0;
        used += transfer_count;
        if (idle->dcc) {
            int next = dcc_timeout(idle->dcc, now_ms());
            if (next < timeout) timeout = next;
        }
        size_t bg_offset = used;
        used += background_pollfds(idle->background, descriptors + used, BACKGROUND_POLL_CAP);
        timeout = background_timeout(idle->background, timeout);
        if (idle->escape_length && !idle->paste) {
            int grace = idle->escape_length == 1 ? ESCAPE_MS : ESCAPE_CONTINUATION_MS;
            int64_t remaining = grace - (now_ms() - idle->escape_time);
            if (remaining < 0) remaining = 0;
            if (remaining < timeout) timeout = (int)remaining;
        }
        /* Only WATCH can drain retained chat. REVIEW holds those records
         * until it closes; waking for them would spin on every incoming line. */
        if (!detached && (((idle->mode == COMPOSE || idle->mode == COMMAND || idle->mode == SCROLLBACK || idle->mode == CATCHUP || idle->mode == REVIEW || idle->mode == READING || idle->mode == SETUP || idle->mode == CONTEXT_HELP) && idle->redraw) || idle->pending_review_draw ||
            (idle->mode == WATCH && idle->shown_sequence < idle->next_sequence)) &&
            !idle->output_length) timeout = 0;
        int p = poll(descriptors, (nfds_t)used, timeout);
        if (p < 0 && errno != EINTR) {
            idle->failed = 1;
            break;
        }
        if (p < 0) for (size_t i = 0; i < used; ++i) descriptors[i].revents = 0;
        if (stop_signal) break;
        unsigned events = background_process(idle->background, descriptors + bg_offset, used - bg_offset);
        if (events & BACKGROUND_STOP) {
            stop_signal = background_signal(idle->background);
            break;
        }
        int hung_up = process_hangup(idle);
        if (!hung_up) break;
        /* A hangup can race an accepted terminal in the same poll batch.
         * Do not restore or switch frontends using its discarded fds. */
        if (hung_up == 2) events = 0;
        if (!background_detached(idle->background) &&
            ((descriptors[0].revents | descriptors[1].revents) & (POLLHUP | POLLERR | POLLNVAL))) {
            if (!background_managed(idle->background)) break;
            events |= BACKGROUND_DETACH;
        }
        if (events & BACKGROUND_DETACH) {
            if (!detach_lost_terminal(idle)) break;
        } else if (events & BACKGROUND_SUSPEND) {
            release_terminal(idle);
            background_suspend(idle->background);
        } else if (events & BACKGROUND_ATTACH) {
#ifndef ONEKEY_ONLY
            if (background_frontend(idle->background) == BACKGROUND_FRONTEND_FULL) {
                switch_frontend = BACKGROUND_FRONTEND_FULL;
                break;
            }
#endif
            if (!restore_terminal(idle)) {
                if (!background_detach(idle->background, &stop_signal, error, sizeof error)) idle->failed = 1;
            } else {
                client_set_backgrounded(idle->client, 0);
                /* A resumed WATCH view must repaint retained history even when
                 * no IRC activity occurred while detached. */
                context_update(idle, 1);
            }
        } else if (events & BACKGROUND_CONTINUE) {
            if (!restore_terminal(idle) && !background_detach(idle->background, &stop_signal, error, sizeof error)) idle->failed = 1;
        } else if (events & BACKGROUND_RESIZE) {
            geometry(&idle->tty);
            idle->redraw = 1;
            if (idle->mode == SETUP) idle->setup_drawn = 0;
            idle->editor_frame = idle->hotkey_return_drawn = idle->setup_frame = idle->paste_drawn = 0;
        }
        for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
            App *app = client->slots[i].app;
            if (app) {
                /* Detach/suspend may have arrived with network activity in
                 * this poll. Keep those newly unread records unread too. */
                if (background_detached(idle->background)) app_set_visible(app, 0);
                app_network(app, indexes[i] != SIZE_MAX ? descriptors[indexes[i]].revents : 0);
            }
        }
        if (idle->dcc) {
            dcc_process(idle->dcc, descriptors + transfer_offset, transfer_ids, transfer_count, now_ms());
            transfer_changes(idle, 1);
        }
        if (!background_detached(idle->background) && (descriptors[0].revents & POLLIN)) {
            /* Resolve an already expired prefix before a later read arrives;
             * otherwise an ordinary key could swallow a completed Escape. */
            escape_timeout(idle);
            unsigned char input[4096];
            ssize_t n = read(STDIN_FILENO, input, sizeof input);
            if (n > 0) {
                client_user_activity(idle->client);
                ++idle->input_batch;
                for (ssize_t i = 0; i < n && !idle->quit && !stop_signal; ++i) input_byte(idle, input[i]);
            } else if (!n || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                if (background_managed(idle->background)) {
                    if (!detach_lost_terminal(idle)) idle->failed = 1;
                } else if (!n) idle->quit = 1;
                else idle->failed = 1;
            }
            config_wipe(input, sizeof input);
        }
        escape_timeout(idle);
    }
    int result = switch_frontend ? 1000 : stop_signal ? 128 + stop_signal : idle->failed ? 1 : 0;
    if (!switch_frontend && !background_detached(idle->background)) release_terminal(idle);
    if (own_background) background_finish(idle->background, result);
    restore_signals(previous, installed);
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        App *app = client->slots[i].app;
        if (app && app->event_context == idle) {
            app->line_event = NULL;
            app->status_event = NULL;
            app->event_context = NULL;
            app->dcc_offer = NULL;
            app->dcc_context = NULL;
        }
    }
    catchup_close(idle);
    while (idle->count) stream_pop(idle);
    for (size_t i = 0; i < idle->history_count; ++i) {
        config_wipe(idle->history[i].text, strlen(idle->history[i].text));
        free(idle->history[i].text);
    }
    if (own_background) background_free(idle->background);
    if (own_dcc) dcc_free(idle->dcc);
    response_reset(idle);
    idle_setup_free(idle->setup);
    if (switch_frontend && next_frontend) *next_frontend = switch_frontend;
    config_wipe(idle, sizeof *idle);
    free(idle);
    return result;
}

int idle_run(Client *client, const char *program)
{
    return idle_run_mode(client, program, 0, 0, 0, NULL, NULL, NULL);
}

int idle_run_background(Client *client, const char *program,
                        int primary_connect, int no_connect)
{
    return idle_run_mode(client, program, 1, primary_connect, no_connect, NULL, NULL, NULL);
}

int idle_run_session(Client *client, const char *program, Background *background, DccManager *dcc,
                     int start_background, int primary_connect, int no_connect, int *next_frontend)
{
    return idle_run_mode(client, program, start_background, primary_connect, no_connect,
                         background, dcc, next_frontend);
}
