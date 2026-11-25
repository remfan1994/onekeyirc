/* Real curses rendition checks: ordinary text, focus and erased cells keep
 * terminal defaults. No PTY, sockets, or user files are used. */
#include "focus.h"
#include <assert.h>
#include <stdio.h>
#include <wchar.h>

static void normal_cell(int row, int column)
{
    chtype cell = mvinch(row, column);
    assert(cell != (chtype)ERR && !(cell & (A_REVERSE | A_BOLD | A_DIM)));
    assert(PAIR_NUMBER(cell) == 0);
}

static void normal_screen(void)
{
    assert(!(getbkgd(stdscr) & (A_REVERSE | A_BOLD | A_DIM)));
    assert(PAIR_NUMBER(getbkgd(stdscr)) == 0);
    assert(erase() != ERR);
    normal_cell(0, 0);
    normal_cell(LINES - 1, COLS - 1);
    assert(attrset(A_NORMAL) != ERR);
    assert(mvaddstr(2, 0, "ordinary text") != ERR);
    assert(mvaddnwstr(3, 0, L"wide text", 9) != ERR);
    normal_cell(2, 0);
    normal_cell(3, 0);
    attr_t focus = vipirc_focus_attrs();
    assert(attr_set(focus, (short)PAIR_NUMBER(focus), NULL) != ERR);
    assert(mvaddstr(4, 0, "[selected]") != ERR);
    assert(attrset(A_NORMAL) != ERR);
    chtype cell = mvinch(4, 0);
    assert(cell != (chtype)ERR && (cell & A_BOLD) && !(cell & (A_REVERSE | A_DIM)));
    assert(PAIR_NUMBER(cell) == 0);
    assert(erase() != ERR);
    normal_cell(2, 0);
    normal_cell(4, 0);
    normal_cell(LINES - 1, COLS - 1);
}

int main(void)
{
    static const struct {
        const char *term;
        int configure_defaults;
        short foreground, background;
    } cases[] = {
        {"xterm-256color", 0, 0, 0},
        {"xterm-256color", 1, COLOR_BLACK, COLOR_WHITE},
        {"xterm-256color", 1, COLOR_WHITE, COLOR_BLACK},
        {"xterm", 1, COLOR_BLACK, COLOR_WHITE},
        {"xterm", 1, COLOR_WHITE, COLOR_BLACK},
        {"screen", 0, 0, 0},
        {"vt100", 0, 0, 0}
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        /* A second independent screen models acquiring another terminal on
         * resume. Palette state must not depend on the discarded screen. */
        for (int acquisition = 0; acquisition < 2; ++acquisition) {
            FILE *output = tmpfile(), *input = tmpfile();
            assert(output && input);
            SCREEN *screen = newterm(cases[i].term, output, input);
            if (!screen) {
                assert(i != 0);
                printf("palette: %s terminfo unavailable; optional case skipped\n", cases[i].term);
                fclose(output); fclose(input);
                break;
            }
            (void)set_term(screen);
            assert(resizeterm(24, 80) != ERR);
            chtype inherited = (chtype)' ' | A_REVERSE;
#ifdef NCURSES_VERSION
            if (cases[i].configure_defaults && has_colors()) {
                assert(start_color() != ERR);
                assert(assume_default_colors(cases[i].foreground, cases[i].background) != ERR);
                assert(init_pair(1, COLOR_RED, COLOR_BLUE) != ERR);
                inherited |= COLOR_PAIR(1);
            }
#endif
            /* Inherited reverse video must not become the whole-screen base. */
            assert(bkgd(inherited) != ERR);
            vipirc_focus_colors();
            normal_screen();
#ifdef NCURSES_VERSION
            if (cases[i].configure_defaults && has_colors()) {
                short foreground, background;
                assert(pair_content(0, &foreground, &background) != ERR);
                assert(foreground == cases[i].foreground && background == cases[i].background);
                assert(pair_content(1, &foreground, &background) != ERR);
                assert(foreground == COLOR_RED && background == COLOR_BLUE);
            }
#endif
            assert(bkgd(inherited) != ERR);
            vipirc_focus_colors();
            assert(resizeterm(9, 20) != ERR);
            normal_screen();
            (void)endwin();
            delscreen(screen);
            fclose(output); fclose(input);
        }
    }
    puts("palette: terminal-default ordinary/erased/focus cells, unchanged dark/light color schemes, bold selection, repeated initialization, resize, terminal reacquisition and monochrome fallback passed");
    return 0;
}
