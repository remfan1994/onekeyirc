#!/usr/bin/env python3
"""Check exact production curses terminal-policy helpers against a real PTY.

Only curses calls are simulated: they deliberately alter terminal settings
and cache/restore program and shell modes. Kernel tcgetattr/tcsetattr, the
production start/stop helpers and failure cleanup are exercised unchanged.
This is a terminal-policy boundary test, not a curses runtime/render test.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def block(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    for end in range(opening + 1, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if not depth:
            return source[start:end + 1]
    raise AssertionError("unterminated production block")


PREFIX = r'''
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
typedef struct Screen { int marker; } SCREEN;
static SCREEN screen;
static int stdscr;
enum { FALSE = 0, TRUE = 1, ERR = -1, PASTE_BEGIN = 1000, PASTE_END };
#define NCURSES_VERSION "boundary-fixture"
static struct termios shell_mode, program_mode;
static int newterm_fails, program_fails, end_calls;
static unsigned read_calls, fail_read, set_calls, fail_set;

static void change_tty(void)
{
    struct termios mode;
    assert(tcgetattr(STDIN_FILENO, &mode) == 0);
    mode.c_lflag |= ISIG;
    mode.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    mode.c_iflag &= (tcflag_t)~ICRNL;
    mode.c_cc[VINTR] = 3; mode.c_cc[VQUIT] = 28; mode.c_cc[VSUSP] = 26;
#ifdef VDSUSP
    mode.c_cc[VDSUSP] = 25;
#endif
    mode.c_cc[VMIN] = 1; mode.c_cc[VTIME] = 0;
    assert(tcsetattr(STDIN_FILENO, TCSANOW, &mode) == 0);
}
static SCREEN *newterm(const char *name, FILE *output, FILE *input)
{
    (void)name; (void)output; (void)input;
    assert(tcgetattr(STDIN_FILENO, &shell_mode) == 0);
    change_tty();
    return newterm_fails ? NULL : &screen;
}
static void delscreen(SCREEN *s) { assert(s == &screen); }
static SCREEN *set_term(SCREEN *s) { assert(s == &screen); return s; }
static void vipirc_focus_colors(void) { }
static int noraw(void) { change_tty(); return 0; }
static int cbreak(void) { change_tty(); return 0; }
static int noecho(void) { change_tty(); return 0; }
static int nonl(void) { change_tty(); return 0; }
static int intrflush(int w, int b) { (void)w; (void)b; change_tty(); return 0; }
static int keypad(int w, int b) { (void)w; (void)b; return 0; }
static int meta(int w, int b) { (void)w; (void)b; change_tty(); return 0; }
static int curs_set(int b) { (void)b; return 0; }
static int set_escdelay(int b) { (void)b; return 0; }
static int define_key(const char *bytes, int key) { (void)bytes; (void)key; return 0; }
static int def_prog_mode(void)
{
    assert(tcgetattr(STDIN_FILENO, &program_mode) == 0);
    return program_fails ? ERR : 0;
}
static int endwin(void)
{
    ++end_calls;
    /* Deliberately imperfect restoration proves the explicit production
     * restore is needed, and makes an inactive repeated stop observable. */
    struct termios reset = shell_mode;
    reset.c_lflag |= ISIG;
    reset.c_cc[VINTR] = 3;
    assert(tcsetattr(STDIN_FILENO, TCSANOW, &reset) == 0);
    return 0;
}

static int policy_get(int fd, struct termios *mode)
{
    if (++read_calls == fail_read) { errno = EIO; return -1; }
    return tcgetattr(fd, mode);
}
static int policy_set(int fd, int action, const struct termios *mode)
{
    if (++set_calls == fail_set) { errno = EIO; return -1; }
    return tcsetattr(fd, action, mode);
}
'''

TESTS = r'''
#undef tcgetattr
#undef tcsetattr
static void equal_mode(const struct termios *a, const struct termios *b)
{
    assert(a->c_iflag == b->c_iflag && a->c_oflag == b->c_oflag);
    assert(a->c_cflag == b->c_cflag && a->c_lflag == b->c_lflag);
    assert(!memcmp(a->c_cc, b->c_cc, sizeof a->c_cc));
    assert(cfgetispeed(a) == cfgetispeed(b) && cfgetospeed(a) == cfgetospeed(b));
}

static struct termios set_incoming(int signals, int disabled, unsigned variant)
{
    struct termios mode;
    assert(tcgetattr(STDIN_FILENO, &mode) == 0);
    mode.c_lflag |= ICANON | ECHO;
    if (signals) mode.c_lflag |= ISIG;
    else mode.c_lflag &= (tcflag_t)~ISIG;
    mode.c_iflag |= ICRNL;
    mode.c_cc[VINTR] = disabled ? _POSIX_VDISABLE : (cc_t)(2 + variant);
    mode.c_cc[VQUIT] = disabled ? _POSIX_VDISABLE : (cc_t)(6 + variant);
    mode.c_cc[VSUSP] = disabled ? _POSIX_VDISABLE : (cc_t)(12 + variant);
#ifdef VDSUSP
    mode.c_cc[VDSUSP] = disabled ? _POSIX_VDISABLE : (cc_t)(20 + variant);
#endif
    mode.c_cc[VERASE] = (cc_t)(15 + variant);
    mode.c_cc[VKILL] = (cc_t)(17 + variant);
    mode.c_cc[VMIN] = 7;
    mode.c_cc[VTIME] = 2;
    assert(tcsetattr(STDIN_FILENO, TCSANOW, &mode) == 0);
    assert(tcgetattr(STDIN_FILENO, &mode) == 0);
    read_calls = set_calls = fail_read = fail_set = 0;
    return mode;
}

static void active_policy(const struct termios *incoming, int force)
{
    struct termios active;
    assert(tcgetattr(STDIN_FILENO, &active) == 0);
    assert(!!(active.c_lflag & ISIG) == (force || !!(incoming->c_lflag & ISIG)));
    assert(!(active.c_lflag & (ICANON | ECHO)));
    for (size_t i = 0; i < sizeof active.c_cc / sizeof active.c_cc[0]; ++i)
        if (i != VMIN && i != VTIME) assert(active.c_cc[i] == incoming->c_cc[i]);
    assert(active.c_cc[VMIN] == 1 && active.c_cc[VTIME] == 0);
    equal_mode(&active, &program_mode);
    /* A later curses program-mode reset must preserve the selected policy. */
    assert(tcsetattr(STDIN_FILENO, TCSANOW, &program_mode) == 0);
    assert(tcgetattr(STDIN_FILENO, &active) == 0);
    equal_mode(&active, &program_mode);
}

int main(void)
{
    int saved_input = dup(STDIN_FILENO);
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(saved_input >= 0 && master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    const char *name = ptsname(master); assert(name);
    int slave = open(name, O_RDWR | O_NOCTTY);
    assert(slave >= 0 && dup2(slave, STDIN_FILENO) >= 0 && close(slave) == 0);
    assert(setenv("TERM", "xterm", 1) == 0);
    for (int signals = 0; signals <= 1; ++signals)
        for (int force = 0; force <= 1; ++force)
            for (int disabled = 0; disabled <= 1; ++disabled) {
                struct termios before = set_incoming(signals, disabled, 0), after;
                assert(terminal_start(force) && terminal_active);
                active_policy(&before, force);
                terminal_stop();
                assert(!terminal_active && tcgetattr(STDIN_FILENO, &after) == 0);
                equal_mode(&before, &after);
            }
    /* Re-entry inherits shell changes made while suspended. A repeated stop
     * while inactive must not put an older curses snapshot over those edits. */
    struct termios newer = set_incoming(0, 0, 1), after;
    int ended = end_calls;
    terminal_stop();
    assert(end_calls == ended && "inactive stop must not invoke curses cleanup again");
    assert(tcgetattr(STDIN_FILENO, &after) == 0);
    equal_mode(&newer, &after);
    assert(terminal_start(0)); active_policy(&newer, 0); terminal_stop();
    assert(tcgetattr(STDIN_FILENO, &after) == 0); equal_mode(&newer, &after);

    for (int failure = 0; failure < 5; ++failure) {
        struct termios before = set_incoming(0, 0, 2);
        newterm_fails = failure == 0;
        program_fails = failure == 1;
        fail_read = failure == 2 ? 1u : failure == 3 ? 2u : 0u;
        fail_set = failure == 4 ? 1u : 0u;
        assert(!terminal_start(1) && !terminal_active);
        fail_read = fail_set = 0;
        assert(tcgetattr(STDIN_FILENO, &after) == 0); equal_mode(&before, &after);
    }
    newterm_fails = program_fails = 0;
    if (terminal_screen) delscreen(terminal_screen);
    terminal_screen = NULL;
    assert(dup2(saved_input, STDIN_FILENO) >= 0 && close(saved_input) == 0 && close(master) == 0);
    puts("full-UI terminal policy: 8 inherited/forced/disabled-map combinations, cached program mode, exact restoration, inactive stop, changed-shell re-entry and 5 startup failure boundaries passed on a real PTY; curses calls simulated");
    return 0;
}
'''


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    production = (args.source or args.root / "src/ui.c").read_text(encoding="ascii")
    globals_start = production.index("static int terminal_utf8;")
    globals_end = production.index("static void stop_signal(", globals_start)
    code = PREFIX + production[globals_start:globals_end]
    code += "\n#define tcgetattr policy_get\n#define tcsetattr policy_set\n"
    code += block(production, "static void terminal_stop(void)") + "\n"
    code += block(production, "static int terminal_start(") + "\n" + TESTS
    with tempfile.TemporaryDirectory(prefix="ui-terminal-policy-") as directory:
        generated = Path(directory) / "probe.c"
        binary = Path(directory) / "probe"
        generated.write_text(code, encoding="ascii")
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
        command += (["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
                    if args.sanitize else ["-O2"])
        command += [str(generated), "-o", str(binary)]
        subprocess.run(command, check=True, timeout=30)
        environment = dict(os.environ)
        if args.sanitize:
            environment["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
            environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        subprocess.run([str(binary)], check=True, timeout=10, env=environment)


if __name__ == "__main__":
    main()
