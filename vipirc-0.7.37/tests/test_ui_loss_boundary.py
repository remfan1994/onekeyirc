#!/usr/bin/env python3
"""Compile the actual full-UI loss branch against a blocking cleanup fixture.

Uses real PTYs and the production background detach implementation. The curses
cleanup call is instrumented with blocking stdio; no curses header is generated
and no ncurses runtime or AF_UNIX handshake is claimed by this boundary test.
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
    # The selected blocks contain no braces in their strings or comments.
    for end in range(opening + 1, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if not depth:
            return source[start:end + 1]
    raise AssertionError("unterminated production block")


PREFIX = r'''
#if defined(__OpenBSD__) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "background.c"
#include <assert.h>
#include <time.h>

int net_detach_ready(Net *n) { (void)n; return 1; }
void credential_reap(void) { }
int credential_cleanup_pending(void) { return 0; }
int64_t net_now_ms(void)
{
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

typedef struct UiSet { Background *background; int background_requested; } UiSet;
typedef struct Ui { int unused; } Ui;
static volatile sig_atomic_t stop_requested, suspend_requested;
static int released;
static void release_terminal(App *a, Ui *u)
{
    (void)a; (void)u;
    /* Stdio/curses cleanup can block forever if the old PTY has no reader.
     * This call must run only after production detach redirects stdout. */
    assert(fputs("cleanup", stdout) >= 0 && fflush(stdout) == 0);
    ++released;
}
'''

SUFFIX = r'''
int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    const char *name = ptsname(master);
    assert(name);
    int slave = open(name, O_RDWR | O_NOCTTY | O_CLOEXEC);
    assert(slave >= 0 && tcflow(slave, TCOOFF) == 0);
    int flags = fcntl(slave, F_GETFL);
    assert(flags >= 0 && !(flags & O_NONBLOCK));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        close(master);
        assert(dup2(slave, STDIN_FILENO) == STDIN_FILENO);
        assert(dup2(slave, STDOUT_FILENO) == STDOUT_FILENO);
        char buffer[1024];
        assert(setvbuf(stdout, buffer, _IOFBF, sizeof buffer) == 0);
        assert(fputs("old presentation", stdout) >= 0);
        UiSet all;
        memset(&all, 0, sizeof all);
        all.background = background_create();
        assert(all.background);
        all.background->listener = open("/dev/null", O_RDWR | O_CLOEXEC);
        assert(all.background->listener >= 0);
        all.background->managed = 1;
        App app;
        Ui ui;
        alarm(2);
        assert(run_loss_branch(&all, &app, &ui));
        assert(released == 1 && background_detached(all.background));
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    close(slave); close(master);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        fprintf(stderr, "full-UI loss cleanup failed (wait status %d)\n", status);
        return 1;
    }
    puts("full-UI loss boundary: detached before blocked stdio cleanup; no AF_UNIX/ncurses claim");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--source", type=Path, default=root / "src" / "ui.c")
    args = parser.parse_args()
    text = args.source.read_text(encoding="ascii")
    helper = block(text, "static int detach_lost_terminal(") if "static int detach_lost_terminal(" in text else ""
    branch = block(text, "if (background_events & BACKGROUND_DETACH)")
    wrapper = ("\nstatic int run_loss_branch(UiSet *all, App *a, Ui *u)\n{\n"
               "    char error[512]; int input_pending = 1, failed = 0;\n"
               "    unsigned background_events = BACKGROUND_DETACH;\n"
               "    do {\n" + branch + "\n    } while (0);\n"
               "    return !failed && !input_pending;\n}\n")
    with tempfile.TemporaryDirectory(prefix="ui-loss-boundary-") as temporary:
        path = Path(temporary)
        source = path / "probe.c"
        binary = path / "probe"
        source.write_text(PREFIX + helper + wrapper + SUFFIX, encoding="ascii")
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-D_POSIX_C_SOURCE=200809L", "-D_XOPEN_SOURCE=700", "-std=c11",
            "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-O2",
            "-I" + str(root / "src"), str(source), "-o", str(binary)]
        subprocess.run(command, check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=5)


if __name__ == "__main__":
    main()
