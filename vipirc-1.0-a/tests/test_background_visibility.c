/* A transport record and terminal detach can be returned by one poll.
 * Exercise the production network dispatch with only the attachment state
 * injected; the App parser, activity accounting and editor are real. */
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif
#include "../src/background.h"
static int force_detached;
static int test_background_detached(const Background *);
#define background_detached test_background_detached
#define main status_mouse_existing_main
#include "test_status_mouse.c"
#undef main
#undef background_detached

static int test_background_detached(const Background *background)
{
    return force_detached || background_detached(background);
}

#if UI_MOUSE
static void input_after_same_poll_detach(void)
{
    Fixture f = fresh(CONFIG_FOCUS_REVERSE, 120);
    size_t view = f.app->current, indexes[CLIENT_MAX_SERVERS];
    struct pollfd fds[1] = {{ .fd = -1, .events = POLLIN, .revents = POLLIN }};
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) indexes[i] = SIZE_MAX;
    indexes[0] = 0;
    for (size_t i = 0; i < f.app->buffer_count; ++i)
        f.app->buffers[i].unread = f.app->buffers[i].unread_mentions = 0;
    Editor *editor = draft(f.app, f.ui);
    assert(editor && ed_from_utf8(editor, "DETACH_RETAINS_DRAFT"));
    editor->pos = 7;
    update_visibility(f.all, f.app, f.ui);
    assert(f.app->visible);

    strcpy(f.app->net->input, ":Al!u@local PRIVMSG tester :attached message\r\n");
    f.app->net->size = strlen(f.app->net->input);
    process_server_input(f.all, fds, indexes, 1);
    assert(f.app->visible && !f.app->buffers[view].unread);
    size_t before = f.app->buffers[view].line_count;

    /* The previous frame was visible, but the same poll has just detached
     * its terminal. No intervening repaint or visibility update is allowed. */
    force_detached = 1;
    strcpy(f.app->net->input, ":Al!u@local PRIVMSG tester :tester unread after detach\r\n");
    f.app->net->size = strlen(f.app->net->input);
    process_server_input(f.all, fds, indexes, 1);
    assert(!f.app->visible);
    assert(f.app->buffers[view].line_count == before + 1);
    assert(f.app->buffers[view].unread == 1);
    assert(f.app->buffers[view].unread_mentions == 1);
    size_t unread = 0, mentions = 0;
    app_activity(f.app, &unread, &mentions);
    assert(unread == 1 && mentions == 1);
    assert(f.app->current == view && draft(f.app, f.ui) == editor && editor->pos == 7);
    assert_editor_text(editor, "DETACH_RETAINS_DRAFT");

    force_detached = 0;
    update_visibility(f.all, f.app, f.ui);
    assert(f.app->visible && !f.app->buffers[view].unread && !f.app->buffers[view].unread_mentions);
    assert(f.app->buffers[view].line_count == before + 1);
    dispose(&f);
}

int main(void)
{
    FILE *out = tmpfile(), *in = tmpfile();
    assert(out && in);
    SCREEN *screen = newterm("xterm-256color", out, in);
    assert(screen && keypad(stdscr, TRUE) != ERR);
    vipirc_focus_colors();
    terminal_utf8 = 0;
    input_after_same_poll_detach();
    endwin(); delscreen(screen);
    fclose(out); fclose(in);
    puts("curses background: same-poll detach clears visibility before IRC input; unread/mentions and draft survive");
    return 0;
}
#else
int main(void)
{
    puts("curses background visibility: fixture requires mouse-enabled curses");
    return 0;
}
#endif
