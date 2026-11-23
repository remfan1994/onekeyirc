/* Full curses frontend. Peer text is data, never terminal commands.
 *
 * Entry points are at the end of this file. UiSet owns the workspace's UI
 * objects, while Ui holds one server's editors and navigation state.
 * Start with ui_run_mode(), then follow draw() for presentation or
 * terminal_byte() -> key_input() for input. Settings, guided setup, command
 * editing and transfer forms delegate to their own modules. */
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
/* Terminal resize signals are BSD extensions with strict POSIX settings. */
#define _BSD_SOURCE 1
#endif
#include "ui.h"
#include "l10n.h"
#include "display_l10n.h"
#include "editor.h"
#include "editor_curses.h"
#include "arabic_visual.h"
#include <curses.h>
#include "focus.h"
#include "settings.h"
#include "guided.h"
#include "dcc.h"
#include "dcc_commands.h"
#include "transfers.h"
#include "prompt.h"
#include "commands.h"
#include "idle_commands.h"
#include "summary.h"
#include "readpage.h"
#include "nicks.h"
#include "background.h"
#include "license.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <langinfo.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#define PASTE_BEGIN (KEY_MAX + 1)
#define PASTE_END (KEY_MAX + 2)
#define UI_ESCAPE_MS 500
#define UI_ESCAPE_CONTINUATION_MS 1000
#define CATEGORY_FIRST KEY_F(1)
#define CATEGORY_LAST KEY_F(6)
#define LIST_ACTION_ROW 3
static const char bloodguilt_warning[] = "Not vegetarian?  www.bloodguiltcurse.net will change your mind";
#if defined(KEY_MOUSE) && defined(BUTTON1_PRESSED)
#define UI_MOUSE 1
#else
#define UI_MOUSE 0
#endif

typedef enum FocusControl {
    FOCUS_APP, FOCUS_SERVERS, FOCUS_CHANNELS, FOCUS_QUERIES, FOCUS_ACTIVITY,
    FOCUS_TRANSFERS, FOCUS_CLEAR, FOCUS_SEND, FOCUS_COUNT
} FocusControl;
#define FOCUS_LAST FOCUS_TRANSFERS
typedef struct StatusHit { int x, y, width; } StatusHit;
typedef struct StatusHits {
    StatusHit controls[FOCUS_COUNT];
    size_t view;
    BufferKind kind;
    unsigned present;
    unsigned panel;
    int rows, cols, input_mode, shortcut_labels, before, after, valid;
} StatusHits;

typedef struct TextHit {
    int x, y, width, channel;
    char target[IRC_TARGET_MAX + 1];
} TextHit;
#define UI_TEXT_HITS_MAX 512u

typedef struct UiSet UiSet;
typedef enum AppEntry {
    APP_SERVER, APP_NICK, APP_SETTINGS, APP_BINDINGS, APP_MANUAL,
    APP_COMMAND, APP_BACKGROUND, APP_ABOUT, APP_LICENSE, APP_ENTRIES
} AppEntry;
typedef struct AboutSnapshot {
    int64_t uptime_ms;
    double user_seconds, system_seconds, peak_mib;
    size_t servers, registered, channels, queries;
    pid_t pid;
    int cpu_available, memory_available;
} AboutSnapshot;
typedef struct ReadView {
    ReadPage pages[2];
    size_t scroll[2];
    int active, page, action, paste, hidden;
    char title[256], parent[160], inquiry[24];
} ReadView;
typedef struct CommandOrigin {
    int app_menu, channels, servers, unread, manual, input_mode, summary;
} CommandOrigin;
/* This modal stages only key/action pairs. Resolving a fresh Config on Apply
 * prevents a long-open table from overwriting unrelated settings or secrets. */
typedef struct HotkeyTable {
    ConfigHotkey rows[CONFIG_HOTKEY_MAX];
    size_t count, selected, first, action, action_first;
    unsigned old_key;
    char key_name[32];
    size_t key_length;
    int active, stage, paste, from_app, drawn;
    int64_t changed_at;
} HotkeyTable;

/* Per-server presentation and modal state. Drafts use buffer positions that
 * are adjusted by view_closed(); App owns the actual history.
 * Confirmation/paste guards are input ownership, not just visual decoration. */
typedef struct Ui {
    UiSet *owner;
    size_t slot;
    Settings *settings;
    CommandPrompt *command;
    /* A shortcut may temporarily open a page without destroying the command
     * editor, its caret, suggestion selection, or in-memory recall state. */
    CommandPrompt *hotkey_command;
    int hotkey_command_return;
    uint64_t hotkey_command_view;
    int command_target_lost, command_guard, command_guard_drawn, command_guard_enter_seen;
    uint64_t command_guard_batch;
    int64_t command_guard_after;
    HotkeyTable bindings;
    uint64_t hotkey_block_batch, hotkey_guard_batch;
    int hotkey_guard, hotkey_guard_drawn;
    int64_t hotkey_after;
    int command_from_app;
    CommandOrigin command_origin;
    ReadView summary, context_help;
    Editor *drafts[APP_BUFFERS];
    int literal[APP_BUFFERS];
    int limit_warned[APP_BUFFERS], input_scroll[APP_BUFFERS];
    SendPlan pending;
    size_t pending_view;
    int pending_command, close_command, command_limit_warned, warning_command;
    int confirming, paste, paste_rejected, paste_cr, paste_ignore;
    size_t paste_end_len;
    int escape_len;
    char escape[32];
    int64_t escape_at;
    int input_mode, channels, manual;
    int app_menu, app_selection, app_first, status_selected, status_cycle, quit_confirm;
    int64_t quit_confirm_at;
    StatusHit app_hits[APP_ENTRIES];
    int license;
    size_t license_scroll;
    int about, about_action;
    size_t about_scroll;
    AboutSnapshot about_snapshot;
    size_t header_origin;
    NickCompletion completion;
    TextHit *text_hits;
    size_t text_hit_count, text_hit_capacity;
    int nick_menu, nick_action, nick_menu_top;
    char selected_nick[64];
    size_t chooser_map[APP_BUFFERS], chooser_count;
    int chooser_drawn, chooser_kind;
    size_t manual_view;
    int manual_from_app, manual_action, manual_searching, manual_found;
    Editor *manual_search;
    char manual_term[257];
    size_t manual_match_line, manual_match_offset, manual_match_length;
    size_t manual_source_start, manual_source_count, manual_search_first;
    int manual_search_scroll, manual_search_visual;
    int manual_draw_width, manual_draw_height;
    size_t nav_focus, chooser_action;
    size_t channel_selection, close_view, close_source, submitting_view;
    char close_name[201];
    int input_width, input_rows, input_top, body_rows;
    size_t warning_view, warning_budget;
    int warning_multiple;
    int mouse_active;
    int mouse_ready, mouse_rows, mouse_cols;
    StatusHits status_hits;
    int64_t confirm_at;
    uint32_t utf8_value, utf8_minimum;
    int utf8_remaining;
} Ui;

typedef struct OfferContext { UiSet *owner; size_t slot; } OfferContext;
typedef struct UnreadView { size_t slot, view; } UnreadView;
typedef struct ActivityHit {
    size_t item, slot;
    uint64_t server_token, buffer_id;
    int y, rows;
} ActivityHit;
#define UI_ACTIVITY_HITS_MAX 512u

/* Shared for one frontend invocation. Client is borrowed; shared Background
 * and DccManager ownership is selected by ui_run_mode(), not by these forms. */
struct UiSet {
    Client *client;
    OfferContext offers[CLIENT_MAX_SERVERS];
    Ui *states[CLIENT_MAX_SERVERS];
    DccManager *dcc;
    Transfers *transfers;
    Guided *guided;
    size_t guided_origin;
    int guided_from_app, guided_was_connected;
    Background *background;
    int background_requested;
    int64_t started_at;
    uint64_t input_batch;
    int servers, unread, close_confirm;
    size_t server_selection, server_action, switch_to, close_slot, switch_view, unread_selection;
    UnreadView unread_views[CLIENT_MAX_SERVERS * APP_BUFFERS];
    size_t unread_count, unread_first;
    size_t activity_count, activity_first, activity_hits_count, unread_action;
    int activity_scroll;
    ActivityHit activity_hits[UI_ACTIVITY_HITS_MAX];
    int64_t close_at;
};

typedef enum ChooserAction {
    CHOOSE_OPEN, CHOOSE_NEW, CHOOSE_PART, CHOOSE_CLOSE, CHOOSE_BACK
} ChooserAction;

static volatile sig_atomic_t stop_requested, hangup_requested, suspend_requested;
static int terminal_utf8;
static SCREEN *terminal_screen;
static struct termios terminal_saved;
static int terminal_active;
static void stop_signal(int sig)
{
    if (sig == SIGHUP) hangup_requested = 1;
    else stop_requested = sig;
}
static void suspend_signal(int sig) { (void)sig; suspend_requested = 1; }
static int history_rows(const char *, int, int, int, int);
static void clear_input(App *, Ui *);
static void submit(App *, Ui *);
static int open_command(App *, Ui *, const char *);
static void command_action(App *, Ui *, PromptAction);
static int main_screen(const Ui *);
static void return_to_chat(App *, Ui *);
static void draw_app_menu(App *, Ui *);
static void open_about(App *, Ui *);
static void draw_about(App *, Ui *);
static void capture_about(Ui *);
static void scroll_about(Ui *, size_t, int);
static int about_bar(const App *, const Ui *, int, int);
static void return_to_app(App *, Ui *);
static void draw_nick_menu(App *, Ui *);
static void activate_chooser(App *, Ui *, ChooserAction);
static void open_directory(App *, Ui *);
static void draw_directory(App *, Ui *);
static void directory_key(App *, Ui *, wint_t, int);
static void directory_resort(App *, Ui *, int, int);
static void open_servers(App *, Ui *);
static void draw_servers(App *, Ui *);
static void server_key(App *, Ui *, wint_t, int);
static void focus_header(App *, Ui *, int);
static void restore_command_origin(Ui *);
static int open_summary(App *, Ui *);
static int open_context_help(App *, Ui *);
static void reader_key(App *, Ui *, wint_t, int);
static void open_bindings(App *, Ui *, int);
static void draw_bindings(App *, Ui *);
static void bindings_key(App *, Ui *, wint_t, int);
static void restore_hotkey_command(App *, Ui *);
static int custom_hotkey(App *, Ui *, wint_t, int);
static int capturing_paste(const Ui *);
static void restore_hotkey_target(App *, Ui *);
static void guard_hotkey_command(App *, Ui *);
static int input_guard_ready(App *, Ui *);
static void arm_input_guard(App *, Ui *);
#if UI_MOUSE
static void server_mouse(App *, Ui *, const MEVENT *);
#endif

static int settings_is_open(const Ui *u)
{
    return u->settings && settings_active(u->settings);
}

static unsigned mouse_panel(const Ui *u)
{
    return (unsigned)u->owner->unread | ((unsigned)u->channels << 1) |
           ((unsigned)u->manual << 3) | ((unsigned)settings_is_open(u) << 4) |
           ((unsigned)u->owner->servers << 5) |
           ((unsigned)transfers_active(u->owner->transfers) << 6) |
           ((unsigned)u->confirming << 7) | ((unsigned)u->owner->close_confirm << 9) |
           ((unsigned)prompt_active(u->command) << 12) |
           ((unsigned)u->app_menu << 13) | ((unsigned)u->status_selected << 14) |
           ((unsigned)u->nick_menu << 15) | ((unsigned)u->license << 16) |
           ((unsigned)u->about << 17) | ((unsigned)u->manual_searching << 18) |
           ((unsigned)u->quit_confirm << 19) |
           ((unsigned)guided_active(u->owner->guided) << 20) |
           ((unsigned)(u->summary.active && !u->summary.hidden) << 21) |
           ((unsigned)u->context_help.active << 22) |
           ((unsigned)u->bindings.active << 23);
}

static int main_screen(const Ui *u)
{
    return !u->bindings.active && !u->context_help.active && !u->summary.active && !u->nick_menu && !u->app_menu && !u->license && !u->about && !u->channels && !u->manual && !u->owner->servers &&
           !u->owner->unread && !settings_is_open(u) && !guided_active(u->owner->guided) &&
           !transfers_active(u->owner->transfers) && !prompt_active(u->command);
}

/* Labels and dispatch share the same screen scope, so an overridden F-key
 * is never advertised as the built-in category shortcut while it is active. */
static int custom_hotkey_scope(const Ui *u)
{
    return (main_screen(u) || prompt_active(u->command)) &&
        !u->bindings.active && !u->context_help.active &&
        !(u->summary.active && !u->summary.hidden) && !u->app_menu && !u->channels &&
        !u->manual && !u->nick_menu && !u->license && !u->about &&
        !u->owner->servers && !u->owner->unread && !settings_is_open(u) &&
        !guided_active(u->owner->guided) && !transfers_active(u->owner->transfers) &&
        !u->confirming && !u->quit_confirm && !u->owner->close_confirm;
}

static void resume_body(Ui *u)
{
    u->status_selected = 0;
    if (main_screen(u) || prompt_active(u->command) || guided_active(u->owner->guided) ||
        (settings_mouse_state(u->settings) & 2u) ||
        (transfers_mouse_state(u->owner->transfers) & 8u)) {
        u->status_cycle = 0;
        u->nav_focus = FOCUS_APP;
    } else {
        u->status_cycle = 1;
        u->nav_focus = u->owner->servers ? FOCUS_SERVERS :
                       (u->channels == 1 || u->channels == 3) ? FOCUS_CHANNELS :
                       u->channels == 2 ? FOCUS_QUERIES :
                       transfers_active(u->owner->transfers) ? FOCUS_TRANSFERS :
                       u->owner->unread ? FOCUS_ACTIVITY : FOCUS_APP;
    }
}

static void invalidate_mouse(Ui *u)
{
    u->mouse_ready = 0;
    u->status_hits.valid = 0;
}

static void mouse_drawn(Ui *u)
{
    u->mouse_rows = LINES;
    u->mouse_cols = COLS;
    u->mouse_ready = LINES >= 9 && COLS >= 20;
}

static void paste_mode(int on)
{
    /* Fixed local terminal negotiation; never formed from peer data. */
    fputs(on ? "\033[?2004h" : "\033[?2004l", stdout);
    fflush(stdout);
}

static void configure_mouse(Ui *u, int enabled)
{
#if UI_MOUSE
    if (!enabled || !has_key(KEY_MOUSE)) {
        if (u->mouse_active) (void)mousemask(0, NULL);
        u->mouse_active = 0;
        return;
    }
    /* Report both transitions without click coalescing. Otherwise curses can
     * defer a complete press/release until after a following typed character. */
    mmask_t wanted = BUTTON1_PRESSED | BUTTON1_RELEASED;
#ifdef BUTTON4_PRESSED
    wanted |= BUTTON4_PRESSED;
#endif
#ifdef BUTTON5_PRESSED
    wanted |= BUTTON5_PRESSED;
#endif
    (void)mouseinterval(0);
    u->mouse_active = mousemask(wanted, NULL) != 0;
#else
    (void)enabled;
    u->mouse_active = 0;
#endif
}

static void terminal_stop(void)
{
    if (!terminal_active) return;
    if (terminal_screen) (void)endwin();
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &terminal_saved);
    terminal_active = 0;
}

/* Rebuild curses for the currently attached tty, retaining the terminal's
 * signal characters. Detach/continue can supply a different terminal. */
static int terminal_start(int use_signals)
{
    char charset[8];
    size_t used = 0;
    const char *codeset = nl_langinfo(CODESET);
    for (; *codeset && used + 1 < sizeof charset; ++codeset) {
        if (*codeset == '-' || *codeset == '_') continue;
        char c = *codeset;
        charset[used++] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
    }
    charset[used] = 0;
    terminal_utf8 = !*codeset && !strcmp(charset, "UTF8");
    if (!getenv("TERM") || !*getenv("TERM")) {
        fputs("vipirc: TERM must name your terminal\n", stderr);
        return 0;
    }
    /* Curses can enable ISIG itself. Capture the caller's policy before it
     * touches the tty, then put that policy into the saved program mode. */
    if (tcgetattr(STDIN_FILENO, &terminal_saved) < 0) return 0;
    terminal_active = 1;
    if (terminal_screen) {
        delscreen(terminal_screen);
        terminal_screen = NULL;
    }
    terminal_screen = newterm(NULL, stdout, stdin);
    if (!terminal_screen) {
        terminal_stop();
        return 0;
    }
    (void)set_term(terminal_screen);
    vipirc_focus_colors();
    noraw();
    cbreak();
    noecho();
    nonl();
    intrflush(stdscr, FALSE);
    keypad(stdscr, TRUE);
    meta(stdscr, TRUE);
    (void)curs_set(1);
#ifdef NCURSES_VERSION
    set_escdelay(40);
    (void)define_key("\033[200~", PASTE_BEGIN);
    (void)define_key("\033[201~", PASTE_END);
#endif
    struct termios mode;
    if (tcgetattr(STDIN_FILENO, &mode) < 0) {
        terminal_stop();
        return 0;
    }
    mode.c_lflag = use_signals ? mode.c_lflag | ISIG : mode.c_lflag & (tcflag_t)~ISIG;

    mode.c_cc[VINTR] = terminal_saved.c_cc[VINTR];
    mode.c_cc[VQUIT] = terminal_saved.c_cc[VQUIT];
    mode.c_cc[VSUSP] = terminal_saved.c_cc[VSUSP];
#ifdef VDSUSP
    mode.c_cc[VDSUSP] = terminal_saved.c_cc[VDSUSP];
#endif
    if (tcsetattr(STDIN_FILENO, TCSANOW, &mode) < 0 || def_prog_mode() == ERR) {
        terminal_stop();
        return 0;
    }
    return 1;
}

/* Interface settings are application-wide. Update the active curses tty
 * immediately and save its new program mode for future curses refreshes.
 * terminal_stop() still restores terminal_saved (the launching shell state). */
static int terminal_signals_apply(int use_signals)
{
    if (!terminal_active) return 1;
    struct termios mode;
    if (tcgetattr(STDIN_FILENO, &mode) < 0) return 0;
    tcflag_t wanted = use_signals ? mode.c_lflag | ISIG : mode.c_lflag & (tcflag_t)~ISIG;
    if (wanted == mode.c_lflag) return 1;
    mode.c_lflag = wanted;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &mode) < 0) return 0;
    return def_prog_mode() != ERR;
}

/* Locale selects display/input support, never the IRC wire encoding.
 * In ASCII mode consume a whole sanitized UTF-8 scalar for one '?' cell. */
static size_t display_character(const char *s, wchar_t *wc, mbstate_t *state)
{
    if (terminal_utf8) {
        size_t n = mbrtowc(wc, s, MB_CUR_MAX, state);
        if (n != (size_t)-1 && n != (size_t)-2) return n;
        memset(state, 0, sizeof *state);
    } else if ((unsigned char)*s >= 128) {
        unsigned char c = (unsigned char)*s;
        size_t n = c >= 0xc2 && c <= 0xdf ? 2u :
                   c >= 0xe0 && c <= 0xef ? 3u :
                   c >= 0xf0 && c <= 0xf4 ? 4u : 1u;
        for (size_t i = 1; i < n; ++i)
            if (!s[i] || ((unsigned char)s[i] & 0xc0u) != 0x80u) { n = 1; break; }
        *wc = L'?';
        return n;
    } else {
        *wc = (wchar_t)(unsigned char)*s;
        return *s ? 1u : 0u;
    }
    *wc = L'?';
    return 1;
}

/* All ncurses text paths use this same paragraph map for paint, wrapping,
 * search highlighting and mouse targets. A failed optional map leaves the
 * ordinary logical renderer available; source strings are never rewritten. */
static IrcArabicLayout *visual_text_layout(const char *text, int width, unsigned flags)
{
    if (!terminal_utf8 || !text || width < 1 || !irc_arabic_visual_selected()) return NULL;
    size_t bytes = strnlen(text, IRC_ARABIC_MAX_BYTES + 1u);
    if (bytes > IRC_ARABIC_MAX_BYTES) return NULL;
    return irc_arabic_layout_utf8(text, bytes, width, flags);
}

/* Print already-sanitized UTF-8 without splitting a wide cell at the edge. */
static void text_at_mode(int y, int x, int width, const char *s, attr_t attr,
                         int *visual_used)
{
    mbstate_t state;
    memset(&state, 0, sizeof state);
    int col = 0;
    if (visual_used) *visual_used = 0;
    if (y < 0 || y >= LINES || x < 0 || x >= COLS || width <= 0) return;
    if (width > COLS - x) width = COLS - x;
    (void)attr_set(attr, (short)PAIR_NUMBER(attr), NULL);
    IrcArabicLayout *visual = visual_text_layout(s, width, IRC_ARABIC_SINGLE_LINE);
    if (visual) {
        if (visual_used) *visual_used = 1;
        irc_curses_arabic_paint(visual, y, x, width, 0, 1);
        irc_arabic_layout_free(visual);
        attrset(A_NORMAL);
        return;
    }
    move(y, x);
    while (*s && col < width) {
        wchar_t wc;
        size_t n = display_character(s, &wc, &state);
        if (!n) break;
        int w = wcwidth(wc);
        if (w < 0) { wc = L'?'; w = 1; }
        if (col + w > width) break;
        wchar_t one[2] = { wc, 0 };
        (void)addnwstr(one, 1);
        col += w; s += n;
    }
    attrset(A_NORMAL);
}

static void text_at(int y, int x, int width, const char *s, attr_t attr)
{
    text_at_mode(y, x, width, s, attr, NULL);
}

/* Optional tips occupy an existing secondary row. Keep complete short tips
 * at narrow widths rather than displaying half a key instruction. */
static void hint_at(const App *a, int y, int x, int width,
                    const char *full, const char *compact)
{
    if (!a->cfg.hints || width <= 0) return;
    const char *text = strlen(full) <= (size_t)width ? full : compact;
    if (text && strlen(text) <= (size_t)width) text_at(y, x, width, text, A_DIM);
}

static void list_hint(const App *a)
{
    hint_at(a, LINES - 2, 0, COLS,
            "Up/Down: item | Left/Right: action | Enter: run | ! mentions",
            "Arrows: item/action");
}

static int text_cells(const char *s, int maximum)
{
    mbstate_t state;
    int cells = 0;
    memset(&state, 0, sizeof state);
    while (*s && cells < maximum) {
        wchar_t wc;
        size_t n = display_character(s, &wc, &state);
        if (!n) break;
        int width = wcwidth(wc);
        if (width < 0) width = 1;
        if (cells + width > maximum) break;
        cells += width;
        s += n;
    }
    return cells;
}

static attr_t focus_attribute(void)
{
    return vipirc_focus_attrs();
}

static void focus_text(const App *a, int y, int x, int width,
                        const char *label, int selected)
{
    (void)a;
    if (width >= 2) {
        int cells = text_cells(label, width - 2);
        attr_t attr = selected ? focus_attribute() : A_NORMAL;
        if (selected) text_at(y, x, 1, "[", attr);
        text_at(y, x + 1, cells, label, attr);
        if (selected) text_at(y, x + cells + 1, 1, "]", attr);
    } else text_at(y, x, width, selected ? "[" : label,
                   selected ? focus_attribute() : A_NORMAL);
}

static void focus_button(const App *a, int y, int x, int width,
                          const char *label, int selected)
{
    focus_text(a, y, x, width, label, selected);
}

/* Recognize only the two fixed paste markers when curses has not decoded
 * them, including on implementations without define_key(). No body is saved. */
static int welcome_marker(wint_t key, int *length, int *ending, int *paste)
{
    static const char prefix[] = "\033[20";
    if (key == 27) { *length = 1; return 1; }
    if (!*length) return 0;
    if (*length < 4 && key == (wint_t)(unsigned char)prefix[*length]) {
        ++*length;
        return 1;
    }
    if (*length == 4 && (key == '0' || key == '1')) {
        *ending = key == '1';
        *length = 5;
        return 1;
    }
    if (*length == 5 && key == '~') {
        *paste = !*ending;
        *length = 0;
        return 1;
    }
    int partial = *length > 1;
    *length = 0;
    return partial;
}

/* The welcome screen temporarily owns tty signals
 * and input. Restore prior handlers/terminal state on every exit; pasted
 * keys must never silently dismiss warnings as repeated confirmations. */
int ui_welcome(int use_signals)
{
    static const int signals[] = { SIGINT, SIGQUIT, SIGTERM, SIGHUP, SIGTSTP };
    struct sigaction saved[sizeof signals / sizeof signals[0]], action;
    size_t installed = 0;
    int result = 0, started = 0, redraw = 1, paste = 0;
    int marker_length = 0, marker_ending = 0;
    stop_requested = hangup_requested = suspend_requested = 0;
    memset(&action, 0, sizeof action);
    sigemptyset(&action.sa_mask);
    for (; installed < sizeof signals / sizeof signals[0]; ++installed) {
        action.sa_handler = signals[installed] == SIGTSTP ? suspend_signal : stop_signal;
        if (sigaction(signals[installed], &action, &saved[installed]) < 0) goto finish;
    }
    if (!terminal_start(use_signals)) goto finish;
    started = 1;
    /* Terminal-generated signals retain their configured mappings. Other
     * control bytes do not dismiss the welcome when signal handling is off. */
    wtimeout(stdscr, 100);
    paste_mode(1);
    const char *warning = bloodguilt_warning;
    const char *mode_hint = "ASCII terminal mode: non-ASCII input is disabled. Select an installed UTF-8 LC_CTYPE for Unicode.";
    while (!stop_requested && !hangup_requested) {
        if (suspend_requested) {
            suspend_requested = 0;
            paste_mode(0);
            terminal_stop();
            action.sa_handler = SIG_DFL;
            (void)sigaction(SIGTSTP, &action, NULL);
            (void)raise(SIGTSTP);
            action.sa_handler = suspend_signal;
            (void)sigaction(SIGTSTP, &action, NULL);
            if (stop_requested) break;
            if (!terminal_start(use_signals)) { started = 0; break; }
            wtimeout(stdscr, 100);
            paste_mode(1);
            clearok(stdscr, TRUE);
            redraw = 1;
        }
        if (redraw) {
            erase();
            int width = COLS > 1 ? COLS - 1 : 1;
            int rows = history_rows(warning, width, -1, 0, 0);
            int mode_rows = terminal_utf8 ? 0 : history_rows(mode_hint, width, -1, 0, 0) + 1;
            int r = LINES > rows + mode_rows + 4 ? (LINES - rows - mode_rows - 4) / 2 : 0;
            text_at(r, 0, COLS, irc_tr("Welcome to vipirc"), A_BOLD);
            (void)history_rows(warning, width, r + 2, 0, LINES - r - 2);
            if (!terminal_utf8)
                (void)history_rows(mode_hint, width, r + rows + 3, 0, LINES - r - rows - 3);
            text_at(r + rows + mode_rows + 3, 0, COLS, irc_tr("(Press a printable key, Enter, Tab, or an arrow to begin.)"), A_NORMAL);
            refresh();
            redraw = 0;
        }
        wint_t key;
        int read_result = get_wch(&key);
        if (stop_requested || hangup_requested || suspend_requested) continue;
        if (read_result == ERR) {
            struct pollfd input = { STDIN_FILENO, POLLIN, 0 };
            if (poll(&input, 1, 0) < 0 && errno != EINTR) break;
            if (input.revents & (POLLHUP | POLLERR | POLLNVAL)) break;
            continue;
        }
        if (read_result != KEY_CODE_YES && key == 4) continue;
        if (read_result == KEY_CODE_YES) {
            if (key == KEY_RESIZE) { redraw = 1; continue; }
            marker_length = 0;
            if (key == PASTE_BEGIN) { paste = 1; continue; }
            if (key == PASTE_END) { paste = 0; continue; }
            if (paste || key == KEY_BACKSPACE) continue;
#if UI_MOUSE
            if (key == KEY_MOUSE) continue;
#endif
            result = 1;
            break;
        }
        if (welcome_marker(key, &marker_length, &marker_ending, &paste) || paste) continue;
        /* A non-signal control byte is not permission to leave this screen.
         * Signal controls themselves are consumed by the terminal driver. */
        if (key == '\r' || key == '\n' || key == '\t' || iswprint(key)) {
            result = 1;
            break;
        }
    }
    if (stop_requested || hangup_requested)
        result = -(int)(stop_requested ? stop_requested : SIGHUP);
    else if (result == 1) (void)flushinp();
finish:
    if (started) { paste_mode(0); terminal_stop(); }
    while (installed) {
        --installed;
        (void)sigaction(signals[installed], &saved[installed], NULL);
    }
    return result;
}

static Editor *draft(App *a, Ui *u)
{
    if (!u->drafts[a->current]) {
        u->drafts[a->current] = calloc(1, sizeof(Editor));
        if (u->drafts[a->current]) ed_init(u->drafts[a->current]);
        else { app_status(a, "%s", irc_tr("Cannot allocate input buffer.")); a->quit = 1; }
    }
    return u->drafts[a->current];
}

/* Modal entry/exit and the adapters for dedicated form modules. */
static void open_settings(App *a, Ui *u)
{
    if (!u->settings) u->settings = settings_create();
    if (!u->settings) {
        app_status(a, "%s", irc_tr("Cannot allocate Settings; draft preserved."));
        return;
    }
    Editor *e = draft(a, u);
    if (e) ed_unselect(e);
    u->input_mode = 0;
    u->channels = 0;
    settings_open_client(u->settings, u->owner->client);
    app_program_notice(a, "Settings opened. Chat draft preserved.");
}

static void settings_changed(App *a, Ui *u, int previous_mouse)
{
    if (a->cfg.mouse != previous_mouse) configure_mouse(u, a->cfg.mouse);
    if (!terminal_signals_apply(a->cfg.use_signals))
        app_status(a, "%s", irc_tr("Cannot update terminal signals; saved preference takes effect on next terminal attachment."));
    if (!settings_is_open(u)) return_to_app(a, u);
    resume_body(u);
    a->dirty = 1;
}

static int open_guided(App *a, Ui *u, int quick)
{
    UiSet *all = u->owner;
    char error[512];
    if (!all->guided) all->guided = guided_create(all->client);
    if (!guided_open(all->guided, quick, error, sizeof error)) {
        app_status(a, "%s", error); return 0;
    }
    all->guided_origin = u->slot;
    all->guided_from_app = u->app_menu || (prompt_active(u->command) && u->command_from_app);
    all->guided_was_connected = a->net != NULL || a->sim != NULL;
    u->app_menu = u->channels = u->input_mode = 0;
    all->servers = all->unread = 0;
    resume_body(u);
    invalidate_mouse(u);
    a->dirty = 1;
    return 1;
}

/* The shared draft may connect another profile. Defer switching App/Ui until
 * this handler has returned, just as ordinary shared commands do. */
static void guided_changed(App *a, Ui *u)
{
    UiSet *all = u->owner;
    char message[768];
    int result = guided_take_result(all->guided, message, sizeof message);
    if (!result) { a->dirty = 1; return; }
    if (!terminal_signals_apply(a->cfg.use_signals))
        app_status(a, "%s", irc_tr("Cannot update terminal signals; saved preference takes effect on next terminal attachment."));
    /* A completed asynchronous setup exposes a new input owner just like
     * a shortcut. Its result must be drawn before trailing input acts. */
    arm_input_guard(a, u);
    size_t selected = all->client->current;
    App *destination = all->client->slots[selected].app;
    int switched = selected != all->guided_origin;
    int connected = result == 2 && (switched ||
        (!all->guided_was_connected && (a->net != NULL || a->sim != NULL)));
    if (switched) {
        if (destination) app_set_visible(destination, 0);
        all->client->current = all->guided_origin;
        all->switch_to = selected;
        all->switch_view = 0;
    }
    if (connected) u->header_origin = SIZE_MAX;
    if (all->guided_from_app && !connected) return_to_app(a, u);
    else return_to_chat(a, u);
    app_status(a, "%s", message);
    if (destination && destination != a) app_status(destination, "%s", message);
    config_wipe(message, sizeof message);
    resume_body(u);
    invalidate_mouse(u);
    a->dirty = 1;
}

static int measure_input(const App *a, const Ui *u, AppDraftMeasure *measure)
{
    char text[ED_MAX_BYTES + 1];
    int command = prompt_active(u->command);
    const Editor *e = command ? prompt_const_editor(u->command) : u->drafts[a->current];
    memset(measure, 0, sizeof *measure);
    if (!e || ed_to_utf8(e, text, sizeof text) == SIZE_MAX) return 0;
    int result = app_measure_draft(a, a->current, text,
                                   command ? 0 : u->literal[a->current], measure);
    config_wipe(text, sizeof text);
    return result;
}

static void update_input_warning(App *a, Ui *u)
{
    if (u->bindings.active || settings_is_open(u) || guided_active(u->owner->guided) || u->nick_menu || u->app_menu || u->license || u->about || u->manual || u->owner->servers || u->owner->unread || transfers_active(u->owner->transfers)) return;
    AppDraftMeasure measure;
    size_t view = a->current;
    int command = prompt_active(u->command);
    int *warned = command ? &u->command_limit_warned : &u->limit_warned[view];
    if (u->confirming == 3 && (u->warning_view != view || u->warning_command != command)) {
        if (u->warning_command) u->command_limit_warned = 0;
        else if (u->warning_view < APP_BUFFERS) u->limit_warned[u->warning_view] = 0;
        u->confirming = 0;
    }
    int at_limit = measure_input(a, u, &measure) && measure.budget &&
                   (measure.longest_line_bytes >= measure.budget || measure.messages > 1);
    if (!at_limit) {
        *warned = 0;
        if (u->confirming == 3) { u->confirming = 0; a->dirty = 1; }
        return;
    }
    if (u->confirming == 3) {
        u->warning_budget = measure.budget;
        u->warning_multiple = measure.messages > 1;
        return;
    }
    /* Capture a bracketed paste in full before warning. Keystrokes are
     * checked individually, so a held key cannot run past the first alert. */
    if ((!u->input_mode && !command) || *warned || u->confirming ||
        u->channels || u->paste || prompt_pasting(u->command)) return;
    *warned = 1;
    u->warning_command = command;
    u->warning_view = view;
    u->warning_budget = measure.budget;
    u->warning_multiple = measure.messages > 1;
    u->confirming = 3;
    u->confirm_at = app_now_ms(a);
    a->dirty = 1;
}

/* Channel numbers are local to this list. Server is always channel zero. */
static size_t channel_map(const App *a, size_t *map)
{
    size_t count = 1;
    map[0] = 0;
    for (size_t i = 3; i < a->buffer_count; ++i)
        if (a->buffers[i].kind == B_CHANNEL) map[count++] = i;
    return count;
}

static void open_channels(App *a, Ui *u)
{
    size_t map[APP_BUFFERS], count = channel_map(a, map);
    u->channels = 1;
    u->chooser_drawn = 0;
    u->chooser_action = 0;
    u->input_mode = 0;
    u->channel_selection = 0;
    for (size_t i = 0; i < count; ++i)
        if (map[i] == a->current) u->channel_selection = i;
    a->dirty = 1;
}

static size_t pm_map(const App *a, size_t *map)
{
    size_t count = 0;
    for (size_t i = 3; i < a->buffer_count; ++i)
        if (a->buffers[i].kind == B_QUERY) map[count++] = i;
    return count;
}

static void open_pms(App *a, Ui *u)
{
    size_t map[APP_BUFFERS], count = pm_map(a, map);
    u->channels = 2;
    u->chooser_drawn = 0;
    u->chooser_action = 0;
    u->input_mode = 0;
    u->channel_selection = 0;
    for (size_t i = 0; i < count; ++i)
        if (map[i] == a->current) u->channel_selection = i;
    a->dirty = 1;
}

static void open_directory(App *a, Ui *u)
{
    u->channels = 3;
    u->chooser_drawn = 0;
    u->chooser_action = a->list.sort;
    u->input_mode = 0;
    u->channel_selection = 0;
    app_list_sort(a, u->chooser_action, a->list.descending);
    invalidate_mouse(u);
    a->dirty = 1;
}

static void open_manual(App *a, Ui *u)
{
    Editor *e = draft(a, u);
    if (e) ed_unselect(e);
    if (!u->manual) u->manual_view = a->current;
    u->manual_from_app = u->manual_action = u->manual_searching = u->manual_found = 0;
    u->manual_term[0] = 0;
    u->manual_draw_width = u->manual_draw_height = 0;
    u->manual_search_first = 0;
    u->manual_search_scroll = u->manual_search_visual = 0;
    if (u->manual_search) ed_init(u->manual_search);
    u->manual = 1;
    u->channels = u->input_mode = 0;
    app_help(a);
    invalidate_mouse(u);
    app_status(a, "");
}

static void cancel_manual_search(App *a, Ui *u)
{
    u->manual_searching = 0;
    invalidate_mouse(u);
    a->dirty = 1;
}

static void close_manual(App *a, Ui *u)
{
    size_t view = u->manual_view;
    if (view >= a->buffer_count || a->buffers[view].kind == B_HELP) view = 0;
    u->manual = 0;
    u->manual_from_app = u->manual_searching = u->manual_found = 0;
    u->manual_term[0] = 0;
    if (u->manual_search) {
        config_wipe(u->manual_search, sizeof *u->manual_search);
        free(u->manual_search);
        u->manual_search = NULL;
    }
    u->manual_view = SIZE_MAX;
    u->input_mode = 1;
    app_switch(a, view);
    {
        char label[1024];
        const IrcDisplayField fields[] = {{"view", a->buffers[view].name}};
        if (irc_display_render(label, sizeof label, "Returned to {view}.", fields, 1))
            app_status(a, "%s", label);
        else app_status(a, "Returned to %s.", a->buffers[view].name);
    }
}

static void leave_manual(App *a, Ui *u)
{
    int from_app = u->manual_from_app;
    close_manual(a, u);
    if (from_app) {
        return_to_app(a, u);
        app_status(a, "%s", irc_tr("Returned to vipirc."));
    }
    else {
        resume_body(u);
        invalidate_mouse(u);
    }
}

static void return_to_chat(App *a, Ui *u)
{
    if (u->manual) close_manual(a, u);
    config_wipe(&u->bindings, sizeof u->bindings);
    if (settings_is_open(u)) settings_close(u->settings, a);
    if (guided_active(u->owner->guided)) guided_close(u->owner->guided);
    if (transfers_active(u->owner->transfers)) transfers_close(u->owner->transfers);
    if (prompt_active(u->command)) prompt_close(u->command);
    u->command_from_app = 0;
    u->owner->servers = u->owner->unread = u->owner->close_confirm = 0;
    u->app_menu = u->license = u->about = u->channels = u->confirming = u->nick_menu = u->quit_confirm = 0;
    if (u->header_origin < a->buffer_count) app_switch(a, u->header_origin);
    u->header_origin = SIZE_MAX;
    u->pending_command = u->close_command = u->command_limit_warned = 0;
    config_wipe(&u->pending, sizeof u->pending);
    u->input_mode = 1;
    u->status_selected = u->status_cycle = 0;
    u->nav_focus = FOCUS_APP;
    nick_completion_reset(&u->completion);
    invalidate_mouse(u);
    a->dirty = 1;
}

static void return_to_app(App *a, Ui *u)
{
    int selected = u->app_selection;
    return_to_chat(a, u);
    u->app_menu = 1;
    u->app_selection = selected >= 0 && selected < APP_ENTRIES ? selected : APP_SERVER;
    u->input_mode = 0;
    resume_body(u);
}

static void app_menu_action(App *a, Ui *u)
{
    u->app_menu = 0;
    if (u->app_selection == APP_SERVER || u->app_selection == APP_NICK) {
        open_settings(a, u);
        if (u->settings && !settings_open_server(u->settings, u->owner->client, u->slot,
                                               u->app_selection == APP_NICK))
            app_status(a, "%s", irc_tr("Could not open this server's settings. Choose it in Settings."));
    } else if (u->app_selection == APP_LICENSE) {
        u->license = 1;
        u->license_scroll = 0;
        u->input_mode = 0;
        app_status(a, "");
    } else if (u->app_selection == APP_ABOUT) open_about(a, u);
    else if (u->app_selection == APP_SETTINGS) open_settings(a, u);
    else if (u->app_selection == APP_BINDINGS) open_bindings(a, u, 1);
    else if (u->app_selection == APP_MANUAL) {
        open_manual(a, u);
        u->manual_from_app = 1;
    }
    else if (u->app_selection == APP_COMMAND) {
        u->app_menu = 1; /* Capture the invoking screen before open_command hides it. */
        if (open_command(a, u, "/")) u->command_from_app = 1;
        else { u->app_menu = 1; u->input_mode = 0; }
    }
    else if (u->app_selection == APP_BACKGROUND) { return_to_chat(a, u); u->owner->background_requested = 1; }
}

static void bindings_changed(App *a, Ui *u)
{
    u->bindings.drawn = 0;
    u->bindings.changed_at = app_now_ms(a);
    invalidate_mouse(u);
    a->dirty = 1;
}

static void open_bindings(App *a, Ui *u, int from_app)
{
    HotkeyTable *table = &u->bindings;
    config_wipe(table, sizeof *table);
    table->active = 1;
    table->from_app = from_app;
    table->count = a->cfg.hotkey_count;
    if (table->count > CONFIG_HOTKEY_MAX) table->count = CONFIG_HOTKEY_MAX;
    memcpy(table->rows, a->cfg.hotkeys, table->count * sizeof table->rows[0]);
    u->app_menu = 0;
    u->input_mode = 0;
    bindings_changed(a, u);
    app_status(a, "%s", irc_tr("Custom bindings only. Add a key and action, then Apply or Save. Esc discards staged edits."));
}

static void close_bindings(App *a, Ui *u)
{
    int from_app = u->bindings.from_app;
    config_wipe(&u->bindings, sizeof u->bindings);
    if (from_app && !u->hotkey_command) return_to_app(a, u);
    else u->input_mode = !prompt_active(u->command);
    invalidate_mouse(u);
    app_status(a, "%s", irc_tr("Key binding table closed. Unapplied edits discarded."));
}

static void bindings_edit(App *a, Ui *u, int add)
{
    HotkeyTable *table = &u->bindings;
    if (!add && table->selected >= table->count) return;
    if (add && table->count >= CONFIG_HOTKEY_MAX) {
        app_status(a, "%s", irc_tr("The table is full. Edit or remove a binding first."));
        return;
    }
    table->old_key = add ? 0 : table->rows[table->selected].key;
    table->key_name[0] = 0;
    table->action = table->action_first = 0;
    if (!add) {
        (void)config_hotkey_key_name(table->old_key, table->key_name, sizeof table->key_name);
        for (size_t i = 0; i < config_hotkey_action_count(); ++i)
            if (config_hotkey_action_at(i)->id == table->rows[table->selected].action) table->action = i;
    }
    table->key_length = strlen(table->key_name);
    table->stage = 1;
    bindings_changed(a, u);
    app_status(a, "%s", irc_tr("Type a key name such as F7, Ctrl-K, Alt-x or PageUp; Enter chooses its action."));
}

static int bindings_store(App *a, Ui *u)
{
    HotkeyTable *table = &u->bindings;
    unsigned key = config_hotkey_key(table->key_name);
    const ConfigHotkeyAction *action = config_hotkey_action_at(table->action);
    Config candidate;
    char error[256];
    config_defaults(&candidate);
    memcpy(candidate.hotkeys, table->rows, sizeof candidate.hotkeys);
    candidate.hotkey_count = table->count;
    int okay = key && action;
    if (okay && table->old_key && table->old_key != key)
        okay = config_hotkey_set(&candidate, table->old_key, CONFIG_ACTION_NONE, error, sizeof error) > 0;
    if (okay) okay = config_hotkey_set(&candidate, key, action->id, error, sizeof error) > 0;
    if (okay) {
        memcpy(table->rows, candidate.hotkeys, sizeof table->rows);
        table->count = candidate.hotkey_count;
        for (size_t i = 0; i < table->count; ++i)
            if (table->rows[i].key == key) table->selected = i;
        table->stage = 0;
        bindings_changed(a, u);
        app_status(a, "%s", irc_tr("Binding staged. Apply changes this session; Save also writes the primary config."));
    } else app_status(a, "%s", irc_tr("Cannot stage that binding; choose a supported key and action."));
    config_wipe(&candidate, sizeof candidate);
    return okay;
}

/* The table is a staged copy of primary application
 * preferences, not the current server identity. Apply/Save go through
 * client_interface_apply so all active frontends see the same bindings. */
static void bindings_apply(App *a, Ui *u, int save)
{
    Config candidate;
    char error[512];
    ClientSlot *primary = &u->owner->client->slots[0];
    if (primary->app) app_config_effective(primary->app, &candidate);
    else candidate = primary->seed;
    memcpy(candidate.hotkeys, u->bindings.rows, sizeof candidate.hotkeys);
    candidate.hotkey_count = u->bindings.count;
    int okay = client_interface_apply(u->owner->client, &candidate, save, error, sizeof error);
    config_wipe(&candidate, sizeof candidate);
    if (okay < 0) {
        char label[1024];
        const IrcDisplayField fields[] = {{"error", error}};
        if (irc_display_render(label, sizeof label, "Could not apply/save bindings: {error}", fields, 1))
            app_status(a, "%s", label);
        else app_status(a, "Could not apply/save bindings: %s", error);
    }
    else app_status(a, "%s", save ? "Bindings saved in the primary config and applied across servers." :
                    "Bindings applied across servers. Save or /save input makes them persistent.");
    bindings_changed(a, u);
}

static void bindings_key(App *a, Ui *u, wint_t key, int special)
{
    HotkeyTable *table = &u->bindings;
    if (special && key == PASTE_BEGIN) { table->paste = 1; bindings_changed(a, u); return; }
    if (special && key == PASTE_END) {
        table->paste = 0; bindings_changed(a, u);
        app_status(a, "%s", irc_tr("Paste ignored in the key binding table; type key names explicitly."));
        return;
    }
    if (table->paste) return;
    int enter = (special && key == KEY_ENTER) || (!special && (key == '\r' || key == '\n'));
    int fresh = table->drawn && app_now_ms(a) - table->changed_at >= 100;
    if (!special && key == 27) {
        if (table->stage) { --table->stage; bindings_changed(a, u); }
        else close_bindings(a, u);
        return;
    }
    if (table->stage == 1) {
        if ((special && key == KEY_BACKSPACE) || (!special && (key == 8 || key == 127))) {
            if (table->key_length) table->key_name[--table->key_length] = 0;
        } else if (enter && fresh) {
            unsigned binding = config_hotkey_key(table->key_name);
            if (!binding) { app_status(a, "%s", irc_tr("Unknown key. Use F1..F12, Ctrl-A..Z, Alt-a..z/0..9 or a named navigation key.")); return; }
            for (size_t i = 0; i < table->count; ++i)
                if (table->rows[i].key == binding && binding != table->old_key) {
                    app_status(a, "%s", irc_tr("That key already has a binding. Edit its existing row or choose another key."));
                    return;
                }
            (void)config_hotkey_key_name(binding, table->key_name, sizeof table->key_name);
            table->key_length = strlen(table->key_name);
            table->stage = 2;
            bindings_changed(a, u);
            app_status(a, "%s", irc_tr("Select an action with arrows, then Enter stages the binding."));
        } else if (!special && key >= 33 && key <= 126 && table->key_length + 1 < sizeof table->key_name) {
            table->key_name[table->key_length++] = (char)key;
            table->key_name[table->key_length] = 0;
        }
        return;
    }
    if (table->stage == 2) {
        size_t count = config_hotkey_action_count();
        if ((special && key == KEY_UP) || (!special && key == 'k')) {
            if (table->action) --table->action;
        } else if ((special && key == KEY_DOWN) || (!special && key == 'j')) {
            if (table->action + 1 < count) ++table->action;
        } else if (special && key == KEY_HOME) table->action = 0;
        else if (special && key == KEY_END) table->action = count ? count - 1 : 0;
        else if (enter && fresh) (void)bindings_store(a, u);
        return;
    }
    size_t page = LINES > 9 ? (size_t)(LINES - 9) : 1u;
    if ((special && key == KEY_UP) || (!special && key == 'k')) {
        if (table->selected) --table->selected;
    } else if ((special && key == KEY_DOWN) || (!special && key == 'j')) {
        if (table->selected + 1 < table->count) ++table->selected;
    } else if (special && key == KEY_HOME) table->selected = 0;
    else if (special && key == KEY_END) table->selected = table->count ? table->count - 1 : 0;
    else if (special && key == KEY_PPAGE) table->selected = table->selected > page ? table->selected - page : 0;
    else if (special && key == KEY_NPAGE && table->count) {
        size_t remaining = table->count - 1 - table->selected;
        table->selected += remaining < page ? remaining : page;
    } else if (!special && key == 'a' && fresh) bindings_edit(a, u, 1);
    else if ((enter || (!special && key == 'e')) && fresh) bindings_edit(a, u, 0);
    else if (((special && key == KEY_DC) || (!special && key == 'd')) && fresh && table->selected < table->count) {
        size_t index = table->selected;
        memmove(table->rows + index, table->rows + index + 1, (table->count - index - 1) * sizeof table->rows[0]);
        --table->count;
        memset(table->rows + table->count, 0, sizeof table->rows[0]);
        if (table->selected && table->selected >= table->count) --table->selected;
        bindings_changed(a, u);
        app_status(a, "%s", irc_tr("Removal staged. Apply or Save to use the changed table."));
    } else if (!special && key == 'p' && fresh) bindings_apply(a, u, 0);
    else if (!special && key == 's' && fresh) bindings_apply(a, u, 1);
}

static void draw_bindings(App *a, Ui *u)
{
    HotkeyTable *table = &u->bindings;
    char line[512];
    erase();
    text_at(2, 0, COLS, irc_tr("Key bindings"), A_BOLD);
    if (LINES < 9 || COLS < 20) {
        text_at(3, 0, COLS, irc_tr("Resize; Esc returns."), A_NORMAL);
        (void)curs_set(0);
        return;
    }
    size_t height = LINES > 9 ? (size_t)(LINES - 9) : 1u;
    if (table->stage == 1) {
        text_at(3, 0, COLS, irc_tr("Key name (type the name, not the key itself):"), A_NORMAL);
        (void)snprintf(line, sizeof line, "> %s", table->key_name);
        text_at(5, 0, COLS, line, focus_attribute());
        text_at(6, 0, COLS, irc_tr("Examples: F7, Ctrl-K, Alt-x, PageUp, Enter"), A_DIM);
        text_at(LINES - 2, 0, COLS, irc_tr("Enter: choose action | Backspace: edit | Esc: table"), A_NORMAL);
        (void)curs_set(1);
    } else {
        size_t count = table->stage == 2 ? config_hotkey_action_count() : table->count;
        size_t selected = table->stage == 2 ? table->action : table->selected;
        size_t *first = table->stage == 2 ? &table->action_first : &table->first;
        if (selected < *first) *first = selected;
        if (selected >= *first + height) *first = selected - height + 1;
        size_t maximum = count > height ? count - height : 0;
        if (*first > maximum) *first = maximum;
        if (table->stage == 2) {
            const IrcDisplayField fields[] = {{"key", table->key_name}};
            if (!irc_display_render(line, sizeof line, "Choose action for {key}", fields, 1))
                (void)snprintf(line, sizeof line, "Choose action for %s", table->key_name);
        } else {
            char count[32], limit[32];
            (void)snprintf(count, sizeof count, "%zu", table->count);
            (void)snprintf(limit, sizeof limit, "%u", CONFIG_HOTKEY_MAX);
            const IrcDisplayField fields[] = {{"count", count}, {"limit", limit}};
            if (!irc_display_render(line, sizeof line, "Custom bindings: {count} / {limit} (none installed by default)", fields, 2))
                (void)snprintf(line, sizeof line,
                    "Custom bindings: %zu / %u (none installed by default)", table->count, CONFIG_HOTKEY_MAX);
        }
        text_at(3, 0, COLS, line, A_NORMAL);
        text_at(4, 0, COLS, table->stage == 2 ? irc_tr("ACTION              DESCRIPTION") :
                irc_tr("KEY                 ACTION"), A_BOLD);
        for (size_t row = 0; row < height && *first + row < count; ++row) {
            size_t index = *first + row;
            if (table->stage == 2) {
                const ConfigHotkeyAction *action = config_hotkey_action_at(index);
                (void)snprintf(line, sizeof line, "%-19s %s", action->name, action->description);
            } else {
                char key[CONFIG_HOTKEY_NAME_SIZE];
                const ConfigHotkeyAction *action = config_hotkey_action(table->rows[index].action);
                (void)config_hotkey_key_name(table->rows[index].key, key, sizeof key);
                (void)snprintf(line, sizeof line, "%-19s %s", key, action ? action->name : "(invalid)");
            }
            text_at((int)row + 5, 0, COLS, line, index == selected ? focus_attribute() : A_NORMAL);
        }
        if (!count) text_at(5, 0, COLS, irc_tr("No custom bindings. Press a to add one."), A_DIM);
        text_at(LINES - 3, 0, COLS, irc_tr("Up/Down: select | Home/End: first/last"), A_DIM);
        text_at(LINES - 2, 0, COLS, table->stage == 2 ? irc_tr("Enter: stage binding | Esc: key name") :
                irc_tr("a:Add  Enter:Edit  d:Remove  p:Apply  s:Save  Esc:Back"), A_NORMAL);
        (void)curs_set(0);
    }
    text_at(LINES - 1, 0, COLS, app_status_text(a), A_NORMAL);
    if (table->stage == 1) {
        int cursor = (int)table->key_length + 2;
        move(5, cursor < COLS ? cursor : COLS - 1);
    } else move(2, 0);
}

static void scroll_manual(App *a, size_t amount, int direction)
{
    Buffer *b = &a->buffers[2];
    int width = COLS > 1 ? COLS - 1 : 1;
    int available = LINES - 7;
    size_t height = available > 0 ? (size_t)available : 1u, rows = 0;
    for (size_t i = 0; i < b->line_count; ++i)
        rows += (size_t)history_rows(b->lines[(b->line_start + i) % b->line_cap], width, -1, 0, 0);
    size_t maximum = rows > height ? rows - height : 0;
    if (b->scroll > maximum) b->scroll = maximum;
    if (direction < 0) b->scroll += amount < maximum - b->scroll ? amount : maximum - b->scroll;
    else b->scroll = b->scroll > amount ? b->scroll - amount : 0;
    a->dirty = 1;
}

static unsigned char manual_fold(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

/* Translate the saved byte position again after wrapping changes. Matches
 * belong to manual lines, never to a particular terminal row or column. */
static void reveal_manual_match(App *a, Ui *u)
{
    Buffer *b = &a->buffers[2];
    int width = COLS > 1 ? COLS - 1 : 1;
    size_t height = LINES > 7 ? (size_t)(LINES - 7) : 1u;
    size_t rows = 0, match_row = 0;
    if (!u->manual_found || u->manual_match_line >= b->line_count) return;
    for (size_t i = 0; i < b->line_count; ++i) {
        const char *line = b->lines[(b->line_start + i) % b->line_cap];
        if (i == u->manual_match_line) {
            IrcArabicLayout *visual = visual_text_layout(line, width, 0);
            if (visual) {
                match_row = rows;
                for (size_t g = 0; g < visual->glyph_count; ++g) {
                    const IrcArabicGlyph *glyph = &visual->glyphs[g];
                    if (glyph->byte_first <= u->manual_match_offset &&
                        glyph->byte_past > u->manual_match_offset) {
                        match_row += (size_t)glyph->row;
                        break;
                    }
                }
                irc_arabic_layout_free(visual);
            } else {
                mbstate_t state;
                memset(&state, 0, sizeof state);
                int col = 0;
                match_row = rows;
                for (size_t offset = 0; line[offset];) {
                    wchar_t wc;
                    size_t n = display_character(line + offset, &wc, &state);
                    if (!n) break;
                    int cells = wcwidth(wc);
                    if (cells < 0 || cells > width) cells = 1;
                    if (col + cells > width || (col == width && cells > 0)) {
                        ++match_row; col = 0;
                    }
                    if (offset >= u->manual_match_offset) break;
                    col += cells;
                    offset += n;
                }
            }
        }
        rows += (size_t)history_rows(line, width, -1, 0, 0);
    }
    size_t maximum = rows > height ? rows - height : 0;
    size_t first = match_row > height / 2 ? match_row - height / 2 : 0;
    if (first > maximum) first = maximum;
    b->scroll = maximum - first;
}

/* KMP keeps each search linear in the bounded manual size, even for repeated
 * text and a 256-byte near-match. ASCII folds only; other UTF-8 bytes compare
 * literally. Overlapping matches are retained and repeats wrap once. */
static void find_manual_match(App *a, Ui *u, int direction, int repeat)
{
    Buffer *b = &a->buffers[2];
    size_t prefix[256], length = strlen(u->manual_term), count = 0;
    size_t first_line = 0, first_offset = 0, last_line = 0, last_offset = 0;
    size_t chosen_line = 0, chosen_offset = 0, chosen_number = 0;
    int prior = repeat && u->manual_found && u->manual_source_start == b->line_start &&
                u->manual_source_count == b->line_count;
    if (!length) {
        u->manual_found = 0;
        app_status(a, "%s", irc_tr("Search cleared."));
        return;
    }
    prefix[0] = 0;
    for (size_t i = 1, matched = 0; i < length; ++i) {
        unsigned char c = manual_fold((unsigned char)u->manual_term[i]);
        while (matched && c != manual_fold((unsigned char)u->manual_term[matched]))
            matched = prefix[matched - 1];
        if (c == manual_fold((unsigned char)u->manual_term[matched])) ++matched;
        prefix[i] = matched;
    }
    for (size_t i = 0; i < b->line_count; ++i) {
        const char *line = b->lines[(b->line_start + i) % b->line_cap];
        for (size_t offset = 0, matched = 0; line[offset]; ++offset) {
            unsigned char c = manual_fold((unsigned char)line[offset]);
            while (matched && c != manual_fold((unsigned char)u->manual_term[matched]))
                matched = prefix[matched - 1];
            if (c == manual_fold((unsigned char)u->manual_term[matched])) ++matched;
            if (matched != length) continue;
            size_t start = offset + 1 - length;
            ++count;
            if (count == 1) { first_line = i; first_offset = start; }
            last_line = i; last_offset = start;
            int after = i > u->manual_match_line ||
                        (i == u->manual_match_line && start > u->manual_match_offset);
            int before = i < u->manual_match_line ||
                         (i == u->manual_match_line && start < u->manual_match_offset);
            if ((direction >= 0 && !chosen_number && (!prior || after)) ||
                (direction < 0 && (!prior || before))) {
                chosen_line = i; chosen_offset = start; chosen_number = count;
            }
            matched = prefix[matched - 1];
        }
    }
    u->manual_found = count != 0;
    if (!count) {
        app_status(a, "%s", irc_tr("No matches. Press / to change the search."));
        return;
    }
    int wrapped = !chosen_number;
    if (!chosen_number) {
        chosen_line = direction >= 0 ? first_line : last_line;
        chosen_offset = direction >= 0 ? first_offset : last_offset;
        chosen_number = direction >= 0 ? 1 : count;
    }
    u->manual_match_line = chosen_line;
    u->manual_match_offset = chosen_offset;
    u->manual_match_length = length;
    u->manual_source_start = b->line_start;
    u->manual_source_count = b->line_count;
    reveal_manual_match(a, u);
    {
        char label[1024], position[32], total[32];
        (void)snprintf(position, sizeof position, "%zu", chosen_number);
        (void)snprintf(total, sizeof total, "%zu", count);
        const IrcDisplayField fields[] = {
            {"current", position}, {"total", total},
            {"wrap", wrapped ? irc_tr(" Wrapped.") : ""}
        };
        if (irc_display_render(label, sizeof label, "Match {current} of {total}.{wrap}", fields, 3))
            app_status(a, "%s", label);
        else app_status(a, "Match %zu of %zu.%s", chosen_number, count, wrapped ? " Wrapped." : "");
    }
}

static void begin_manual_search(App *a, Ui *u)
{
    if (!u->manual_search) u->manual_search = malloc(sizeof *u->manual_search);
    if (!u->manual_search) {
        app_status(a, "%s", irc_tr("Out of memory; could not open manual search."));
        return;
    }
    ed_init(u->manual_search);
    (void)ed_from_utf8(u->manual_search, u->manual_term);
    u->manual_search_first = 0;
    u->manual_search_scroll = u->manual_search_visual = 0;
    u->manual_searching = 1;
    u->manual_action = 1;
    invalidate_mouse(u);
    app_status(a, "%s", irc_tr("Enter: find | Esc: cancel | Literal search, up to 256 UTF-8 bytes."));
}

static void repeat_manual_search(App *a, Ui *u, int direction)
{
    if (!u->manual_term[0]) begin_manual_search(a, u);
    else {
        u->manual_action = direction < 0 ? 2 : 3;
        find_manual_match(a, u, direction, 1);
        invalidate_mouse(u);
    }
}

static void run_manual_action(App *a, Ui *u)
{
    if (u->manual_action == 0) leave_manual(a, u);
    else if (u->manual_action == 1) begin_manual_search(a, u);
    else repeat_manual_search(a, u, u->manual_action == 2 ? -1 : 1);
}

static void move_manual_action(Ui *u, int direction)
{
    u->manual_action = (u->manual_action + (direction < 0 ? 3 : 1)) % 4;
    invalidate_mouse(u);
}

static int manual_bar(const App *a, const Ui *u, int draw_it, int clicked_x)
{
    static const char *const labels[] = {"Back", "Search", "Previous", "Next"};
    int x = 0, total = 0;
    for (size_t i = 0; i < 4; ++i) total += text_cells(irc_tr(labels[i]), COLS) + 3;
    if (total > COLS) {
        if (draw_it) {
            text_at(3, 0, 1, "<", A_BOLD);
            text_at(3, COLS - 1, 1, ">", A_BOLD);
            focus_button(a, 3, 2, COLS - 4, irc_tr(labels[u->manual_action]), 1);
        }
        return clicked_x == 0 ? -2 : clicked_x == COLS - 1 ? -3 :
               clicked_x >= 2 && clicked_x < COLS - 2 ? u->manual_action : -1;
    }
    for (size_t i = 0; i < 4; ++i) {
        int width = text_cells(irc_tr(labels[i]), COLS) + 2;
        if (draw_it) focus_button(a, 3, x, width, irc_tr(labels[i]), (int)i == u->manual_action);
        else if (clicked_x >= x && clicked_x < x + width) return (int)i;
        x += width + 1;
    }
    return -1;
}

static void manual_search_key(App *a, Ui *u, wint_t key, int special)
{
    Editor *e = u->manual_search;
    int width = COLS > 8 ? COLS - 8 : 1;
    if (!e) { cancel_manual_search(a, u); return; }
    if ((special && key == KEY_ENTER) || (!special && (key == '\r' || key == '\n'))) {
        if (ed_to_utf8(e, u->manual_term, sizeof u->manual_term) == SIZE_MAX) {
            app_status(a, "%s", irc_tr("Search is limited to 256 UTF-8 bytes."));
            return;
        }
        cancel_manual_search(a, u);
        find_manual_match(a, u, 1, 0);
    } else if (special) {
        switch (key) {
        case KEY_LEFT: ed_visual_left(e, width); break;
        case KEY_RIGHT: ed_visual_right(e, width); break;
        case KEY_HOME: ed_visual_home(e, width); break;
        case KEY_END: ed_visual_end(e, width); break;
        case KEY_BACKSPACE: (void)ed_backspace(e); break;
        case KEY_DC: (void)ed_delete(e); break;
        default: break;
        }
    } else if (key == 8 || key == 127) (void)ed_backspace(e);
    else if (key >= 32) {
        size_t bytes = key < (wint_t)0x80 ? 1u : key < (wint_t)0x800 ? 2u : key < (wint_t)0x10000 ? 3u : 4u;
        if (e->bytes > sizeof u->manual_term - 1 - bytes)
            app_status(a, "%s", irc_tr("Search is limited to 256 UTF-8 bytes."));
        else if (!ed_insert(e, (wchar_t)key))
            app_status(a, "%s", irc_tr("Unsupported control character in search."));
    }
}

/* Keep frontend-owned draft/navigation indices aligned with App removal. */
static void view_closed(void *context, size_t index)
{
    Ui *u = context;
    invalidate_mouse(u);
    u->chooser_drawn = 0;
    if (u->drafts[index]) config_wipe(u->drafts[index], sizeof *u->drafts[index]);
    free(u->drafts[index]);
    for (size_t i = index; i + 1 < APP_BUFFERS; ++i) {
        u->drafts[i] = u->drafts[i + 1];
        u->literal[i] = u->literal[i + 1];
        u->limit_warned[i] = u->limit_warned[i + 1];
        u->input_scroll[i] = u->input_scroll[i + 1];
    }
    u->drafts[APP_BUFFERS - 1] = NULL;
    u->literal[APP_BUFFERS - 1] = 0;
    u->limit_warned[APP_BUFFERS - 1] = 0;
    u->input_scroll[APP_BUFFERS - 1] = 0;
    if (u->warning_view == index) {
        if (u->confirming == 3) u->confirming = 0;
        u->warning_view = SIZE_MAX;
    } else if (u->warning_view != SIZE_MAX && u->warning_view > index) --u->warning_view;
    if (u->submitting_view == index) u->submitting_view = SIZE_MAX;
    else if (u->submitting_view != SIZE_MAX && u->submitting_view > index) --u->submitting_view;
    if (u->close_source == index) u->close_source = SIZE_MAX;
    else if (u->close_source != SIZE_MAX && u->close_source > index) --u->close_source;
    if (u->pending_view == index && u->confirming == 1) u->confirming = 0;
    else if (u->pending_view > index) --u->pending_view;
    if (u->manual_view == index) u->manual_view = 0;
    else if (u->manual_view != SIZE_MAX && u->manual_view > index) --u->manual_view;
    if (u->header_origin == index) u->header_origin = 0;
    else if (u->header_origin != SIZE_MAX && u->header_origin > index) --u->header_origin;
    nick_completion_reset(&u->completion);
}

/* The warning contains local ASCII text. Wrap words rather than silently
 * clipping its acknowledgment instructions on narrow SSH terminals. */
static int dialog_paragraph(const char *s, int width, int y, int x, attr_t attr)
{
    char line[128];
    if (!s || width <= 0) return 0;
    if ((size_t)width >= sizeof line) width = (int)sizeof line - 1;
    int rows = 0;
    while (*s) {
        size_t n = strlen(s), take;
        if (n > (size_t)width) n = (size_t)width;
        take = n;
        if (s[n] && s[n] != ' ')
            while (take && s[take] != ' ') --take;
        if (!take) take = n;
        memcpy(line, s, take); line[take] = 0;
        if (y >= 0) text_at(y + rows, x, width, line, attr);
        ++rows; s += take;
        while (*s == ' ') ++s;
    }
    return rows;
}

static void draw_limit_warning(const Ui *u)
{
    char title[80];
    const char *parts[5];
    int count = 3, w = COLS > 76 ? 72 : COLS - 2, height = 2;
    int inner = w - 4;
    snprintf(title, sizeof title, "Message limit reached (%zu bytes).", u->warning_budget);
    parts[0] = title;
    parts[1] = u->warning_multiple ? "This draft needs multiple chat lines." :
                                   "More text will need multiple chat lines.";
    parts[2] = "Enter/Esc: keep editing. Nothing sent.";
    for (int i = 0; i < count; ++i)
        height += dialog_paragraph(parts[i], inner, -1, 0, A_NORMAL);
    if (height > LINES) {
        snprintf(title, sizeof title, "%zu-byte limit", u->warning_budget);
        parts[0] = title;
        parts[1] = u->warning_multiple ? "Multiple" : "More text ->";
        parts[2] = "chat lines.";
        parts[3] = "Enter/Esc";
        parts[4] = "Nothing sent.";
        count = 5; height = 2;
        for (int i = 0; i < count; ++i)
            height += dialog_paragraph(parts[i], inner, -1, 0, A_NORMAL);
    }
    if (height > LINES) height = LINES;
    int left = (COLS - w) / 2, top = (LINES - height) / 2, row = top + 1;
    attrset(A_REVERSE);
    for (int i = 0; i < height; ++i) mvhline(top + i, left, ' ', w);
    for (int i = 0; i < count; ++i)
        row += dialog_paragraph(parts[i], inner, row, left + 2,
                                i ? A_REVERSE : A_REVERSE | A_BOLD);
    move(top + height - 1, left + w - 2);
    attrset(A_NORMAL);
}

static void draw_dialog(const Ui *u)
{
    if (!u->confirming) return;
    if (u->confirming == 3) { draw_limit_warning(u); return; }
    char line[512];
    int w = COLS > 76 ? 72 : COLS - 4;
    int left = (COLS - w) / 2, top = LINES / 2 - 2;
    for (int k = 0; k < 5; ++k) {
        attron(A_REVERSE); mvhline(top + k, left, ' ', w); attroff(A_REVERSE);
    }
    if (u->confirming == 1) {
        snprintf(line, sizeof line, "This text needs %zu IRC messages.", u->pending.count);
        text_at(top + 1, left + 2, w - 4, line, A_REVERSE | A_BOLD);
        snprintf(line, sizeof line, "Send all to %s? [y/N]", u->pending.target);
        text_at(top + 2, left + 2, w - 4, line, A_REVERSE);
        text_at(top + 3, left + 2, w - 4, irc_tr("No keeps your text for editing."), A_REVERSE);
    } else {
        snprintf(line, sizeof line, "Close %s?", u->close_name);
        text_at(top + 1, left + 2, w - 4, line, A_REVERSE | A_BOLD);
        text_at(top + 2, left + 2, w - 4, irc_tr("Discard this view's unsent draft? [y/N]"), A_REVERSE);
        text_at(top + 3, left + 2, w - 4, irc_tr("No keeps the view and draft."), A_REVERSE);
    }
    move(top + 2, left + w - 3);
}

static void activity_label(char *out, size_t cap, size_t unread, size_t mentions)
{
    char one[24], two[24];
    if (unread > 999) snprintf(one, sizeof one, "999+");
    else snprintf(one, sizeof one, "%zu", unread);
    if (mentions > 999) snprintf(two, sizeof two, "999+");
    else snprintf(two, sizeof two, "%zu", mentions);
    snprintf(out, cap, "new %s !%s", one, two);
}

static size_t server_map(const Client *client, size_t map[CLIENT_MAX_SERVERS])
{
    size_t count = 0;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (client->slots[i].used) map[count++] = i;
    return count;
}

static void open_servers(App *a, Ui *u)
{
    UiSet *all = u->owner;
    size_t map[CLIENT_MAX_SERVERS], count = server_map(all->client, map);
    all->server_selection = 0;
    for (size_t i = 0; i < count; ++i)
        if (map[i] == u->slot) all->server_selection = i;
    all->server_action = 0;
    all->servers = 1;
    all->close_confirm = 0;
    u->input_mode = u->channels = 0;
    app_status(a, "");
}

static const char *const server_actions[] = {"Open", "New", "Connect", "Disconnect", "Close", "Away", "Back"};
#define SERVER_ACTIONS (sizeof server_actions / sizeof server_actions[0])

/* Returns an action index, -2/-3 for compact arrows, or -1 outside controls. */
static int server_bar(const App *a, const UiSet *all, int draw_it, int clicked_x)
{
    int total = 0, x = 0;
    for (size_t i = 0; i < SERVER_ACTIONS; ++i) total += text_cells(irc_tr(server_actions[i]), COLS) + 3;
    if (total > COLS) {
        if (draw_it) {
            text_at(LIST_ACTION_ROW, 0, 1, "<", A_BOLD);
            text_at(LIST_ACTION_ROW, COLS - 1, 1, ">", A_BOLD);
            focus_button(a, LIST_ACTION_ROW, 2, COLS - 4, irc_tr(server_actions[all->server_action]), 1);
        }
        return clicked_x == 0 ? -2 : clicked_x == COLS - 1 ? -3 :
               clicked_x >= 2 && clicked_x < COLS - 2 ? (int)all->server_action : -1;
    }
    for (size_t i = 0; i < SERVER_ACTIONS; ++i) {
        int width = text_cells(irc_tr(server_actions[i]), COLS) + 2;
        if (draw_it) focus_button(a, LIST_ACTION_ROW, x, width, irc_tr(server_actions[i]), i == all->server_action);
        else if (clicked_x >= x && clicked_x < x + width) return (int)i;
        x += width + 1;
    }
    return -1;
}

static void draw_servers(App *a, Ui *u)
{
    UiSet *all = u->owner;
    size_t map[CLIENT_MAX_SERVERS], count = server_map(all->client, map);
    char line[1024], safe[1536];
    erase();
    if (LINES < 9 || COLS < 20) {
        text_at(1, 0, COLS, irc_tr("Resize; Esc returns to chat."), A_NORMAL);
        (void)curs_set(0); move(0, 0); wnoutrefresh(stdscr); a->dirty = 0; return;
    }
    text_at(2, 0, COLS, a->test_mode ? irc_tr("[OFFLINE] Servers") : irc_tr("Servers"), A_BOLD);
    list_hint(a);
    if (all->server_selection >= count) all->server_selection = count ? count - 1 : 0;
    size_t height = (size_t)(LINES - 6);
    size_t first = all->server_selection >= height ? all->server_selection - height + 1 : 0;
    for (size_t row = 0; row < height && first + row < count; ++row) {
        size_t index = map[first + row], unread = 0, mentions = 0;
        ClientSlot *slot = &all->client->slots[index];
        const App *session = slot->app;
        Config effective;
        if (session && !session->net && !session->sim) app_config_effective(session, &effective);
        else effective = session ? session->cfg : slot->seed;
        const Config *cfg = &effective;
        if (session) app_activity(session, &unread, &mentions);
        char activity[64];
        activity_label(activity, sizeof activity, unread, mentions);
        snprintf(line, sizeof line, "%s | %c%zu: %.255s:%d (%.63s) | %s%s",
            activity, index == all->client->current ? '*' : ' ', index, *cfg->server ? cfg->server : slot->profile_id,
            cfg->port, *cfg->nick ? cfg->nick : "?", slot->load_failed ? irc_tr("unavailable") :
            session ? app_connection_label(session) : irc_tr("disconnected"), slot->saved ? "" : " | unsaved");
        irc_sanitize(safe, sizeof safe, line);
        config_wipe(&effective, sizeof effective);
        focus_text(a, 4 + (int)row, 0, COLS - 1, safe, first + row == all->server_selection);
    }
    (void)server_bar(a, all, 1, -1);
    const ClientSlot *selected = count ? &all->client->slots[map[all->server_selection]] : NULL;
    const char *status = selected && *selected->error ? selected->error :
                         *all->client->catalog_error ? all->client->catalog_error : a->status;
    irc_sanitize(safe, sizeof safe, status);
    text_at(LINES - 1, 0, COLS - 1, safe, A_NORMAL);
    if (all->close_confirm) {
        const char *warning = "Close this server? Drafts and transfers will be discarded. Its saved startup entry will be removed; the profile file will be kept. [y/N]";
        int width = COLS > 72 ? 68 : COLS - 4;
        int rows = dialog_paragraph(warning, width, -1, 0, A_NORMAL);
        int top = (LINES - rows - 2) / 2;
        if (top < 0) top = 0;
        int left = (COLS - width) / 2;
        for (int y = top; y < LINES && y < top + rows + 2; ++y) mvhline(y, left, ' ', width);
        text_at(top, left, width, irc_tr("Close server"), A_REVERSE | A_BOLD);
        (void)dialog_paragraph(warning, width, top + 1, left, A_BOLD);
    }
    (void)curs_set(0); move(0, 0); wnoutrefresh(stdscr); a->dirty = 0;
}

static void server_action(App *a, Ui *u)
{
    UiSet *all = u->owner;
    size_t map[CLIENT_MAX_SERVERS], count = server_map(all->client, map);
    char error[512];
    if (!count) return;
    if (all->server_selection >= count) all->server_selection = count - 1;
    size_t index = map[all->server_selection];
    switch (all->server_action) {
    case 0:
        if (!client_get_app(all->client, index, error, sizeof error)) app_status(a, "%s", error);
        else { all->switch_to = index; all->switch_view = 0; all->servers = 0; }
        break;
    case 1: {
        Config config;
        if (!u->settings) u->settings = settings_create();
        if (!u->settings) { app_status(a, "%s", irc_tr("Cannot allocate the New server form.")); break; }
        if (count >= CLIENT_MAX_SERVERS) { app_status(a, "%s", irc_tr("Server limit reached (8). Close a secondary server first.")); break; }
        client_new_config(all->client, &config);
        settings_open_new(u->settings, &config);
        config_wipe(&config, sizeof config);
        all->servers = 0;
        break;
    }
    case 2:
        if (client_connect(all->client, index, error, sizeof error) < 0) app_status(a, "%s", error);
        else {
            char label[512], number[32];
            (void)snprintf(number, sizeof number, "%zu", index);
            const IrcDisplayField fields[] = {{"number", number}};
            if (irc_display_render(label, sizeof label, "Connecting server {number}.", fields, 1))
                app_status(a, "%s", label);
            else app_status(a, "Connecting server %zu.", index);
        }
        break;
    case 3:
        client_disconnect(all->client, index);
        {
            char label[512], number[32];
            (void)snprintf(number, sizeof number, "%zu", index);
            const IrcDisplayField fields[] = {{"number", number}};
            if (irc_display_render(label, sizeof label,
                    "Server {number} disconnected. Accepted direct transfers continue.", fields, 1))
                app_status(a, "%s", label);
            else app_status(a, "Server %zu disconnected. Accepted direct transfers continue.", index);
        }
        break;
    case 4:
        if (!index) { app_status(a, "%s", irc_tr("Server 0 is the primary entry; use Disconnect to stop it.")); break; }
        all->close_confirm = 1;
        all->close_at = app_now_ms(a);
        break;
    case 5: {
        App *selected = client_get_app(all->client, index, error, sizeof error);
        SendPlan unused;
        if (!selected) app_status(a, "%s", error);
        else if (!selected->registered) {
            char number[32], shown[1024];
            (void)snprintf(number, sizeof number, "%zu", index);
            const IrcDisplayField fields[] = {{"number", number}};
            if (irc_display_render(shown, sizeof shown, "Connect server {number} before setting away status.", fields, 1))
                app_status(a, "%s", shown);
            else app_status(a, "Connect server %zu before setting away status.", index);
        }
        else {
            if (app_command(selected, selected->away ? "/back" : "/away Away", &unused))
                {
            char number[32], shown[1024];
            (void)snprintf(number, sizeof number, "%zu", index);
            const IrcDisplayField fields[] = {{"number", number}};
            if (irc_display_render(shown, sizeof shown, "Away change requested on server {number}; waiting for its acknowledgment.", fields, 1))
                app_status(a, "%s", shown);
            else app_status(a, "Away change requested on server %zu; waiting for its acknowledgment.", index);
        }
            else if (selected != a)
                {
            char number[32], shown[1024];
            (void)snprintf(number, sizeof number, "%zu", index);
            const IrcDisplayField fields[] = {{"number", number}, {"status", selected->status}};
            if (irc_display_render(shown, sizeof shown, "Server {number}: {status}", fields, 2))
                app_status(a, "%s", shown);
            else app_status(a, "Server %zu: %s", index, selected->status);
        }
        }
        break;
    }
    case 6: all->servers = 0; break;
    default: break;
    }
    a->dirty = 1;
}

static void server_key(App *a, Ui *u, wint_t key, int special)
{
    UiSet *all = u->owner;
    size_t map[CLIENT_MAX_SERVERS], count = server_map(all->client, map);
    int enter = (special && key == KEY_ENTER) || (!special && (key == '\r' || key == '\n'));
    if (!special && key == 27) { all->servers = all->close_confirm = 0; return; }
    if (all->close_confirm) {
        if (app_now_ms(a) - all->close_at < 100) return;
        if (!special && (key == 'y' || key == 'Y')) {
            if (count && all->server_selection < count) all->close_slot = map[all->server_selection];
            all->close_confirm = 0;
        } else if (enter || (!special && (key == 'n' || key == 'N'))) all->close_confirm = 0;
        return;
    }
    if ((special && key == KEY_UP) || (!special && key == 'k')) {
        if (all->server_selection) --all->server_selection;
    } else if ((special && key == KEY_DOWN) || (!special && key == 'j')) {
        if (all->server_selection + 1 < count) ++all->server_selection;
    } else if ((special && key == KEY_LEFT) || (!special && key == 'h')) {
        all->server_action = (all->server_action + SERVER_ACTIONS - 1) % SERVER_ACTIONS;
    } else if ((special && key == KEY_RIGHT) || (!special && key == 'l')) {
        all->server_action = (all->server_action + 1) % SERVER_ACTIONS;
    } else if ((special && key == KEY_HOME) || (!special && key == 'g')) {
        all->server_selection = 0;
    } else if ((special && key == KEY_END) || (!special && key == 'G')) {
        all->server_selection = count ? count - 1 : 0;
    } else if (special && key == KEY_DC) { all->server_action = 4; server_action(a, u); }
    else if (enter) server_action(a, u);
    else if (!special && key == 'n') { all->server_action = 1; server_action(a, u); }
    else if (!special && key == 'q') all->servers = 0;
}

#if UI_MOUSE
static void server_mouse(App *a, Ui *u, const MEVENT *event)
{
    UiSet *all = u->owner;
    if (all->close_confirm || LINES < 9 || COLS < 20) return;
    size_t map[CLIENT_MAX_SERVERS], count = server_map(all->client, map);
    int direction = 0;
#ifdef BUTTON4_PRESSED
    if (event->bstate & BUTTON4_PRESSED) direction = -1;
#endif
#ifdef BUTTON5_PRESSED
    if (event->bstate & BUTTON5_PRESSED) direction = 1;
#endif
    if (direction) {
        if (direction < 0 && all->server_selection) --all->server_selection;
        else if (direction > 0 && all->server_selection + 1 < count) ++all->server_selection;
    } else if (event->bstate & BUTTON1_PRESSED) {
        if (event->y == LIST_ACTION_ROW) {
            int action = server_bar(a, all, 0, event->x);
            if (action == -2) all->server_action = (all->server_action + SERVER_ACTIONS - 1) % SERVER_ACTIONS;
            else if (action == -3) all->server_action = (all->server_action + 1) % SERVER_ACTIONS;
            else if (action >= 0) { all->server_action = (size_t)action; server_action(a, u); }
        } else if (event->y >= 4 && event->y < LINES - 2) {
            size_t height = (size_t)(LINES - 6);
            size_t first = all->server_selection >= height ? all->server_selection - height + 1 : 0;
            size_t row = first + (size_t)(event->y - 4);
            if (row < count) {
                all->server_selection = row;
                all->server_action = 0;
                server_action(a, u);
            }
        }
    }
    a->dirty = 1;
}
#endif

static size_t chooser_action_count(const Ui *u)
{
    return u->channels == 2 ? 4u : 5u;
}

static ChooserAction chooser_action_kind(const Ui *u, size_t index)
{
    static const ChooserAction channel_actions[] = {
        CHOOSE_OPEN, CHOOSE_NEW, CHOOSE_PART, CHOOSE_CLOSE, CHOOSE_BACK
    };
    static const ChooserAction query_actions[] = {
        CHOOSE_OPEN, CHOOSE_NEW, CHOOSE_CLOSE, CHOOSE_BACK
    };
    return u->channels == 2 ? query_actions[index] : channel_actions[index];
}

static const char *chooser_action_label(const Ui *u, size_t index)
{
    switch (chooser_action_kind(u, index)) {
    case CHOOSE_OPEN: return irc_tr("Open");
    case CHOOSE_NEW: return irc_tr(u->channels == 2 ? "New" : "Join");
    case CHOOSE_PART: return irc_tr("Part");
    case CHOOSE_CLOSE: return irc_tr("Close");
    case CHOOSE_BACK: return irc_tr("Back");
    }
    return "";
}

static int chooser_bar_width(const Ui *u)
{
    size_t count = chooser_action_count(u);
    int width = (int)count - 1;
    for (size_t i = 0; i < count; ++i)
        width += text_cells(chooser_action_label(u, i), COLS) + 2;
    return width;
}

static void move_chooser_action(Ui *u, int direction)
{
    size_t count = chooser_action_count(u);
    u->chooser_action = direction < 0 ?
        (u->chooser_action + count - 1) % count : (u->chooser_action + 1) % count;
}

static void draw_chooser_bar(const App *a, Ui *u)
{
    size_t count = chooser_action_count(u);
    if (u->chooser_action >= count) u->chooser_action = 0;
    if (chooser_bar_width(u) <= COLS - 1) {
        int x = 0;
        for (size_t i = 0; i < count; ++i) {
            const char *label = chooser_action_label(u, i);
            int width = text_cells(label, COLS) + 2;
            focus_button(a, LIST_ACTION_ROW, x, width, label, i == u->chooser_action);
            x += width + 1;
        }
    } else {
        const char *label = chooser_action_label(u, u->chooser_action);
        int width = text_cells(label, COLS) + 2;
        int left = (COLS - width - 4) / 2;
        text_at(LIST_ACTION_ROW, left, 2, "< ", A_BOLD);
        focus_button(a, LIST_ACTION_ROW, left + 2, width, label, 1);
        text_at(LIST_ACTION_ROW, left + width + 2, 2, " >", A_BOLD);
    }
}

/* Focus is attached to displayed state, rather than a separate menu row. */
typedef struct StatusLayout {
    char labels[FOCUS_COUNT][512];
    int x[FOCUS_COUNT], y[FOCUS_COUNT], width[FOCUS_COUNT];
    unsigned present;
    int before, after;
} StatusLayout;

static size_t unread_map(const UiSet *all, UnreadView *map)
{
    size_t count = 0;
    for (size_t slot = 0; slot < CLIENT_MAX_SERVERS; ++slot) {
        const App *session = all->client->slots[slot].app;
        if (!session) continue;
        for (size_t view = 0; view < session->buffer_count; ++view) {
            const Buffer *b = &session->buffers[view];
            if (!b->unread || (b->kind != B_SERVER && b->kind != B_CHANNEL && b->kind != B_QUERY)) continue;
            if (map) map[count] = (UnreadView){slot, view};
            ++count;
        }
    }
    return count;
}

/* Rebuild unread membership only at repaint. Visible rows below retain stable
 * server and buffer identities, so events arriving before a click cannot
 * redirect an already displayed item. Mention text remains in the App ring. */
static void unread_snapshot(UiSet *all)
{
    size_t previous = all->unread_count;
    size_t previous_total = all->activity_count;
    size_t previous_mentions = previous_total - previous;
    UnreadView selected = { SIZE_MAX, SIZE_MAX };
    if (all->unread_selection >= previous_mentions && all->unread_selection < previous_total)
        selected = all->unread_views[all->unread_selection - previous_mentions];
    all->unread_count = unread_map(all, all->unread_views);
    all->activity_count = all->unread_count;
    for (size_t slot = 0; slot < CLIENT_MAX_SERVERS; ++slot)
        if (all->client->slots[slot].app)
            all->activity_count += all->client->slots[slot].app->buffers[1].line_count;
    size_t mentions = all->activity_count - all->unread_count;
    if (selected.slot != SIZE_MAX) {
        for (size_t i = 0; i < all->unread_count; ++i)
            if (all->unread_views[i].slot == selected.slot && all->unread_views[i].view == selected.view) {
                all->unread_selection = mentions + i;
                break;
            }
    }
    if (all->unread_selection >= all->activity_count)
        all->unread_selection = all->activity_count ? all->activity_count - 1 : 0;
}

static void view_name(const App *a, size_t view, char *out, size_t cap)
{
    const Buffer *b = &a->buffers[view];
    if (b->kind == B_CHANNEL) {
        size_t number = 0;
        for (size_t i = 3; i <= view; ++i) if (a->buffers[i].kind == B_CHANNEL) ++number;
        snprintf(out, cap, "Channel %zu: %s", number, b->name);
    } else if (b->kind == B_SERVER) snprintf(out, cap, "%s", irc_tr("Channel 0: Server"));
    else if (b->kind == B_QUERY) snprintf(out, cap, "Query: %s", b->name);
    else snprintf(out, cap, "%s", b->name);
}

static void status_layout(const App *a, const Ui *u, StatusLayout *s)
{
    memset(s, 0, sizeof *s);
    for (size_t i = 0; i < FOCUS_COUNT; ++i) s->x[i] = -1;
    size_t queries = 0, query_unread = 0, query_mentions = 0, channels = 0, servers = 0;
    size_t mentions = 0;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        const ClientSlot *slot = &u->owner->client->slots[i];
        if (!slot->used) continue;
        Config effective;
        if (slot->app) app_config_effective(slot->app, &effective);
        else effective = slot->seed;
        if (*effective.server || *slot->profile_id || (slot->app && *slot->app->cfg.server)) ++servers;
        if (slot->app) mentions += slot->app->buffers[1].line_count;
        config_wipe(&effective, sizeof effective);
    }
    for (size_t i = 3; i < a->buffer_count; ++i)
        if (a->buffers[i].kind == B_CHANNEL) ++channels;
    snprintf(s->labels[FOCUS_APP], sizeof s->labels[0], "vipirc");
    snprintf(s->labels[FOCUS_SERVERS], sizeof s->labels[0], "%s (%zu)", irc_tr("Servers"), servers);
    if (channels) snprintf(s->labels[FOCUS_CHANNELS], sizeof s->labels[0], "%s (%zu)", irc_tr("Channels"), channels);
    else snprintf(s->labels[FOCUS_CHANNELS], sizeof s->labels[0], "%s", irc_tr("Channels"));
    for (size_t i = 3; i < a->buffer_count; ++i) if (a->buffers[i].kind == B_QUERY) {
        ++queries;
        if (a->buffers[i].unread) ++query_unread;
        if (a->buffers[i].unread_mentions) ++query_mentions;
    }
    size_t transfers = dcc_count(u->owner->dcc);
    if (queries) snprintf(s->labels[FOCUS_QUERIES], sizeof s->labels[0], "%s (%zu)%s", irc_tr("Queries"), queries,
                         query_mentions ? "!" : query_unread ? "*" : "");
    else snprintf(s->labels[FOCUS_QUERIES], sizeof s->labels[0], "%s", irc_tr("Queries"));
    if (transfers) snprintf(s->labels[FOCUS_TRANSFERS], sizeof s->labels[0], "%s (%zu)", irc_tr("File transfers"), transfers);
    else snprintf(s->labels[FOCUS_TRANSFERS], sizeof s->labels[0], "%s", irc_tr("File transfers"));
    size_t unread = unread_map(u->owner, NULL);
    snprintf(s->labels[FOCUS_ACTIVITY], sizeof s->labels[0], "%s (%zu/%zu)", irc_tr("Mentions/unread"), mentions, unread);
    snprintf(s->labels[FOCUS_CLEAR], sizeof s->labels[0], "%s", irc_tr("Clear"));
    snprintf(s->labels[FOCUS_SEND], sizeof s->labels[0], "%s", irc_tr("Send"));
    s->present = 1u << FOCUS_APP;
    if (servers) s->present |= 1u << FOCUS_SERVERS;
    if (channels) s->present |= 1u << FOCUS_CHANNELS;
    if (queries) s->present |= 1u << FOCUS_QUERIES;
    if (transfers) s->present |= 1u << FOCUS_TRANSFERS;
    if (unread || mentions) s->present |= 1u << FOCUS_ACTIVITY;
    if (main_screen(u) && u->mouse_active) s->present |= (1u << FOCUS_CLEAR) | (1u << FOCUS_SEND);
    for (size_t i = 0; i < FOCUS_COUNT; ++i) {
        char safe[512];
        irc_sanitize(safe, sizeof safe, s->labels[i]);
        int overridden = i <= FOCUS_LAST && custom_hotkey_scope(u) &&
                         config_hotkey_lookup(&a->cfg, CONFIG_KEY_F1 + (unsigned)i) != CONFIG_ACTION_NONE;
        if (i <= FOCUS_LAST && a->cfg.shortcut_labels && !overridden && !u->bindings.active)
            snprintf(s->labels[i], sizeof s->labels[i], "F%zu %.480s", i + 1, safe);
        else snprintf(s->labels[i], sizeof s->labels[i], "%s", safe);
        s->width[i] = text_cells(s->labels[i], COLS) + 2;
    }
    size_t order[FOCUS_LAST + 1], count = 0, focus = 0;
    int total = 0;
    for (size_t i = FOCUS_APP; i <= FOCUS_LAST; ++i) {
        if (!(s->present & (1u << i))) continue;
        if (i == u->nav_focus) focus = count;
        order[count++] = i;
        s->y[i] = 0;
        total += s->width[i] + (count > 1 ? 3 : 0);
    }
    size_t first = 0, end = count;
    int x = 0, available = COLS;
    if (total > available) {
        available -= 4; x = 2;
        first = focus;
        if (s->width[order[first]] > available) s->width[order[first]] = available;
        int used = s->width[order[first]];
        end = first + 1;
        while (first && used + s->width[order[first - 1]] + 3 <= available) {
            --first; used += s->width[order[first]] + 3;
        }
        while (end < count && used + s->width[order[end]] + 3 <= available)
            used += s->width[order[end++]] + 3;
        s->before = first != 0;
        s->after = end != count;
    }
    for (size_t i = first; i < end; ++i) {
        size_t control = order[i];
        s->x[control] = x;
        x += s->width[control] + 3;
    }
    if (main_screen(u) && u->mouse_active) {
        s->y[FOCUS_CLEAR] = s->y[FOCUS_SEND] = LINES - 1;
        s->x[FOCUS_CLEAR] = 0;
        s->x[FOCUS_SEND] = s->width[FOCUS_CLEAR] + 2;
    }
}

static void draw_status_controls(const App *a, Ui *u)
{
    StatusLayout s;
    if (u->nav_focus > FOCUS_LAST) u->nav_focus = FOCUS_APP;
    status_layout(a, u, &s);
    mvhline(0, 0, ' ', COLS);
    for (size_t i = 0; i < FOCUS_COUNT; ++i) if (s.x[i] >= 0)
        focus_text(a, s.y[i], s.x[i], s.width[i], s.labels[i], u->status_selected && i == u->nav_focus);
    int last_end = -1;
    for (size_t i = FOCUS_APP; i <= FOCUS_LAST; ++i) if (s.x[i] >= 0) {
        if (last_end >= 0) text_at(0, last_end, 3, " > ", A_NORMAL);
        last_end = s.x[i] + s.width[i];
    }
    if (s.before) text_at(0, 0, 1, "<", A_BOLD);
    if (s.after) text_at(0, COLS - 1, 1, ">", A_BOLD);
    /* Network events can change label widths before queued input is handled.
     * Retain only the spans actually drawn, never recompute a click's target
     * from newer peer data. Labels remain temporary rendering storage. */
    for (size_t i = 0; i < FOCUS_COUNT; ++i)
        u->status_hits.controls[i] = (StatusHit){s.x[i], s.y[i], s.width[i]};
    u->status_hits.view = a->current;
    u->status_hits.kind = a->buffers[a->current].kind;
    /* Keep the drawn row's eligible controls, including those reachable by
     * horizontal scrolling on a narrow terminal. Network updates only change
     * this set at the next repaint, just as with the mouse hit spans. */
    u->status_hits.present = s.present;
    u->status_hits.rows = LINES;
    u->status_hits.cols = COLS;
    u->status_hits.input_mode = u->input_mode;
    u->status_hits.shortcut_labels = a->cfg.shortcut_labels;
    u->status_hits.panel = mouse_panel(u);
    u->status_hits.before = s.before;
    u->status_hits.after = s.after;
    u->status_hits.valid = !u->confirming && !u->owner->close_confirm;
}

static void move_status_focus(const App *a, Ui *u, int direction)
{
    StatusLayout layout;
    status_layout(a, u, &layout);
    if (!u->status_cycle || u->nav_focus > FOCUS_LAST)
        u->nav_focus = direction < 0 ? FOCUS_LAST : FOCUS_APP;
    else u->nav_focus = (u->nav_focus + (direction < 0 ? FOCUS_LAST : 1u)) % (FOCUS_LAST + 1u);
    for (size_t i = 0; i <= FOCUS_LAST && !(layout.present & (1u << u->nav_focus)); ++i)
        u->nav_focus = (u->nav_focus + (direction < 0 ? FOCUS_LAST : 1u)) % (FOCUS_LAST + 1u);
    u->status_cycle = u->status_selected = 1;
}

static void open_unread(App *a, Ui *u)
{
    u->owner->unread = 1;
    u->owner->unread_selection = 0;
    u->owner->unread_count = u->owner->unread_first = 0;
    u->owner->activity_count = u->owner->activity_first = u->owner->activity_hits_count = 0;
    u->owner->unread_action = 0;
    u->owner->activity_scroll = 0;
    u->input_mode = u->channels = 0;
    app_status(a, "");
}

static void activate_status(App *a, Ui *u)
{
    FocusControl selected = (FocusControl)u->nav_focus;
    if (selected <= FOCUS_LAST) {
        size_t origin = u->header_origin < a->buffer_count ? u->header_origin : a->current;
        return_to_chat(a, u);
        u->header_origin = origin;
        u->nav_focus = (size_t)selected;
        u->status_cycle = 1;
    }
    switch (selected) {
    case FOCUS_APP: u->app_menu = 1; u->app_selection = 0; u->app_first = 0; break;
    case FOCUS_SERVERS: open_servers(a, u); break;
    case FOCUS_CHANNELS: open_channels(a, u); break;
    case FOCUS_QUERIES: open_pms(a, u); break;
    case FOCUS_TRANSFERS: transfers_open(u->owner->transfers, u->slot); break;
    case FOCUS_ACTIVITY: open_unread(a, u); break;
    case FOCUS_CLEAR: clear_input(a, u); break;
    case FOCUS_SEND: submit(a, u); break;
    default: break;
    }
    a->dirty = 1;
}

static void focus_header(App *a, Ui *u, int direction)
{
    if (!u->status_selected && u->header_origin == SIZE_MAX) u->header_origin = a->current;
    move_status_focus(a, u, direction);
    activate_status(a, u);
    u->status_selected = 1;
}

static int activity_entry(const UiSet *all, size_t item, char *out, size_t cap, ActivityHit *hit)
{
    size_t slot, view = SIZE_MAX;
    const App *session = NULL;
    memset(hit, 0, sizeof *hit);
    hit->item = item;
    if (item >= all->activity_count) return 0;
    size_t mention_count = all->activity_count - all->unread_count;
    if (item >= mention_count) {
        UnreadView entry = all->unread_views[item - mention_count];
        slot = entry.slot; view = entry.view;
        session = all->client->slots[slot].app;
        if (!session || view >= session->buffer_count) return 0;
        const Buffer *b = &session->buffers[view];
        char name[256], activity[64];
        view_name(session, view, name, sizeof name);
        activity_label(activity, sizeof activity, (size_t)b->unread, (size_t)b->unread_mentions);
        snprintf(out, cap, "%s | S%zu %s | %s", activity, slot, name, session->cfg.server);
    } else {
        size_t line = item;
        for (slot = 0; slot < CLIENT_MAX_SERVERS; ++slot) {
            session = all->client->slots[slot].app;
            if (!session) continue;
            const Buffer *mentions = &session->buffers[1];
            if (line >= mentions->line_count) { line -= mentions->line_count; continue; }
            view = app_mention_view(session, line);
            hit->buffer_id = app_mention_origin(session, line);
            snprintf(out, cap, "S%zu %s%s", slot, view == SIZE_MAX ? irc_tr("[source closed] ") : "",
                     mentions->lines[(mentions->line_start + line) % mentions->line_cap]);
            break;
        }
        if (slot == CLIENT_MAX_SERVERS) return 0;
    }
    hit->slot = slot;
    hit->server_token = all->client->slots[slot].token;
    if (view < session->buffer_count) hit->buffer_id = session->buffers[view].id;
    return 1;
}

static void activate_unread(App *a, Ui *u)
{
    UiSet *all = u->owner;
    const ActivityHit *hit = NULL;
    for (size_t i = 0; i < all->activity_hits_count; ++i)
        if (all->activity_hits[i].item == all->unread_selection) { hit = &all->activity_hits[i]; break; }
    if (!hit) { app_status(a, "%s", irc_tr("Select a visible activity entry.")); return; }
    const ClientSlot *slot = &all->client->slots[hit->slot];
    if (!slot->used || slot->token != hit->server_token || !slot->app) {
        app_status(a, "%s", irc_tr("That source server is no longer available.")); return;
    }
    if (!hit->buffer_id) { app_status(a, "%s", irc_tr("That source conversation is no longer available.")); return; }
    size_t view = app_open_mention(slot->app, hit->buffer_id);
    if (view == SIZE_MAX) {
        if (slot->app != a) app_status(a, "%s", slot->app->status);
        return;
    }
    all->switch_to = hit->slot;
    all->switch_view = view;
    all->unread = 0;
}

static const char *const unread_actions[] = {"Open", "Clear mentions", "Back"};
#define UNREAD_ACTIONS (sizeof unread_actions / sizeof unread_actions[0])

static int unread_bar(const App *a, const UiSet *all, int draw_it, int clicked_x)
{
    int total = 0, x = 0;
    for (size_t i = 0; i < UNREAD_ACTIONS; ++i) total += text_cells(irc_tr(unread_actions[i]), COLS) + 3;
    if (total > COLS) {
        if (draw_it) {
            text_at(LIST_ACTION_ROW, 0, 1, "<", A_BOLD);
            text_at(LIST_ACTION_ROW, COLS - 1, 1, ">", A_BOLD);
            focus_button(a, LIST_ACTION_ROW, 2, COLS - 4, irc_tr(unread_actions[all->unread_action]), 1);
        }
        return clicked_x == 0 ? -2 : clicked_x == COLS - 1 ? -3 :
               clicked_x >= 2 && clicked_x < COLS - 2 ? (int)all->unread_action : -1;
    }
    for (size_t i = 0; i < UNREAD_ACTIONS; ++i) {
        int width = text_cells(irc_tr(unread_actions[i]), COLS) + 2;
        if (draw_it) focus_button(a, LIST_ACTION_ROW, x, width, irc_tr(unread_actions[i]), i == all->unread_action);
        else if (clicked_x >= x && clicked_x < x + width) return (int)i;
        x += width + 1;
    }
    return -1;
}

/* The activity screen and /mentions clear have one workspace-wide scope.
 * Removing feed copies never marks source conversations read or drops drafts. */
static void clear_mentions(App *a, Ui *u)
{
    UiSet *all = u->owner;
    size_t cleared = 0;
    for (size_t slot = 0; slot < CLIENT_MAX_SERVERS; ++slot)
        if (all->client->slots[slot].used && all->client->slots[slot].app)
            cleared += app_clear_mentions(all->client->slots[slot].app);
    all->unread_selection = all->unread_first = 0;
    all->activity_first = all->activity_hits_count = 0;
    all->activity_scroll = 0;
    unread_snapshot(all);
    invalidate_mouse(u);
    app_status(a, "Cleared %zu mention%s across all servers.", cleared, cleared == 1 ? "" : "s");
}

static void unread_action(App *a, Ui *u)
{
    switch (u->owner->unread_action) {
    case 0: activate_unread(a, u); break;
    case 1: clear_mentions(a, u); break;
    case 2: return_to_chat(a, u); break;
    default: break;
    }
}

static void unread_key(App *a, Ui *u, wint_t key, int special)
{
    UiSet *all = u->owner;
    size_t count = all->activity_count, *selected = &all->unread_selection;
    if (!count) *selected = 0;
    else if (*selected >= count) *selected = count - 1;
    size_t before = *selected;
    if (!special && (key == 27 || key == 'q')) return_to_chat(a, u);
    else if (!special && key == '\t') {
        if (a->cfg.hints) app_status(a, "%s", irc_tr("Input is on the chat screen. Press Esc to return."));
    }
    else if ((special && key == KEY_UP) || (!special && key == 'k')) { if (*selected) --*selected; }
    else if ((special && key == KEY_DOWN) || (!special && key == 'j')) { if (*selected + 1 < count) ++*selected; }
    else if ((special && key == KEY_LEFT) || (!special && key == 'h')) {
        all->unread_action = (all->unread_action + UNREAD_ACTIONS - 1) % UNREAD_ACTIONS;
    }
    else if ((special && key == KEY_RIGHT) || (!special && key == 'l')) {
        all->unread_action = (all->unread_action + 1) % UNREAD_ACTIONS;
    }
    else if ((special && key == KEY_HOME) || (!special && key == 'g')) { *selected = 0; all->activity_scroll = 0; }
    else if ((special && key == KEY_END) || (!special && key == 'G')) { *selected = count ? count - 1 : 0; all->activity_scroll = 0; }
    else if (special && (key == KEY_PPAGE || key == KEY_NPAGE)) {
        char line[4096]; ActivityHit hit;
        int page = LINES > 9 ? LINES - 9 : 1;
        int rows = activity_entry(all, *selected, line, sizeof line, &hit) ? history_rows(line, COLS - 1, -1, 0, 0) : 1;
        int maximum = rows > page ? rows - page : 0;
        if (key == KEY_PPAGE) {
            if (all->activity_scroll) all->activity_scroll = all->activity_scroll > page ? all->activity_scroll - page : 0;
            else if (*selected) --*selected;
        } else if (all->activity_scroll < maximum) {
            all->activity_scroll += page;
            if (all->activity_scroll > maximum) all->activity_scroll = maximum;
        } else if (*selected + 1 < count) ++*selected;
    }
    else if ((special && key == KEY_ENTER) || (!special && (key == '\r' || key == '\n'))) unread_action(a, u);
    if (*selected != before) all->activity_scroll = 0;
}

/* Activity rows are snapshots over multiple server
 * slots. Resolve saved hit targets by current slot and buffer identities
 * before acting: indices alone can be recycled after a close. */
static void draw_unread(App *a, Ui *u)
{
    UiSet *all = u->owner;
    unread_snapshot(all);
    if (all->activity_first > all->unread_selection) all->activity_first = all->unread_selection;
    for (int pass = 0; pass < 2; ++pass) {
        int selected_visible = 0, row = 5;
        size_t previous_slot = SIZE_MAX;
        all->activity_hits_count = 0;
        erase();
        char heading[128];
        snprintf(heading, sizeof heading, "%sMentions and unread | All servers", a->test_mode ? irc_tr("[OFFLINE] ") : "");
        text_at(2, 0, COLS, heading, A_BOLD);
        if (LINES < 9 || COLS < 20) {
            text_at(3, 0, COLS, irc_tr("Resize; Esc returns."), A_NORMAL);
            break;
        }
        (void)unread_bar(a, all, 1, -1);
        size_t mentions = all->activity_count - all->unread_count;
        int mention_section = all->activity_first < mentions || !all->activity_count;
        snprintf(heading, sizeof heading, mention_section ? "Mentions (%zu)" : "Unread conversations (%zu)",
                 mention_section ? mentions : all->unread_count);
        text_at(4, 0, COLS, heading, A_BOLD | A_UNDERLINE);
        if (!all->activity_count) text_at(row, 0, COLS, irc_tr("No mentions or unread conversations."), A_NORMAL);
        for (size_t item = all->activity_first; item < all->activity_count && row < LINES - 2 &&
             all->activity_hits_count < UI_ACTIVITY_HITS_MAX; ++item) {
            if (item == mentions && mention_section) {
                snprintf(heading, sizeof heading, "Unread conversations (%zu)", all->unread_count);
                text_at(row++, 0, COLS, heading, A_BOLD | A_UNDERLINE);
                mention_section = 0;
                previous_slot = SIZE_MAX;
                if (row >= LINES - 2) break;
            }
            char line[4096]; ActivityHit hit;
            if (!activity_entry(all, item, line, sizeof line, &hit)) continue;
            if (hit.slot != previous_slot) {
                const App *session = all->client->slots[hit.slot].app;
                snprintf(heading, sizeof heading, "  Server %zu: %.90s", hit.slot, session->cfg.server);
                text_at(row++, 0, COLS, heading, A_BOLD);
                previous_slot = hit.slot;
                if (row >= LINES - 2) break;
            }
            int selected = item == all->unread_selection;
            char focused[4100];
            const char *painted = line;
            if (selected) { snprintf(focused, sizeof focused, "[%s]", line); painted = focused; }
            int rows = history_rows(painted, COLS - 1, -1, 0, 0);
            int skip = selected ? all->activity_scroll : 0;
            if (skip >= rows) skip = rows - 1;
            int visible = rows - skip < LINES - 2 - row ? rows - skip : LINES - 2 - row;
            attr_t attr = selected ? focus_attribute() : A_NORMAL;
            (void)attr_set(attr, (short)PAIR_NUMBER(attr), NULL);
            (void)history_rows(painted, COLS - 1, row, skip, visible);
            attrset(A_NORMAL);
            hit.y = row; hit.rows = visible;
            all->activity_hits[all->activity_hits_count++] = hit;
            if (selected) selected_visible = 1;
            row += visible;
        }
        if (all->activity_count && !selected_visible && !pass) {
            all->activity_first = all->unread_selection;
            continue;
        }
        break;
    }
    all->unread_first = all->activity_first;
    hint_at(a, LINES - 2, 0, COLS,
            "Up/Down: item | Left/Right: action | Enter: run | Esc: chat", "Enter: run | Esc: chat");
    text_at(LINES - 1, 0, COLS, app_status_text(a), A_NORMAL);
    (void)curs_set(0); move(0, 0); wnoutrefresh(stdscr); a->dirty = 0;
}

static const char *directory_sort_name(int sort)
{
    return sort == APP_LIST_SORT_CHANNEL ? "Channel" :
           sort == APP_LIST_SORT_TOPIC ? "Topic" : "Users";
}

static void draw_directory(App *a, Ui *u)
{
    char line[1024], filter[512];
    int sort = u->chooser_action;
    char selected_channel[201] = "";
    size_t before_count = app_list_visible_count(a);
    if (before_count && u->channel_selection < before_count)
        snprintf(selected_channel, sizeof selected_channel, "%s", a->list.entries[u->channel_selection].channel);
    if (sort < APP_LIST_SORT_CHANNEL || sort > APP_LIST_SORT_TOPIC) sort = APP_LIST_SORT_USERS;
    u->chooser_action = sort;
    app_list_sort(a, sort, a->list.descending);
    size_t count = app_list_visible_count(a);
    if (*selected_channel)
        for (size_t i = 0; i < count; ++i)
            if (!strcmp(a->list.entries[i].channel, selected_channel)) { u->channel_selection = i; break; }
    if (!count) u->channel_selection = 0;
    else if (u->channel_selection >= count) u->channel_selection = count - 1;

    snprintf(line, sizeof line, "Network channels | S%zu %s:%d | %s%s", u->slot,
             *a->cfg.server ? a->cfg.server : "(no server)", a->cfg.port,
             a->list.active ? "receiving" : a->list.complete ? "complete" : "cached",
             a->list.truncated ? ", cache truncated" : "");
    text_at(2, 0, COLS, line, A_BOLD);

    snprintf(filter, sizeof filter, "Sort: %s %s | received %zu | matched %zu | showing %zu",
             directory_sort_name(sort), a->list.descending ? "desc" : "asc",
             a->list.received, a->list.matched, count);
    size_t filter_used = strlen(filter);
#define DIRECTORY_FILTER(...) do { \
        if (filter_used + 1 < sizeof filter) { \
            int added = snprintf(filter + filter_used, sizeof filter - filter_used, __VA_ARGS__); \
            if (added > 0) filter_used += (size_t)added < sizeof filter - filter_used ? (size_t)added : sizeof filter - filter_used - 1; \
        } \
    } while (0)
    if (*a->list.server_mask) DIRECTORY_FILTER(" | mask %s", a->list.server_mask);
    if (a->list.min_set) DIRECTORY_FILTER(" | min %llu", (unsigned long long)a->list.min_users);
    if (a->list.max_set) DIRECTORY_FILTER(" | max %llu", (unsigned long long)a->list.max_users);
    if (*a->list.name_filter) DIRECTORY_FILTER(" | name %s", a->list.name_filter);
    if (*a->list.topic_filter) DIRECTORY_FILTER(" | topic %s", a->list.topic_filter);
    if (a->list.display_limit) DIRECTORY_FILTER(" | limit %zu", a->list.display_limit);
#undef DIRECTORY_FILTER
    text_at(3, 0, COLS, filter, A_DIM);

    int channel_width = COLS >= 90 ? 28 : COLS >= 60 ? 20 : 14;
    int users_width = 10;
    int topic_x = channel_width + users_width + 4;
    int topic_width = COLS - topic_x - 1;
    if (topic_width < 8) topic_width = 8;
    snprintf(line, sizeof line, "%-*s | %*s | %s", channel_width,
             sort == APP_LIST_SORT_CHANNEL ? (a->list.descending ? "Channel v" : "Channel ^") : "Channel",
             users_width, sort == APP_LIST_SORT_USERS ? (a->list.descending ? "Users v" : "Users ^") : "Users",
             sort == APP_LIST_SORT_TOPIC ? (a->list.descending ? "Topic v" : "Topic ^") : "Topic");
    text_at(4, 0, COLS - 1, line, A_BOLD | A_UNDERLINE);

    size_t height = LINES > 8 ? (size_t)(LINES - 8) : 1u;
    size_t first = u->channel_selection >= height ? u->channel_selection - height + 1 : 0;
    for (size_t row = 0; row < height && first + row < count; ++row) {
        size_t index = first + row;
        const ChannelListEntry *entry = &a->list.entries[index];
        snprintf(line, sizeof line, "%-*.*s | %*llu | %.*s",
                 channel_width, channel_width, entry->channel, users_width,
                 (unsigned long long)entry->users, topic_width,
                 *entry->topic ? entry->topic : "(no topic)");
        focus_text(a, 5 + (int)row, 0, COLS - 1, line, index == u->channel_selection);
    }
    if (!count)
        focus_text(a, 5, 0, COLS - 1, a->list.active ? irc_tr("Receiving channels...") : irc_tr("No channels match this LIST."), 1);

    hint_at(a, LINES - 2, 0, COLS,
            "Up/Down: channel | Left/Right: sort column | s: direction | Enter: join | r: refresh | /: command | Esc: back",
            "Enter: join | s: sort | Esc: back");
    text_at(LINES - 1, 0, COLS - 1, app_status_text(a), A_NORMAL);
    (void)curs_set(0);
    move(5 + (int)(u->channel_selection - first), 0);
    wnoutrefresh(stdscr);
    a->dirty = 0;
}

static void draw_channels(App *a, Ui *u)
{
    int private_list = u->channels == 2;
    size_t map[APP_BUFFERS], count = private_list ? pm_map(a, map) : channel_map(a, map);
    char line[512];
    memcpy(u->chooser_map, map, count * sizeof *map);
    u->chooser_count = count;
    u->chooser_kind = u->channels;
    u->chooser_drawn = 1;
    if (!count) u->channel_selection = 0;
    else if (u->channel_selection >= count) u->channel_selection = count - 1;
    snprintf(line, sizeof line, "%s%s | S%zu %s:%d", a->test_mode ? irc_tr("[OFFLINE] ") : "",
             irc_tr(private_list ? "Queries" : "Channels"), u->slot, a->cfg.server, a->cfg.port);
    text_at(2, 0, COLS, line, A_BOLD);
    list_hint(a);
    size_t height = (size_t)(LINES - 6);
    size_t first = u->channel_selection >= height ? u->channel_selection - height + 1 : 0;
    for (size_t row = 0; row < height && first + row < count; ++row) {
        size_t n = first + row;
        Buffer *b = &a->buffers[map[n]];
        char activity[64];
        activity_label(activity, sizeof activity, (size_t)b->unread, (size_t)b->unread_mentions);
        if (private_list) snprintf(line, sizeof line, "%s | %s", activity, b->name);
        else if (!n) snprintf(line, sizeof line, "%s | 0: server | %s", activity, app_connection_label(a));
        else snprintf(line, sizeof line, "%s | %zu: %s [%s] | %zu users",
                      activity, n, b->name, irc_tr(b->joined ? "joined" : "left"), b->user_count);
        focus_text(a, 4 + (int)row, 0, COLS - 1, line, n == u->channel_selection);
    }
    if (!count) focus_text(a, 4, 0, COLS - 1, irc_tr("New query..."), 1);
    draw_chooser_bar(a, u);
    text_at(LINES - 1, 0, COLS - 1, app_status_text(a), A_NORMAL);
    move(4 + (int)(u->channel_selection - first), 0);
    draw_dialog(u);
    (void)curs_set(u->confirming ? 1 : 0);
    wnoutrefresh(stdscr); a->dirty = 0;
}

static int user_compare(const void *pa, const void *pb)
{
    const User *a = pa, *b = pb;
    /* Group status users first; nickname comparison is display order only. */
    unsigned ma = a->modes ? a->modes & (0u - a->modes) : UINT_MAX;
    unsigned mb = b->modes ? b->modes & (0u - b->modes) : UINT_MAX;
    if (ma != mb) return ma < mb ? -1 : 1;
    const unsigned char *x = (const unsigned char *)a->nick;
    const unsigned char *y = (const unsigned char *)b->nick;
    while (*x && *y) {
        int d = tolower(*x) - tolower(*y);
        if (d) return d;
        ++x; ++y;
    }
    return (int)*x - (int)*y;
}

static void text_hit(Ui *u, int y, int x, int width, int channel, const char *target)
{
    if (!u->mouse_active || width < 1 || y < 0 || y >= LINES || x < 0 || x >= COLS) return;
    if (width > COLS - x) width = COLS - x;
    if (u->text_hit_count) {
        TextHit *last = &u->text_hits[u->text_hit_count - 1];
        if (last->y == y && last->channel == channel && !strcmp(last->target, target)) {
            if (last->x == x && last->width == width) return;
            if (last->x + last->width == x) { last->width += width; return; }
            if (x + width == last->x) { last->x = x; last->width += width; return; }
        }
    }
    if (u->text_hit_count >= UI_TEXT_HITS_MAX) return;
    if (u->text_hit_count == u->text_hit_capacity) {
        size_t capacity = u->text_hit_capacity ? u->text_hit_capacity * 2 : 32;
        if (capacity > UI_TEXT_HITS_MAX) capacity = UI_TEXT_HITS_MAX;
        TextHit *hits = realloc(u->text_hits, capacity * sizeof *hits);
        if (!hits) return; /* Optional mouse metadata never loses chat text. */
        u->text_hits = hits; u->text_hit_capacity = capacity;
    }
    TextHit *hit = &u->text_hits[u->text_hit_count++];
    hit->x = x; hit->y = y; hit->width = width; hit->channel = channel;
    snprintf(hit->target, sizeof hit->target, "%s", target);
}

/* Store copies of rendered targets, not offsets into mutable user/history
 * arrays. Incoming NICK/PART/history eviction cannot retarget a queued click. */
static void history_hits(Ui *u, const char *line, int width, int y,
                         int skip, int maxrows, unsigned flags, int visual_used)
{
    if (!u->mouse_active || width < 1) return;
    IrcArabicLayout *visual = visual_text_layout(line, width, flags);
    if ((visual != NULL) != visual_used) {
        /* A temporary allocation failure may affect only paint or only hit
         * metadata. Never make differently ordered text clickable. */
        irc_arabic_layout_free(visual);
        return;
    }
    const IrcArabicGlyph **positions = NULL;
    size_t logical_index = 0;
    if (visual) {
        positions = calloc(visual->count, sizeof *positions);
        if (!positions) {
            /* Optional hit metadata may be omitted on allocation failure;
             * logical coordinates must never target reordered text. */
            irc_arabic_layout_free(visual);
            return;
        }
        for (size_t i = 0; i < visual->glyph_count; ++i) {
            const IrcArabicGlyph *glyph = &visual->glyphs[i];
            if (glyph->cells <= 0) continue;
            for (size_t j = glyph->first; j < glyph->past && j < visual->count; ++j)
                positions[j] = glyph;
        }
    }
    mbstate_t state;
    memset(&state, 0, sizeof state);
    const char *target_end = line, *sender = NULL;
    char target[IRC_TARGET_MAX + 1] = "";
    int channel = 0, row = 0, col = 0;
    /* A timestamp and target precede displayed ACTION/NOTICE senders. */
    const char *prefix = strchr(line, ']');
    if (prefix && prefix[1] == ' ' && prefix[2] == '[') {
        const char *end = strchr(prefix + 3, ']');
        if (end && end[1] == ' ' && end[2]) {
            if (end[2] == '*' && end[3] == ' ') sender = end + 4;
            else if (end[2] == '-') sender = end + 3;
        }
    }
    for (const char *s = line; *s; ) {
        if (s >= target_end) {
            const char *end = s;
            int kind = -1;
            if (*s == '#' && (s == line || strchr(" \t[('\"<", s[-1]))) {
                const App *a = u->owner->client->slots[u->slot].app;
                size_t known = 0, remaining = strlen(s);
                for (size_t i = 3; a && i < a->buffer_count; ++i) {
                    const Buffer *b = &a->buffers[i];
                    if (b->kind != B_CHANNEL) continue;
                    size_t length = strlen(b->name);
                    if (length <= known || length > remaining) continue;
                    const char *tail = s + length;
                    while (*tail && strchr("].!?)", *tail)) ++tail;
                    if (*tail && (unsigned char)*tail > 32 && !strchr(",;:<>\"", *tail)) continue;
                    char exact[IRC_TARGET_MAX + 1];
                    memcpy(exact, s, length); exact[length] = 0;
                    if (irc_equal(exact, b->name, a->casemap)) known = length;
                }
                if (known) end = s + known;
                else {
                    while (*end && (unsigned char)*end > 32 && !strchr(",;:()<>\"", *end)) ++end;
                    /* Without a known name, ordinary sentence punctuation
                     * delimits a link; retained names keep their exact bytes. */
                    while (end > s + 1 && strchr("].!?", end[-1])) --end;
                }
                kind = 1;
            } else if ((s > line && s[-1] == '<') || s == sender) {
                while (*end && (unsigned char)*end > 32 && *end != '>') ++end;
                if (s == sender && sender[-1] == '-' && end > s && end[-1] == '-') --end;
                kind = 0;
            }
            size_t length = (size_t)(end - s);
            if (kind >= 0 && length && length < sizeof target) {
                memcpy(target, s, length); target[length] = 0;
                if (kind ? irc_valid_channel(target) : irc_valid_nick(target)) {
                    channel = kind; target_end = end;
                }
            }
        }
        wchar_t wc;
        size_t n = display_character(s, &wc, &state);
        if (!n) break;
        int cells = wcwidth(wc);
        if (cells < 0 || cells > width) cells = 1;
        if (visual) {
            const IrcArabicGlyph *glyph = logical_index < visual->count ? positions[logical_index] : NULL;
            if (glyph) { row = glyph->row; col = glyph->col; cells = glyph->cells; }
            else cells = 0;
        } else if (col + cells > width || (col == width && cells > 0)) { ++row; col = 0; }
        if (cells > 0 && s < target_end && row >= skip && row - skip < maxrows)
            text_hit(u, y + row - skip, col, cells, channel, target);
        col += cells; s += n; ++logical_index;
    }
    free(positions);
    irc_arabic_layout_free(visual);
}

static void draw_users(App *a, Ui *u, Buffer *b, int top, int height, int left, int width)
{
    char label[256];
    snprintf(label, sizeof label, "Users: %zu%s", b->user_count,
             b->names_truncated ? "+ (limit)" : "");
    text_at(top, left + 1, width - 1, label, A_BOLD);
    if (b->user_count) qsort(b->users, b->user_count, sizeof(User), user_compare);
    if (b->user_scroll >= b->user_count) b->user_scroll = b->user_count ? b->user_count - 1 : 0;
    for (int row = 1; row < height; ++row) {
        mvaddch(top + row, left, '|');
        size_t i = b->user_scroll + (size_t)(row - 1);
        if (i >= b->user_count) continue;
        char prefix = ' ';
        for (size_t m = 0; m < strlen(a->prefix_chars) && m < 16; ++m) {
            if (b->users[i].modes & (1u << m)) { prefix = a->prefix_chars[m]; break; }
        }
        snprintf(label, sizeof label, "%c%s", prefix, b->users[i].nick);
        int visual_used;
        text_at_mode(top + row, left + 1, width - 1, label, A_NORMAL, &visual_used);
        IrcArabicLayout *visual = visual_text_layout(label, width - 1, IRC_ARABIC_SINGLE_LINE);
        if (visual && visual_used) {
            for (size_t j = 0; j < visual->glyph_count; ++j) {
                const IrcArabicGlyph *glyph = &visual->glyphs[j];
                if (glyph->cells > 0 && glyph->byte_past > 1)
                    text_hit(u, top + row, left + 1 + glyph->col, glyph->cells, 0, b->users[i].nick);
            }
        } else if (!visual && !visual_used)
            text_hit(u, top + row, left + 2, text_cells(b->users[i].nick, width - 2), 0, b->users[i].nick);
        irc_arabic_layout_free(visual);
    }
}

/* Count and paint with an already chosen layout, including its logical
 * fallback. A visible line may retain this map until both steps complete. */
static int history_rows_layout(const char *s, int width, int y, int skip,
                               int maxrows, const IrcArabicLayout *visual)
{
    mbstate_t state;
    memset(&state, 0, sizeof state);
    int row = 0, col = 0;
    if (width < 1) return 1;
    if (visual) {
        if (y >= 0) irc_curses_arabic_paint(visual, y, 0, width, skip, maxrows);
        return visual->rows;
    }
    while (*s) {
        wchar_t wc;
        size_t n = display_character(s, &wc, &state);
        if (!n) break;
        int w = wcwidth(wc);
        if (w < 0 || w > width) { wc = L'?'; w = 1; }
        if (col + w > width || (col == width && w > 0)) { ++row; col = 0; }
        if (y >= 0 && row >= skip && row - skip < maxrows) {
            int py = y + row - skip;
            if (py >= 0 && py < LINES) {
                wchar_t one[2] = {wc, 0};
                move(py, col);
                (void)addnwstr(one, 1);
            }
        }
        col += w; s += n;
    }
    return row + 1;
}

/* Count or paint wrapped history rows. Drawing walks only the lines selected
 * for the viewport; scroll position is measured in visual rows from the end. */
static int history_rows_mode(const char *s, int width, int y, int skip, int maxrows,
                             int *visual_used)
{
    IrcArabicLayout *visual = visual_text_layout(s, width, 0);
    if (visual_used) *visual_used = visual != NULL;
    int rows = history_rows_layout(s, width, y, skip, maxrows, visual);
    irc_arabic_layout_free(visual);
    return rows;
}

static int history_rows(const char *s, int width, int y, int skip, int maxrows)
{
    return history_rows_mode(s, width, y, skip, maxrows, NULL);
}

static void draw_history(Ui *u, Buffer *b, int top, int height, int width)
{
    if (b->scroll) {
        size_t rows = 0;
        for (size_t i = 0; i < b->line_count; ++i)
            rows += (size_t)history_rows(b->lines[(b->line_start + i) % b->line_cap], width, -1, 0, 0);
        size_t maximum = rows > (size_t)height ? rows - (size_t)height : 0;
        if (b->scroll > maximum) b->scroll = maximum;
    }
    size_t remaining = b->scroll;
    int bottom = top + height;
    for (size_t k = b->line_count; k > 0 && bottom > top; --k) {
        const char *line = b->lines[(b->line_start + k - 1) % b->line_cap];
        IrcArabicLayout *visual = visual_text_layout(line, width, 0);
        int rows = history_rows_layout(line, width, -1, 0, 0, visual);
        if (remaining >= (size_t)rows) {
            remaining -= (size_t)rows;
            irc_arabic_layout_free(visual);
            continue;
        }
        int shown = rows - (int)remaining;
        remaining = 0;
        int take = shown < bottom - top ? shown : bottom - top;
        int visual_used = visual != NULL;
        (void)history_rows_layout(line, width, bottom - take, shown - take, take, visual);
        irc_arabic_layout_free(visual);
        if (u) history_hits(u, line, width, bottom - take, shown - take, take, 0, visual_used);
        bottom -= take;
    }
}

static void highlight_manual_match(const char *line, const Ui *u,
                                   int width, int y, int skip, int height, int visual_used)
{
    mbstate_t state;
    memset(&state, 0, sizeof state);
    int row = 0, col = 0;
    size_t end = u->manual_match_offset + u->manual_match_length;
    attr_t attr = focus_attribute() | A_REVERSE;
    IrcArabicLayout *visual = visual_text_layout(line, width, 0);
    if ((visual != NULL) != visual_used) {
        irc_arabic_layout_free(visual);
        return;
    }
    if (visual) {
        for (size_t i = 0; i < visual->glyph_count; ++i) {
            const IrcArabicGlyph *glyph = &visual->glyphs[i];
            if (glyph->cells > 0 && glyph->byte_first < end &&
                glyph->byte_past > u->manual_match_offset &&
                glyph->row >= skip && glyph->row - skip < height)
                (void)mvchgat(y + glyph->row - skip, glyph->col, glyph->cells,
                              attr, (short)PAIR_NUMBER(attr), NULL);
        }
        irc_arabic_layout_free(visual);
        attrset(A_NORMAL);
        return;
    }
    for (size_t offset = 0; line[offset] && offset < end;) {
        wchar_t wc;
        size_t n = display_character(line + offset, &wc, &state);
        if (!n) break;
        int cells = wcwidth(wc);
        if (cells < 0 || cells > width) cells = 1;
        if (col + cells > width || (col == width && cells > 0)) { ++row; col = 0; }
        if (offset >= u->manual_match_offset && row >= skip && row - skip < height) {
            int x = cells ? col : col > 0 ? col - 1 : 0;
            (void)mvchgat(y + row - skip, x, cells ? cells : 1,
                          attr, (short)PAIR_NUMBER(attr), NULL);
        }
        col += cells;
        offset += n;
    }
    attrset(A_NORMAL);
}

static void draw_manual_body(App *a, Ui *u, int height, int width)
{
    Buffer *b = &a->buffers[2];
    size_t rows = 0;
    for (size_t i = 0; i < b->line_count; ++i)
        rows += (size_t)history_rows(b->lines[(b->line_start + i) % b->line_cap], width, -1, 0, 0);
    size_t maximum = rows > (size_t)height ? rows - (size_t)height : 0;
    if (b->scroll > maximum) b->scroll = maximum;
    size_t skipped = maximum - b->scroll;
    int row = 5;
    for (size_t i = 0; i < b->line_count && row < 5 + height; ++i) {
        const char *line = b->lines[(b->line_start + i) % b->line_cap];
        int line_rows = history_rows(line, width, -1, 0, 0);
        if (skipped >= (size_t)line_rows) { skipped -= (size_t)line_rows; continue; }
        int visible = line_rows - (int)skipped;
        if (visible > 5 + height - row) visible = 5 + height - row;
        int visual_used;
        (void)history_rows_mode(line, width, row, (int)skipped, visible, &visual_used);
        if (u->manual_found && i == u->manual_match_line)
            highlight_manual_match(line, u, width, row, (int)skipped, visible, visual_used);
        row += visible;
        skipped = 0;
    }
}

static void draw_manual_search(Ui *u)
{
    Editor *e = u->manual_search;
    int width = COLS - 8, row = LINES - 2, col = 0;
    u->manual_search_visual = 0;
    if (!e || width < 1) return;
    attr_t focus = focus_attribute();
    text_at(row, 0, 7, "Find: [", focus);
    text_at(row, COLS - 1, 1, "]", focus);
    IrcCursesEditorPosition visual;
    if (irc_curses_editor_visual(e, width, row, 7, 1, &u->manual_search_scroll,
            e->selection_all ? A_REVERSE : A_NORMAL, &visual)) {
        u->manual_search_visual = 1;
        (void)mvchgat(row, 7 + visual.col, visual.span,
                      focus | A_REVERSE, (short)PAIR_NUMBER(focus), NULL);
        attrset(A_NORMAL);
        (void)move(row, 7 + visual.col);
        (void)curs_set(1);
        return;
    }
    if (u->manual_search_first > e->pos) u->manual_search_first = e->pos;
    for (size_t i = u->manual_search_first; i < e->pos; i = ed_next(e, i))
        col += ed_cluster_width(e, i, width);
    int span = ed_cluster_width(e, e->pos, width);
    if (span < 1) span = 1;
    while (col + span > width && u->manual_search_first < e->pos) {
        col -= ed_cluster_width(e, u->manual_search_first, width);
        u->manual_search_first = ed_next(e, u->manual_search_first);
    }
    int x = 0;
    for (size_t i = u->manual_search_first; i < e->len;) {
        int cells = ed_cluster_width(e, i, width);
        if (x + cells > width) break;
        size_t end = ed_next(e, i);
        move(row, 7 + x);
        int actual = wcwidth(e->data[i]);
        if (actual < 0 || actual > width) (void)addch('?');
        else {
            if (!actual) (void)addch('?');
            (void)addnwstr(e->data + i, (int)(end - i));
        }
        x += cells;
        i = end;
    }
    (void)mvchgat(row, 7 + col, span, focus | A_REVERSE, (short)PAIR_NUMBER(focus), NULL);
    attrset(A_NORMAL);
    move(row, 7 + col);
    (void)curs_set(1);
}

static void draw_manual(App *a, Ui *u)
{
    char line[512], safe[1024], label[256];
    Buffer *b = &a->buffers[2];
    text_at(2, 0, COLS, irc_tr("Help / Manual"), A_BOLD);
    if (LINES < 9 || COLS < 20) {
        text_at(LINES > 5 ? LINES - 3 : 1, 0, COLS, irc_tr("Resize; Esc returns."), A_NORMAL);
        (void)curs_set(u->manual_searching ? 1 : 0);
        move(LINES > 3 ? 3 : 0, 0);
    } else {
        if (u->manual_found && (u->manual_source_start != b->line_start ||
                               u->manual_source_count != b->line_count))
            u->manual_found = 0;
        if (u->manual_found && (u->manual_draw_width != COLS - 1 ||
                               u->manual_draw_height != LINES - 7))
            reveal_manual_match(a, u);
        u->manual_draw_width = COLS - 1;
        u->manual_draw_height = LINES - 7;
        (void)manual_bar(a, u, 1, -1);
        size_t view = u->manual_view < a->buffer_count ? u->manual_view : 0;
        if (u->manual_from_app) snprintf(label, sizeof label, "vipirc");
        else view_name(a, view, label, sizeof label);
        snprintf(line, sizeof line, "Return to: %s", label);
        irc_sanitize(safe, sizeof safe, line);
        text_at(4, 0, COLS, safe, A_DIM);
        draw_manual_body(a, u, LINES - 7, COLS - 1);
        if (!u->manual_searching)
            hint_at(a, LINES - 2, 0, COLS,
                    "Up/Down: scroll | Left/Right: action | /: search | n/N: next/previous | Esc: back",
                    "/: search | Esc: back");
        text_at(LINES - 1, 0, COLS, app_status_text(a), A_NORMAL);
        if (u->manual_searching) draw_manual_search(u);
        else { (void)curs_set(0); move(0, 0); }
    }
    wnoutrefresh(stdscr);
    a->dirty = 0;
}

static void draw_app_menu(App *a, Ui *u)
{
    static const char *const actions[] = {
        "Settings", "Key bindings", "Manual", "Command", "Background", "About", "Insha-Allah license agreement"
    };
    char labels[APP_ENTRIES][512], safe[1024];
    erase();
    text_at(2, 0, COLS, "vipirc | ThatIAm.vip", A_BOLD);
    for (size_t i = 0; i < APP_ENTRIES; ++i) u->app_hits[i] = (StatusHit){-1, -1, 0};
    if (LINES < 9 || COLS < 20) {
        text_at(2, 0, COLS, irc_tr("Resize; Esc returns."), A_NORMAL);
        (void)curs_set(0);
        return;
    }
    Config effective;
    if (!a->net && !a->sim) app_config_effective(a, &effective);
    else effective = a->cfg;
    if (*effective.server)
        snprintf(labels[APP_SERVER], sizeof labels[0], "Server %zu: %.255s:%d", u->slot, effective.server, effective.port);
    else snprintf(labels[APP_SERVER], sizeof labels[0], "Server %zu: %s", u->slot, irc_tr("(not configured)"));
    const char *nick = !a->net && !a->sim ? effective.nick : a->nick;
    snprintf(labels[APP_NICK], sizeof labels[0], "Nick: %.63s", *nick ? nick : irc_tr("(not configured)"));
    config_wipe(&effective, sizeof effective);
    for (int i = APP_SETTINGS; i < APP_ENTRIES; ++i)
        snprintf(labels[i], sizeof labels[0], "%s", irc_tr(actions[i - APP_SETTINGS]));
    /* One physical-row viewport includes the wrapped warning and both blank
     * separators, so the same menu remains reachable on small terminals. */
    int warning_rows = history_rows(bloodguilt_warning, COLS - 1, -1, 0, 0);
    int height = LINES - 5;
    int selected_row = warning_rows + u->app_selection +
                       (u->app_selection >= APP_ABOUT ? 3 : u->app_selection >= APP_SETTINGS ? 2 : 0);
    int total = warning_rows + APP_ENTRIES + 3;
    if (selected_row < u->app_first) u->app_first = selected_row;
    if (selected_row >= u->app_first + height) u->app_first = selected_row - height + 1;
    int last_first = total > height ? total - height : 0;
    if (u->app_first > last_first) u->app_first = last_first;
    if (u->app_first < 0) u->app_first = 0;
    if (u->app_first < warning_rows)
        (void)history_rows(bloodguilt_warning, COLS - 1, 3, u->app_first, height);
    int connection_row = warning_rows + 2 - u->app_first + 3;
    if (connection_row >= 3 && connection_row < LINES - 2) {
        snprintf(safe, sizeof safe, "Connection: %s", app_connection_label(a));
        text_at(connection_row, 1, COLS - 1, safe, A_NORMAL);
    }
    for (int i = 0; i < APP_ENTRIES; ++i) {
        int row = warning_rows + i + (i >= APP_ABOUT ? 3 : i >= APP_SETTINGS ? 2 : 0) - u->app_first + 3;
        if (row < 3 || row >= LINES - 2) continue;
        irc_sanitize(safe, sizeof safe, labels[i]);
        int width = text_cells(safe, COLS - 3) + 2;
        attr_t attr = i == u->app_selection ? focus_attribute() : A_UNDERLINE;
        text_at(row, 0, 1, "[", attr);
        text_at(row, 1, width - 2, safe, attr);
        text_at(row, width - 1, 1, "]", attr);
        u->app_hits[i] = (StatusHit){0, row, width};
    }
    focus_button(a, LINES - 2, 0, 10, "Back (b)", 0);
    hint_at(a, LINES - 2, 12, COLS - 12,
            "Up/Down: item | Enter: open | /: command | Esc: quit", "Esc: quit");
    text_at(LINES - 1, 0, COLS, app_status_text(a), A_NORMAL);
    (void)curs_set(0);
    move(2, 0);
}

static void capture_about(Ui *u)
{
    AboutSnapshot *snapshot = &u->about_snapshot;
    struct rusage usage;
    memset(snapshot, 0, sizeof *snapshot);
    snapshot->pid = getpid();
    int64_t now = net_now_ms();
    if (u->owner->started_at > 0 && now > u->owner->started_at)
        snapshot->uptime_ms = now - u->owner->started_at;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        snapshot->cpu_available = 1;
        snapshot->user_seconds = (double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec / 1000000.0;
        snapshot->system_seconds = (double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec / 1000000.0;
        /* POSIX guarantees the CPU fields, not RSS or its units. Darwin can
         * also hide its non-POSIX fields in a strict feature-test namespace. */
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
        if (usage.ru_maxrss >= 0) {
            snapshot->peak_mib = (double)usage.ru_maxrss / 1024.0;
            snapshot->memory_available = 1;
        }
#elif defined(__APPLE__) && defined(__DARWIN_C_LEVEL) && defined(__DARWIN_C_FULL) && __DARWIN_C_LEVEL >= __DARWIN_C_FULL
        if (usage.ru_maxrss >= 0) {
            snapshot->peak_mib = (double)usage.ru_maxrss / (1024.0 * 1024.0);
            snapshot->memory_available = 1;
        }
#endif
    }
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        const ClientSlot *slot = &u->owner->client->slots[i];
        if (!slot->used) continue;
        ++snapshot->servers;
        const App *session = slot->app;
        if (!session) continue;
        if (session->registered) ++snapshot->registered;
        for (size_t j = 0; j < session->buffer_count; ++j) {
            if (session->buffers[j].kind == B_CHANNEL) ++snapshot->channels;
            else if (session->buffers[j].kind == B_QUERY) ++snapshot->queries;
        }
    }
}

#define ABOUT_LINES 14
static void about_lines(const Ui *u, char lines[ABOUT_LINES][192])
{
    const AboutSnapshot *snapshot = &u->about_snapshot;
    memset(lines, 0, ABOUT_LINES * sizeof *lines);
    snprintf(lines[0], sizeof lines[0], "vipirc %s", VIPIRC_VERSION);
    snprintf(lines[1], sizeof lines[0], "%s", irc_tr("An application of ThatIAm.vip"));
    snprintf(lines[2], sizeof lines[0], "%s", irc_tr("Website: www.thatiam.vip"));
    snprintf(lines[3], sizeof lines[0], "%s", irc_tr("Designed by remfan1994"));
    snprintf(lines[4], sizeof lines[0], "%s", irc_tr("License: Insha-Allah license agreement (IALA)"));
    snprintf(lines[6], sizeof lines[0], "%s", irc_tr("Process snapshot"));
    snprintf(lines[7], sizeof lines[0], "PID: %ld", (long)snapshot->pid);
    int64_t seconds = snapshot->uptime_ms / 1000;
    snprintf(lines[8], sizeof lines[0], "Session uptime: %lldh %02lldm %02llds",
             (long long)(seconds / 3600), (long long)((seconds / 60) % 60), (long long)(seconds % 60));
    if (snapshot->cpu_available)
        snprintf(lines[9], sizeof lines[0], "CPU time: %.3f s user, %.3f s system",
                 snapshot->user_seconds, snapshot->system_seconds);
    else snprintf(lines[9], sizeof lines[0], "%s", irc_tr("CPU time: unavailable"));
    if (snapshot->memory_available)
        snprintf(lines[10], sizeof lines[0], "Peak resident memory: %.2f MiB", snapshot->peak_mib);
    else snprintf(lines[10], sizeof lines[0], "%s", irc_tr("Peak resident memory: unavailable on this platform"));
    snprintf(lines[11], sizeof lines[0], "Server entries: %zu; registered connections: %zu", snapshot->servers, snapshot->registered);
    snprintf(lines[12], sizeof lines[0], "Channel views: %zu; query views: %zu", snapshot->channels, snapshot->queries);
    snprintf(lines[13], sizeof lines[0], "%s", irc_tr("CPU and memory cover this process; helper processes are excluded."));
}

static void open_about(App *a, Ui *u)
{
    u->about = 1;
    u->about_action = 0;
    u->about_scroll = 0;
    u->input_mode = 0;
    capture_about(u);
    app_status(a, "");
}

static int about_bar(const App *a, const Ui *u, int draw_it, int clicked_x)
{
    static const char *const labels[] = {"Back", "Refresh"};
    int x = 0;
    for (int i = 0; i < 2; ++i) {
        int width = text_cells(irc_tr(labels[i]), COLS) + 2;
        if (draw_it) focus_button(a, 3, x, width, irc_tr(labels[i]), i == u->about_action);
        else if (clicked_x >= x && clicked_x < x + width) return i;
        x += width + 1;
    }
    return -1;
}

static void scroll_about(Ui *u, size_t amount, int direction)
{
    char lines[ABOUT_LINES][192];
    about_lines(u, lines);
    size_t total = 0;
    int width = COLS > 1 ? COLS - 1 : 1;
    for (size_t i = 0; i < ABOUT_LINES; ++i)
        total += (size_t)history_rows(lines[i], width, -1, 0, 0);
    size_t height = LINES > 6 ? (size_t)(LINES - 6) : 1u;
    size_t maximum = total > height ? total - height : 0;
    if (u->about_scroll > maximum) u->about_scroll = maximum;
    if (direction < 0) u->about_scroll = amount < u->about_scroll ? u->about_scroll - amount : 0;
    else u->about_scroll += amount < maximum - u->about_scroll ? amount : maximum - u->about_scroll;
}

static void draw_about(App *a, Ui *u)
{
    char lines[ABOUT_LINES][192];
    erase();
    text_at(2, 0, COLS, irc_tr("About vipirc"), A_BOLD);
    if (LINES < 9 || COLS < 20) {
        text_at(3, 0, COLS, irc_tr("Resize; Esc returns."), A_NORMAL);
        (void)curs_set(0);
        return;
    }
    (void)about_bar(a, u, 1, -1);
    about_lines(u, lines);
    scroll_about(u, 0, 1);
    size_t skipped = u->about_scroll;
    int row = 4;
    for (size_t i = 0; i < ABOUT_LINES && row < LINES - 2; ++i) {
        int rows = history_rows(lines[i], COLS - 1, -1, 0, 0);
        if (skipped >= (size_t)rows) { skipped -= (size_t)rows; continue; }
        int visible = rows - (int)skipped;
        if (visible > LINES - 2 - row) visible = LINES - 2 - row;
        (void)history_rows(lines[i], COLS - 1, row, (int)skipped, visible);
        row += visible;
        skipped = 0;
    }
    hint_at(a, LINES - 2, 0, COLS, "Up/Down: scroll | Left/Right: action | r: refresh | Esc: vipirc", "r: refresh | Esc: back");
    text_at(LINES - 1, 0, COLS, app_status_text(a), A_NORMAL);
    (void)curs_set(0);
    move(2, 0);
}

static size_t license_rows(int width)
{
    size_t rows = 0;
    for (size_t i = 0; i < VIPIRC_LICENSE_LINES; ++i)
        rows += (size_t)history_rows(vipirc_license[i], width, -1, 0, 0);
    return rows;
}

static void scroll_license(Ui *u, size_t amount, int direction)
{
    int width = COLS > 1 ? COLS - 1 : 1;
    size_t height = LINES > 5 ? (size_t)(LINES - 5) : 1u;
    size_t rows = license_rows(width), maximum = rows > height ? rows - height : 0;
    if (u->license_scroll > maximum) u->license_scroll = maximum;
    if (direction < 0) u->license_scroll = amount < u->license_scroll ? u->license_scroll - amount : 0;
    else u->license_scroll += amount < maximum - u->license_scroll ? amount : maximum - u->license_scroll;
}

static void draw_license(App *a, Ui *u)
{
    char detail[80];
    erase();
    text_at(2, 0, COLS, "Insha-Allah license agreement", A_BOLD);
    if (LINES < 9 || COLS < 20) {
        text_at(3, 0, COLS, "Resize; Esc: vipirc.", A_NORMAL);
    } else {
        scroll_license(u, 0, 1);
        size_t skipped = u->license_scroll;
        int row = 3, width = COLS - 1;
        for (size_t i = 0; i < VIPIRC_LICENSE_LINES && row < LINES - 2; ++i) {
            int rows = history_rows(vipirc_license[i], width, -1, 0, 0);
            if (skipped >= (size_t)rows) { skipped -= (size_t)rows; continue; }
            int visible = rows - (int)skipped;
            if (visible > LINES - 2 - row) visible = LINES - 2 - row;
            (void)history_rows(vipirc_license[i], width, row, (int)skipped, visible);
            row += visible;
            skipped = 0;
        }
        focus_button(a, LINES - 2, 0, 6, "Back", 1);
        hint_at(a, LINES - 2, 8, COLS - 8,
                "Up/Down: scroll | Esc: vipirc", "Esc: vipirc");
        snprintf(detail, sizeof detail, "Line %zu/%zu", u->license_scroll + 1, license_rows(width));
        text_at(LINES - 1, 0, COLS, detail, A_NORMAL);
    }
    (void)curs_set(0);
    move(2, 0);
}

static int nick_ignored(const App *a, const Ui *u)
{
    for (size_t i = 0; i < a->cfg.ignore_count; ++i)
        if (irc_equal(a->cfg.ignores[i], u->selected_nick, a->casemap)) return 1;
    return 0;
}

static void draw_nick_menu(App *a, Ui *u)
{
    const char *entries[] = {"Query", "Whois", nick_ignored(a, u) ? "Unignore" : "Ignore", "Back"};
    char title[96];
    erase();
    snprintf(title, sizeof title, "Nickname: %s", u->selected_nick);
    text_at(2, 0, COLS, title, A_BOLD);
    u->nick_menu_top = 4;
    for (int i = 0; i < 4 && i + u->nick_menu_top < LINES - 1; ++i)
        focus_text(a, i + u->nick_menu_top, 0, COLS, entries[i], u->nick_action == i);
    text_at(LINES - 1, 0, COLS, app_status_text(a), A_NORMAL);
    (void)curs_set(0);
    move(2, 0);
}

static void nick_menu_action(App *a, Ui *u)
{
    char command[96];
    const char *verb = u->nick_action == 0 ? "pm" : u->nick_action == 1 ? "whois" :
                       nick_ignored(a, u) ? "unignore" : "ignore";
    int action = u->nick_action;
    if (!irc_valid_nick(u->selected_nick)) { return_to_chat(a, u); return; }
    if (action == 1 && !a->registered) {
        app_status(a, "%s", irc_tr("Connect this server before requesting Whois."));
        return;
    }
    snprintf(command, sizeof command, "/%s %s", verb, u->selected_nick);
    return_to_chat(a, u);
    if (action < 3) (void)app_command(a, command, &u->pending);
    config_wipe(&u->pending, sizeof u->pending);
    a->dirty = 1;
}

static void click_text_target(App *a, Ui *u, const TextHit *hit)
{
    char target[IRC_TARGET_MAX + 1];
    /* A later draw can replace the snapshot; keep the clicked identity local. */
    snprintf(target, sizeof target, "%s", hit->target);
    if (hit->channel) {
        if (!irc_valid_channel(target)) return;
        size_t view = app_buffer(a, target, B_CHANNEL, 0);
        if (view != SIZE_MAX && a->buffers[view].joined) app_switch(a, view);
        else {
            char command[IRC_TARGET_MAX + 8];
            snprintf(command, sizeof command, "/join %s", target);
            (void)app_command(a, command, &u->pending);
            config_wipe(&u->pending, sizeof u->pending);
        }
        u->header_origin = SIZE_MAX;
        u->input_mode = 1;
    } else if (irc_valid_nick(target)) {
        snprintf(u->selected_nick, sizeof u->selected_nick, "%.*s", (int)IRC_NICK_MAX, target);
        u->nick_menu = 1; u->nick_action = 0; u->input_mode = 0;
    }
    nick_completion_reset(&u->completion);
    invalidate_mouse(u);
    a->dirty = 1;
}

static IrcCursesEditorPosition draw_editor(Editor *e, Ui *u, size_t view,
                                          int top, int available)
{
    IrcCursesEditorPosition visual;
    if (irc_curses_editor_visual(e, u->input_width, top, 1, available,
            &u->input_scroll[view], e->selection_all ? A_REVERSE : A_NORMAL,
            &visual)) {
        int py = top + visual.row - visual.first;
        if (u->input_mode && py >= top && py < top + available &&
            visual.col >= 0 && visual.col < u->input_width) {
            attr_t attr = e->selection_all ? A_REVERSE | A_BOLD | A_UNDERLINE : A_REVERSE;
            (void)mvchgat(py, visual.col + 1, visual.span, attr, 0, NULL);
            (void)move(py, visual.col + 1);
        }
        return visual;
    }
    int total, row, col;
    ed_layout(e, u->input_width, &total, &row, &col);
    int first = u->input_scroll[view];
    int last_first = total > available ? total - available : 0;
    if (first > last_first) first = last_first;
    if (row < first) first = row;
    if (row >= first + available) first = row - available + 1;
    u->input_scroll[view] = first;
    int r = 0, c = 0, wrapped = 0;
    attrset(e->selection_all ? A_REVERSE : A_NORMAL);
    for (size_t i = 0; i < e->len;) {
        int advance = ed_cluster_width(e, i, u->input_width);
        if (advance && c + advance > u->input_width) { ++r; c = 0; }
        size_t end = ed_next(e, i);
        if (r >= first && r < first + available && e->data[i] != L'\n') {
            int w = wcwidth(e->data[i]);
            move(top + r - first, c + 1);
            if (w < 0 || w > u->input_width) {
                addch('?');
            } else {
                if (!w) addch('?');
                (void)addnwstr(e->data + i, (int)(end - i));
            }
        }
        if (e->data[i] == L'\n') {
            if (!wrapped) ++r;
            c = 0; wrapped = 0;
        } else {
            c += advance;
            wrapped = c >= u->input_width;
            if (wrapped) { ++r; c = 0; }
        }
        if (r >= first + available) break;
        i = end;
    }
    attrset(A_NORMAL);
    /* A reverse-video cell remains visible even when a terminal ignores
     * curs_set(). Hardware caret and explicit position indicator agree. */
    int py = top + row - first;
    if (u->input_mode && py >= top && py < top + available && col >= 0 && col < u->input_width) {
        /* Attributes must cover both cells of a wide glyph. Changing only
         * its leading cell can make curses repaint following text one cell
         * too early when the caret or selection moves away. */
        int span = ed_cluster_width(e, e->pos, u->input_width);
        if (span < 1) span = 1;
        (void)mvchgat(py, col + 1, span, e->selection_all ? A_REVERSE | A_BOLD | A_UNDERLINE : A_REVERSE, 0, NULL);
        move(py, col + 1);
    }
    return (IrcCursesEditorPosition){total, row, col, first, 1};
}

/* Divider values are local ASCII. Keep each detail whole: a clipped column
 * or budget would be a different number, not merely a shorter label. */
static int input_detail(char *line, size_t capacity, int width, const char *detail)
{
    size_t used = strlen(line), more = strlen(detail);
    if (width < 0 || used >= capacity || more >= capacity - used || used + more > (size_t)width)
        return 0;
    memcpy(line + used, detail, more + 1);
    return 1;
}

static void draw_input_border(const Ui *u, const char *budget)
{
    int top = u->input_top - 1, bottom = u->input_top + u->input_rows;
    attr_t attr = !u->status_selected ? focus_attribute() : A_NORMAL;
    (void)attr_set(attr, (short)PAIR_NUMBER(attr), NULL);
    (void)mvhline(top, 1, ACS_HLINE, u->input_width);
    (void)mvhline(bottom, 1, ACS_HLINE, u->input_width);
    (void)mvvline(u->input_top, 0, ACS_VLINE, u->input_rows);
    (void)mvvline(u->input_top, COLS - 1, ACS_VLINE, u->input_rows);
    (void)mvaddch(top, 0, ACS_ULCORNER);
    (void)mvaddch(top, COLS - 1, ACS_URCORNER);
    (void)mvaddch(bottom, 0, ACS_LLCORNER);
    (void)mvaddch(bottom, COLS - 1, ACS_LRCORNER);
    attrset(A_NORMAL);
    text_at(top, 1, u->input_width, budget, attr);
}

static void free_readview(ReadView *view)
{
    readpage_free(&view->pages[0]);
    readpage_free(&view->pages[1]);
    memset(view, 0, sizeof *view);
}

static int open_inquiry(App *a, Ui *u, const char *command)
{
    if (strcmp(command, "/autojoin") && strcmp(command, "/autoauth status")) return 0;
    ReadPage fresh = {0};
    IdleCommands shared = {u->owner->client, "vipirc", readpage_add, &fresh, 1, 0, u->owner->dcc, irc_tr};
    int result = idle_command(&shared, command, NULL);
    if (result != IDLE_COMMAND_DONE || fresh.failed) {
        readpage_free(&fresh);
        app_status(a, "%s", irc_tr("Unable to refresh the inquiry; the previous snapshot is preserved."));
        return 0;
    }
    if (prompt_active(u->command)) restore_command_origin(u);
    int same = u->summary.active && !strcmp(u->summary.inquiry, command);
    readpage_free(&u->summary.pages[0]);
    u->summary.pages[0] = fresh;
    if (!same) u->summary.scroll[0] = 0;
    u->summary.active = 1;
    u->summary.hidden = u->summary.paste = 0;
    (void)snprintf(u->summary.inquiry, sizeof u->summary.inquiry, "%s", command);
    (void)snprintf(u->summary.title, sizeof u->summary.title, "%s",
                   !strcmp(command, "/autojoin") ? irc_tr("Auto-join settings") : irc_tr("Authentication settings"));
    invalidate_mouse(u);
    app_status(a, "%s", irc_tr("Local inquiry ready. R refreshes; Esc returns to the previous screen."));
    return 1;
}

static int open_summary(App *a, Ui *u)
{
    ReadPage fresh = {0};
    summary_print(u->owner->client, u->owner->dcc, 0, readpage_add, &fresh);
    if (fresh.failed) {
        readpage_free(&fresh);
        app_status(a, "%s", irc_tr("Unable to refresh the summary; the previous snapshot is preserved."));
        return 0;
    }
    if (prompt_active(u->command)) restore_command_origin(u);
    readpage_free(&u->summary.pages[0]);
    u->summary.pages[0] = fresh;
    if (!u->summary.active || *u->summary.inquiry) u->summary.scroll[0] = 0;
    u->summary.active = 1;
    u->summary.hidden = u->summary.paste = 0;
    u->summary.inquiry[0] = 0;
    snprintf(u->summary.title, sizeof u->summary.title, "%s", irc_tr("Session summary"));
    invalidate_mouse(u);
    app_status(a, "%s", irc_tr("Snapshot ready. R refreshes; Esc returns to the previous screen."));
    return 1;
}

static void refresh_reader(App *a, Ui *u)
{
    char inquiry[sizeof u->summary.inquiry];
    (void)snprintf(inquiry, sizeof inquiry, "%s", u->summary.inquiry);
    if (*inquiry) (void)open_inquiry(a, u, inquiry);
    else (void)open_summary(a, u);
}

static int open_context_help(App *a, Ui *u)
{
    char screen[160] = "", item[512] = "", detail[3072] = "", intro[768];
    const char *screen_help = irc_tr("Use Up/Down to select an item and Enter to activate it. Escape returns to the previous screen. Press ? on a selected control to read its help. Question marks remain text inside editors.");
    char translated_screen_help[4096] = "";
    const CommandSpec *command = NULL;
    if (u->context_help.active) return 1;
    if (u->paste || u->confirming || u->quit_confirm || u->owner->close_confirm ||
        settings_pasting(u->settings) || guided_pasting(u->owner->guided) ||
        transfers_pasting(u->owner->transfers) || prompt_pasting(u->command)) return 0;
    if (prompt_active(u->command)) {
        command = prompt_help_command(u->command, a);
        if (!command) return 0;
        snprintf(screen, sizeof screen, "%s", irc_tr("command screen"));
        snprintf(item, sizeof item, "/%s", command->name);
        snprintf(detail, sizeof detail, "%s\n\n%s\n\n%s", command->usage, command->summary,
            irc_tr("Enter on this suggestion copies it into the command field. It must appear there for review before another Enter can execute it. Escape returns to the same suggestion."));
        (void)snprintf(translated_screen_help, sizeof translated_screen_help, "%s\n\n%s\n\n%s",
            irc_tr("The command screen has its own input, preserving the conversation draft. Down focuses suggestions; Up/Down selects; Enter copies the selection for editing. Up from the first suggestion returns to input. Right at the end completes a command name."),
            irc_tr("Settings offers Common, Recent history, Most used with this config, Alphabetical, and Contextual. Contextual uses the screen that opened the command prompt. Other matching commands remain available."),
            irc_tr("Delete removes a selected Recent history entry or resets a selected personal usage count in Most used. Command arguments and credentials are never saved in personal usage counts. ? explains a highlighted suggestion; in the input field it types a question mark."));
        screen_help = translated_screen_help;
    } else if (u->summary.active && !u->summary.hidden) {
        snprintf(screen, sizeof screen, "%s", *u->summary.inquiry ?
                 !strcmp(u->summary.inquiry, "/autojoin") ? irc_tr("auto-join inquiry screen") : irc_tr("authentication inquiry screen") :
                 irc_tr("session summary screen"));
        snprintf(item, sizeof item, "%s", irc_tr(u->summary.action ? "Back" : "Refresh"));
        snprintf(detail, sizeof detail, "%s", u->summary.action ? irc_tr("Return to the screen that opened this snapshot, keeping its selection and draft.") : irc_tr("Replace this snapshot with current local session state. Refresh does not request data from the network or mark conversations read. If allocation fails, the existing snapshot and scroll position are kept."));
        (void)snprintf(translated_screen_help, sizeof translated_screen_help, "%s\n\n%s",
            *u->summary.inquiry ?
                irc_tr("This local inquiry identifies the selected server, connection and authentication state, and current versus pending settings. Auto-join reports show actual channel membership. Explicit authentication reports show complete configured file locations, never password contents, and do not open credential files.") :
                irc_tr("The summary groups current servers, channel 0, joined and left channel views, queries, unread counts, mentions, pending work, and file transfers by server. Mentions are not added to unread totals."),
            irc_tr("R refreshes; Up/Down, Page Up/Page Down, Home/End scroll. Left/Right selects Refresh or Back. / opens a command prompt; cancelling it restores this snapshot. Escape returns to the originating screen."));
        screen_help = translated_screen_help;
    } else if (guided_active(u->owner->guided)) {
        if (!guided_context_help(u->owner->guided, screen, sizeof screen, item, sizeof item, detail, sizeof detail)) return 0;
        screen_help = irc_tr("This configuration panel collects answers in a separate draft. Tab selects the answer field or Next, Back and Cancel controls. Enter validates the answer or activates a selected control. The final review applies only the choices you approve. Closing help preserves the answer and draft.");
    } else if (settings_is_open(u)) {
        if (!settings_context_help(u->settings, screen, sizeof screen, item, sizeof item, detail, sizeof detail)) return 0;
        (void)snprintf(translated_screen_help, sizeof translated_screen_help,
            "%s\n\n%s", irc_tr("Settings contains application preferences and individual server profiles. Up/Down selects a field; Left/Right selects an action; Enter edits or activates it. Apply changes runtime preferences; Save also writes the configuration. Connection changes take effect on a subsequent connection."), irc_tr("Choice popups keep every option reachable on small terminals. Escape closes the popup or returns through the settings screens. Help preserves unsaved settings and the current selection. In a text or password editor, ? remains ordinary input."));
        screen_help = translated_screen_help;
    } else if (transfers_active(u->owner->transfers)) {
        if (!transfers_context_help(u->owner->transfers, screen, sizeof screen, item, sizeof item, detail, sizeof detail)) return 0;
        (void)snprintf(translated_screen_help, sizeof translated_screen_help,
            "%s\n\n%s", irc_tr("The transfer screen lists incoming offers and retained transfer progress. Up/Down selects a transfer or field; Left/Right selects the action; Enter activates it. Accept and Send open forms for review. The file browser can fill a source path before an offer is sent. Escape returns without accepting an offer."), irc_tr("DCC transfers use a direct, unencrypted connection and expose the selected network address to the peer. A received offer alone does not open files or a connection."));
        screen_help = translated_screen_help;
    } else if (u->app_menu) {
        static const char *const names[APP_ENTRIES] = {"Server", "Nickname", "Settings", "Key bindings", "Manual", "Command", "Background", "About", "Insha-Allah license agreement"};
        static const char *const help[APP_ENTRIES] = {
            "Open the selected server's connection settings. Its hostname, port, transport and startup choices belong to that server profile.",
            "Open nickname settings for the selected server. /nick changes the nickname on a live connection.",
            "Open application preferences and saved server settings. Application preferences include command suggestion order. Select an option to edit it, then Apply or Save as appropriate.",
            "Edit the application-wide custom key bindings. No custom bindings are installed by default. Add a key and choose an action, then Apply or Save. Escape discards changes made since opening or applying the table.",
            "Open the complete built-in manual. It works while disconnected. Search its contents with /, then use n/N for the next or previous match.",
            "Open the separate command input. Suggestions help discover commands; selecting one inserts it for review before execution. Your conversation draft is preserved.",
            "Keep connections running while releasing the current terminal. Resume the session with vipirc --resume. Pending setup or credential operations can require waiting before detaching.",
            "Show the program version, connection totals and resource snapshot. Refresh updates that snapshot.",
            "Read the included Insha-Allah license agreement. Escape returns to the vipirc main screen."
        };
        int selected = u->app_selection >= 0 && u->app_selection < APP_ENTRIES ? u->app_selection : APP_SETTINGS;
        snprintf(screen, sizeof screen, "%s", irc_tr("vipirc main screen"));
        snprintf(item, sizeof item, "%s", irc_tr(names[selected]));
        snprintf(detail, sizeof detail, "%s", irc_tr(help[selected]));
        (void)snprintf(translated_screen_help, sizeof translated_screen_help, "%s\n\n%s",
            irc_tr("The vipirc main screen provides the current server and nickname, Settings, Key bindings, Manual, Command, Background, About and the license. Up/Down selects an item; Enter opens it. / opens the command prompt with client context. B returns to the conversation. Escape opens the quit question."),
            irc_tr("The top categories lead to servers, channels, queries, activity and transfers when those categories are available. Each screen retains its own selection."));
        screen_help = translated_screen_help;
    } else if (u->owner->servers) {
        size_t map[CLIENT_MAX_SERVERS], count = server_map(u->owner->client, map);
        snprintf(screen, sizeof screen, "%s", irc_tr("Servers screen"));
        if (count) {
            size_t row = u->owner->server_selection < count ? u->owner->server_selection : count - 1;
            const ClientSlot *slot = &u->owner->client->slots[map[row]];
            const Config *cfg = slot->app ? &slot->app->cfg : &slot->seed;
            {
                char number[32], host[256];
                (void)snprintf(number, sizeof number, "%zu", map[row]);
                (void)snprintf(host, sizeof host, "%.255s", *cfg->server ? cfg->server : irc_tr("not configured"));
                const IrcDisplayField fields[] = {{"number", number}, {"host", host}};
                if (!irc_display_render(item, sizeof item, "Server {number}: {host}", fields, 2))
                    (void)snprintf(item, sizeof item, "Server %zu: %.255s", map[row],
                        *cfg->server ? cfg->server : irc_tr("not configured"));
            }
            {
                const char *state = slot->load_failed ? irc_tr("unavailable") :
                    slot->app ? app_connection_label(slot->app) : irc_tr("disconnected");
                const IrcDisplayField fields[] = {{"state", state}};
                if (!irc_display_render(detail, sizeof detail, "Connection: {state}. Open visits channel 0 for this server. Connect starts its connection; Disconnect ends it while retaining its views. Close removes a secondary session and its drafts after confirmation. Away changes this server's away state.\n\nSelecting this entry or reading help does not switch servers or connect.", fields, 1))
                    (void)snprintf(detail, sizeof detail,
                        "Connection: %s. Open visits channel 0 for this server. Connect starts its connection; Disconnect ends it while retaining its views. Close removes a secondary session and its drafts after confirmation. Away changes this server's away state.\n\nSelecting this entry or reading help does not switch servers or connect.",
                        state);
            }
        } else snprintf(item, sizeof item, "%s", irc_tr("Server list"));
        screen_help = irc_tr("Servers lists every current server session. Up/Down chooses a server; Left/Right chooses an action; Enter activates that action. Open selects its channel 0, which shows server notices, /list results and connection replies. The server list and /list are different: /list requests channel names from the selected network.");
    } else if (u->channels == 3) {
        app_list_sort(a, u->chooser_action, a->list.descending);
        size_t count = app_list_visible_count(a);
        snprintf(screen, sizeof screen, "%s", irc_tr("Network channel directory"));
        if (count) {
            size_t row = u->channel_selection < count ? u->channel_selection : count - 1;
            const ChannelListEntry *entry = &a->list.entries[row];
            snprintf(item, sizeof item, "%s", entry->channel);
            {
                char users[32];
                (void)snprintf(users, sizeof users, "%llu", (unsigned long long)entry->users);
                const char *topic = *entry->topic ? entry->topic : irc_tr("(no topic)");
                const IrcDisplayField fields[] = {
                    {"channel", entry->channel}, {"users", users}, {"topic", topic}
                };
                if (!irc_display_render(detail, sizeof detail, "{channel} has {users} listed users. Topic: {topic}\n\nEnter joins the selected channel. Left/Right changes the sort column; S reverses its direction. R repeats the current LIST request with the same filters.", fields, 3))
                    (void)snprintf(detail, sizeof detail,
                        "%s has %llu listed users. Topic: %s\n\nEnter joins the selected channel. Left/Right changes the sort column; S reverses its direction. R repeats the current LIST request with the same filters.",
                        entry->channel, (unsigned long long)entry->users, topic);
            }
        } else {
            snprintf(item, sizeof item, "%s", irc_tr("Channel directory"));
            snprintf(detail, sizeof detail, "%s", a->list.active ? irc_tr("The client is receiving LIST replies; no matching channel has arrived yet.") : irc_tr("No cached channel matches the current LIST filters."));
        }
        screen_help = irc_tr("The network channel directory is built from IRC LIST replies. Up/Down selects a channel; Left/Right selects Channel, Users or Topic sorting; S toggles ascending/descending; Enter joins; R refreshes; / opens the command prompt; Escape returns. /list options provide minimum/maximum users, name/topic filtering and a display limit.");
    } else if (u->channels) {
        size_t map[APP_BUFFERS], count = u->channels == 1 ? channel_map(a, map) : pm_map(a, map);
        snprintf(screen, sizeof screen, "%s", u->channels == 1 ? irc_tr("Channels screen") : irc_tr("Queries screen"));
        if (count) {
            size_t row = u->channel_selection < count ? u->channel_selection : count - 1;
            const Buffer *buffer = &a->buffers[map[row]];
            snprintf(item, sizeof item, "%s", map[row] == 0 ? irc_tr("Channel 0 (server)") : buffer->name);
            {
                const char *description = map[row] == 0 ? irc_tr("Channel 0 contains server replies and notices. /join and /list work here without selecting a chat conversation.") :
                buffer->kind == B_QUERY ? irc_tr("This is a private conversation. Open selects it without sending. Close discards its retained view and draft after any required confirmation.") :
                buffer->joined ? irc_tr("You are joined to this channel. Part leaves it but keeps its history and unfinished draft. Close removes the view.") :
                irc_tr("You are not joined to this channel. The retained view keeps its scrollback and unfinished draft. Use /join to join again, or Close to remove it.");
                char unread[32], mentions[32];
                (void)snprintf(unread, sizeof unread, "%zu", (size_t)buffer->unread);
                (void)snprintf(mentions, sizeof mentions, "%zu", (size_t)buffer->unread_mentions);
                const IrcDisplayField fields[] = {
                    {"description", description}, {"unread", unread}, {"mentions", mentions}
                };
                if (!irc_display_render(detail, sizeof detail, "{description}\n\nUnread messages: {unread}; mentions: {mentions}. Selecting Open visits this view. Help preserves the view's unread counts, history and draft.", fields, 3))
                    (void)snprintf(detail, sizeof detail,
                        "%s\n\nUnread messages: %zu; mentions: %zu. Selecting Open visits this view. Help preserves the view's unread counts, history and draft.",
                        description, (size_t)buffer->unread, (size_t)buffer->unread_mentions);
            }
        } else {
            snprintf(item, sizeof item, "%s", irc_tr("New query"));
            snprintf(detail, sizeof detail, "%s", irc_tr("There are no open private conversations. New opens a command input for a nickname; /query NICK opens a local conversation without sending a message."));
        }
        screen_help = irc_tr("These lists show views already open in the selected server session. Up/Down selects a view; Left/Right selects Open, New, Part or Close as appropriate; Enter performs the selected action. Channel 0 is the server. /cl and /channels show local channel views; /list [MASK] [OPTIONS] opens the sortable network channel directory. /ql shows private conversations.");
    } else if (u->owner->unread) {
        ActivityHit hit;
        snprintf(screen, sizeof screen, "%s", irc_tr("Activity screen"));
        if (!activity_entry(u->owner, u->owner->unread_selection, item, sizeof item, &hit))
            snprintf(item, sizeof item, "%s", irc_tr("Activity list"));
        snprintf(detail, sizeof detail, "%s", irc_tr("Open visits this item's source conversation. The server label distinguishes identical names on different networks. A closed query can be reopened locally; a missing channel is not silently joined. Reading help leaves the source unread."));
        screen_help = irc_tr("Activity brings together retained mentions and unread source conversations across all servers. Up/Down selects an entry; Enter opens its source. Clear mentions removes copied mention records while preserving source histories and unread counters. Mentions can overlap unread messages, so their counts are separate.");
    } else if (u->nick_menu) {
        static const char *const choices[] = {"Query", "Whois", "Ignore", "Back"};
        snprintf(screen, sizeof screen, "%s", irc_tr("nickname actions screen"));
        {
            char nick[64];
            (void)snprintf(nick, sizeof nick, "%.63s", u->selected_nick);
            const char *action = irc_tr(choices[u->nick_action < 4 ? u->nick_action : 0]);
            const IrcDisplayField fields[] = {{"action", action}, {"nick", nick}};
            if (!irc_display_render(item, sizeof item, "{action} for {nick}", fields, 2))
                (void)snprintf(item, sizeof item, "%s for %.63s", action, u->selected_nick);
        }
        snprintf(detail, sizeof detail, "%s", irc_tr("Query opens a private conversation without sending. Whois asks the server for public nickname information. Ignore changes local filtering. Back returns to the conversation. Up/Down chooses an action; Enter activates it."));
    } else if (u->manual) {
        if (u->manual_searching) return 0;
        static const char *const actions[] = {"Return", "Find", "Previous match", "Next match"};
        snprintf(screen, sizeof screen, "%s", irc_tr("manual screen"));
        snprintf(item, sizeof item, "%s", irc_tr(actions[u->manual_action >= 0 && u->manual_action < 4 ? u->manual_action : 0]));
        snprintf(detail, sizeof detail, "%s", irc_tr("The selected action belongs to the manual reader. Find opens its search field; Previous and Next move between matches; Return restores the originating screen. Up/Down and Page Up/Page Down scroll the document."));
    } else if (u->about || u->license) {
        snprintf(screen, sizeof screen, "%s", u->about ? irc_tr("About screen") : irc_tr("license screen"));
        snprintf(item, sizeof item, "%s", irc_tr(u->about && u->about_action ? "Refresh" : "Back"));
        snprintf(detail, sizeof detail, "%s", u->about && u->about_action ? irc_tr("Update the displayed local resource snapshot. This does not request network information.") : irc_tr("Return to the vipirc main screen. Up/Down or Page Up/Page Down scroll this document."));
    } else return 0; /* Ordinary conversation input keeps '?' literal. */
    ReadView fresh = {0};
    {
        const IrcDisplayField fields[] = {{"item", item}};
        if (!irc_display_render(fresh.title, sizeof fresh.title, "Help: {item}", fields, 1))
            snprintf(fresh.title, sizeof fresh.title, "Help: %.240s", item);
    }
    snprintf(fresh.parent, sizeof fresh.parent, "%s", screen);
    {
        const IrcDisplayField fields[] = {{"item", item}, {"screen", screen}};
        const char *message = u->app_menu ? "{item} is an option on the {screen}." :
            command ? "{item} is a suggestion on the {screen}." :
                      "{item} is selected on the {screen}.";
        if (!irc_display_render(intro, sizeof intro, message, fields, 2))
            (void)snprintf(intro, sizeof intro, "%s is %s the %s.", item,
                u->app_menu ? "an option on" : command ? "a suggestion on" : "selected on", screen);
    }
    readpage_add(&fresh.pages[0], intro);
    readpage_add(&fresh.pages[0], "");
    {
        const IrcDisplayField fields[] = {{"screen", screen}};
        if (!irc_display_render(intro, sizeof intro, "About the {screen}: use the selectable link above.", fields, 1))
            snprintf(intro, sizeof intro, "About the %s: use the selectable link above.", screen);
    }
    readpage_add(&fresh.pages[0], intro);
    readpage_add(&fresh.pages[0], "");
    readpage_add(&fresh.pages[0], item);
    readpage_text(&fresh.pages[0], detail);
    readpage_add(&fresh.pages[1], screen);
    readpage_add(&fresh.pages[1], "");
    readpage_text(&fresh.pages[1], screen_help);
    if (fresh.pages[0].failed || fresh.pages[1].failed) {
        free_readview(&fresh);
        app_status(a, "%s", irc_tr("Cannot allocate context help; the selected screen is preserved."));
        return 1;
    }
    fresh.active = 1;
    free_readview(&u->context_help);
    u->context_help = fresh;
    invalidate_mouse(u);
    a->dirty = 1;
    return 1;
}

static size_t reader_rows(const ReadPage *page, int width)
{
    size_t rows = 0;
    for (size_t i = 0; i < page->count; ++i)
        rows += (size_t)history_rows(page->lines[i], width, -1, 0, 0);
    return rows;
}

static void reader_scroll(ReadView *view, size_t amount, int direction)
{
    size_t total = reader_rows(&view->pages[view->page], COLS > 1 ? COLS - 1 : 1);
    size_t height = LINES > 6 ? (size_t)(LINES - 6) : 1;
    size_t last = total > height ? total - height : 0;
    size_t *scroll = &view->scroll[view->page];
    if (*scroll > last) *scroll = last;
    if (direction < 0) *scroll = *scroll > amount ? *scroll - amount : 0;
    else *scroll += amount < last - *scroll ? amount : last - *scroll;
}

static int reader_bar(App *a, ReadView *view, int help, int draw_it, int clicked_x)
{
    char link[192];
    if (help && !view->page) snprintf(link, sizeof link, "About %s", view->parent);
    else snprintf(link, sizeof link, "%s", help ? "Back to item" : "Refresh");
    const char *labels[] = {link, irc_tr(help ? "Close help" : "Back")};
    int total = text_cells(labels[0], COLS) + text_cells(labels[1], COLS) + 5;
    if (total > COLS) {
        if (draw_it) {
            text_at(3, 0, 1, "<", A_BOLD);
            focus_button(a, 3, 2, COLS - 4, labels[view->action], 1);
            text_at(3, COLS - 1, 1, ">", A_BOLD);
        }
        return clicked_x == 0 || clicked_x == COLS - 1 ? -2 : clicked_x >= 2 && clicked_x < COLS - 2 ? view->action : -1;
    }
    int x = 0;
    for (int i = 0; i < 2; ++i) {
        int width = text_cells(labels[i], COLS) + 2;
        if (draw_it) focus_button(a, 3, x, width, labels[i], i == view->action);
        else if (clicked_x >= x && clicked_x < x + width) return i;
        x += width + 1;
    }
    return -1;
}

static void draw_reader(App *a, Ui *u)
{
    int help = u->context_help.active;
    ReadView *view = help ? &u->context_help : &u->summary;
    erase();
    text_at(2, 0, COLS, help && view->page ? view->parent : view->title, A_BOLD);
    if (LINES >= 9 && COLS >= 20) {
        (void)reader_bar(a, view, help, 1, -1);
        reader_scroll(view, 0, 1);
        size_t skip = view->scroll[view->page];
        int row = 4, height = LINES - 6;
        const ReadPage *page = &view->pages[view->page];
        for (size_t i = 0; i < page->count && row < 4 + height; ++i) {
            int rows = history_rows(page->lines[i], COLS - 1, -1, 0, 0);
            if ((size_t)rows <= skip) { skip -= (size_t)rows; continue; }
            (void)history_rows(page->lines[i], COLS - 1, row, (int)skip, 4 + height - row);
            row += rows - (int)skip;
            skip = 0;
        }
        text_at(LINES - 2, 0, COLS, help ? irc_tr("Arrows: scroll/link | Enter: open | Esc: back") : irc_tr("R: refresh | /: command | Arrows: scroll/action | Esc: back"), A_DIM);
        text_at(LINES - 1, 0, COLS, app_status_text(a), A_NORMAL);
    } else text_at(LINES > 4 ? LINES - 2 : 0, 0, COLS, irc_tr("Resize; Esc returns."), A_NORMAL);
    (void)curs_set(0);
    move(2 < LINES ? 2 : 0, 0);
}

static void reader_activate(App *a, Ui *u, int action)
{
    ReadView *view = u->context_help.active ? &u->context_help : &u->summary;
    int help = u->context_help.active;
    if (action == 1) free_readview(view);
    else if (help) { view->page = !view->page; view->action = 0; }
    else refresh_reader(a, u);
    invalidate_mouse(u);
    a->dirty = 1;
}

static void reader_key(App *a, Ui *u, wint_t key, int special)
{
    ReadView *view = u->context_help.active ? &u->context_help : &u->summary;
    int help = u->context_help.active;
    if (special && key == PASTE_BEGIN) { view->paste = 1; return; }
    if (special && key == PASTE_END) { view->paste = 0; return; }
    if (view->paste) return;
    if (!special && key == 27) {
        if (help && view->page) { view->page = 0; view->action = 0; }
        else free_readview(view);
        invalidate_mouse(u); return;
    }
    size_t step = LINES > 7 ? (size_t)(LINES - 7) : 1;
    if ((special && key == KEY_UP) || (!special && key == 'k')) reader_scroll(view, 1, -1);
    else if ((special && key == KEY_DOWN) || (!special && key == 'j')) reader_scroll(view, 1, 1);
    else if (special && key == KEY_PPAGE) reader_scroll(view, step, -1);
    else if (special && key == KEY_NPAGE) reader_scroll(view, step, 1);
    else if ((special && key == KEY_HOME) || (!special && key == 'g')) view->scroll[view->page] = 0;
    else if ((special && key == KEY_END) || (!special && key == 'G')) reader_scroll(view, SIZE_MAX, 1);
    else if ((special && (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_BTAB)) ||
             (!special && (key == '\t' || key == 'h' || key == 'l'))) {
        view->action = !view->action;
        invalidate_mouse(u);
    }
    else if ((special && key == KEY_ENTER) || (!special && (key == '\r' || key == '\n')))
        reader_activate(a, u, view->action);
    else if (!help && !special && (key == 'r' || key == 'R')) refresh_reader(a, u);
    else if (!help && !special && key == '?') (void)open_context_help(a, u);
    else if (!help && !special && key == '/') (void)open_command(a, u, "/");
}

static void draw_content(App *a, Ui *u)
{
    invalidate_mouse(u);
    u->text_hit_count = 0;
    if (u->bindings.active) { draw_bindings(a, u); return; }
    if (u->context_help.active || (u->summary.active && !u->summary.hidden)) {
        draw_reader(a, u);
        return;
    }
    if (guided_active(u->owner->guided)) {
        guided_draw(u->owner->guided, a, text_at);
        return;
    }
    if (u->nick_menu) { draw_nick_menu(a, u); return; }
    if (u->app_menu) { draw_app_menu(a, u); return; }
    if (u->about) { draw_about(a, u); return; }
    if (u->license) { draw_license(a, u); return; }
    if (settings_is_open(u)) {
        erase();
        settings_draw(u->settings, a, text_at);
        wnoutrefresh(stdscr);
        a->dirty = 0;
        mouse_drawn(u);
        return;
    }
    if (transfers_active(u->owner->transfers)) {
        transfers_draw(u->owner->transfers, a, text_at);
        a->dirty = 0;
        mouse_drawn(u);
        return;
    }
    if (u->owner->servers) { draw_servers(a, u); mouse_drawn(u); return; }
    if (u->owner->unread) { draw_unread(a, u); mouse_drawn(u); return; }
    if (u->manual) {
        erase();
        draw_manual(a, u);
        mouse_drawn(u);
        return;
    }
    if (prompt_active(u->command)) {
        update_input_warning(a, u);
        prompt_draw(u->command, a, text_at);
        draw_dialog(u);
        if (wnoutrefresh(stdscr) != ERR) mouse_drawn(u);
        a->dirty = 0;
        return;
    }
    if (main_screen(u)) u->input_mode = 1;
    Editor *e = draft(a, u);
    char line[1024];
    if (!e) return;
    update_input_warning(a, u);
    erase();
    if (LINES < 9 || COLS < 20) {
        text_at(0, 0, COLS, irc_tr("Too small."), A_BOLD);
        text_at(1, 0, COLS, a->test_mode ? "[OFFLINE] Resize; draft is safe." : irc_tr("Resize; draft is safe."), A_NORMAL);
        (void)curs_set(u->input_mode ? 1 : 0);
        move(u->input_mode && LINES > 2 ? 2 : 0, 0);
        wnoutrefresh(stdscr); a->dirty = 0; return;
    }
    if (u->channels == 3) { draw_directory(a, u); mouse_drawn(u); return; }
    if (u->channels) { draw_channels(a, u); mouse_drawn(u); return; }
    Buffer *b = &a->buffers[a->current];
    u->input_width = COLS - 2;
    /* Reserve a full ordinary message plus its caret from an empty draft.
     * Command text, wrapping and typing never resize the field. */
    size_t view_budget = app_budget(a,
        (b->kind == B_CHANNEL || b->kind == B_QUERY) ? b->name : "a", 0);
    size_t wanted_rows = view_budget / (size_t)u->input_width + 1;
    int max_input = LINES - 9; /* header/divider/context, history, borders, footer */
    if (max_input < 1) max_input = 1;
    u->input_rows = wanted_rows < (size_t)max_input ? (int)wanted_rows : max_input;
    u->input_top = LINES - 2 - u->input_rows;
    u->body_rows = u->input_top - 4;
    if (u->body_rows < 1) u->body_rows = 1;
    int sidebar = a->cfg.user_list && b->kind == B_CHANNEL && COLS >= 60 ? 22 : 0;
    char context[256];
    view_name(a, a->current, context, sizeof context);
    if (*a->cfg.server)
        snprintf(line, sizeof line, "%s%s | S%zu %s:%d", a->test_mode ? irc_tr("[OFFLINE] ") : "", context, u->slot, a->cfg.server, a->cfg.port);
    else snprintf(line, sizeof line, "%s", irc_tr("No server configured"));
    int visual_used;
    text_at_mode(2, 0, COLS, line, A_BOLD, &visual_used);
    history_hits(u, line, COLS, 2, 0, 1, IRC_ARABIC_SINGLE_LINE, visual_used);
    draw_history(u, b, 3, u->body_rows, COLS - sidebar - 1);
    if (sidebar) draw_users(a, u, b, 3, u->body_rows, COLS - sidebar, sidebar);
    /* Notifications have their own footer, beside mouse actions when enabled.
     * They never consume the conversation or the fixed input viewport. */
    int notice_x = u->mouse_active ? 17 : 0;
    if (notice_x < COLS) text_at(LINES - 1, notice_x, COLS - notice_x, app_status_text(a), A_NORMAL);
    /* Use the successfully painted frame's geometry for the footer as well.
     * A failed visual snapshot must fall back as one unit: text, caret, scroll
     * and the position indicator may not describe different layouts. */
    IrcCursesEditorPosition position = draw_editor(e, u, a->current,
                                                  u->input_top, u->input_rows);
    int caret_y, caret_x;
    getyx(stdscr, caret_y, caret_x);
    AppDraftMeasure measure;
    int is_chat = measure_input(a, u, &measure);
    size_t budget = is_chat ? measure.budget : 0;
    int divider_width = u->input_width;
    size_t bytes = budget ? measure.payload_bytes : e->bytes;
    char detail[128];
    snprintf(line, sizeof line, "%zu bytes |", bytes);
    if (strlen(line) > (size_t)divider_width)
        snprintf(line, sizeof line, "%zu bytes", bytes);
    if (budget) {
        snprintf(detail, sizeof detail, " text budget <=%zu/message", budget);
        if (!input_detail(line, sizeof line, divider_width, detail)) {
            snprintf(detail, sizeof detail, " text budget <=%zu", budget);
            (void)input_detail(line, sizeof line, divider_width, detail);
        }
    } else (void)input_detail(line, sizeof line, divider_width, " commands or /msg");
    if (u->paste) (void)input_detail(line, sizeof line, divider_width, " | PASTING");
    if (u->literal[a->current]) (void)input_detail(line, sizeof line, divider_width, " | literal paste");
    snprintf(detail, sizeof detail, " | row %d col %d", position.row + 1, position.col + 1);
    (void)input_detail(line, sizeof line, divider_width, detail);
    draw_input_border(u, line);
    (void)move(caret_y, caret_x);
    draw_dialog(u);
    if ((u->input_mode && !u->status_selected) || u->confirming) {
        /* Ask terminfo for its most visible hardware caret; terminals which
         * lack that capability still get the ordinary visible cursor. The
         * reverse-video cell above remains the portable fallback. */
        int previous_cursor = curs_set(2);
        if (previous_cursor == ERR) (void)curs_set(1);
        else if (previous_cursor == 0) {
            /* Some terminfo entries (notably screen) define cvvis as a
             * shape change only. Restore visibility with cnorm first. */
            (void)curs_set(1);
            (void)curs_set(2);
        }
    } else (void)curs_set(0);
    if (!u->input_mode && !u->confirming) move(0, 0);
    if (wnoutrefresh(stdscr) != ERR) mouse_drawn(u);
    a->dirty = 0;
}

static void draw_quit_question(void)
{
    const char *question = "Really quit? [Y/N]";
    const char *hint = "Y: quit; other key: stay";
    if (LINES < 6 || COLS < 20) {
        text_at(LINES > 2 ? LINES / 2 : 0, 0, COLS, question, focus_attribute());
        (void)curs_set(0);
        return;
    }
    int width = COLS > 34 ? 30 : COLS - 4;
    int left = (COLS - width - 2) / 2;
    int height = dialog_paragraph(question, width, -1, 0, A_BOLD) +
                 dialog_paragraph(hint, width, -1, 0, A_NORMAL) + 3;
    if (height > LINES) height = LINES;
    int top = (LINES - height) / 2;
    attrset((int)focus_attribute());
    for (int row = top; row < top + height; ++row) mvhline(row, left, ' ', width + 2);
    mvhline(top, left + 1, ACS_HLINE, width);
    mvhline(top + height - 1, left + 1, ACS_HLINE, width);
    mvvline(top + 1, left, ACS_VLINE, height - 2);
    mvvline(top + 1, left + width + 1, ACS_VLINE, height - 2);
    mvaddch(top, left, ACS_ULCORNER);
    mvaddch(top, left + width + 1, ACS_URCORNER);
    mvaddch(top + height - 1, left, ACS_LLCORNER);
    mvaddch(top + height - 1, left + width + 1, ACS_LRCORNER);
    attrset(A_NORMAL);
    int rows = dialog_paragraph(question, width, top + 1, left + 1, focus_attribute());
    (void)dialog_paragraph(hint, width, top + rows + 2, left + 1, A_NORMAL);
    (void)curs_set(0);
    move(top + 1, left + 1);
}

/* Called only after physical refresh succeeds. A resize notice is not a
 * completed editor/form view, and the review interval starts after display. */
static void input_guard_drawn(App *a, Ui *u, int refreshed)
{
    int enough = LINES >= 9 && COLS >= 20;
    if (transfers_active(u->owner->transfers)) enough = LINES >= 12 && COLS >= 20;
    if (guided_active(u->owner->guided)) enough = LINES >= 14 && COLS >= 32;
    if (refreshed && enough && u->hotkey_guard && !u->hotkey_guard_drawn) {
        u->hotkey_guard_drawn = 1;
        u->hotkey_after = app_now_ms(a) + 100;
    }
}

/* Stage the body first and paint the shared selector last. Only this function
 * updates the physical terminal, so panel titles never flash over the bar. */
static void draw(App *a, Ui *u)
{
    if (u->status_selected && !u->context_help.active && !u->summary.active) {
        StatusLayout current;
        status_layout(a, u, &current);
        if (!(current.present & (1u << u->nav_focus))) focus_header(a, u, -1);
    }
    draw_content(a, u);
    int y, x;
    getyx(stdscr, y, x);
    if (LINES >= 9 && COLS >= 20) {
        draw_status_controls(a, u);
        attr_t attr = focus_attribute();
        (void)attr_set(attr, (short)PAIR_NUMBER(attr), NULL);
        (void)mvhline(1, 0, ACS_HLINE, COLS);
        attrset(A_NORMAL);
    }
    if (u->quit_confirm) { draw_quit_question(); getyx(stdscr, y, x); }
    if (y >= 0 && y < LINES && x >= 0 && x < COLS) move(y, x);
    int refreshed = wnoutrefresh(stdscr) != ERR && doupdate() != ERR;
    input_guard_drawn(a, u, refreshed);
    if (refreshed) {
        mouse_drawn(u);
        if (u->bindings.active && LINES >= 9 && COLS >= 20) u->bindings.drawn = 1;
        if (u->command_guard && LINES >= 9 && COLS >= 20 &&
            prompt_active(u->command) && !u->bindings.active &&
            !u->context_help.active && !(u->summary.active && !u->summary.hidden) &&
            !u->manual && !u->app_menu && !u->channels && !u->owner->servers &&
            !u->owner->unread && !settings_is_open(u) &&
            !guided_active(u->owner->guided) && !transfers_active(u->owner->transfers) &&
            !u->command_guard_drawn) {
            u->command_guard_drawn = 1;
            u->command_guard_after = app_now_ms(a) + 100;
        }
    }
    /* A transient output failure must get another attempt even if no
     * network traffic or ordinary key reaches a screen handler. */
    a->dirty = !refreshed;
}

static void clear_draft(Ui *u, size_t view)
{
    if (view >= APP_BUFFERS) return;
    if (u->drafts[view]) ed_init(u->drafts[view]);
    u->literal[view] = 0;
    u->limit_warned[view] = 0;
    u->input_scroll[view] = 0;
}

static void clear_input(App *a, Ui *u)
{
    clear_draft(u, a->current);
    app_program_notice(a, "Input cleared. Nothing sent.");
}

#if UI_MOUSE
static void chooser_bar_mouse(App *a, Ui *u, int x)
{
    size_t count = chooser_action_count(u);
    if (chooser_bar_width(u) <= COLS - 1) {
        int left = 0;
        for (size_t i = 0; i < count; ++i) {
            int width = text_cells(chooser_action_label(u, i), COLS) + 2;
            if (x >= left && x < left + width) {
                u->chooser_action = i;
                activate_chooser(a, u, chooser_action_kind(u, i));
                return;
            }
            left += width + 1;
        }
    } else {
        if (u->chooser_action >= count) u->chooser_action = 0;
        int width = (int)strlen(chooser_action_label(u, u->chooser_action)) + 2;
        int left = (COLS - width - 4) / 2;
        if (x >= left && x < left + 2) move_chooser_action(u, -1);
        else if (x >= left + width + 2 && x < left + width + 4) move_chooser_action(u, 1);
        else if (x >= left + 2 && x < left + width + 2)
            activate_chooser(a, u, chooser_action_kind(u, u->chooser_action));
    }
}

static void apply_mouse(App *a, Ui *u, MEVENT event)
{
    if (!input_guard_ready(a, u)) return;
    if (u->bindings.active || !u->mouse_active || u->confirming || u->quit_confirm || u->owner->close_confirm || u->paste ||
        settings_pasting(u->settings) || guided_pasting(u->owner->guided) || transfers_pasting(u->owner->transfers) || prompt_pasting(u->command) ||
        !u->mouse_ready || u->mouse_rows != LINES || u->mouse_cols != COLS ||
        event.x < 0 || event.x >= COLS || event.y < 0 || event.y >= LINES) return;
    const StatusHits *controls = &u->status_hits;
    if (!controls->valid || controls->rows != LINES || controls->cols != COLS ||
        controls->view != a->current || controls->kind != a->buffers[a->current].kind ||
        controls->panel != mouse_panel(u) || controls->shortcut_labels != a->cfg.shortcut_labels) return;
    if (u->context_help.active || (u->summary.active && !u->summary.hidden)) {
        ReadView *view = u->context_help.active ? &u->context_help : &u->summary;
        if (view->paste) return;
#ifdef BUTTON4_PRESSED
        if (event.bstate & BUTTON4_PRESSED) reader_scroll(view, 3, -1);
#endif
#ifdef BUTTON5_PRESSED
        if (event.bstate & BUTTON5_PRESSED) reader_scroll(view, 3, 1);
#endif
        if ((event.bstate & BUTTON1_PRESSED) && event.y == 3) {
            int action = reader_bar(a, view, u->context_help.active, 0, event.x);
            if (action == -2) { view->action = !view->action; invalidate_mouse(u); }
            else if (action >= 0) { view->action = action; reader_activate(a, u, action); }
        }
        a->dirty = 1;
        return;
    }
    if (guided_active(u->owner->guided)) {
        guided_mouse(u->owner->guided, a, &event);
        guided_changed(a, u);
        return;
    }
    if ((event.bstate & BUTTON1_PRESSED) && event.y == 0) {
        for (size_t i = FOCUS_APP; i <= FOCUS_LAST; ++i) {
            const StatusHit *hit = &controls->controls[i];
            if (hit->x >= 0 && event.x >= hit->x && event.x < hit->x + hit->width) {
                u->nav_focus = i;
                activate_status(a, u);
                resume_body(u);
                return;
            }
        }
        if ((controls->before && event.x < 2) || (controls->after && event.x >= COLS - 2)) {
            focus_header(a, u, event.x < 2 ? -1 : 1);
            resume_body(u);
            a->dirty = 1;
        }
        return;
    }
    resume_body(u);
    if (main_screen(u)) u->header_origin = SIZE_MAX;
    if (u->nick_menu) {
        if ((event.bstate & BUTTON1_PRESSED) && event.y >= u->nick_menu_top &&
            event.y < u->nick_menu_top + 4) {
            u->nick_action = event.y - u->nick_menu_top;
            nick_menu_action(a, u);
        }
        a->dirty = 1;
        return;
    }
    if (u->app_menu) {
        if (event.bstate & BUTTON1_PRESSED) {
            if (event.y == LINES - 2 && event.x < 10) return_to_chat(a, u);
            else for (int i = 0; i < APP_ENTRIES; ++i) {
                const StatusHit *hit = &u->app_hits[i];
                if (event.y == hit->y && event.x >= hit->x && event.x < hit->x + hit->width) {
                    u->app_selection = i;
                    app_menu_action(a, u);
                    break;
                }
            }
        }
        a->dirty = 1;
        return;
    }
    if (settings_is_open(u)) {
        int previous_mouse = a->cfg.mouse;
        settings_mouse(u->settings, a, &event);
        settings_changed(a, u, previous_mouse);
        return;
    }
    if (transfers_active(u->owner->transfers)) {
        transfers_mouse(u->owner->transfers, a, &event);
        resume_body(u);
        return;
    }
    if (prompt_active(u->command)) {
        command_action(a, u, prompt_mouse(u->command, a, &event));
        return;
    }
    if (u->owner->servers) { server_mouse(a, u, &event); return; }
    if (u->owner->unread) {
        if (LINES < 9 || COLS < 20) return;
        size_t count = u->owner->activity_count, *selected = &u->owner->unread_selection;
        if (!count) *selected = 0;
        else if (*selected >= count) *selected = count - 1;
#ifdef BUTTON4_PRESSED
        if ((event.bstate & BUTTON4_PRESSED) && *selected) --*selected;
#endif
#ifdef BUTTON5_PRESSED
        if ((event.bstate & BUTTON5_PRESSED) && *selected + 1 < count) ++*selected;
#endif
        if (event.bstate & BUTTON1_PRESSED) {
            if (event.y == LIST_ACTION_ROW) {
                int action = unread_bar(a, u->owner, 0, event.x);
                if (action == -2) u->owner->unread_action =
                    (u->owner->unread_action + UNREAD_ACTIONS - 1) % UNREAD_ACTIONS;
                else if (action == -3) u->owner->unread_action =
                    (u->owner->unread_action + 1) % UNREAD_ACTIONS;
                else if (action >= 0) { u->owner->unread_action = (size_t)action; unread_action(a, u); }
            }
            else for (size_t i = 0; i < u->owner->activity_hits_count; ++i) {
                const ActivityHit *hit = &u->owner->activity_hits[i];
                if (event.y >= hit->y && event.y < hit->y + hit->rows) {
                    *selected = hit->item;
                    activate_unread(a, u);
                    break;
                }
            }
        }
        u->owner->activity_scroll = 0;
        a->dirty = 1;
        return;
    }
    if (LINES < 9 || COLS < 20) return;
    int direction = 0;
#ifdef BUTTON4_PRESSED
    if (event.bstate & BUTTON4_PRESSED) direction = -1;
#endif
#ifdef BUTTON5_PRESSED
    if (event.bstate & BUTTON5_PRESSED) direction = 1;
#endif
    if (u->about) {
        if (direction) scroll_about(u, 3, direction);
        else if ((event.bstate & BUTTON1_PRESSED) && event.y == 3) {
            int action = about_bar(a, u, 0, event.x);
            if (action == 0) return_to_app(a, u);
            else if (action == 1) {
                u->about_action = 1;
                capture_about(u);
                app_program_notice(a, "Resource snapshot refreshed.");
            }
        }
        a->dirty = 1;
        return;
    }
    if (u->manual) {
        if (direction) scroll_manual(a, 3, direction);
        else if ((event.bstate & BUTTON1_PRESSED) && event.y == 3) {
            int action = manual_bar(a, u, 0, event.x);
            if (action == -2 || action == -3) {
                if (!u->manual_searching) move_manual_action(u, action == -2 ? -1 : 1);
            } else if (action >= 0) {
                u->manual_action = action;
                cancel_manual_search(a, u);
                run_manual_action(a, u);
            }
        } else if ((event.bstate & BUTTON1_PRESSED) && u->manual_searching &&
                   u->manual_search && event.y == LINES - 2 && event.x >= 7 && event.x < COLS - 1) {
            Editor *search = u->manual_search;
            int col = event.x - 7;
            if (u->manual_search_visual)
                ed_visual_set_cell(search, u->manual_search_scroll, col, COLS - 8);
            else {
                for (size_t i = 0; i < u->manual_search_first; i = ed_next(search, i))
                    col += ed_cluster_width(search, i, 1024);
                ed_set_cell(search, 0, col, 1024);
            }
        }
        a->dirty = 1;
        return;
    }
    if (u->license) {
        if (direction) scroll_license(u, 3, direction);
        else if ((event.bstate & BUTTON1_PRESSED) && event.y == LINES - 2 && event.x < 6)
            return_to_app(a, u);
        a->dirty = 1;
        return;
    }
    if (u->channels == 3) {
        size_t count = app_list_visible_count(a);
        if (direction && count) {
            if (direction < 0) u->channel_selection = u->channel_selection > 3 ? u->channel_selection - 3 : 0;
            else u->channel_selection = u->channel_selection + 3 < count ? u->channel_selection + 3 : count - 1;
        } else if (event.bstate & BUTTON1_PRESSED) {
            if (event.y == 4) {
                int channel_width = COLS >= 90 ? 28 : COLS >= 60 ? 20 : 14;
                int users_width = 10;
                int sort = event.x < channel_width + 2 ? APP_LIST_SORT_CHANNEL :
                           event.x < channel_width + users_width + 5 ? APP_LIST_SORT_USERS : APP_LIST_SORT_TOPIC;
                directory_resort(a, u, sort, (size_t)sort == u->chooser_action ? !a->list.descending : 0);
            } else if (event.y >= 5 && event.y < LINES - 2 && count) {
                size_t height = LINES > 8 ? (size_t)(LINES - 8) : 1u;
                size_t first = u->channel_selection >= height ? u->channel_selection - height + 1 : 0;
                size_t row = first + (size_t)(event.y - 5);
                if (row < count) u->channel_selection = row;
            }
        }
        a->dirty = 1;
        return;
    }
    if (u->channels) {
        if (!u->chooser_drawn || u->chooser_kind != u->channels) return;
        size_t count = u->chooser_count;
        if (direction && count) {
            if (direction < 0) u->channel_selection = u->channel_selection > 3 ? u->channel_selection - 3 : 0;
            else u->channel_selection = u->channel_selection + 3 < count ? u->channel_selection + 3 : count - 1;
        } else if (event.bstate & BUTTON1_PRESSED) {
            if (event.y == LIST_ACTION_ROW) {
                chooser_bar_mouse(a, u, event.x);
            }
            else if (event.y >= 4 && event.y < LINES - 2) {
                size_t height = (size_t)(LINES - 6);
                size_t first = u->channel_selection >= height ? u->channel_selection - height + 1 : 0;
                size_t row = first + (size_t)(event.y - 4);
                if (!count && !row) {
                    activate_chooser(a, u, CHOOSE_NEW);
                } else if (row < count) {
                    u->channel_selection = row;
                    activate_chooser(a, u, CHOOSE_OPEN);
                }
            }
        }
        a->dirty = 1;
        return;
    }
    Buffer *b = &a->buffers[a->current];
    if (direction && event.y >= 3 && event.y < 3 + u->body_rows) {
        int sidebar = a->cfg.user_list && b->kind == B_CHANNEL && COLS >= 60 && event.x >= COLS - 22;
        if (sidebar) {
            size_t visible = u->body_rows > 1 ? (size_t)u->body_rows - 1 : 1;
            size_t maximum = b->user_count > visible ? b->user_count - visible : 0;
            if (direction < 0) b->user_scroll = b->user_scroll > 3 ? b->user_scroll - 3 : 0;
            else b->user_scroll = b->user_scroll + 3 < maximum ? b->user_scroll + 3 : maximum;
        } else if (direction < 0) {
            if (b->scroll < 1000000) b->scroll += 3;
        } else b->scroll = b->scroll > 3 ? b->scroll - 3 : 0;
        a->dirty = 1;
        return;
    }
    if (!(event.bstate & BUTTON1_PRESSED)) return;
    for (size_t i = 0; i < u->text_hit_count; ++i) {
        const TextHit *hit = &u->text_hits[i];
        if (hit->y == event.y && event.x >= hit->x && event.x < hit->x + hit->width) {
            click_text_target(a, u, hit);
            return;
        }
    }
    for (size_t i = FOCUS_CLEAR; i < FOCUS_COUNT; ++i) {
        const StatusHit *hit = &controls->controls[i];
        if (hit->x >= 0 && event.y == hit->y && event.x >= hit->x &&
            event.x < hit->x + hit->width) {
            u->nav_focus = i;
            activate_status(a, u);
            return;
        }
    }
    if (event.y >= u->input_top && event.y < u->input_top + u->input_rows &&
               event.x >= 1 && event.x <= u->input_width) {
        Editor *e = draft(a, u);
        if (e) {
            u->input_mode = 1;
            ed_visual_set_cell(e, u->input_scroll[a->current] + event.y - u->input_top,
                        event.x - 1, u->input_width);
        }
    }
    a->dirty = 1;
}

static void handle_mouse(App *a, Ui *u)
{
    /* A fast press/release pair may produce one KEY_MOUSE notification.
     * getmouse pops the newest queued event first. Drain the bounded batch
     * and apply it in arrival order before handling the next typed key. */
    MEVENT events[64];
    size_t count = 0;
    while (count < sizeof events / sizeof events[0] && getmouse(&events[count]) == OK) ++count;
    while (count) {
        size_t view = a->current;
        unsigned settings_state = settings_mouse_state(u->settings);
        unsigned transfer_state = transfers_mouse_state(u->owner->transfers);
        unsigned guided_state = guided_mouse_state(u->owner->guided);
        unsigned panel = mouse_panel(u);
        int shortcut_labels = a->cfg.shortcut_labels;
        size_t server_selection = u->owner->server_selection, server_action_before = u->owner->server_action;
        size_t channel_selection = u->channel_selection, chooser_action_before = u->chooser_action;
        size_t unread_selection = u->owner->unread_selection, unread_action_before = u->owner->unread_action;
        apply_mouse(a, u, events[--count]);
        unsigned next = mouse_panel(u);
        /* Queued coordinates describe the old view and its exact subpanel.
         * In particular, a list click cannot approve the transfer review it
         * just opened, and a field/list click cannot affect its new editor.
         * Deferred session changes must also complete before more events. */
        if (next != panel || a->current != view || shortcut_labels != a->cfg.shortcut_labels ||
            server_selection != u->owner->server_selection || server_action_before != u->owner->server_action ||
            channel_selection != u->channel_selection || chooser_action_before != u->chooser_action ||
            unread_selection != u->owner->unread_selection || unread_action_before != u->owner->unread_action ||
            settings_mouse_state(u->settings) != settings_state ||
            transfers_mouse_state(u->owner->transfers) != transfer_state ||
            guided_mouse_state(u->owner->guided) != guided_state ||
            u->owner->switch_to != SIZE_MAX ||
            u->owner->close_slot != SIZE_MAX) {
            invalidate_mouse(u);
            break;
        }
    }
}
#endif

static int finish_close(App *a, Ui *u)
{
    u->submitting_view = u->close_source;
    int closed = app_close_view(a, u->close_view);
    if (closed && u->submitting_view != SIZE_MAX)
        clear_draft(u, u->submitting_view);
    u->submitting_view = SIZE_MAX;
    u->close_source = SIZE_MAX;
    u->confirming = 0;
    a->dirty = 1;
    return closed;
}

static int request_close(App *a, Ui *u, size_t index, size_t source)
{
    if (index < 3 || index >= a->buffer_count) {
        app_status(a, "%s", irc_tr("Server, mentions and help stay available. Use /disconnect to leave the server."));
        return 0;
    }
    u->close_view = index;
    u->close_source = source;
    snprintf(u->close_name, sizeof u->close_name, "%s", a->buffers[index].name);
    if (index != source && u->drafts[index] && u->drafts[index]->len) {
        u->confirming = 2;
        u->confirm_at = app_now_ms(a);
        a->dirty = 1;
        return 2;
    }
    return finish_close(a, u);
}

static size_t resolve_close(const App *a, const char *arg)
{
    while (*arg == ' ') ++arg;
    if (!*arg) return a->current;
    char *end;
    errno = 0;
    unsigned long number = strtoul(arg, &end, 10);
    if (*arg && !*end && !errno) {
        size_t map[APP_BUFFERS], count = channel_map(a, map);
        return number < count ? map[number] : SIZE_MAX;
    }
    for (size_t i = 0; i < a->buffer_count; ++i)
        if (a->buffers[i].kind == B_QUERY && irc_equal(a->buffers[i].name, arg, a->casemap)) return i;
    for (size_t i = 0; i < a->buffer_count; ++i)
        if (irc_equal(a->buffers[i].name, arg, a->casemap)) return i;
    return SIZE_MAX;
}

static void restore_command_origin(Ui *u)
{
    u->app_menu = u->command_origin.app_menu;
    u->channels = u->command_origin.channels;
    u->owner->servers = u->command_origin.servers;
    u->owner->unread = u->command_origin.unread;
    u->manual = u->command_origin.manual;
    u->input_mode = u->command_origin.input_mode;
    if (u->command_origin.summary) u->summary.hidden = 0;
}

static int open_command(App *a, Ui *u, const char *prefix)
{
    if (u->hotkey_command) {
        /* A shortcut opened another page while this command was unfinished.
         * Return to that exact editor rather than silently replacing it. */
        if (u->command) prompt_destroy(u->command);
        u->command = u->hotkey_command;
        u->hotkey_command = NULL;
        u->hotkey_command_return = 0;
        restore_hotkey_target(a, u);
        guard_hotkey_command(a, u);
        u->app_menu = u->owner->servers = u->owner->unread = u->manual = 0;
        u->channels = u->input_mode = 0;
        if (u->summary.active) u->summary.hidden = 1;
        app_status(a, "%s", u->command_target_lost ?
                   "Original conversation closed. Command preserved but cannot run; Esc cancels it." :
                   "Unfinished command restored at its original conversation; chat draft preserved.");
        invalidate_mouse(u);
        return 1;
    }
    CommandContext context = COMMAND_CONTEXT_CLIENT;
    if (!u->app_menu && !u->summary.active && !u->about && !u->license && !settings_is_open(u)) {
        context = a->buffers[a->current].kind == B_CHANNEL ? COMMAND_CONTEXT_CHANNEL :
                  a->buffers[a->current].kind == B_QUERY ? COMMAND_CONTEXT_QUERY : COMMAND_CONTEXT_SERVER;
        if (u->owner->servers) context = COMMAND_CONTEXT_SERVER;
    }
    if (!u->command) u->command = prompt_create();
    if (!u->command || !prompt_open(u->command, prefix)) {
        app_status(a, "%s", irc_tr("Cannot open the command prompt; chat draft preserved."));
        return 0;
    }
    prompt_set_context(u->command, context);
    u->command_origin = (CommandOrigin){u->app_menu, u->channels, u->owner->servers,
        u->owner->unread, u->manual, u->input_mode, u->summary.active};
    u->command_from_app = u->app_menu;
    u->app_menu = u->owner->servers = u->owner->unread = u->manual = 0;
    if (u->summary.active) u->summary.hidden = 1;
    u->channels = u->input_mode = 0;
    u->nav_focus = FOCUS_APP;
    u->status_selected = u->status_cycle = 0;
    u->command_limit_warned = u->pending_command = u->close_command = 0;
    u->command_target_lost = u->command_guard = 0;
    app_status(a, "");
    invalidate_mouse(u);
    return 1;
}

static void activate_chooser(App *a, Ui *u, ChooserAction action)
{
    int private_list = u->channels == 2;
    size_t count = u->chooser_count;
    if (action == CHOOSE_BACK) { u->channels = 0; return; }
    if (action == CHOOSE_NEW) {
        (void)open_command(a, u, private_list ? "/pm " : "/join ");
        return;
    }
    if (!u->chooser_drawn || u->chooser_kind != u->channels) {
        app_status(a, "%s", irc_tr("The conversation list is updating; try again after it redraws."));
        return;
    }
    if (!count) {
        if (action == CHOOSE_OPEN) {
            (void)open_command(a, u, "/pm ");
            return;
        }
        app_status(a, "%s", irc_tr("No conversation selected. Choose New to start one."));
        return;
    }
    if (u->channel_selection >= count) u->channel_selection = count - 1;
    size_t index = u->chooser_map[u->channel_selection];
    if (index >= a->buffer_count) { u->chooser_drawn = 0; a->dirty = 1; return; }
    switch (action) {
    case CHOOSE_OPEN:
        app_switch(a, index);
        u->channels = 0;
        break;
    case CHOOSE_PART: {
        u->chooser_action = 0;
        if (private_list || !index) {
            app_status(a, "%s", irc_tr("Server is channel 0. Choose Disconnect in the server list."));
            break;
        }
        char command[256];
        SendPlan unused;
        snprintf(command, sizeof command, "/part %s", a->buffers[index].name);
        (void)app_command(a, command, &unused);
        break;
    }
    case CHOOSE_CLOSE:
        u->chooser_action = 0;
        u->close_command = 0;
        (void)request_close(a, u, index, SIZE_MAX);
        break;
    default: break;
    }
}

static void directory_resort(App *a, Ui *u, int sort, int descending)
{
    char selected[201] = "";
    size_t count = app_list_visible_count(a);
    if (count && u->channel_selection < count)
        snprintf(selected, sizeof selected, "%s", a->list.entries[u->channel_selection].channel);
    if (sort < APP_LIST_SORT_CHANNEL) sort = APP_LIST_SORT_TOPIC;
    if (sort > APP_LIST_SORT_TOPIC) sort = APP_LIST_SORT_CHANNEL;
    u->chooser_action = sort;
    app_list_sort(a, sort, descending);
    count = app_list_visible_count(a);
    if (*selected)
        for (size_t i = 0; i < count; ++i)
            if (!strcmp(a->list.entries[i].channel, selected)) { u->channel_selection = i; break; }
    if (count && u->channel_selection >= count) u->channel_selection = count - 1;
    a->dirty = 1;
}

static void directory_key(App *a, Ui *u, wint_t key, int special)
{
    size_t count = app_list_visible_count(a);
    size_t page = LINES > 8 ? (size_t)(LINES - 8) : 1u;
    int enter = (special && key == KEY_ENTER) || (!special && (key == '\r' || key == '\n'));
    if (!count) u->channel_selection = 0;
    else if (u->channel_selection >= count) u->channel_selection = count - 1;
    if ((special && key == KEY_UP) || (!special && key == 'k')) {
        if (u->channel_selection) --u->channel_selection;
    } else if ((special && key == KEY_DOWN) || (!special && key == 'j')) {
        if (u->channel_selection + 1 < count) ++u->channel_selection;
    } else if (special && key == KEY_PPAGE) {
        u->channel_selection = u->channel_selection > page ? u->channel_selection - page : 0;
    } else if (special && key == KEY_NPAGE) {
        if (count) {
            size_t last = count - 1;
            u->channel_selection = u->channel_selection + page < last ? u->channel_selection + page : last;
        }
    } else if ((special && key == KEY_HOME) || (!special && key == 'g')) {
        u->channel_selection = 0;
    } else if ((special && key == KEY_END) || (!special && key == 'G')) {
        u->channel_selection = count ? count - 1 : 0;
    } else if ((special && key == KEY_LEFT) || (!special && key == 'h')) {
        directory_resort(a, u, (int)u->chooser_action - 1, a->list.descending);
    } else if ((special && key == KEY_RIGHT) || (!special && key == 'l')) {
        directory_resort(a, u, (int)u->chooser_action + 1, a->list.descending);
    } else if (!special && (key == 's' || key == 'S')) {
        directory_resort(a, u, (int)u->chooser_action, !a->list.descending);
    } else if (!special && key == 'r') {
        if (app_list_repeat(a)) { u->channel_selection = 0; app_status(a, "%s", irc_tr("Refreshing network channel directory.")); }
    } else if (!special && key == '/') {
        (void)open_command(a, u, "/");
    } else if (!special && (key == 27 || key == 'q')) {
        u->channels = 0;
        u->input_mode = 1;
        invalidate_mouse(u);
    } else if (enter) {
        count = app_list_visible_count(a);
        if (!count) { app_status(a, "%s", irc_tr("No channel is selected.")); return; }
        if (u->channel_selection >= count) u->channel_selection = count - 1;
        char channel[201], command[256];
        snprintf(channel, sizeof channel, "%s", a->list.entries[u->channel_selection].channel);
        snprintf(command, sizeof command, "/join %s", channel);
        SendPlan unused;
        if (app_command(a, command, &unused)) {
            u->channels = 0;
            u->input_mode = 1;
            invalidate_mouse(u);
        }
    }
}

static void set_mouse(App *a, Ui *u, int enabled)
{
    Config candidate;
    char error[512];
    app_config_effective(a, &candidate);
    candidate.mouse = enabled;
    int applied = client_interface_apply(u->owner->client, &candidate, 0, error, sizeof error);
    config_wipe(&candidate, sizeof candidate);
    if (applied < 0) { app_status(a, "%s", error); return; }
    configure_mouse(u, enabled);
    if (!enabled) app_status(a, "%s", irc_tr("Mouse off. Keyboard navigation remains available."));
    else if (!u->mouse_active) app_status(a, "%s", irc_tr("Mouse unavailable for this terminal. Use arrows or hjkl."));
    else app_status(a, "%s", irc_tr("Mouse on."));
}

/* Completion appends a space for arguments. No-argument UI commands accept
 * that separator, but never absorb an argument or a composed newline. */
static const char *ui_command_tail(const char *text, const char *command)
{
    size_t length = strlen(command);
    if (strncasecmp(text, command, length)) return NULL;
    text += length;
    if (*text && *text != ' ') return NULL;
    while (*text == ' ') ++text;
    return text;
}

static int ui_command_exact(const char *text, const char *command)
{
    const char *tail = ui_command_tail(text, command);
    return tail && !*tail;
}

typedef struct ClientCommandOutput {
    Client *client;
    App *source;
    char first[1024], last[1024];
    size_t lines;
} ClientCommandOutput;

/* Shared connection/configuration and transfer commands emit local diagnostics.
 * Preserve every line in the server view; conversation histories stay clean.
 * On failure the first explanation remains visible in the command editor. */
static void client_command_output(void *context, const char *line)
{
    ClientCommandOutput *out = context;
    App *target = out->client->slots[out->client->current].app;
    if (!target) target = out->source;
    if (!out->lines) (void)snprintf(out->first, sizeof out->first, "%s", line);
    (void)snprintf(out->last, sizeof out->last, "%s", line);
    ++out->lines;
    app_log(target, 0, "%s", line);
}

/* The connection parser and profile operations are shared with the plain
 * terminal interface. Session destruction and UI switching stay deferred:
 * a command handler must never free the App/Ui it is still using. */
static int client_command(App *a, Ui *u, const char *text, const CommandSpec *spec)
{
    UiSet *all = u->owner;
    const char *rest = text + 1;
    while (*rest == ' ') ++rest;
    while (*rest && *rest != ' ') ++rest;
    while (*rest == ' ') ++rest;
    if ((!strcmp(spec->name, "autojoin") && (!*rest || ui_command_exact(rest, "list"))) ||
        (!strcmp(spec->name, "autoauth") && (!*rest || ui_command_exact(rest, "status"))))
        return open_inquiry(a, u, !strcmp(spec->name, "autojoin") ? "/autojoin" : "/autoauth status");
    int server = !strcmp(spec->name, "server");
    const char *closing = server ? ui_command_tail(rest, "close") : NULL;
    if (closing) {
        char *end;
        errno = 0;
        unsigned long index = strtoul(closing, &end, 10);
        while (*end == ' ') ++end;
        if (*closing < '0' || *closing > '9' || *end || errno ||
            index >= CLIENT_MAX_SERVERS || !all->client->slots[index].used) {
            app_status(a, "%s", irc_tr("Usage: /server close NUMBER. Use /servers to see server numbers."));
            return 0;
        }
        if (!index) {
            app_status(a, "%s", irc_tr("Server 0 is the primary entry; use /disconnect 0 to stop it."));
            return 0;
        }
        all->close_slot = (size_t)index;
        {
        char shown[512], number[32];
        (void)snprintf(number, sizeof number, "%lu", index);
        const IrcDisplayField fields[] = {{"number", number}};
        if (irc_display_render(shown, sizeof shown, "Closing server {number}.", fields, 1))
            app_status(a, "%s", shown);
        else app_status(a, "Closing server %lu.", index);
    }
        return 1;
    }
    size_t previous = all->client->current;
    ClientCommandOutput output;
    memset(&output, 0, sizeof output);
    output.client = all->client;
    output.source = a;
    IdleCommands shared = {all->client, "vipirc", client_command_output, &output, 1, 0, all->dcc, irc_tr};
    int result = idle_command(&shared, text, NULL);
    size_t selected = all->client->current;
    App *destination = all->client->slots[selected].app;
    if (selected != previous && destination) app_set_visible(destination, 0);
    all->client->current = previous;
    if (output.lines) {
        const char *message = result == IDLE_COMMAND_ERROR ? output.first : output.last;
        app_status(a, "%s", message);
        if (destination && destination != a) app_status(destination, "%s", message);
    }
    if (result == IDLE_COMMAND_DONE &&
        (selected != previous || !strcmp(spec->name, "connect") ||
         (server && !ui_command_tail(rest, "add") &&
          !ui_command_tail(rest, "connect") && !ui_command_tail(rest, "disconnect")))) {
        all->switch_to = selected;
        all->switch_view = 0;
    }
    config_wipe(&output, sizeof output);
    return result == IDLE_COMMAND_DONE;
}

static int help_command(App *a, Ui *u, const char *topic)
{
    char term[257];
    size_t length = strlen(topic);
    while (length && topic[length - 1] == ' ') --length;
    if (length >= sizeof term || strchr(topic, '\n')) {
        app_status(a, "%s", irc_tr("Usage: /help [TEXT]. TEXT searches the manual (up to 256 UTF-8 bytes)."));
        return 0;
    }
    memcpy(term, topic, length);
    term[length] = 0;
    const CommandSpec *spec = commands_find(term);
    int command_section = !strcasecmp(term, "commands") || !strcasecmp(term, "ls");
    if (command_section)
        (void)snprintf(term, sizeof term, "COMMAND PROMPT");
    else if (spec && !strchr(term, ' '))
        (void)snprintf(term, sizeof term, "/%s", spec->name);
    int from_app = u->command_from_app;
    open_manual(a, u);
    u->manual_from_app = from_app;
    if (*term && strcasecmp(term, "all")) {
        (void)snprintf(u->manual_term, sizeof u->manual_term, "%s", term);
        if (command_section) {
            const Buffer *manual = &a->buffers[2];
            for (size_t i = 0; i < manual->line_count; ++i) {
                const char *line = manual->lines[(manual->line_start + i) % manual->line_cap];
                if (strcmp(line, "COMMAND PROMPT")) continue;
                u->manual_found = 1;
                u->manual_match_line = i;
                u->manual_match_offset = 0;
                u->manual_match_length = strlen(term);
                u->manual_source_start = manual->line_start;
                u->manual_source_count = manual->line_count;
                reveal_manual_match(a, u);
                app_status(a, "%s", irc_tr("Command prompt reference. / searches; n/N finds related text."));
                return 1;
            }
        }
        find_manual_match(a, u, 1, 0);
    } else app_status(a, "%s", irc_tr("Opened the manual. /help TEXT searches it; it does not print topic help."));
    return 1;
}

/* Execute either editor without borrowing the other editor's storage. A
 * command has no chat-draft source: switching or closing a view must not
 * accidentally erase the conversation's saved draft. 2 means consent is
 * still pending; only a completed action belongs in command history. */
static int submit_text_command(App *a, Ui *u, const char *text, int from_command)
{
    if (from_command) {
        u->pending_command = 0;
        config_wipe(&u->pending, sizeof u->pending);
    }
    size_t source_view = from_command ? SIZE_MAX : a->current;
    int literal = !from_command && u->literal[a->current];
    const CommandSpec *spec = !literal && *text == '/' ? commands_find(text) : NULL;
    const char *tail;
    if (!literal && ui_command_exact(text, "/bindings")) {
        open_bindings(a, u, from_command && u->command_from_app);
        clear_draft(u, source_view);
        return 1;
    }
    if (!literal && (tail = ui_command_tail(text, "/summary"))) {
        if (*tail) { app_status(a, "%s", irc_tr("Usage: /summary")); return 0; }
        if (!open_summary(a, u)) return 0;
        clear_draft(u, source_view);
        return 1;
    }
    if (!literal && (tail = ui_command_tail(text, "/help"))) {
        int result = help_command(a, u, tail);
        if (result) clear_draft(u, source_view);
        return result;
    }
    if (!literal && (tail = ui_command_tail(text, "/mentions"))) {
        if (!*tail) app_switch(a, 1);
        else if (ui_command_exact(tail, "clear")) clear_mentions(a, u);
        else { app_status(a, "%s", irc_tr("Usage: /mentions [clear]. Clear removes retained mentions across servers.")); return 0; }
        clear_draft(u, source_view);
        return 1;
    }
    if (!literal && ui_command_tail(text, "/ls")) {
        tail = ui_command_tail(text, "/ls");
        if (!*tail || !strcasecmp(tail, "channels")) open_channels(a, u);
        else if (!strcasecmp(tail, "servers")) open_servers(a, u);
        else if (!strcasecmp(tail, "queries")) open_pms(a, u);
        else if (!strcasecmp(tail, "unread")) open_unread(a, u);
        else { app_status(a, "%s", irc_tr("Usage: /ls [servers|channels|queries|unread]")); return 0; }
        clear_draft(u, source_view);
        return 1;
    }
    if (!literal && ui_command_exact(text, "/panel")) {
        if (!open_guided(a, u, 0)) return 0;
        clear_draft(u, source_view);
        return 1;
    }
    if (!literal && ui_command_exact(text, "/quickstart")) {
        if (!open_guided(a, u, 1)) return 0;
        clear_draft(u, source_view);
        return 1;
    }
    if (spec && (!strcmp(spec->name, "connect") || !strcmp(spec->name, "server") ||
                 !strcmp(spec->name, "disconnect") || !strcmp(spec->name, "save") ||
                 !strcmp(spec->name, "saveconf") || !strcmp(spec->name, "autoconnect") ||
                 !strcmp(spec->name, "autojoin") || !strcmp(spec->name, "autoauth") ||
                 !strcmp(spec->name, "autocommands") ||
                 !strcmp(spec->name, "query") || !strcmp(spec->name, "set") ||
                 !strcmp(spec->name, "bind") || !strcmp(spec->name, "unbind") ||
                 !strcmp(spec->name, "bindings"))) {
        char legacy[128];
        const char *command = text;
        if (!strcmp(spec->name, "set") && (tail = ui_command_tail(text, "/set autoconnect"))) {
            if (strlen(tail) >= sizeof legacy - sizeof "/autoconnect ") {
                app_status(a, "%s", irc_tr("Usage: /set autoconnect on|off"));
                return 0;
            }
            (void)snprintf(legacy, sizeof legacy, "/autoconnect %s", tail);
            command = legacy;
        }
        int result = client_command(a, u, command, spec);
        config_wipe(legacy, sizeof legacy);
        if (result) clear_draft(u, source_view);
        return result;
    }
    if (!literal && ui_command_exact(text, "/clearinput")) {
        clear_input(a, u);
        return 1;
    }
    if (!literal && ui_command_exact(text, "/background")) {
        clear_draft(u, source_view);
        u->owner->background_requested = 1;
        return 1;
    }
    if (!literal && ui_command_exact(text, "/settings")) {
        clear_draft(u, source_view); open_settings(a, u); return 1;
    }
    if (!literal && ui_command_exact(text, "/unread")) {
        clear_draft(u, source_view); open_unread(a, u); return 1;
    }
    if (!literal && (ui_command_tail(text, "/servers") || ui_command_tail(text, "/sl"))) {
        if (!ui_command_exact(text, "/servers") && !ui_command_exact(text, "/sl")) {
            app_status(a, "%s", irc_tr("Usage: /servers (or /sl); neither accepts arguments."));
            return 0;
        }
        clear_draft(u, source_view);
        open_servers(a, u);
        return 1;
    }
    if (!literal && ((tail = ui_command_tail(text, "/dcc")) ||
                     (tail = ui_command_tail(text, "/transfers")))) {
        if (!*tail) {
            clear_draft(u, source_view);
            u->input_mode = 0;
            transfers_open(u->owner->transfers, u->slot);
            return 1;
        }
        ClientCommandOutput output;
        memset(&output, 0, sizeof output);
        output.client = u->owner->client;
        output.source = a;
        int result = dcc_command(&u->owner->dcc, u->owner->client, a, tail,
                                 client_command_output, &output);
        if (output.lines) app_status(a, "%s", result ? output.last : output.first);
        config_wipe(&output, sizeof output);
        if (result) clear_draft(u, source_view);
        return result;
    }
    if (!literal && !strncasecmp(text, "/keymap", 7) &&
        (text[7] == 0 || text[7] == ' ')) {
        const char *setting = text + 7;
        while (*setting == ' ') ++setting;
        if (*setting && strcmp(setting, "original") && strcmp(setting, "vim")) {
            app_status(a, "%s", irc_tr("Usage: /keymap [original|vim]. Use /bindings to customize keys in chat and the command prompt."));
            return 0;
        }
        app_status(a, "%s", irc_tr("Built-in F1..F6 open categories and Tab completes nicknames. /bindings customizes keys in chat and the command prompt."));
        clear_draft(u, source_view);
        return 1;
    }
    if (!literal && !strncasecmp(text, "/mouse", 6) &&
        (text[6] == 0 || text[6] == ' ')) {
        const char *setting = text + 6;
        while (*setting == ' ') ++setting;
        int enabled;
        if (!*setting) enabled = !a->cfg.mouse;
        else if (!strcmp(setting, "on")) enabled = 1;
        else if (!strcmp(setting, "off")) enabled = 0;
        else { app_status(a, "%s", irc_tr("Usage: /mouse [on|off]")); return 0; }
        set_mouse(a, u, enabled);
        clear_draft(u, source_view);
        return 1;
    }
    if (!literal && !strncasecmp(text, "/close", 6) &&
        (text[6] == 0 || text[6] == ' ')) {
        size_t index = resolve_close(a, text + 6);
        if (index == SIZE_MAX) {
            app_status(a, "%s", irc_tr("No such conversation; use /channels or /ql."));
            return 0;
        }
        u->close_command = from_command;
        return request_close(a, u, index, source_view);
    }
    if (!literal && !strncasecmp(text, "/users", 6) &&
        (text[6] == 0 || text[6] == ' ')) {
        const char *setting = text + 6;
        while (*setting == ' ') ++setting;
        if (!*setting || !strcmp(setting, "toggle") || !strcmp(setting, "on") || !strcmp(setting, "off")) {
            Config candidate;
            char error[512];
            app_config_effective(a, &candidate);
            candidate.user_list = !strcmp(setting, "on") ? 1 : !strcmp(setting, "off") ? 0 : !a->cfg.user_list;
            int applied = client_interface_apply(u->owner->client, &candidate, 0, error, sizeof error);
            config_wipe(&candidate, sizeof candidate);
            if (applied < 0) { app_status(a, "%s", error); return 0; }
            app_status(a, "User list %s.", a->cfg.user_list ? "on" : "off");
            clear_draft(u, source_view);
            return 1;
        }
    }
    if (!literal &&
        (!strcasecmp(text, "/users up") || !strcasecmp(text, "/users down"))) {
        Buffer *b = &a->buffers[a->current];
        if (text[7] == 'u' || text[7] == 'U') b->user_scroll = b->user_scroll > 10 ? b->user_scroll - 10 : 0;
        else if (b->user_scroll + 10 < b->user_count) b->user_scroll += 10;
        clear_draft(u, source_view); a->dirty = 1; return 1;
    }
    if (!literal && !strncasecmp(text, "/quit", 5) &&
        (text[5] == 0 || text[5] == ' ')) {
        const char *reason = text + 5;
        while (*reason == ' ') ++reason;
        if (strlen(reason) >= 256) {
            app_status(a, "%s", irc_tr("QUIT reason must be at most 255 UTF-8 bytes."));
            return 0;
        }
        if (!client_request_quit(u->owner->client, reason)) {
            app_status(a, "%s", irc_tr("QUIT reason could not be sent."));
            return 0;
        }
        clear_draft(u, source_view);
        return 1;
    }
    int r;
    if (literal) {
        Buffer *b = &a->buffers[source_view];
        if (b->kind != B_CHANNEL && b->kind != B_QUERY) {
            app_status(a, "%s", irc_tr("Pasted text is literal. Choose a channel or query to send it."));
            return 0;
        }
        if (b->kind == B_CHANNEL && !b->joined) {
            app_status(a, "%s", irc_tr("Join this channel before sending pasted text; your draft is preserved."));
            return 0;
        }
        r = app_plan(a, b->name, text, 0, &u->pending) ? 2 : 0;
    } else {
        u->submitting_view = source_view;
        r = app_command(a, text, &u->pending);
        source_view = u->submitting_view;
        u->submitting_view = SIZE_MAX;
    }
    if (r == 2) {
        u->pending_view = from_command ? a->current : source_view;
        u->pending_command = from_command;
        if (u->pending.count > 1) {
            u->confirming = 1;
            u->confirm_at = app_now_ms(a);
            a->dirty = 1;
            return 2;
        }
        r = app_send_plan(a, &u->pending) ? 1 : 0;
        if (r) clear_draft(u, source_view);
        if (!r || !from_command) {
            u->pending_command = 0;
            config_wipe(&u->pending, sizeof u->pending);
        }
    } else if (r == 1) clear_draft(u, source_view);
    else if (r == 3) {
        if (source_view != SIZE_MAX) clear_draft(u, source_view);
        open_channels(a, u);
    } else if (r == 4) {
        if (source_view != SIZE_MAX) clear_draft(u, source_view);
        open_pms(a, u);
    } else if (r == 5) {
        if (source_view != SIZE_MAX) clear_draft(u, source_view);
        int from_app = from_command && u->command_from_app;
        open_manual(a, u);
        u->manual_from_app = from_app;
    }
    else if (r == 6) {
        if (source_view != SIZE_MAX) clear_draft(u, source_view);
        open_directory(a, u);
    }
    a->dirty = 1;
    return r != 0;
}

static int submit_text(App *a, Ui *u, const char *text, int from_command)
{
    char expanded[ED_MAX_BYTES + 1];
    /* Pasted conversation text beginning with a slash remains literal text.
     * Only the dedicated command editor expands executable command names. */
    int changed = from_command ? commands_expand_alias(text, expanded, sizeof expanded) : 0;
    int result;
    if (changed < 0) { app_status(a, "%s", irc_tr("Command exceeds the input byte limit.")); result = 0; }
    else result = submit_text_command(a, u, changed ? expanded : text, from_command);
    config_wipe(expanded, sizeof expanded);
    return result;
}

static void submit(App *a, Ui *u)
{
    if (!input_guard_ready(a, u)) return;
    Editor *e = draft(a, u);
    char text[ED_MAX_BYTES + 1];
    if (!e || !e->len || ed_to_utf8(e, text, sizeof text) == SIZE_MAX) return;
    (void)submit_text(a, u, text, 0);
    config_wipe(text, sizeof text);
}

static void finish_command(App *a, Ui *u, int accepted)
{
    if (accepted) prompt_remember(u->command, a, u->pending_command ? &u->pending : NULL);
    prompt_close(u->command);
    u->command_target_lost = u->command_guard = 0;
    u->command_from_app = 0;
    u->input_mode = 1;
    u->nav_focus = FOCUS_APP;
    u->status_selected = u->status_cycle = 0;
    u->pending_command = u->close_command = u->command_limit_warned = 0;
    config_wipe(&u->pending, sizeof u->pending);
    invalidate_mouse(u);
    a->dirty = 1;
    if (!accepted) restore_command_origin(u);
    else if (u->summary.active && u->summary.hidden) free_readview(&u->summary);
}

static void command_action(App *a, Ui *u, PromptAction action)
{
    if (action == PROMPT_HELP) {
        (void)open_context_help(a, u);
    } else if (action == PROMPT_CANCEL) {
        finish_command(a, u, 0);
        app_program_notice(a, "Command cancelled; chat draft preserved.");
    } else if (action == PROMPT_RUN) {
        if (!input_guard_ready(a, u)) return;
        if (u->command_target_lost) {
            app_status(a, "%s", irc_tr("Original conversation closed. This preserved command cannot run; Esc cancels it."));
            return;
        }
        if (u->command_guard) {
            int64_t now = app_now_ms(a);
            int fresh = u->command_guard_drawn && u->command_guard_batch != u->owner->input_batch &&
                        now >= u->command_guard_after;
            /* A reader's Back/Enter must not become an unseen Run. The first
             * fresh activation acknowledges the restored command, and key
             * repeat never supplies the quiet interval for the next one. */
            u->command_guard_after = now + 100;
            if (!fresh) return;
            if (!u->command_guard_enter_seen) {
                u->command_guard_enter_seen = 1;
                app_status(a, "%s", irc_tr("Restored command ready. Review it, then press Enter again to run."));
                return;
            }
            u->command_guard = 0;
        }
        char text[ED_MAX_BYTES + 1];
        const Editor *e = prompt_const_editor(u->command);
        if (!e || ed_to_utf8(e, text, sizeof text) == SIZE_MAX) return;
        int result = 0;
        if (*text != '/' || !text[1]) app_status(a, "%s", irc_tr("Enter a command beginning with /, or Esc to return."));
        else result = submit_text(a, u, text, 1);
        config_wipe(text, sizeof text);
        if (result == 1) finish_command(a, u, 1);
    }
    a->dirty = 1;
}

static void insert_paste(App *a, Ui *u, wchar_t key)
{
    Editor *e = draft(a, u);
    if (!e) return;
    u->literal[a->current] = 1;
    if (key == L'\n' && u->paste_cr) { u->paste_cr = 0; return; }
    u->paste_cr = key == L'\r';
    if (key == L'\r') key = L'\n';
    if (key == L'\t') key = L' ';
    if (!ed_insert(e, key)) u->paste_rejected = 1;
}

static void end_paste(App *a, Ui *u)
{
    if (!u->paste) return;
    u->paste = 0;
    u->paste_cr = 0;
    if (u->paste_ignore) { u->paste_ignore = 0; return; }
    if (u->paste_rejected)
        app_status(a, "%s", irc_tr("Paste contained rejected controls or exceeded input limits; review before sending."));
    else app_status(a, "%s", irc_tr("Paste captured as literal text. Review, then press Enter to send."));
    u->paste_rejected = 0;
}

static void begin_paste(Ui *u)
{
    u->paste = 1;
    u->paste_rejected = 0;
    u->paste_cr = 0;
    u->paste_ignore = u->confirming || !main_screen(u);
    if (!u->paste_ignore) u->input_mode = 1;
}

static void scroll_history(App *a, size_t amount, int direction)
{
    size_t *scroll = &a->buffers[a->current].scroll;
    if (direction < 0) {
        if (*scroll < 1000000) *scroll += amount;
    } else *scroll = *scroll > amount ? *scroll - amount : 0;
}

static void park_hotkey_command(App *a, Ui *u, int restore_on_return)
{
    if (!prompt_active(u->command)) return;
    /* Only one command editor exists while a shortcut page is open. */
    if (u->hotkey_command) return;
    u->hotkey_command = u->command;
    u->command = NULL;
    u->hotkey_command_return = restore_on_return;
    if (!u->command_target_lost) u->hotkey_command_view = a->buffers[a->current].id;
}

static void restore_hotkey_target(App *a, Ui *u)
{
    const Editor *editor = prompt_const_editor(u->command);
    u->command_target_lost = 0;
    if (!editor || editor->len <= 1) return;
    for (size_t i = 0; i < a->buffer_count; ++i)
        if (a->buffers[i].id == u->hotkey_command_view) {
            app_switch(a, i);
            u->header_origin = SIZE_MAX;
            return;
        }
    /* A closed view can be replaced at the same array index. The stable ID,
     * never that index, determines whether a parked command still has a target. */
    u->command_target_lost = 1;
}

static void guard_hotkey_command(App *a, Ui *u)
{
    u->command_guard = 1;
    u->command_guard_drawn = u->command_guard_enter_seen = 0;
    u->command_guard_batch = u->owner->input_batch;
    u->command_guard_after = app_now_ms(a) + 100;
}

static void restore_hotkey_command(App *a, Ui *u)
{
    if (!u->hotkey_command || !u->hotkey_command_return ||
        u->bindings.active || u->context_help.active ||
        (u->summary.active && !u->summary.hidden) || u->nick_menu ||
        u->license || u->about || u->channels || u->manual ||
        u->owner->servers || u->owner->unread || settings_is_open(u) ||
        guided_active(u->owner->guided) || transfers_active(u->owner->transfers) ||
        u->confirming || u->quit_confirm || u->owner->close_confirm) return;
    if (u->command && prompt_active(u->command)) return;
    if (u->command) prompt_destroy(u->command);
    u->command = u->hotkey_command;
    u->hotkey_command = NULL;
    u->hotkey_command_return = 0;
    restore_hotkey_target(a, u);
    guard_hotkey_command(a, u);
    u->app_menu = u->input_mode = 0;
    if (u->summary.active) u->summary.hidden = 1;
    invalidate_mouse(u);
    app_status(a, "%s", u->command_target_lost ?
               "Original conversation closed. Command preserved but cannot run; Esc cancels it." :
               "Returned to the unfinished command at its original conversation; chat draft preserved.");
    a->dirty = 1;
}

static unsigned hotkey_code(wint_t key, int special)
{
    if (!special) {
        if (key > 0 && key <= 27) return (unsigned)key;
        if (key == 127) return CONFIG_KEY_BACKSPACE;
        return 0;
    }
    if (key >= KEY_F(1) && key <= KEY_F(12)) return CONFIG_KEY_F1 + (unsigned)(key - KEY_F(1));
    if ((key >= CONFIG_KEY_ALT + 'a' && key <= CONFIG_KEY_ALT + 'z') ||
        (key >= CONFIG_KEY_ALT + '0' && key <= CONFIG_KEY_ALT + '9')) return (unsigned)key;
    switch (key) {
    case KEY_UP: return CONFIG_KEY_UP;
    case KEY_DOWN: return CONFIG_KEY_DOWN;
    case KEY_LEFT: return CONFIG_KEY_LEFT;
    case KEY_RIGHT: return CONFIG_KEY_RIGHT;
    case KEY_HOME: return CONFIG_KEY_HOME;
    case KEY_END: return CONFIG_KEY_END;
    case KEY_PPAGE: return CONFIG_KEY_PAGE_UP;
    case KEY_NPAGE: return CONFIG_KEY_PAGE_DOWN;
    case KEY_IC: return CONFIG_KEY_INSERT;
    case KEY_DC: return CONFIG_KEY_DELETE;
    case KEY_BACKSPACE: return CONFIG_KEY_BACKSPACE;
    case KEY_BTAB: return CONFIG_KEY_SHIFT_TAB;
    case KEY_ENTER: return CONFIG_KEY_ENTER;
    default: return 0;
    }
}

static int custom_hotkey(App *a, Ui *u, wint_t key, int special)
{
    /* Frontend actions are deliberately unavailable in credential editors,
     * confirmations, readers and paste consumers. No binding can send a
     * message or execute user-supplied command text. */
    if (!custom_hotkey_scope(u) || capturing_paste(u) ||
        u->escape_len || u->paste_end_len || u->utf8_remaining) return 0;
    unsigned code = hotkey_code(key, special);
    int action = code ? config_hotkey_lookup(&a->cfg, code) : CONFIG_ACTION_NONE;
    if (action == CONFIG_ACTION_NONE) return 0;
    if (!input_guard_ready(a, u) ||
        (u->owner->input_batch && u->hotkey_block_batch == u->owner->input_batch) ||
        app_now_ms(a) < u->hotkey_after) return 1;
    arm_input_guard(a, u);
    a->dirty = 1;
    const Editor *command = prompt_active(u->command) ? prompt_const_editor(u->command) : NULL;
    if (command && command->len > 1 &&
        (action == CONFIG_ACTION_NEXT || action == CONFIG_ACTION_PREV ||
         action == CONFIG_ACTION_MENTIONS || action == CONFIG_ACTION_SERVERS ||
         action == CONFIG_ACTION_CHANNELS || action == CONFIG_ACTION_QUERIES ||
         action == CONFIG_ACTION_UNREAD || action == CONFIG_ACTION_SETTINGS ||
         action == CONFIG_ACTION_QUICKSTART)) {
        app_status(a, "%s", irc_tr("Finish or cancel the command before a shortcut changes its conversation or connection."));
        return 1;
    }
    switch (action) {
    case CONFIG_ACTION_COMPOSE:
        park_hotkey_command(a, u, 0);
        u->input_mode = 1;
        app_status(a, "%s", irc_tr("Conversation input focused. The Command action restores an unfinished command."));
        break;
    case CONFIG_ACTION_COMMAND:
        if (!prompt_active(u->command)) (void)open_command(a, u, "/");
        break;
    case CONFIG_ACTION_HELP:
        if (open_context_help(a, u)) {
            /* Context help keeps its prompt underneath rather than parking
             * it. Give that return the same review gate as other readers. */
            if (prompt_active(u->command)) guard_hotkey_command(a, u);
            break;
        }
        park_hotkey_command(a, u, 1);
        open_manual(a, u);
        break;
    case CONFIG_ACTION_BINDINGS:
        park_hotkey_command(a, u, 1);
        open_bindings(a, u, 0);
        break;
    case CONFIG_ACTION_MANUAL:
        park_hotkey_command(a, u, 1);
        open_manual(a, u);
        break;
    case CONFIG_ACTION_SUMMARY:
        park_hotkey_command(a, u, 1);
        (void)open_summary(a, u);
        break;
    case CONFIG_ACTION_SERVERS:
        park_hotkey_command(a, u, 1);
        open_servers(a, u);
        break;
    case CONFIG_ACTION_CHANNELS:
        park_hotkey_command(a, u, 1);
        open_channels(a, u);
        break;
    case CONFIG_ACTION_QUERIES:
        park_hotkey_command(a, u, 1);
        open_pms(a, u);
        break;
    case CONFIG_ACTION_UNREAD:
        park_hotkey_command(a, u, 1);
        open_unread(a, u);
        break;
    case CONFIG_ACTION_TRANSFERS:
        park_hotkey_command(a, u, 1);
        u->input_mode = 0;
        transfers_open(u->owner->transfers, u->slot);
        break;
    case CONFIG_ACTION_SETTINGS:
        park_hotkey_command(a, u, 1);
        open_settings(a, u);
        break;
    case CONFIG_ACTION_QUICKSTART:
        park_hotkey_command(a, u, 1);
        (void)open_guided(a, u, 1);
        break;
    case CONFIG_ACTION_MENTIONS:
        app_switch(a, 1);
        u->header_origin = SIZE_MAX;
        break;
    case CONFIG_ACTION_NEXT:
    case CONFIG_ACTION_PREV:
    case CONFIG_ACTION_CANCEL: {
        const ConfigHotkeyAction *binding = config_hotkey_action(action);
        SendPlan unused;
        memset(&unused, 0, sizeof unused);
        (void)app_command(a, binding->command, &unused);
        config_wipe(&unused, sizeof unused);
        u->header_origin = SIZE_MAX;
        break;
    }
    case CONFIG_ACTION_CLEARINPUT:
        if (prompt_active(u->command)) {
            /* An explicit clear action resets prompt selection/recall state
             * too, so a later arrow cannot resurrect the cleared draft. */
            (void)prompt_open(u->command, "/");
            u->command_target_lost = u->command_guard = 0;
            app_status(a, "%s", irc_tr("Command draft cleared; chat draft preserved."));
        } else clear_input(a, u);
        break;
    case CONFIG_ACTION_BACKGROUND:
        u->owner->background_requested = 1;
        break;
    case CONFIG_ACTION_QUIT:
        u->quit_confirm = 1;
        u->quit_confirm_at = app_now_ms(a);
        break;
    default: return 0;
    }
    restore_hotkey_command(a, u);
    nick_completion_reset(&u->completion);
    invalidate_mouse(u);
    a->dirty = 1;
    return 1;
}

static void handle_key(App *a, Ui *u, wint_t key, int special)
{
    Editor *e = draft(a, u);
    int enter = (special && key == KEY_ENTER) ||
                (!special && (key == '\r' || key == '\n'));
    if (!e) return;
    a->dirty = 1;
    if (special && key == KEY_RESIZE) {
        if (u->bindings.active) bindings_changed(a, u);
        u->command_guard_drawn = u->hotkey_guard_drawn = 0;
        invalidate_mouse(u); clearok(stdscr, TRUE); return;
    }
#if UI_MOUSE
    if (special && key == KEY_MOUSE) { handle_mouse(a, u); return; }
#endif
    if (!special && (key == 3 || key == 26) && !a->cfg.use_signals) {
        /* Never allow typed control bytes to act as quit confirmation.
         * A command composer is cancelled; a chat compose is cleared. */
        if (capturing_paste(u)) return;
        if (u->quit_confirm || u->app_menu) return;
        if (main_screen(u) && !prompt_active(u->command) && !u->confirming) {
            clear_input(a, u);
            return;
        }
        key = 27; /* Existing modal Escape handling owns cancellation. */
    }
    if (u->bindings.active) { bindings_key(a, u, key, special); return; }
    if (custom_hotkey(a, u, key, special)) return;
    if (u->context_help.active || (u->summary.active && !u->summary.hidden)) {
        reader_key(a, u, key, special);
        return;
    }
    if (!special && key == '?' && open_context_help(a, u)) return;
    if (guided_active(u->owner->guided)) {
        if (special && key == PASTE_BEGIN) guided_paste(u->owner->guided, 1);
        else if (special && key == PASTE_END) guided_paste(u->owner->guided, 0);
        else guided_key(u->owner->guided, a, key, special);
        guided_changed(a, u);
        return;
    }
    if (u->quit_confirm) {
        if (special && key == PASTE_BEGIN) { begin_paste(u); return; }
        if (special && key == PASTE_END) { end_paste(a, u); return; }
        if (u->paste) return;
        /* Job controls are handled by the terminal and signal handlers.
         * Do not turn a pasted control byte into a confirmation either. */
        if (!special && (key == 3 || key == 24 || key == 26 || key == 28)) return;
        if (!special && (key == 'y' || key == 'Y')) {
            if (app_now_ms(a) - u->quit_confirm_at >= 100) {
                if (!client_request_quit(u->owner->client, NULL))
                    app_status(a, "%s", irc_tr("Could not prepare IRC QUIT messages; session remains open."));
            }
        } else {
            u->quit_confirm = 0;
            invalidate_mouse(u);
            app_status(a, "");
        }
        return;
    }
    int capturing_paste = u->paste || settings_pasting(u->settings) ||
                          transfers_pasting(u->owner->transfers) || prompt_pasting(u->command);
    if (special && key >= CATEGORY_FIRST && key <= CATEGORY_LAST) {
        if (capturing_paste || u->confirming || u->owner->close_confirm) return;
        StatusLayout layout;
        status_layout(a, u, &layout);
        size_t selected = (size_t)(key - CATEGORY_FIRST);
        if (layout.present & (1u << selected)) {
            u->nav_focus = selected;
            activate_status(a, u);
            resume_body(u);
        } else app_status(a, "%s", irc_tr("That category is empty."));
        return;
    }
    /* A browser or field editor owns its own Escape stack. Category keys
     * above remain available, but global Escape must not skip these screens. */
    if (transfers_active(u->owner->transfers)) {
        if (special && key == PASTE_BEGIN) transfers_paste(u->owner->transfers, 1);
        else if (special && key == PASTE_END) transfers_paste(u->owner->transfers, 0);
        else transfers_key(u->owner->transfers, a, key, special);
        resume_body(u);
        return;
    }
    if (!capturing_paste && !u->confirming && !u->owner->close_confirm) {
        if (!special && key == 27) {
            if (u->app_menu) {
                u->quit_confirm = 1;
                u->quit_confirm_at = app_now_ms(a);
                invalidate_mouse(u);
            }
            else if (settings_is_open(u) || u->license || u->about) return_to_app(a, u);
            else if (u->manual) {
                if (u->manual_searching) cancel_manual_search(a, u);
                else leave_manual(a, u);
            }
            else if (prompt_active(u->command)) command_action(a, u, PROMPT_CANCEL);
            else if (main_screen(u)) {
                u->nav_focus = a->buffers[a->current].kind == B_QUERY ? FOCUS_QUERIES : FOCUS_APP;
                activate_status(a, u);
                resume_body(u);
            } else return_to_chat(a, u);
            app_status(a, "");
            return;
        }
        /* Category shortcuts open their body directly. Arrow keys belong to
         * that body or editor; there is no competing top-bar arrow mode. */
        resume_body(u);
        if (main_screen(u)) { u->input_mode = 1; u->header_origin = SIZE_MAX; }
        if (main_screen(u) && ((!special && key == '\t') || (special && key == KEY_BTAB))) {
            u->header_origin = SIZE_MAX;
            int completed = nick_complete(a, e, &u->completion, special ? -1 : 1);
            if (!completed) app_status(a, "%s", irc_tr("No matching nickname."));
            else if (completed < 0) app_status(a, "%s", irc_tr("Nickname does not fit; draft preserved."));
            return;
        }
    }
    if (u->nick_menu) {
        if (special && key == PASTE_BEGIN) { begin_paste(u); return; }
        if (special && key == PASTE_END) { end_paste(a, u); return; }
        if (u->paste) return;
        if ((special && key == KEY_UP) || (!special && key == 'k')) {
            if (u->nick_action) --u->nick_action;
        } else if ((special && key == KEY_DOWN) || (!special && key == 'j')) {
            if (u->nick_action < 3) ++u->nick_action;
        } else if (enter) nick_menu_action(a, u);
        return;
    }
    if (u->app_menu) {
        if (special && key == PASTE_BEGIN) { begin_paste(u); return; }
        if (special && key == PASTE_END) { end_paste(a, u); return; }
        if (u->paste) return;
        if (!special && key == 'b') { return_to_chat(a, u); return; }
        if (!special && key == '/') {
            if (open_command(a, u, "/")) { u->app_menu = 0; u->command_from_app = 1; }
        } else if ((special && key == KEY_UP) || (!special && key == 'k')) {
            if (u->app_selection) --u->app_selection;
        } else if ((special && key == KEY_DOWN) || (!special && key == 'j')) {
            if (u->app_selection + 1 < APP_ENTRIES) ++u->app_selection;
        } else if ((special && key == KEY_HOME) || (!special && key == 'g')) {
            u->app_selection = APP_SERVER;
        } else if ((special && key == KEY_END) || (!special && key == 'G')) {
            u->app_selection = APP_ENTRIES - 1;
        } else if (enter) app_menu_action(a, u);
        return;
    }
    if (settings_is_open(u) && !u->confirming) {
        int previous_mouse = a->cfg.mouse;
        if (special && key == PASTE_BEGIN) settings_begin_paste(u->settings);
        else if (special && key == PASTE_END) settings_end_paste(u->settings);
        else settings_key(u->settings, a, key, special);
        settings_changed(a, u, previous_mouse);
        return;
    }
    if (prompt_active(u->command) && !u->confirming && !u->paste) {
        if (special && key == PASTE_BEGIN) prompt_paste(u->command, a, 1);
        else if (special && key == PASTE_END) prompt_paste(u->command, a, 0);
        else command_action(a, u, prompt_key(u->command, a, key, special));
        return;
    }
    if (special && key == PASTE_BEGIN) { begin_paste(u); return; }
    if (special && key == PASTE_END) { end_paste(a, u); return; }
    if (u->paste) {
        if (!special && !u->paste_ignore) insert_paste(a, u, (wchar_t)key);
        return;
    }
    if (u->owner->servers) { server_key(a, u, key, special); return; }
    if (u->owner->unread) { unread_key(a, u, key, special); return; }
    if (u->confirming) {
        /* Confirmation cannot be satisfied by bytes already queued in the
         * same paste/key burst which opened the dialog. Escape and Enter
         * acknowledge warnings without entering navigation or sending. */
        if (app_now_ms(a) - u->confirm_at < 100) return;
        if (!special && key == 27) {
            int was_close = u->confirming == 2;
            int was_warning = u->confirming == 3;
            u->confirming = 0;
            if (!was_close && !was_warning) {
                u->pending_command = 0;
                config_wipe(&u->pending, sizeof u->pending);
            }
            app_program_notice(a, "%s", was_warning ? "Draft preserved; keep editing. Nothing sent." :
                       was_close ? "View kept open; draft preserved." : "Not sent; edit your draft.");
            return;
        }
        if (u->confirming == 3) {
            if (enter) {
                u->confirming = 0;
                app_program_notice(a, "Draft preserved; keep editing. Nothing sent.");
            }
            return;
        }
        if (!special && (key == 'y' || key == 'Y')) {
            if (u->confirming == 2) {
                int from_command = u->close_command;
                if (finish_close(a, u) && from_command) finish_command(a, u, 1);
                u->close_command = 0;
            }
            else {
                if (app_send_plan(a, &u->pending)) {
                    if (u->pending_command) finish_command(a, u, 1);
                    else clear_draft(u, u->pending_view);
                }
                u->pending_command = 0;
                u->confirming = 0;
                config_wipe(&u->pending, sizeof u->pending);
            }
        } else if (enter || (!special && (key == 'n' || key == 'N'))) {
            int was_close = u->confirming == 2;
            u->confirming = 0;
            if (!was_close) {
                u->pending_command = 0;
                config_wipe(&u->pending, sizeof u->pending);
            }
            app_program_notice(a, "%s", was_close ? "View kept open; draft preserved." : "Not sent; edit your draft.");
        }
        return;
    }
    if (u->about) {
        size_t step = LINES > 6 ? (size_t)(LINES - 6) : 1u;
        if (enter) {
            if (u->about_action == 0) return_to_app(a, u);
            else { capture_about(u); app_status(a, "%s", irc_tr("Resource snapshot refreshed.")); }
        } else if (!special && key == 'q') return_to_app(a, u);
        else if (!special && key == 'r') { capture_about(u); app_status(a, "%s", irc_tr("Resource snapshot refreshed.")); }
        else if ((special && (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_BTAB)) ||
                 (!special && (key == 'h' || key == 'l' || key == '\t'))) u->about_action = !u->about_action;
        else if ((special && key == KEY_UP) || (!special && key == 'k')) scroll_about(u, 1, -1);
        else if ((special && key == KEY_DOWN) || (!special && key == 'j')) scroll_about(u, 1, 1);
        else if (special && key == KEY_PPAGE) scroll_about(u, step, -1);
        else if (special && key == KEY_NPAGE) scroll_about(u, step, 1);
        else if ((special && key == KEY_HOME) || (!special && key == 'g')) u->about_scroll = 0;
        else if ((special && key == KEY_END) || (!special && key == 'G')) u->about_scroll = SIZE_MAX;
        return;
    }
    if (u->license) {
        size_t step = LINES > 5 ? (size_t)(LINES - 5) : 1u;
        if (enter || (!special && key == 'q')) return_to_app(a, u);
        else if ((special && key == KEY_UP) || (!special && key == 'k')) scroll_license(u, 1, -1);
        else if ((special && key == KEY_DOWN) || (!special && key == 'j')) scroll_license(u, 1, 1);
        else if (special && key == KEY_PPAGE) scroll_license(u, step, -1);
        else if (special && key == KEY_NPAGE) scroll_license(u, step, 1);
        else if ((special && key == KEY_HOME) || (!special && key == 'g')) u->license_scroll = 0;
        else if ((special && key == KEY_END) || (!special && key == 'G')) u->license_scroll = SIZE_MAX;
        return;
    }
    if (u->manual) {
        if (u->manual_searching) { manual_search_key(a, u, key, special); return; }
        int available = LINES - 7;
        size_t step = available > 0 ? (size_t)available : 1u;
        if (enter) run_manual_action(a, u);
        else if (!special && (key == 27 || key == 'q')) leave_manual(a, u);
        else if (!special && key == '/') begin_manual_search(a, u);
        else if (!special && (key == 'n' || key == 'N')) repeat_manual_search(a, u, key == 'n' ? 1 : -1);
        else if ((special && key == KEY_LEFT) || (!special && key == 'h')) move_manual_action(u, -1);
        else if ((special && key == KEY_RIGHT) || (!special && key == 'l')) move_manual_action(u, 1);
        else if ((special && key == KEY_UP) || (!special && key == 'k'))
            scroll_manual(a, 1, -1);
        else if ((special && key == KEY_DOWN) || (!special && key == 'j'))
            scroll_manual(a, 1, 1);
        else if (special && key == KEY_PPAGE) scroll_manual(a, step, -1);
        else if (special && key == KEY_NPAGE) scroll_manual(a, step, 1);
        else if ((special && key == KEY_HOME) || (!special && key == 'g'))
            a->buffers[2].scroll = SIZE_MAX;
        else if ((special && key == KEY_END) || (!special && key == 'G'))
            a->buffers[2].scroll = 0;
        return;
    }
    if (u->channels == 3) { directory_key(a, u, key, special); return; }
    if (u->channels) {
        int private_list = u->channels == 2;
        size_t count = u->chooser_drawn && u->chooser_kind == u->channels ? u->chooser_count : 0;
        if (!count) u->channel_selection = 0;
        else if (u->channel_selection >= count) u->channel_selection = count - 1;
        if (u->chooser_action >= chooser_action_count(u)) u->chooser_action = 0;
        if ((special && key == KEY_UP) || (!special && key == 'k')) {
            if (u->channel_selection) --u->channel_selection;
        } else if ((special && key == KEY_DOWN) || (!special && key == 'j')) {
            if (u->channel_selection + 1 < count) ++u->channel_selection;
        } else if ((special && key == KEY_LEFT) || (!special && key == 'h')) {
            move_chooser_action(u, -1);
        } else if ((special && key == KEY_RIGHT) || (!special && key == 'l')) {
            move_chooser_action(u, 1);
        } else if (special && key == KEY_DC) activate_chooser(a, u, CHOOSE_CLOSE);
        else if (enter) activate_chooser(a, u, chooser_action_kind(u, u->chooser_action));
        else if (!special && (key == 27 || key == 'q')) u->channels = 0;
        else if ((special && key == KEY_HOME) || (!special && key == 'g')) {
            u->channel_selection = 0;
        } else if ((special && key == KEY_END) || (!special && key == 'G')) {
            u->channel_selection = count ? count - 1 : 0;
        } else if (!special && key == '0' && !private_list) {
            u->channel_selection = 0;
        }
        else if (!special && key == 'c') activate_chooser(a, u, CHOOSE_CLOSE);
        else if (!special && key == 'p' && !private_list) activate_chooser(a, u, CHOOSE_PART);
        else if (!special && (key == 'o' || (key == 'n' && private_list)))
            activate_chooser(a, u, CHOOSE_NEW);
        return;
    }
    u->input_mode = 1;
    if (special) {
        switch (key) {
        case KEY_LEFT: ed_visual_left(e, u->input_width); break;
        case KEY_RIGHT: ed_visual_right(e, u->input_width); break;
        case KEY_UP: {
            ed_visual_move_vertical(e, -1, u->input_width);
            break;
        }
        case KEY_DOWN: ed_visual_move_vertical(e, 1, u->input_width); break;
        case KEY_HOME: ed_visual_home(e, u->input_width); break;
        case KEY_END: ed_visual_end(e, u->input_width); break;
        case KEY_BACKSPACE: (void)ed_backspace(e); break;
        case KEY_DC: (void)ed_delete(e); break;
        case KEY_ENTER: submit(a, u); break;
        case KEY_PPAGE: scroll_history(a, (size_t)(u->body_rows > 1 ? u->body_rows - 1 : 1), -1); break;
        case KEY_NPAGE: scroll_history(a, (size_t)(u->body_rows > 1 ? u->body_rows - 1 : 1), 1); break;
        default: break;
        }
        return;
    }
    switch (key) {
    case '\r': case '\n': submit(a, u); break;
    case 8: case 127: (void)ed_backspace(e); break;
    default:
        if (key == '/' && e->pos == 0) (void)open_command(a, u, "/");
        else if (key >= 32 && !ed_insert(e, (wchar_t)key))
            app_status(a, "%s", irc_tr("Input limit reached or unsupported control character."));
        break;
    }
}

/* A completed paste and a shortcut share one review boundary. Input that
 * arrived with the transition cannot activate its new state; neither a clock
 * delay alone nor an incomplete/undersized redraw supplies review credit.
 * Rejected key repeat extends the quiet interval instead of eventually
 * becoming an unintended Enter, Save, Run, or transfer confirmation. */
static void arm_input_guard(App *a, Ui *u)
{
    u->hotkey_guard = 1;
    u->hotkey_guard_drawn = 0;
    u->hotkey_guard_batch = u->owner->input_batch;
    u->hotkey_after = app_now_ms(a) + 100;
    invalidate_mouse(u);
}

/* After hotkey-driven navigation, require a
 * completed redraw and later input batch. Otherwise a held Enter could
 * confirm a control on a screen the user has not seen yet. */
static int input_guard_ready(App *a, Ui *u)
{
    if (!u->hotkey_guard) return 1;
    int64_t now = app_now_ms(a);
    if (!u->hotkey_guard_drawn || u->hotkey_guard_batch == u->owner->input_batch ||
        now < u->hotkey_after) {
        u->hotkey_after = now + 100;
        guided_input_discarded(u->owner->guided, now);
        if (!u->hotkey_guard_drawn) a->dirty = 1;
        return 0;
    }
    u->hotkey_guard = 0;
    return 1;
}

static void key_input(App *a, Ui *u, wint_t key, int special)
{
    if (!capturing_paste(u) &&
        !(special && (key == PASTE_BEGIN || key == PASTE_END || key == KEY_RESIZE)) &&
        !input_guard_ready(a, u)) {
#if UI_MOUSE
        if (special && key == KEY_MOUSE) {
            invalidate_mouse(u); handle_mouse(a, u); a->dirty = 1;
        }
#endif
        return;
    }
    if (!((!special && key == '\t') || (special && (key == KEY_BTAB || key == KEY_RESIZE))))
        nick_completion_reset(&u->completion);
    size_t view = a->current;
    int input_mode = u->input_mode;
    int shortcut_labels = a->cfg.shortcut_labels;
    unsigned panel = mouse_panel(u);
    unsigned settings_state = settings_mouse_state(u->settings);
    unsigned transfer_state = transfers_mouse_state(u->owner->transfers);
    unsigned guided_state = guided_mouse_state(u->owner->guided);
    size_t server_selection = u->owner->server_selection, server_action_before = u->owner->server_action;
    size_t channel_selection = u->channel_selection, chooser_action_before = u->chooser_action;
    size_t unread_selection = u->owner->unread_selection, unread_action_before = u->owner->unread_action;
    if (special && key == PASTE_END) {
        /* Every paste consumer uses the same gate. Guard ordinary keys and
         * mouse activation as well as shortcuts, including command Run and
         * modal Save/transfer actions after an exact closing marker. */
        u->hotkey_block_batch = u->owner->input_batch;
        arm_input_guard(a, u);
    }
    handle_key(a, u, key, special);
    restore_hotkey_command(a, u);
    if (view != a->current && !u->status_selected && main_screen(u)) u->header_origin = SIZE_MAX;
    Editor *e = u->drafts[a->current];
    if (e && !e->len) u->literal[a->current] = 0;
    update_input_warning(a, u);
    /* A typed action can change screens or a list's selected target/action
     * before the next redraw. Compact footers reuse the same cells for every
     * action; queued coordinates must not operate a choice never displayed. */
    if (view != a->current || input_mode != u->input_mode || mouse_panel(u) != panel ||
        shortcut_labels != a->cfg.shortcut_labels ||
        server_selection != u->owner->server_selection || server_action_before != u->owner->server_action ||
        channel_selection != u->channel_selection || chooser_action_before != u->chooser_action ||
        unread_selection != u->owner->unread_selection || unread_action_before != u->owner->unread_action ||
        settings_mouse_state(u->settings) != settings_state ||
        transfers_mouse_state(u->owner->transfers) != transfer_state ||
        guided_mouse_state(u->owner->guided) != guided_state)
        invalidate_mouse(u);
}

/* Curses owns complete terminal keys. This bounded fallback retains split
 * CSI/SS3 keys and paste markers across polls, without turning unknown
 * sequences into an Escape action followed by editable or executable text. */
static void flush_escape(App *a, Ui *u, int force)
{
    int grace = u->escape_len == 1 ? UI_ESCAPE_MS : UI_ESCAPE_CONTINUATION_MS;
    if (!u->escape_len || (!force && app_now_ms(a) - u->escape_at < grace)) return;
    int standalone = u->escape_len == 1;
    u->escape_len = 0;
    if (standalone) key_input(a, u, 27, 0);
}

static int fallback_escape(App *a, Ui *u, int key)
{
    static const struct { const char *sequence; int key; } terminal_keys[] = {
        {"\033[200~", PASTE_BEGIN}, {"\033[201~", PASTE_END},
        {"\033[A", KEY_UP}, {"\033OA", KEY_UP},
        {"\033[B", KEY_DOWN}, {"\033OB", KEY_DOWN},
        {"\033[C", KEY_RIGHT}, {"\033OC", KEY_RIGHT},
        {"\033[D", KEY_LEFT}, {"\033OD", KEY_LEFT},
        {"\033[H", KEY_HOME}, {"\033OH", KEY_HOME},
        {"\033[1~", KEY_HOME}, {"\033[7~", KEY_HOME},
        {"\033[F", KEY_END}, {"\033OF", KEY_END},
        {"\033[4~", KEY_END}, {"\033[8~", KEY_END},
        {"\033[2~", KEY_IC}, {"\033[3~", KEY_DC},
        {"\033[5~", KEY_PPAGE}, {"\033[6~", KEY_NPAGE}, {"\033[Z", KEY_BTAB},
        {"\033OP", KEY_F(1)}, {"\033OQ", KEY_F(2)},
        {"\033OR", KEY_F(3)}, {"\033OS", KEY_F(4)},
        {"\033[11~", KEY_F(1)}, {"\033[12~", KEY_F(2)},
        {"\033[13~", KEY_F(3)}, {"\033[14~", KEY_F(4)},
        {"\033[15~", KEY_F(5)}, {"\033[17~", KEY_F(6)},
        {"\033[18~", KEY_F(7)}, {"\033[19~", KEY_F(8)},
        {"\033[20~", KEY_F(9)}, {"\033[21~", KEY_F(10)},
        {"\033[23~", KEY_F(11)}, {"\033[24~", KEY_F(12)},
        {"\033[[A", KEY_F(1)}, {"\033[[B", KEY_F(2)},
        {"\033[[C", KEY_F(3)}, {"\033[[D", KEY_F(4)}, {"\033[[E", KEY_F(5)}
    };
    if (key == KEY_RESIZE) return 0;
    if (key >= KEY_MIN) { flush_escape(a, u, 1); return 0; }
    if (key == 27) {
        flush_escape(a, u, 1);
        if (u->utf8_remaining) {
            u->utf8_remaining = 0;
            app_status(a, "%s", irc_tr("Incomplete UTF-8 input ignored before Escape."));
        }
        u->escape[0] = '\033';
        u->escape[1] = 0;
        u->escape_len = 1;
        u->escape_at = app_now_ms(a);
        return 1;
    }
    if (!u->escape_len) return 0;
    if (u->escape_len == 1 && key != '[' && key != 'O') {
        u->escape_len = 0;
        if ((key >= 'a' && key <= 'z') || (key >= '0' && key <= '9'))
            key_input(a, u, (wint_t)(CONFIG_KEY_ALT + (unsigned)key), 1);
        /* Unsupported/unbound Alt combinations are inert. */
        return 1;
    }
    if (key < 0 || key >= 128 || (size_t)u->escape_len + 1 >= sizeof u->escape) {
        u->escape_len = 0;
        return 1;
    }
    u->escape[u->escape_len++] = (char)key;
    u->escape[u->escape_len] = 0;
    u->escape_at = app_now_ms(a);
    int prefix = 0;
    for (size_t i = 0; i < sizeof terminal_keys / sizeof terminal_keys[0]; ++i) {
        if (!strcmp(u->escape, terminal_keys[i].sequence)) {
            u->escape_len = 0;
            key_input(a, u, (wint_t)terminal_keys[i].key, 1);
            return 1;
        }
        if (!strncmp(terminal_keys[i].sequence, u->escape, (size_t)u->escape_len)) prefix = 1;
    }
    if (prefix) return 1;
    /* Consume an unsupported CSI/SS3 sequence through its final byte. */
    if (u->escape_len >= 3 && key >= 0x40 && key <= 0x7e) u->escape_len = 0;
    return 1;
}

/* Input belongs to the active modal consumer until its paste ends. Check
 * every owner before decoding an Escape prefix or dispatching a shortcut. */
static int capturing_paste(const Ui *u)
{
    return u->bindings.paste || u->paste || settings_pasting(u->settings) || prompt_pasting(u->command) ||
           transfers_pasting(u->owner->transfers) || guided_pasting(u->owner->guided) ||
           u->summary.paste || u->context_help.paste;
}

/* Even with define_key(), a paste terminator split beyond curses' Escape
 * timeout arrives as ordinary bytes. Retain only its fixed prefix while a
 * paste consumer owns input. Near-matches remain literal paste content; the
 * normal Escape/function-key decoder is unchanged outside a paste. */
static int terminal_paste_end(App *a, Ui *u, int key)
{
    static const char ending[] = "\033[201~";
    if (!capturing_paste(u)) {
        u->paste_end_len = 0;
        return 0;
    }
    if (key == KEY_RESIZE) return 0;
    if (key == 27 && u->utf8_remaining) {
        u->utf8_remaining = 0;
        app_status(a, "%s", irc_tr("Incomplete UTF-8 input ignored before a terminal key."));
    }
    if (key >= 0 && key < 128 && key == (unsigned char)ending[u->paste_end_len]) {
        if (++u->paste_end_len == sizeof ending - 1u) {
            u->paste_end_len = 0;
            key_input(a, u, PASTE_END, 1);
        }
        return 1;
    }
    size_t pending = u->paste_end_len;
    u->paste_end_len = 0;
    for (size_t i = 0; i < pending; ++i) key_input(a, u, (wint_t)(unsigned char)ending[i], 0);
    if (key == 27) {
        u->paste_end_len = 1;
        return 1;
    }
    return 0;
}

/* A terminal may deliver one UTF-8 scalar in several reads. Wide get_wch()
 * can discard a partial sequence when its timeout expires. Narrow getch()
 * retains curses key decoding, while this state persists between polls. */
static void terminal_byte(App *a, Ui *u, int key)
{
    /* Ctrl-D never exits or edits input. It can only decline the F1 quit
     * question, without disturbing a partial UTF-8/paste-marker sequence.
     * Terminal signal mappings are handled before a byte reaches here. */
    if (key == 4) {
        if (custom_hotkey(a, u, 4, 0)) return;
        if (u->quit_confirm) key_input(a, u, 4, 0);
        return;
    }
    if (terminal_paste_end(a, u, key)) return;
    if (fallback_escape(a, u, key)) return;
    if (key >= KEY_MIN) {
        if (key != KEY_RESIZE && u->utf8_remaining) {
            u->utf8_remaining = 0;
            app_status(a, "%s", irc_tr("Incomplete UTF-8 input ignored before a terminal key."));
        }
        key_input(a, u, (wint_t)key, 1);
        return;
    }
    unsigned char c = (unsigned char)key;
    if (!terminal_utf8 && c >= 128) {
        if (u->paste && !u->paste_ignore) u->paste_rejected = 1;
        app_status(a, "%s", irc_tr("ASCII terminal mode: non-ASCII input rejected; select a UTF-8 LC_CTYPE."));
        return;
    }
    if (u->utf8_remaining) {
        if ((c & 0xc0u) == 0x80u) {
            u->utf8_value = (u->utf8_value << 6) | (uint32_t)(c & 0x3fu);
            if (--u->utf8_remaining == 0) {
                uint32_t value = u->utf8_value;
                if (value >= u->utf8_minimum && value <= 0x10ffffu &&
                    !(value >= 0xd800u && value <= 0xdfffu))
                    key_input(a, u, (wint_t)value, 0);
                else app_status(a, "%s", irc_tr("Invalid UTF-8 input ignored."));
            }
            return;
        }
        u->utf8_remaining = 0;
        app_status(a, "%s", irc_tr("Incomplete UTF-8 input ignored."));
        /* Recover at this byte, so a following ordinary key is not lost. */
    }
    if (c < 0x80u) key_input(a, u, (wint_t)c, 0);
    else if (c >= 0xc2u && c <= 0xdfu) {
        u->utf8_value = (uint32_t)(c & 0x1fu);
        u->utf8_minimum = 0x80u; u->utf8_remaining = 1;
    } else if (c >= 0xe0u && c <= 0xefu) {
        u->utf8_value = (uint32_t)(c & 0x0fu);
        u->utf8_minimum = 0x800u; u->utf8_remaining = 2;
    } else if (c >= 0xf0u && c <= 0xf4u) {
        u->utf8_value = (uint32_t)(c & 0x07u);
        u->utf8_minimum = 0x10000u; u->utf8_remaining = 3;
    } else app_status(a, "%s", irc_tr("Invalid UTF-8 input ignored."));
}

static void destroy_ui(Ui *u)
{
    if (!u) return;
    for (size_t i = 0; i < APP_BUFFERS; ++i) {
        if (u->drafts[i]) config_wipe(u->drafts[i], sizeof *u->drafts[i]);
        free(u->drafts[i]);
    }
    settings_destroy(u->settings);
    prompt_destroy(u->command);
    prompt_destroy(u->hotkey_command);
    free_readview(&u->summary);
    free_readview(&u->context_help);
    if (u->manual_search) config_wipe(u->manual_search, sizeof *u->manual_search);
    free(u->manual_search);
    free(u->text_hits);
    config_wipe(u, sizeof *u);
    free(u);
}

static Ui *get_ui(UiSet *all, size_t index, char *error, size_t cap)
{
    App *a = client_get_app(all->client, index, error, cap);
    if (!a) return NULL;
    if (all->states[index]) return all->states[index];
    Ui *u = calloc(1, sizeof *u);
    if (!u) { snprintf(error, cap, "%s", irc_tr("Cannot allocate this server's interface.")); return NULL; }
    u->owner = all;
    u->slot = index;
    u->nav_focus = FOCUS_APP;
    u->input_mode = 1;
    u->submitting_view = u->close_source = u->warning_view = u->manual_view = u->header_origin = SIZE_MAX;
    a->view_closed = view_closed;
    a->view_context = u;
    all->states[index] = u;
    return u;
}

static int incoming_offer(void *context, const char *nick, const char *body)
{
    OfferContext *offer = context;
    UiSet *all = offer->owner;
    ClientSlot *slot = &all->client->slots[offer->slot];
    uint64_t id;
    char error[256];
    if (!dcc_offer(all->dcc, slot->token, nick, body, &id, error, sizeof error)) {
        {
            char shown[1024];
            const IrcDisplayField fields[] = {{"nick", nick}, {"error", error}};
            if (irc_display_render(shown, sizeof shown,
                    "DCC offer from {nick} rejected: {error}", fields, 2))
                app_status(slot->app, "%s", shown);
            else
                app_status(slot->app, "DCC offer from %s rejected: %s", nick, error);
        }
        return 1;
    }
    const DccInfo *info = dcc_get(all->dcc, id);
    app_log(slot->app, 0, "DCC file offer #%llu from %s: %s (%u bytes). Open /dcc to review.",
            (unsigned long long)id, nick, info ? info->filename : "file", info ? info->size : 0);
    {
        char shown[1024];
        const IrcDisplayField fields[] = {{"nick", nick}};
        if (irc_display_render(shown, sizeof shown,
                "File offer from {nick}. Open Transfers or /dcc to review.", fields, 1))
            app_status(slot->app, "%s", shown);
        else
            app_status(slot->app, "File offer from %s. Open Transfers or /dcc to review.", nick);
    }
    return 1;
}

static void attach_offers(UiSet *all)
{
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        App *a = all->client->slots[i].app;
        if (a) {
            a->dcc_offer = incoming_offer;
            a->dcc_context = &all->offers[i];
        }
    }
}

static void remove_cancelled_offers(UiSet *all)
{
    for (size_t i = 0; i < dcc_count(all->dcc); ++i) {
        const DccInfo *info = dcc_at(all->dcc, i);
        if (!info->outgoing || (info->state != DCC_DONE && info->state != DCC_REJECTED &&
            info->state != DCC_CANCELLED && info->state != DCC_FAILED)) continue;
        for (size_t j = 0; j < CLIENT_MAX_SERVERS; ++j) {
            ClientSlot *slot = &all->client->slots[j];
            if (slot->used && slot->token == info->server_token && slot->app)
                (void)app_cancel_dcc_offer(slot->app, info->id);
        }
    }
}

/* Apply a scheduled close/switch outside the key handler: the App and Ui
 * referenced by that handler remain alive throughout the entire call. */
static int apply_session_actions(UiSet *all, App **current_app, Ui **current_ui)
{
    App *a = *current_app;
    Ui *u = *current_ui;
    int guarded_switch = u && u->hotkey_guard;
    Config config;
    char error[512];
    int changed = 0;
    int result = settings_take_new(u->settings, &config);
    if (result) {
        size_t index;
        if (client_add(all->client, &config, &index, error, sizeof error) < 0) app_status(a, "%s", error);
        else {
            all->switch_to = index;
            if (result == 2 && client_connect(all->client, index, error, sizeof error) < 0)
                app_status(a, "%s", error);
        }
        config_wipe(&config, sizeof config);
    }
    if (all->close_slot != SIZE_MAX) {
        size_t index = all->close_slot;
        uint64_t token = all->client->slots[index].token;
        all->close_slot = SIZE_MAX;
        if (client_close(all->client, index, error, sizeof error) < 0) app_status(a, "%s", error);
        else {
            dcc_cancel_server(all->dcc, token);
            if (u->slot == index) {
                configure_mouse(u, 0);
                *current_ui = u = NULL;
                *current_app = a = NULL;
            }
            destroy_ui(all->states[index]);
            all->states[index] = NULL;
            all->servers = 0;
            all->switch_to = all->client->current;
            changed = 1;
        }
    }
    if (all->switch_to != SIZE_MAX) {
        size_t index = all->switch_to;
        all->switch_to = SIZE_MAX;
        Ui *next = get_ui(all, index, error, sizeof error);
        if (!next) {
            if (a) app_status(a, "%s", error);
            else return -1;
        } else {
            if (u && next != u) { invalidate_mouse(u); configure_mouse(u, 0); }
            if (a) app_set_visible(a, 0);
            all->client->current = index;
            *current_ui = u = next;
            *current_app = a = all->client->slots[index].app;
            if (guarded_switch) arm_input_guard(a, u);
            /* A server/unread row is a committed navigation action. Drop any
             * old header preview in the destination before applying its view,
             * so the next Help/Escape round trip cannot restore a stale chat. */
            return_to_chat(a, u);
            invalidate_mouse(u);
            configure_mouse(u, a->cfg.mouse);
            if (all->switch_view != SIZE_MAX && all->switch_view < a->buffer_count) {
                u->input_mode = 0;
                app_switch(a, all->switch_view);
            }
            a->dirty = 1;
            changed = 1;
        }
    }
    all->switch_view = SIZE_MAX;
    attach_offers(all);
    remove_cancelled_offers(all);
    return changed;
}

static void update_visibility(UiSet *all, App *a, const Ui *u)
{
    int main_visible = !background_detached(all->background) && stdscr &&
        LINES >= 9 && COLS >= 20 && main_screen(u) && !u->confirming;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        App *session = all->client->slots[i].app;
        if (session) app_set_visible(session, session == a && main_visible);
    }
}

static void release_terminal(App *a, Ui *u)
{
    arm_input_guard(a, u);
    u->command_guard_drawn = u->bindings.drawn = 0;
    /* Terminal input sequences cannot continue on a different terminal.
     * Complete draft characters and editor positions remain untouched. */
    u->utf8_remaining = u->paste = u->paste_ignore = u->paste_cr = 0;
    u->paste_end_len = 0;
    u->escape_len = 0;
    u->summary.paste = u->context_help.paste = 0;
    if (prompt_pasting(u->command)) prompt_paste(u->command, a, 0);
    if (settings_pasting(u->settings)) settings_end_paste(u->settings);
    if (guided_pasting(u->owner->guided)) guided_paste(u->owner->guided, 0);
    if (transfers_pasting(u->owner->transfers)) transfers_paste(u->owner->transfers, 0);
    if (!terminal_screen) return;
    configure_mouse(u, 0);
    paste_mode(0);
    (void)curs_set(1);
    terminal_stop();
}

static int detach_lost_terminal(UiSet *all, App *a, Ui *u, char *error, size_t capacity)
{
    /* There may be no reader left on the old tty. Redirect all standard
     * descriptors and release the controller before curses flushes buffered
     * presentation or tries to drain that terminal during endwin(). */
    if (!background_detach(all->background, &stop_requested, error, capacity)) return 0;
    client_set_backgrounded(all->client, 1);
    all->background_requested = 0;
    suspend_requested = 0;
    release_terminal(a, u);
    return 1;
}

static int restore_terminal(App *a, Ui *u)
{
    const ClientSlot *primary = &u->owner->client->slots[0];
    const Config *cfg = primary->app ? &primary->app->cfg : &primary->seed;
    if (!terminal_start(cfg->use_signals)) return 0;
    wtimeout(stdscr, 0);
    paste_mode(1);
    configure_mouse(u, a->cfg.mouse);
    clearok(stdscr, TRUE);
    invalidate_mouse(u);
    a->dirty = 1;
    return 1;
}

static void process_server_input(UiSet *all, const struct pollfd *fds,
                                 const size_t *indexes, int poll_result)
{
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        App *session = all->client->slots[i].app;
        if (session) {
            /* Terminal loss or suspend can share this poll with IRC
             * input. Those records must remain unread after detach. */
            if (background_detached(all->background)) app_set_visible(session, 0);
            app_network(session, poll_result >= 0 && indexes[i] != SIZE_MAX ?
                        fds[indexes[i]].revents : 0);
        }
    }
}

/* The single main loop owns UI lifetimes and signal restoration. Process
 * worker/terminal control before ordinary keys, but service every App and
 * DCC timer on timeout-only turns as well. A frontend handoff returns 1000
 * without freeing caller-owned session managers or the borrowed Client. */
static int ui_run_mode(Client *client, int start_background,
                       int primary_connect, int no_connect,
                       Background *shared_background, DccManager *shared_dcc,
                       int *next_frontend)
{
    int own_background = shared_background == NULL;
    int own_dcc = shared_dcc == NULL;
    int switch_frontend = 0;
    if (next_frontend) *next_frontend = BACKGROUND_FRONTEND_CURRENT;
    char error[512] = "Cannot initialize the full interface.";
    UiSet *all = calloc(1, sizeof *all);
    if (!all) return 1;
    all->client = client;
    all->started_at = net_now_ms();
    all->switch_to = all->close_slot = all->switch_view = SIZE_MAX;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        all->offers[i].owner = all;
        all->offers[i].slot = i;
    }
    all->dcc = shared_dcc ? shared_dcc : dcc_create(client->test_mode);
    all->transfers = transfers_create(client, all->dcc);
    all->background = shared_background ? shared_background : background_create();
    Ui *u = get_ui(all, client->current, error, sizeof error);
    const ClientSlot *primary = &client->slots[0];
    const Config *primary_cfg = primary->app ? &primary->app->cfg : &primary->seed;
    if (!all->dcc || !all->transfers || !all->background || !u ||
        (!start_background && !terminal_start(primary_cfg->use_signals))) {
        if (start_background) fprintf(stderr, "vipirc: background startup failed: %s\n", error);
        for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) destroy_ui(all->states[i]);
        guided_free(all->guided);
        transfers_free(all->transfers);
        if (own_dcc) dcc_free(all->dcc);
        if (own_background) background_free(all->background);
        free(all);
        return 1;
    }
    App *a = client->slots[client->current].app;
    if (!start_background && !background_detached(all->background)) client_set_backgrounded(client, 0);
    if (*client->catalog_error) app_status(a, "%s", client->catalog_error);
    const int signals[] = {SIGINT, SIGQUIT, SIGTERM, SIGHUP, SIGTSTP};
    struct sigaction act, old[sizeof signals / sizeof signals[0]];
    size_t installed = 0;
    int input_pending = 0, failed = 0;
    memset(&act, 0, sizeof act);
    sigemptyset(&act.sa_mask);
    stop_requested = hangup_requested = suspend_requested = 0;
    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; ++i) {
        act.sa_handler = signals[i] == SIGTSTP ? suspend_signal : stop_signal;
        if (sigaction(signals[i], &act, &old[i]) < 0) {
            failed = 1;
            break;
        }
        ++installed;
    }
    if (start_background) {
        /* Create the owned endpoint and fork before any resolver, credential
         * helper or IRC connection starts. Curses is initialized only when
         * a terminal is attached, using the normal full-screen restore path. */
        if (installed != sizeof signals / sizeof signals[0]) {
            snprintf(error, sizeof error, "%s", irc_tr("Cannot initialize signal handlers."));
            failed = 1;
        } else if (stop_requested || hangup_requested) {
            snprintf(error, sizeof error, "%s", irc_tr("Background startup interrupted."));
            failed = 1;
        } else {
            fputs("Not vegetarian?  www.bloodguiltcurse.net will change your mind\n", stderr);
            if (!background_prepare(all->background, client, error, sizeof error) ||
                !background_detach(all->background, &stop_requested, error, sizeof error)) failed = 1;
            else {
                client_set_backgrounded(client, 1);
                (void)client_startup(client, primary_connect, no_connect, error, sizeof error);
            }
        }
        if (failed) fprintf(stderr, "vipirc: background startup failed: %s\n", error);
    } else {
        wtimeout(stdscr, 0);
        paste_mode(1);
        configure_mouse(u, a->cfg.mouse);
    }
    attach_offers(all);
    a->dirty = 1;
    while (!a->quit && !stop_requested && !failed) {
        if (hangup_requested) {
            hangup_requested = 0;
            if (stop_requested) break;
            if (!background_managed(all->background)) {
                stop_requested = SIGHUP;
                break;
            }
            if (!detach_lost_terminal(all, a, u, error, sizeof error)) {
                if (!stop_requested) failed = 1;
                break;
            }
            input_pending = 0;
        }
        if (all->background_requested) {
            all->background_requested = 0;
            /* The readiness proof is adjacent to fork: no event handling can
             * start a new resolver or GPG child between these two calls. */
            if (!background_prepare(all->background, client, error, sizeof error))
                app_status(a, "%s", error);
            else {
                release_terminal(a, u);
                input_pending = 0;
                if (!background_detach(all->background, &stop_requested, error, sizeof error)) {
                    if (stop_requested) break;
                    if (!restore_terminal(a, u)) { failed = 1; break; }
                    app_status(a, "%s", error);
                } else client_set_backgrounded(client, 1);
            }
        }
        if (suspend_requested) {
            suspend_requested = 0;
            if (background_managed(all->background)) {
                if (!background_detached(all->background)) {
                    release_terminal(a, u);
                    background_suspend(all->background);
                }
            } else {
                release_terminal(a, u);
                signal(SIGTSTP, SIG_DFL); raise(SIGTSTP);
                act.sa_handler = suspend_signal; sigaction(SIGTSTP, &act, NULL);
                if (!restore_terminal(a, u)) { failed = 1; break; }
            }
        }
        if (!background_detached(all->background)) flush_escape(a, u, 0);
        if (guided_tick(all->guided, app_now_ms(a))) {
            guided_changed(a, u);
            invalidate_mouse(u);
            a->dirty = 1;
        }
        /* An async draft commit can queue a session switch without a key
         * event. Do its terminal work only when this session is attached. */
        if (!background_detached(all->background) && all->switch_to != SIZE_MAX) {
            int changed = apply_session_actions(all, &a, &u);
            if (changed < 0) {
                failed = 1;
                break;
            }
            if (changed) input_pending = 1;
        }
        update_visibility(all, a, u);
        if (!background_detached(all->background) && transfers_tick(all->transfers)) {
            invalidate_mouse(u);
            a->dirty = 1;
        }
        for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
            App *session = client->slots[i].app;
            if (session && session != a && session->dirty) { session->dirty = 0; a->dirty = 1; }
        }
        if (!background_detached(all->background) && !client->session_notice_shown &&
            main_screen(u) && LINES >= 9 && COLS >= 20) {
            /* Use ordinary local history, leaving draft and error/status
             * output alone. The Client flag survives frontend switches. */
            app_log(a, a->current, "%s", CLIENT_SESSION_NOTICE);
            client->session_notice_shown = 1;
        }
        if (a->dirty && !background_detached(all->background)) draw(a, u);
        if (!background_detached(all->background) && ferror(stdout)) {
            if (!background_managed(all->background) ||
                !detach_lost_terminal(all, a, u, error, sizeof error)) { failed = 1; break; }
            input_pending = 0;
        }
        /* Readiness and deadlines are collected from all live components. */
        struct pollfd fds[2 + BACKGROUND_POLL_CAP + CLIENT_MAX_SERVERS + DCC_MAX_ACTIVE];
        size_t server_poll_index[CLIENT_MAX_SERVERS];
        uint64_t transfer_ids[DCC_MAX_ACTIVE];
        memset(fds, 0, sizeof fds);
        fds[0].fd = background_detached(all->background) ? -1 : STDIN_FILENO;
        fds[0].events = POLLIN;
        fds[1].fd = background_detached(all->background) ? -1 : STDOUT_FILENO;
        size_t offset = 2;
        int timeout = 1000;
        for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
            App *session = client->slots[i].app;
            int fd = session ? app_fd(session) : -1;
            server_poll_index[i] = SIZE_MAX;
            /* poll's entry count is itself limited by RLIMIT_NOFILE on
             * some systems; inactive slots must not consume that count. */
            if (fd >= 0) {
                server_poll_index[i] = offset;
                fds[offset].fd = fd;
                fds[offset++].events = app_events(session);
            }
            if (session) {
                int next = app_timeout(session);
                if (next < timeout) timeout = next;
            }
        }
        size_t transfer_count = dcc_pollfds(all->dcc, fds + offset, transfer_ids, DCC_MAX_ACTIVE);
        size_t background_offset = offset + transfer_count;
        size_t background_count = background_pollfds(all->background, fds + background_offset, BACKGROUND_POLL_CAP);
        int next_transfer = dcc_timeout(all->dcc, app_now_ms(a));
        if (next_transfer >= 0 && next_transfer < timeout) timeout = next_transfer;
        int next_picker = background_detached(all->background) ? -1 : transfers_timeout(all->transfers);
        if (next_picker >= 0 && next_picker < timeout) timeout = next_picker;
        int next_guided = guided_timeout(all->guided);
        if (next_guided >= 0 && next_guided < timeout) timeout = next_guided;
        if (u->escape_len) {
            int grace = u->escape_len == 1 ? UI_ESCAPE_MS : UI_ESCAPE_CONTINUATION_MS;
            int64_t remaining = grace - (app_now_ms(a) - u->escape_at);
            if (remaining < 0) remaining = 0;
            if (remaining < timeout) timeout = (int)remaining;
        }
        timeout = background_timeout(all->background, timeout);
        if (input_pending) timeout = 0;
        int p = poll(fds, (nfds_t)(background_offset + background_count), timeout);
        if (p < 0 && errno != EINTR) {
            failed = 1;
            break;
        }
        if (p < 0) for (size_t i = 0; i < background_offset + background_count; ++i) fds[i].revents = 0;
        if (stop_requested) break;
        unsigned background_events = background_process(all->background, fds + background_offset, background_count);
        if (background_events & BACKGROUND_STOP) {
            stop_requested = background_signal(all->background);
            break;
        }
        if (hangup_requested) {
            hangup_requested = 0;
            if (!background_managed(all->background)) {
                stop_requested = SIGHUP;
                break;
            }
            background_events |= BACKGROUND_DETACH;
        }
        if ((fds[0].revents | fds[1].revents) & (POLLHUP | POLLERR | POLLNVAL)) {
            if (!background_managed(all->background)) break;
            background_events |= BACKGROUND_DETACH;
        }
        if (background_events & BACKGROUND_DETACH) {
            input_pending = 0;
            if (!detach_lost_terminal(all, a, u, error, sizeof error)) {
                if (!stop_requested) failed = 1;
                break;
            }
        } else if (background_events & BACKGROUND_SUSPEND) {
            release_terminal(a, u);
            input_pending = 0;
            background_suspend(all->background);
        } else if (background_events & BACKGROUND_ATTACH) {
            if (background_frontend(all->background) == BACKGROUND_FRONTEND_IDLE) {
                switch_frontend = BACKGROUND_FRONTEND_IDLE;
                break;
            }
            if (!restore_terminal(a, u)) {
                /* An unknown remote TERM must not destroy a retained IRC
                 * session. Leave it detached for another terminal attempt. */
                if (!background_detach(all->background, &stop_requested, error, sizeof error)) {
                    if (!stop_requested) failed = 1;
                    break;
                }
            } else client_set_backgrounded(client, 0);
        } else if (background_events & BACKGROUND_CONTINUE) {
            if (!restore_terminal(a, u)) {
                if (!background_detach(all->background, &stop_requested, error, sizeof error)) {
                    if (!stop_requested) failed = 1;
                    break;
                }
            }
        } else if ((background_events & BACKGROUND_RESIZE) && !background_detached(all->background)) {
            /* The shell signals the foreground waiter. Deliver its resize
             * locally so curses observes geometry on the passed tty fd. */
            (void)raise(SIGWINCH);
        }
        dcc_process(all->dcc, fds + offset, transfer_ids, transfer_count, app_now_ms(a));
        remove_cancelled_offers(all);
        process_server_input(all, fds, server_poll_index, p);
        if (dcc_changed(all->dcc)) a->dirty = 1;
        update_visibility(all, a, u);
        /* Curses can queue KEY_RESIZE without stdin becoming readable: a
         * SIGWINCH delivered before poll need not interrupt that call. Check
         * its queue nonblockingly after every bounded wait. Network/DCC work
         * still runs before each input batch, and idle polling still sleeps. */
        input_pending = 0;
        if (++all->input_batch == 0) ++all->input_batch;
        for (int count = 0; !background_detached(all->background) && count < 512; ++count) {
            errno = 0;
            int key = getch();
            if (key == ERR) {
                if (errno == EIO || errno == EBADF || errno == ENXIO || ferror(stdout)) {
                    if (!background_managed(all->background) ||
                        !detach_lost_terminal(all, a, u, error, sizeof error)) failed = 1;
                }
                break;
            }
            if (key != KEY_RESIZE) client_user_activity(client);
            terminal_byte(a, u, key);
            int changed = apply_session_actions(all, &a, &u);
            if (changed < 0) {
                failed = 1;
                break;
            }
            update_visibility(all, a, u);
            if (count == 511 || changed) input_pending = 1;
            if (a->quit || changed || stop_requested || all->background_requested) break;
        }
        if (failed) break;
    }
    int result = switch_frontend ? 1000 : failed ? 1 : stop_requested ? 128 + stop_requested : 0;
    if (!switch_frontend && u && !background_detached(all->background)) release_terminal(a, u);
    if (own_background) background_finish(all->background, result);
    if (terminal_screen) {
        delscreen(terminal_screen);
        terminal_screen = NULL;
    }
    while (installed) {
        --installed;
        (void)sigaction(signals[installed], &old[installed], NULL);
    }
    guided_free(all->guided);
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        if (client->slots[i].app) {
            client->slots[i].app->view_closed = NULL;
            client->slots[i].app->view_context = NULL;
            client->slots[i].app->dcc_offer = NULL;
            client->slots[i].app->dcc_context = NULL;
        }
        destroy_ui(all->states[i]);
    }
    transfers_free(all->transfers);
    if (own_dcc) dcc_free(all->dcc);
    if (own_background) background_free(all->background);
    if (switch_frontend && next_frontend) *next_frontend = switch_frontend;
    free(all);
    return result;
}

int ui_run(Client *client)
{
    return ui_run_mode(client, 0, 0, 0, NULL, NULL, NULL);
}

int ui_run_background(Client *client, int primary_connect, int no_connect)
{
    return ui_run_mode(client, 1, primary_connect, no_connect, NULL, NULL, NULL);
}

int ui_run_session(Client *client, Background *background, DccManager *dcc, int start_background,
                   int primary_connect, int no_connect, int *next_frontend)
{
    return ui_run_mode(client, start_background, primary_connect, no_connect,
                       background, dcc, next_frontend);
}
