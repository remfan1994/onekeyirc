/* Bounded DCC review and file forms. Offers alone never open files/sockets. */
#include "transfers.h"
#include "l10n.h"
#include "display_l10n.h"
#include "transfer_ui_l10n.h"
#include "editor_curses.h"
#include "editor.h"
#include "focus.h"
#include "filepicker.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

struct Transfers {
    Client *client;
    DccManager *manager;
    size_t origin, field, action;
    uint64_t selected, form_id;
    int active, form, editing, paste, paste_accept, paste_rejected;
    int scroll, edit_top, edit_rows, edit_width;
    int action_x[5], action_width[5], compact;
    int action_y, body_top, body_rows, drawn_lines, drawn_cols;
    unsigned revision, drawn_revision;
    int browsing;
    size_t picked, first_row;
    FilePicker *picker;
    Editor editor;
    char values[4][DCC_PATH_MAX];
    char launch_directory[DCC_PATH_MAX];
    char message[512];
};

static void message(Transfers *t, const char *text)
{
    irc_sanitize(t->message, sizeof t->message, text);
}

static void changed(Transfers *t)
{
    t->revision = (t->revision + 1u) & 0x07ffffffu;
}

static void close_picker(Transfers *t)
{
    filepicker_free(t->picker);
    t->picker = NULL;
    t->browsing = 0;
    t->picked = 0;
}

Transfers *transfers_create(Client *client, DccManager *manager)
{
    if (!manager) return NULL;
    Transfers *t = calloc(1, sizeof *t);
    if (t) {
        t->client = client; t->manager = manager; ed_init(&t->editor);
        if (!client->test_mode && !getcwd(t->launch_directory, sizeof t->launch_directory))
            t->launch_directory[0] = 0;
    }
    return t;
}

void transfers_free(Transfers *t)
{
    if (!t) return;
    close_picker(t);
    config_wipe(t, sizeof *t);
    free(t);
}

void transfers_open(Transfers *t, size_t server_slot)
{
    if (!t) return;
    close_picker(t);
    t->active = 1;
    t->form = t->editing = t->paste = t->paste_accept = 0;
    t->origin = server_slot;
    t->action = 0;
    changed(t);
    message(t, "");
    if (t->client->test_mode && !dcc_count(t->manager)) {
        char error[160]; uint64_t id;
        (void)dcc_offer(t->manager, t->client->slots[server_slot].token, "Aster",
            "DCC SEND preview.txt 2130706433 9000 1024", &id, error, sizeof error);
        message(t, irc_tr("Offline sample offer. Sending and receiving are disabled before file or network access."));
    }
}

int transfers_active(const Transfers *t) { return t && t->active; }
int transfers_at_top(const Transfers *t)
{
    if (!transfers_active(t) || t->form || t->editing || t->paste || t->action) return 0;
    const DccInfo *first = dcc_at(t->manager, 0);
    return !first || !t->selected || t->selected == first->id;
}
int transfers_pasting(const Transfers *t) { return t && t->active && t->paste; }

void transfers_close(Transfers *t)
{
    if (!t) return;
    close_picker(t);
    t->active = t->editing = t->form = t->paste = t->paste_accept = 0;
    config_wipe(&t->editor, sizeof t->editor);
    ed_init(&t->editor);
    config_wipe(t->values, sizeof t->values);
    changed(t);
}

unsigned transfers_mouse_state(const Transfers *t)
{
    if (!transfers_active(t)) return 0;
    return 1u | ((unsigned)t->form << 1) | (t->editing ? 8u : 0u) |
        (t->browsing ? 16u : 0u) | (t->revision << 5);
}

/* File browsing progresses incrementally while
 * the transfer list is open. A completed scan changes presentation
 * state only; DCC sockets and files remain owned by DccManager. */
int transfers_tick(Transfers *t)
{
    if (!transfers_active(t) || !t->browsing || !filepicker_step(t->picker)) return 0;
    changed(t);
    if (*filepicker_error(t->picker)) message(t, irc_tr(filepicker_error(t->picker)));
    return 1;
}

int transfers_timeout(const Transfers *t)
{
    return transfers_active(t) && t->browsing && filepicker_loading(t->picker) ? 50 : -1;
}

static size_t selection(Transfers *t)
{
    size_t count = dcc_count(t->manager);
    for (size_t i = 0; i < count; ++i)
        if (dcc_at(t->manager, i)->id == t->selected) return i;
    t->selected = count ? dcc_at(t->manager, 0)->id : 0;
    return 0;
}

static const char *field_name(const Transfers *t, size_t field)
{
    static const char *receiving[] = {"Directory", "Save as"};
    static const char *sending[] = {"Nickname", "Source file", "Advertised IPv4", "Listen port"};
    return irc_tr(t->form == 1 ? receiving[field] : sending[field]);
}

int transfers_context_help(const Transfers *t, char *screen, size_t screen_cap,
                           char *item, size_t item_cap, char *detail, size_t detail_cap)
{
    if (!transfers_active(t) || t->editing || t->paste) return 0;
    const char *screen_name = t->browsing ? "file browser" :
        t->form == 1 ? "Receive file screen" :
        t->form == 2 ? "Send file screen" : "File transfers screen";
    snprintf(screen, screen_cap, "%s", irc_tr(screen_name));
    if (t->browsing) {
        const FilePickerEntry *entry = filepicker_at(t->picker, t->picked);
        /* User-chosen filenames remain literal, not translation or format strings. */
        snprintf(item, item_cap, "%s", entry ? entry->name : irc_tr("File selection"));
        snprintf(detail, detail_cap, "%s %s",
            irc_tr(entry && entry->directory ? "This entry is a directory." : "This entry is a file selection."),
            irc_tr("Use Up/Down to select, Enter to open or choose, and Backspace to visit the parent directory. Choosing a file fills the transfer form for review; it does not send it."));
    } else if (t->form) {
        snprintf(item, item_cap, "%s", field_name(t, t->field));
        snprintf(detail, detail_cap, "%s %s %s",
            irc_tr("This field belongs to the transfer review form. Edit changes its value."),
            irc_tr(t->form == 1 ? "Directory and Save as determine the local destination after you choose Accept." :
                                     "Nickname, source file, advertised IPv4 and listen port determine the offer after you choose Offer."),
            irc_tr("Left/Right chooses an action; Enter activates it. Escape returns without approving the transfer."));
    } else {
        const DccInfo *info = dcc_get(t->manager, t->selected);
        if (!info) info = dcc_at(t->manager, 0);
        if (info) {
            char number[32], transferred[32], size[32], summary[512];
            snprintf(number, sizeof number, "%llu", (unsigned long long)info->id);
            snprintf(transferred, sizeof transferred, "%u", info->transferred);
            snprintf(size, sizeof size, "%u", info->size);
            const IrcDisplayField name_fields[] = {{"number", number}, {"filename", info->filename}};
            if (!irc_display_render(item, item_cap, "Transfer #{number}: {filename}", name_fields, 2))
                snprintf(item, item_cap, "Transfer #%llu: %.255s", (unsigned long long)info->id, info->filename);
            const IrcDisplayField status_fields[] = {
                {"state", irc_tr(dcc_state_name(info->state))}, {"nick", info->nick},
                {"transferred", transferred}, {"size", size}};
            if (!irc_display_render(summary, sizeof summary,
                    "{state} with {nick}: {transferred}/{size} bytes.", status_fields, 4))
                snprintf(summary, sizeof summary, "%s with %s: %u/%u bytes.",
                    dcc_state_name(info->state), info->nick, info->transferred, info->size);
            snprintf(detail, detail_cap, "%s %s", summary,
                irc_tr("Accept reviews an offer; Reject declines it; Cancel transfer stops it. Viewing help never accepts a file."));
        } else {
            snprintf(item, item_cap, "%s", irc_tr("Send..."));
            snprintf(detail, detail_cap, "%s", irc_tr("Open a new file-transfer form. There are currently no retained transfer entries. An offer requires an explicit reviewed action."));
        }
    }
    return 1;
}

static void focus(const App *a, SettingsText text, int y, int x, int width,
                  const char *label, int selected)
{
    (void)a;
    if (!selected) { text(y, x, width, label, A_NORMAL); return; }
    int cells = 0;
    mbstate_t state;
    memset(&state, 0, sizeof state);
    for (const char *p = label; *p && cells < width - 2;) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, p, MB_CUR_MAX, &state);
        int advance;
        if (n == (size_t)-1 || n == (size_t)-2 || !n) {
            memset(&state, 0, sizeof state); n = 1; advance = 1;
        } else {
            advance = wcwidth(wc);
            if (advance < 0) advance = 1;
        }
        if (advance > width - 2 - cells) break;
        cells += advance; p += n;
    }
    if (width > cells + 2) width = cells + 2;
    attr_t attrs = vipirc_focus_attrs();
    text(y, x, 1, "[", attrs);
    if (width > 2) text(y, x + 1, width - 2, label, attrs);
    if (width > 1) text(y, x + width - 1, 1, "]", attrs);
}

static void default_directory(char *out, size_t cap)
{
    const char *base = getenv("XDG_DATA_HOME");
    const char *suffix = "/vipirc/downloads";
    if (!base || *base != '/') { base = getenv("HOME"); suffix = "/.local/share/vipirc/downloads"; }
    if (!base || *base != '/') { *out = 0; return; }
    size_t n = strlen(base), extra = strlen(suffix);
    if (n >= cap || extra >= cap - n) { *out = 0; return; }
    memcpy(out, base, n);
    memcpy(out + n, suffix, extra + 1);
}

static void new_form(Transfers *t, App *a, int receiving)
{
    const DccInfo *info = dcc_get(t->manager, t->selected);
    if (receiving && (!info || info->state != DCC_OFFERED)) {
        message(t, irc_tr("Select a pending offer to accept.")); return;
    }
    memset(t->values, 0, sizeof t->values);
    t->form = receiving ? 1 : 2;
    t->form_id = receiving ? info->id : 0;
    t->field = t->action = 0;
    t->editing = 0;
    if (receiving) {
        default_directory(t->values[0], sizeof t->values[0]);
        snprintf(t->values[1], sizeof t->values[1], "%s", info->filename);
        message(t, irc_tr("Accept connects to this peer and creates a new private file; existing files are never overwritten."));
    } else {
        Buffer *b = &a->buffers[a->current];
        if (b->kind == B_QUERY) snprintf(t->values[0], sizeof t->values[0], "%s", b->name);
        snprintf(t->values[3], sizeof t->values[3], "0");
        message(t, a->cfg.hints ? irc_tr("Enter the IPv4 address the peer can reach. Port 0 chooses a local port; NAT may require forwarding.") : "");
    }
}

static void edit_field(Transfers *t)
{
    char safe[DCC_PATH_MAX];
    irc_sanitize(safe, sizeof safe, t->values[t->field]);
    if (!ed_from_utf8(&t->editor, safe)) { message(t, irc_tr("This field is not valid UTF-8.")); return; }
    ed_select_all(&t->editor);
    t->editing = 1; t->scroll = 0;
    message(t, "");
}

static void browse_field(Transfers *t)
{
    close_picker(t);
    t->picker = filepicker_create(t->client->test_mode);
    if (!t->picker) {
        edit_field(t); message(t, irc_tr("File browser unavailable; enter the source path here.")); return;
    }
    t->browsing = 1;
    t->picked = t->action = 0;
    if (!t->client->test_mode && !*t->launch_directory)
        message(t, irc_tr("Cannot read the launch directory. Use Type path."));
    else if (!filepicker_open(t->picker, t->client->test_mode ? NULL : t->launch_directory))
        message(t, irc_tr(filepicker_error(t->picker)));
    else message(t, "");
    changed(t);
}

static void choose_file(Transfers *t)
{
    char path[DCC_PATH_MAX];
    int result = filepicker_choose(t->picker, t->picked, path, sizeof path);
    if (result == 1) {
        memcpy(t->values[1], path, strlen(path) + 1u);
        close_picker(t);
        t->field = 1; t->action = 0;
        message(t, "");
    } else if (result == 2) { t->picked = 0; message(t, ""); }
    else message(t, irc_tr(filepicker_error(t->picker)));
    changed(t);
}

static void commit_field(Transfers *t)
{
    char value[DCC_PATH_MAX];
    size_t bytes = ed_to_utf8(&t->editor, value, sizeof value);
    if (bytes == SIZE_MAX) { message(t, irc_tr("The field allows at most 4095 UTF-8 bytes.")); return; }
    if (strchr(value, '\r') || strchr(value, '\n') || strchr(value, '\t')) {
        message(t, irc_tr("Use a single line without control characters.")); return;
    }
    memcpy(t->values[t->field], value, bytes + 1);
    t->editing = 0;
    ed_init(&t->editor);
    message(t, "");
}

/* Explicit review actions are the only route from
 * an offered DCC transfer to a real socket or file. The form edits
 * parameters but does not implicitly accept or advertise anything. */
static void perform(Transfers *t, App *a)
{
    char error[512];
    changed(t);
    if (t->browsing) {
        if (!t->action) choose_file(t);
        else if (t->action == 1) {
            close_picker(t); t->field = 1; t->action = 0; edit_field(t);
        } else { close_picker(t); t->action = 0; message(t, ""); }
        return;
    }
    if (!t->form) {
        (void)selection(t);
        if (t->action == 0) new_form(t, a, 1);
        else if (t->action == 1) {
            if (!dcc_reject(t->manager, t->selected, error, sizeof error)) message(t, error);
            else message(t, irc_tr("Offer rejected. No connection or file was opened."));
        } else if (t->action == 2) new_form(t, a, 0);
        else if (t->action == 3) {
            const DccInfo *info = dcc_get(t->manager, t->selected);
            int outgoing = info && info->outgoing;
            if (outgoing) for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
                ClientSlot *slot = &t->client->slots[i];
                if (slot->used && slot->token == info->server_token && slot->app)
                    (void)app_cancel_dcc_offer(slot->app, info->id);
            }
            if (!dcc_cancel(t->manager, t->selected, error, sizeof error)) message(t, error);
            else message(t, outgoing ? irc_tr("Transfer cancelled. An offer already sent or being sent cannot be recalled.") :
                         irc_tr("Transfer cancelled. Its incomplete receive file was removed if still unchanged."));
        } else t->active = 0;
        return;
    }
    if (!t->action) {
        if (t->form == 2 && t->field == 1) browse_field(t);
        else edit_field(t);
        return;
    }
    if (t->action == 2) { t->form = 0; t->action = 0; return; }
    if (LINES < 12 || COLS < 20) {
        message(t, irc_tr("Resize to at least 12 rows by 20 columns to review this transfer.")); return;
    }
    if (t->form == 1) {
        if (!dcc_accept(t->manager, t->form_id, t->values[0], t->values[1], error, sizeof error)) {
            message(t, error); return;
        }
        message(t, irc_tr("Receiving file. You can return to chat while it transfers."));
    } else {
        if (t->client->test_mode) { message(t, irc_tr("OFFLINE TEST MODE: DCC sending is disabled before file or network access.")); return; }
        ClientSlot *slot = &t->client->slots[t->origin];
        App *session = slot->app;
        if (!session || !session->registered) { message(t, irc_tr("Connect this IRC server before offering a file.")); return; }
        if (!irc_valid_nick(t->values[0])) { message(t, irc_tr("Enter a valid recipient nickname.")); return; }
        char *end; errno = 0;
        unsigned long port = strtoul(t->values[3], &end, 10);
        if (!*t->values[3] || errno || *end || *t->values[3] == '-' || port > 65535ul) {
            message(t, irc_tr("Listen port must be an integer from 0 to 65535.")); return;
        }
        char body[512]; uint64_t id;
        if (!dcc_send(t->manager, slot->token, t->values[0], t->values[1], t->values[2],
                       (uint16_t)port, &id, body, sizeof body, error, sizeof error)) {
            message(t, error); return;
        }
        if (!app_queue_dcc_offer(session, id, t->values[0], body)) {
            (void)dcc_cancel(t->manager, id, error, sizeof error);
            message(t, irc_tr("The file offer could not be queued within the IRC message budget. Try a shorter filename."));
            return;
        }
        t->selected = id;
        message(t, irc_tr("File offered. Waiting for the peer to connect."));
    }
    t->form = 0; t->action = 0;
}

void transfers_paste(Transfers *t, int begin)
{
    if (!t || !t->active) return;
    if (begin) { t->paste = 1; t->paste_accept = t->editing; t->paste_rejected = 0; }
    else {
        t->paste = t->paste_accept = 0;
        if (t->paste_rejected) message(t, irc_tr("Paste contained rejected controls or exceeded the field limit."));
    }
}

/* Pasted bytes belong to the current form field.
 * Never interpret pasted Enter as Accept, Send, or Cancel on a live
 * transfer; stale review rows require an explicit fresh selection. */
void transfers_key(Transfers *t, App *a, wint_t key, int special)
{
    if (!t || !t->active) return;
    a->dirty = 1;
    changed(t);
    if (t->paste) {
        if (t->paste_accept && !special && (key < 32 || !ed_insert(&t->editor, (wchar_t)key))) t->paste_rejected = 1;
        return;
    }
    if (!special && key == 27) {
        if (t->browsing) { close_picker(t); t->action = 0; message(t, ""); }
        else if (t->editing) {
            t->editing = 0;
            config_wipe(&t->editor, sizeof t->editor); ed_init(&t->editor);
            message(t, "");
        } else transfers_close(t);
        return;
    }
    if (!special && key == '\t') {
        if (a->cfg.hints) message(t, irc_tr("Input is on the chat screen."));
        return;
    }
    int enter = (special && key == KEY_ENTER) || (!special && (key == '\r' || key == '\n'));
    if (t->editing) {
        if (enter) { commit_field(t); return; }
        if (special) switch (key) {
        case KEY_LEFT: ed_visual_left(&t->editor, t->edit_width); break;
        case KEY_RIGHT: ed_visual_right(&t->editor, t->edit_width); break;
        case KEY_UP: ed_visual_move_vertical(&t->editor, -1, t->edit_width); break;
        case KEY_DOWN: ed_visual_move_vertical(&t->editor, 1, t->edit_width); break;
        case KEY_HOME: ed_visual_home(&t->editor, t->edit_width); break;
        case KEY_END: ed_visual_end(&t->editor, t->edit_width); break;
        case KEY_BACKSPACE: (void)ed_backspace(&t->editor); break;
        case KEY_DC: (void)ed_delete(&t->editor); break;
        default: break;
        }
        else if (key == 8 || key == 127) (void)ed_backspace(&t->editor);
        else if (key >= 32 && !ed_insert(&t->editor, (wchar_t)key)) message(t, irc_tr("Field limit reached or unsupported control character."));
        return;
    }
    if (t->browsing && ((special && key == KEY_BACKSPACE) ||
        (!special && (key == 8 || key == 127)))) {
        if (filepicker_parent(t->picker)) { t->picked = 0; message(t, ""); }
        else message(t, irc_tr(filepicker_error(t->picker)));
        return;
    }
    size_t index = t->browsing ? t->picked : t->form ? t->field : selection(t);
    size_t count = t->browsing ? filepicker_count(t->picker) : t->form ? (t->form == 1 ? 2u : 4u) : dcc_count(t->manager);
    size_t actions = t->form ? 3u : 5u;
    if (((special && key == KEY_UP) || (!special && key == 'k')) && index) --index;
    else if (((special && key == KEY_DOWN) || (!special && key == 'j')) && index + 1 < count) ++index;
    else if ((special && key == KEY_HOME) || (!special && key == 'g')) index = 0;
    else if ((special && key == KEY_END) || (!special && key == 'G')) index = count ? count - 1 : 0;
    else if (special && key == KEY_PPAGE) {
        size_t page = t->body_rows > 1 ? (size_t)t->body_rows - 1u : 1u;
        index = index > page ? index - page : 0;
    } else if (special && key == KEY_NPAGE && count) {
        size_t page = t->body_rows > 1 ? (size_t)t->body_rows - 1u : 1u;
        index += page < count - 1u - index ? page : count - 1u - index;
    }
    else if ((special && key == KEY_LEFT) || (!special && key == 'h')) t->action = (t->action + actions - 1) % actions;
    else if ((special && key == KEY_RIGHT) || (!special && key == 'l')) t->action = (t->action + 1) % actions;
    else if (enter) { perform(t, a); return; }
    if (t->browsing) t->picked = index;
    else if (t->form) t->field = index;
    else if (count) t->selected = dcc_at(t->manager, index)->id;
}

static void draw_actions(Transfers *t, const App *a, SettingsText text)
{
    const char *list[] = {"Accept", "Reject", "Send...", "Cancel transfer", "Back"};
    const char *form[] = {t->form == 2 && t->field == 1 ? "Browse" : "Edit", t->form == 1 ? "Accept" : "Offer", "Cancel"};
    const FilePickerEntry *entry = filepicker_at(t->picker, t->picked);
    const char *browser[] = {entry && !entry->directory ? "Choose" : "Open", "Type path", "Cancel"};
    const char **labels = t->browsing ? browser : t->form ? form : list;
    const size_t count = t->form ? 3u : 5u;
    const char *translated[5];
    for (size_t i = 0; i < count; ++i) translated[i] = irc_tr(labels[i]);
    /* Widths and mouse hitboxes use terminal cells, never UTF-8 bytes.
     * Compact mode exposes only the selected action and its two arrows. */
    t->compact = irc_transfer_action_layout(translated, count, COLS, t->action,
                                            t->action_x, t->action_width);
    if (t->compact) {
        text(t->action_y, 0, 1, "<", A_BOLD); text(t->action_y, COLS - 1, 1, ">", A_BOLD);
        focus(a, text, t->action_y, 2, COLS - 4, translated[t->action], 1);
    } else for (size_t i = 0; i < count; ++i)
        focus(a, text, t->action_y, t->action_x[i], t->action_width[i],
              translated[i], i == t->action);
}

static void draw_editor(Transfers *t, const App *a, SettingsText text)
{
    char label[160];
    for (int y = 3; y < LINES - 1; ++y) mvhline(y, 0, ' ', COLS);
    const IrcDisplayField edit_field_name[] = {{"field", field_name(t, t->field)}};
    if (!irc_display_render(label, sizeof label, "Edit: {field}", edit_field_name, 1))
        snprintf(label, sizeof label, "Edit: %s", field_name(t, t->field));
    text(3, 0, COLS, label, A_BOLD);
    if (a->cfg.hints) text(4, 0, COLS, irc_tr("Enter: stage | Esc: cancel"), A_DIM);
    t->edit_top = 6; t->edit_rows = LINES - 9; t->edit_width = COLS - 4;
    if (t->edit_rows > 5) t->edit_rows = 5;
    IrcCursesEditorPosition visual;
    if (irc_curses_editor_visual(&t->editor, t->edit_width, t->edit_top, 2,
            t->edit_rows, &t->scroll, t->editor.selection_all ? A_REVERSE : A_NORMAL,
            &visual)) {
        transfer_editor_position(label, sizeof label, t->editor.bytes, visual.row + 1, visual.col + 1);
        text(LINES - 2, 0, COLS, label, A_NORMAL);
        int y = t->edit_top + visual.row - visual.first, x = 2 + visual.col;
        if (y >= 0 && y < LINES && x >= 0 && x < COLS) {
            (void)mvchgat(y, x, visual.span, A_REVERSE | A_UNDERLINE, 0, NULL);
            (void)move(y, x);
        }
        (void)curs_set(1);
        return;
    }
    int total, row, col;
    ed_layout(&t->editor, t->edit_width, &total, &row, &col);
    if (t->scroll >= total) t->scroll = total - 1;
    if (t->scroll < 0) t->scroll = 0;
    if (row < t->scroll) t->scroll = row;
    if (row >= t->scroll + t->edit_rows) t->scroll = row - t->edit_rows + 1;
    int r = 0, c = 0;
    attrset(t->editor.selection_all ? A_REVERSE : A_NORMAL);
    for (size_t i = 0; i < t->editor.len;) {
        int width = ed_cluster_width(&t->editor, i, t->edit_width);
        if (width && c + width > t->edit_width) { ++r; c = 0; }
        size_t end = ed_next(&t->editor, i);
        if (r >= t->scroll && r < t->scroll + t->edit_rows) {
            int w = wcwidth(t->editor.data[i]);
            move(t->edit_top + r - t->scroll, 2 + c);
            if (w < 0 || w > t->edit_width) addch('?');
            else {
                if (!w) addch('?');
                (void)addnwstr(t->editor.data + i, (int)(end - i));
            }
        }
        c += width;
        if (c >= t->edit_width) { ++r; c = 0; }
        if (r >= t->scroll + t->edit_rows) break;
        i = end;
    }
    attrset(A_NORMAL);
    transfer_editor_position(label, sizeof label, t->editor.bytes, row + 1, col + 1);
    text(LINES - 2, 0, COLS, label, A_NORMAL);
    int y = t->edit_top + row - t->scroll, x = 2 + col;
    if (y >= 0 && y < LINES && x >= 0 && x < COLS) {
        int span = ed_cluster_width(&t->editor, t->editor.pos, t->edit_width);
        if (span < 1) span = 1;
        (void)mvchgat(y, x, span, A_REVERSE | A_UNDERLINE, 0, NULL); move(y, x);
    }
    (void)curs_set(1);
}

static void draw_browser(Transfers *t, App *a, SettingsText text)
{
    char line[DCC_PATH_MAX + 128], safe[DCC_PATH_MAX + 128];
    text(2, 0, COLS, irc_tr("Choose file"), A_BOLD);
    snprintf(line, sizeof line, "%s: %s", irc_tr("Directory"), *filepicker_path(t->picker) ? filepicker_path(t->picker) : irc_tr("(unavailable)"));
    irc_sanitize(safe, sizeof safe, line);
    text(3, 0, COLS, safe, A_NORMAL);
    t->action_y = 4; t->body_top = 5; t->body_rows = LINES - 7;
    draw_actions(t, a, text);
    size_t count = filepicker_count(t->picker);
    if (t->picked >= count) t->picked = count ? count - 1u : 0;
    t->first_row = t->picked >= (size_t)t->body_rows ? t->picked - (size_t)t->body_rows + 1u : 0;
    if (filepicker_loading(t->picker)) text(t->body_top, 0, COLS, irc_tr("Reading directory..."), A_NORMAL);
    else if (!count) text(t->body_top, 0, COLS, irc_tr("No sendable files here."), A_NORMAL);
    for (size_t row = 0; row < (size_t)t->body_rows && t->first_row + row < count; ++row) {
        const FilePickerEntry *entry = filepicker_at(t->picker, t->first_row + row);
        if (entry->directory) snprintf(line, sizeof line, "%s/", entry->name);
        else snprintf(line, sizeof line, "%s", entry->name);
        irc_sanitize(safe, sizeof safe, line);
        focus(a, text, t->body_top + (int)row, 0, COLS, safe, t->first_row + row == t->picked);
    }
    if (filepicker_limited(t->picker)) snprintf(line, sizeof line, "%s", irc_tr("Listing limit reached; Type path for another file."));
    else if (filepicker_skipped(t->picker)) {
        char shown[32], hidden[32];
        snprintf(shown, sizeof shown, "%zu", count);
        snprintf(hidden, sizeof hidden, "%zu", filepicker_skipped(t->picker));
        const IrcDisplayField fields[] = {{"count", shown}, {"hidden", hidden}};
        if (!irc_display_render(line, sizeof line,
                "{count} entries; {hidden} unsupported entries hidden", fields, 2))
            snprintf(line, sizeof line, "%zu entries; %zu unsupported entries hidden", count, filepicker_skipped(t->picker));
    } else {
        const FilePickerEntry *selected = filepicker_at(t->picker, t->picked);
        char amount[32];
        if (selected && !selected->directory) snprintf(amount, sizeof amount, "%u", selected->size);
        else snprintf(amount, sizeof amount, "%zu", count);
        const IrcDisplayField fields[] = {{"count", amount}};
        const char *key = selected && !selected->directory ? "{count} bytes" : "{count} entries";
        if (!irc_display_render(line, sizeof line, key, fields, 1))
            snprintf(line, sizeof line, "%s %s", amount, selected && !selected->directory ? "bytes" : "entries");
    }
    text(LINES - 2, 0, COLS, line, A_NORMAL);
    text(LINES - 1, 0, COLS, t->message, A_BOLD);
    (void)curs_set(0); move(2, 0);
}

void transfers_draw(Transfers *t, App *a, SettingsText text)
{
    char line[DCC_PATH_MAX + 512], safe[DCC_PATH_MAX + 512];
    erase();
    t->drawn_lines = t->drawn_cols = 0;
    if (LINES < 12 || COLS < 20) {
        text(2, 0, COLS, irc_tr("Resize; Esc returns."), A_NORMAL);
        (void)curs_set(0); move(0, 0); wnoutrefresh(stdscr); return;
    }
    if (t->browsing) {
        draw_browser(t, a, text);
        t->drawn_lines = LINES; t->drawn_cols = COLS; t->drawn_revision = t->revision;
        return;
    }
    snprintf(line, sizeof line, "%s%s", a->test_mode ? irc_tr("[OFFLINE] ") : "",
        irc_tr(t->form == 1 ? "Receive file" : t->form == 2 ? "Send file" : "File transfers"));
    text(2, 0, COLS, line, A_BOLD);
    t->action_y = t->form ? 4 : 3;
    t->body_top = t->action_y + 1;
    t->body_rows = LINES - t->body_top - 3;
    const DccInfo *info = NULL;
    int detail_row = 9;
    if (t->form) {
        info = t->form == 1 ? dcc_get(t->manager, t->form_id) : NULL;
        size_t count = t->form == 1 ? 2u : 4u;
        for (size_t i = 0; i < count; ++i) {
            snprintf(line, sizeof line, "%s: %s", field_name(t, i), *t->values[i] ? t->values[i] : irc_tr("(empty)"));
            irc_sanitize(safe, sizeof safe, line);
            focus(a, text, t->body_top + (int)i, 0, COLS, safe, i == t->field);
        }
        if (t->form == 1 && info) {
            const IrcDisplayField peer[] = {{"address", info->address}};
            if (!irc_display_render(line, sizeof line, "Peer {address}", peer, 1))
                snprintf(line, sizeof line, "Peer %s", info->address);
            text(7, 0, COLS, line, A_NORMAL);
            char amount[32]; snprintf(amount, sizeof amount, "%u", info->size);
            const IrcDisplayField size[] = {{"count", amount}};
            if (!irc_display_render(line, sizeof line, "{count} bytes", size, 1))
                snprintf(line, sizeof line, "%u bytes", info->size);
            text(8, 0, COLS, line, A_NORMAL);
        }
        text(3, 0, COLS, COLS < 72 ? irc_tr("Unencrypted; IP sent") : t->form == 1 ?
            irc_tr("Unencrypted peer connection. Accepting reveals your IP to the peer.") :
            irc_tr("Unencrypted peer connection. Your advertised IP is included in the offer."), A_BOLD);
    } else {
        size_t selected = selection(t), count = dcc_count(t->manager), height = (size_t)t->body_rows;
        t->first_row = selected >= height ? selected - height + 1u : 0;
        size_t shown = 0;
        for (; shown < height && t->first_row + shown < count; ++shown) {
            const DccInfo *entry = dcc_at(t->manager, t->first_row + shown);
            char progress[64], rendered[512];
            snprintf(progress, sizeof progress, "%u/%u", entry->transferred, entry->size);
            const IrcDisplayField fields[] = {
                {"direction", irc_tr(entry->outgoing ? "to" : "from")},
                {"nick", entry->nick}, {"state", irc_tr(dcc_state_name(entry->state))},
                {"progress", progress}};
            if (!irc_display_render(rendered, sizeof rendered,
                    "{direction} {nick} | {state} | {progress} | ", fields, 4))
                snprintf(rendered, sizeof rendered, "%s %s | %s | %s | ",
                    entry->outgoing ? "to" : "from", entry->nick, dcc_state_name(entry->state), progress);
            snprintf(line, sizeof line, "#%llu %s%s", (unsigned long long)entry->id, rendered, entry->filename);
            irc_sanitize(safe, sizeof safe, line);
            focus(a, text, t->body_top + (int)shown, 0, COLS, safe, t->first_row + shown == selected);
        }
        if (!count) text(t->body_top, 0, COLS, irc_tr("No file transfers."), A_NORMAL);
        detail_row = t->body_top + (int)(shown ? shown : 1u);
        info = dcc_get(t->manager, t->selected);
    }
    if (info) {
        const char *server = irc_tr("closed server");
        for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
            if (t->client->slots[i].used && t->client->slots[i].token == info->server_token)
                server = t->client->slots[i].app ? t->client->slots[i].app->cfg.server : t->client->slots[i].seed.server;
        char port[32], size[32];
        snprintf(port, sizeof port, "%u", (unsigned)info->port);
        snprintf(size, sizeof size, "%u", info->size);
        if (t->form == 1) {
            const IrcDisplayField fields[] = {{"port", port}, {"nick", info->nick}, {"server", server}};
            if (!irc_display_render(line, sizeof line,
                    "Port {port} | {nick} on {server}", fields, 3))
                snprintf(line, sizeof line, "Port %u | %s on %s", (unsigned)info->port, info->nick, server);
        } else {
            char prefix[320], suffix[320];
            const IrcDisplayField head[] = {{"address", info->address}, {"port", port}, {"size", size}};
            const IrcDisplayField tail[] = {{"nick", info->nick}, {"server", server}};
            if (!irc_display_render(prefix, sizeof prefix,
                    "{address}:{port} | {size} bytes | ", head, 3) ||
                !irc_display_render(suffix, sizeof suffix,
                    "{nick} on {server}", tail, 2))
                snprintf(line, sizeof line, "%s:%u | %u bytes | %s on %s",
                    info->address, (unsigned)info->port, info->size, info->nick, server);
            else snprintf(line, sizeof line, "%s%s", prefix, suffix);
        }
        irc_sanitize(safe, sizeof safe, line); text(detail_row, 0, COLS, safe, A_NORMAL);
        snprintf(line, sizeof line, "%s", *info->error ? info->error : *info->path ? info->path : irc_tr("File stays unopened until Accept."));
    } else if (t->form == 2) {
        const ClientSlot *slot = &t->client->slots[t->origin];
        const char *server = slot->app ? slot->app->cfg.server : slot->seed.server;
        int port_number = slot->app ? slot->app->cfg.port : slot->seed.port;
        char port[32]; snprintf(port, sizeof port, "%d", port_number);
        const IrcDisplayField fields[] = {{"server", server}, {"port", port}};
        if (!irc_display_render(line, sizeof line, "Offer through {server}:{port}", fields, 2))
            snprintf(line, sizeof line, "Offer through %s:%d", server, port_number);
    } else line[0] = 0;
    irc_sanitize(safe, sizeof safe, line); text(detail_row + 1, 0, COLS, safe, A_NORMAL);
    draw_actions(t, a, text);
    (void)curs_set(0); move(2, 0);
    if (t->editing) draw_editor(t, a, text);
    int cursor_y, cursor_x;
    getyx(stdscr, cursor_y, cursor_x);
    text(LINES - 1, 0, COLS, t->message, A_BOLD);
    if (t->editing) move(cursor_y, cursor_x);
    t->drawn_lines = LINES; t->drawn_cols = COLS; t->drawn_revision = t->revision;
}

#if defined(KEY_MOUSE) && defined(BUTTON1_PRESSED)
void transfers_mouse(Transfers *t, App *a, const MEVENT *event)
{
    if (!t || !t->active || t->paste || LINES < 12 || COLS < 20) return;
    if (t->drawn_lines != LINES || t->drawn_cols != COLS || t->drawn_revision != t->revision) return;
    if (t->editing) {
        if ((event->bstate & BUTTON1_PRESSED) && event->y >= t->edit_top &&
            event->y < t->edit_top + t->edit_rows && event->x >= 2 && event->x < 2 + t->edit_width) {
            ed_visual_set_cell(&t->editor, t->scroll + event->y - t->edit_top, event->x - 2, t->edit_width);
            changed(t);
        }
        a->dirty = 1; return;
    }
#ifdef BUTTON4_PRESSED
    if (event->bstate & BUTTON4_PRESSED) { transfers_key(t, a, KEY_UP, 1); return; }
#endif
#ifdef BUTTON5_PRESSED
    if (event->bstate & BUTTON5_PRESSED) { transfers_key(t, a, KEY_DOWN, 1); return; }
#endif
    if (!(event->bstate & BUTTON1_PRESSED)) return;
    changed(t);
    if (event->y == t->action_y) {
        if (t->compact && event->x == 0) transfers_key(t, a, KEY_LEFT, 1);
        else if (t->compact && event->x == COLS - 1) transfers_key(t, a, KEY_RIGHT, 1);
        else for (size_t i = 0; i < (t->form ? 3u : 5u); ++i)
            if (t->action_x[i] >= 0 && event->x >= t->action_x[i] && event->x < t->action_x[i] + t->action_width[i]) {
                t->action = i; perform(t, a); break;
            }
    } else if (t->browsing && event->y >= t->body_top && event->y < t->body_top + t->body_rows) {
        size_t row = t->first_row + (size_t)(event->y - t->body_top);
        if (row < filepicker_count(t->picker)) { t->picked = row; t->action = 0; choose_file(t); }
    } else if (event->y >= t->body_top && t->form && (size_t)(event->y - t->body_top) < (t->form == 1 ? 2u : 4u)) {
        t->field = (size_t)(event->y - t->body_top); t->action = 0;
        if (t->form == 2 && t->field == 1) browse_field(t);
        else edit_field(t);
    } else if (!t->form && event->y >= t->body_top && event->y < t->body_top + t->body_rows) {
        size_t row = t->first_row + (size_t)(event->y - t->body_top);
        if (row < dcc_count(t->manager)) t->selected = dcc_at(t->manager, row)->id;
    }
    a->dirty = 1;
}
#endif
