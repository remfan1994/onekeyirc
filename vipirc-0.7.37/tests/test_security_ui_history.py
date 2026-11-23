#!/usr/bin/env python3
"""Exercise production full-UI send/history branches without curses.

The extracted blocks are compiled unchanged with the actual App, command,
editor, prompt history and offline simulator code. Presentation callbacks are
inert. This is boundary/state coverage, not a curses or terminal runtime test.
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
    # Selected blocks have no braces in string literals or comments.
    for end in range(opening + 1, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if not depth:
            return source[start:end + 1]
    raise AssertionError("unterminated production block")


PREFIX = r'''
#define main legacy_idle_commands_test_main
#include "test_idle_commands.c"
#undef main
#include "commands.h"
#include "editor.h"
typedef struct CommandPrompt CommandPrompt;
'''

UI_TYPES = r'''
typedef struct ReadView { int active, hidden; } ReadView;
typedef struct Ui {
    CommandPrompt *command;
    SendPlan pending;
    size_t pending_view;
    int pending_command, close_command, command_limit_warned, command_from_app;
    int input_mode, nav_focus, status_selected, status_cycle, confirming;
    int64_t confirm_at;
    ReadView summary;
} Ui;
enum { FOCUS_APP = 1 };
static void clear_draft(Ui *u, size_t view) { (void)u; (void)view; }
static void invalidate_mouse(Ui *u) { (void)u; }
static void restore_command_origin(Ui *u) { (void)u; }
static void free_readview(ReadView *view) { memset(view, 0, sizeof *view); }
static int finish_close(App *a, Ui *u) { (void)a; (void)u; return 1; }
'''

TESTS = r'''
static unsigned msg_usage(const App *a)
{
    for (size_t i = 0; i < a->cfg.command_usage_count; ++i)
        if (!strcmp(a->cfg.command_usage[i].name, "msg")) return a->cfg.command_usage[i].count;
    return 0;
}

static void submit_case(App *a, Ui *u, const char *text, size_t messages, int private)
{
    size_t queued = a->chat_count, history = u->command->history.count;
    unsigned used = msg_usage(a);
    assert(prompt_open(u->command, text));
    int result = run_submit(a, u, text, 1);
    if (messages > 1) {
        assert(result == 2 && u->confirming == 1 && u->pending_command);
        assert(u->pending.count == messages && a->chat_count == queued);
        assert(u->command->history.count == history);
        u->confirm_at -= 1000;
        run_confirm(a, u, 'y', 0);
    } else {
        assert(result == 1);
        finish_command(a, u, 1);
    }
    assert(a->chat_count == queued + messages && !u->command->active);
    assert(msg_usage(a) == used + 1);
    if (u->command->history.count != history + (private ? 0u : 1u)) {
        fprintf(stderr, "full-UI history privacy failed: messages=%zu private=%d retained_delta=%zu\n",
                messages, private, u->command->history.count - history);
        abort();
    }
    if (!private) assert(!strcmp(u->command->history.entries[0], text));
    for (size_t i = 0; i < u->command->history.count; ++i)
        assert(!strstr(u->command->history.entries[i], "UI_HISTORY_SECRET_CANARY"));
}

static int plan_zero(const SendPlan *plan)
{
    const unsigned char *p = (const unsigned char *)plan;
    for (size_t i = 0; i < sizeof *plan; ++i) if (p[i]) return 0;
    return 1;
}

int main(void)
{
    Fixture f; init(&f, 1);
    assert(run(&f, "/connect") == IDLE_COMMAND_DONE);
    pump(app(&f));
    App *a = app(&f);
    assert(a->registered && a->sim && !a->net);
    Ui u; memset(&u, 0, sizeof u);
    u.command = prompt_create(); assert(u.command);
    submit_case(a, &u, "/msg CustomAuthBot ::IDENTIFY UI_HISTORY_SECRET_CANARY", 1, 1);
    submit_case(a, &u, "/msg CustomAuthBot ordinary direct message", 1, 0);
    char command[APP_TEXT + 1];
    const char prefix[] = "/msg CustomAuthBot ";
    size_t at = sizeof prefix - 1, budget = app_budget(a, "CustomAuthBot", 0);
    assert(budget > 8 && at + budget + 128 < sizeof command);
    memcpy(command, prefix, at);
    memset(command + at, 'x', budget);
    memcpy(command + at, "HELP ", 5);
    strcpy(command + at + budget, "IDENTIFY UI_HISTORY_SECRET_CANARY");
    submit_case(a, &u, command, 2, 1);
    assert(plan_zero(&u.pending) && !u.pending_command);
    strcpy(command + at + budget, "ordinary reviewed message");
    submit_case(a, &u, command, 2, 0);

    /* Review cancellation retains only the editor draft, never a history
     * entry, queued command or a stale plan consulted by the next command. */
    static const unsigned cancelled[] = {27, 'n', '\r'};
    for (size_t i = 0; i < sizeof cancelled / sizeof cancelled[0]; ++i) {
        strcpy(command + at + budget, "LOGIN 123 UI_HISTORY_SECRET_CANARY");
        assert(prompt_open(u.command, command));
        size_t queued = a->chat_count, history = u.command->history.count;
        assert(run_submit(a, &u, command, 1) == 2);
        u.confirm_at -= 1000;
        run_confirm(a, &u, cancelled[i], 0);
        assert(!u.confirming && !u.pending_command && plan_zero(&u.pending));
        assert(a->chat_count == queued && u.command->history.count == history);
        char retained[APP_TEXT + 1];
        assert(ed_to_utf8(&u.command->editor, retained, sizeof retained) != SIZE_MAX);
        assert(!strcmp(retained, command));
        assert(ed_from_utf8(&u.command->editor, "/whois Alice"));
        assert(run_submit(a, &u, "/whois Alice", 1) == 1);
        finish_command(a, &u, 1);
        assert(!strcmp(u.command->history.entries[0], "/whois Alice"));
        config_wipe(retained, sizeof retained);
    }

    /* A failed send leaves the editor available but no plan/history entry.
     * Later ordinary commands must remain recallable. */
    while (a->chat_count < APP_QUEUE) assert(app_outgoing(a, 1, "PRIVMSG Alice :ordinary queued text"));
    const char secret[] = "/msg CustomAuthBot IDENTIFY UI_HISTORY_SECRET_CANARY";
    assert(prompt_open(u.command, secret));
    size_t history = u.command->history.count;
    assert(run_submit(a, &u, secret, 1) == 0);
    assert(!u.pending_command && plan_zero(&u.pending) && u.command->active);
    assert(u.command->history.count == history && a->chat_count == APP_QUEUE);
    finish_command(a, &u, 0);
    assert(!u.command->active && plan_zero(&u.pending));
    prompt_destroy(u.command);
    cleanup(&f);
    config_wipe(command, sizeof command);
    puts("full-UI history boundary: actual direct/review send and prompt retention paths exclude split credentials; ordinary history, canonical counts, cancellation drafts and failed-send state pass; no curses runtime claim");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    ui = (root / "src/ui.c").read_text(encoding="ascii")
    prompt = (root / "src/prompt.c").read_text(encoding="ascii")
    submit = block(ui, "static int submit_text_command(")
    prefix = submit[submit.index("{") + 1:submit.index("size_t source_view")]
    send = block(submit, "if (r == 2)")
    confirm = block(block(ui, "static void handle_key("), "if (u->confirming)")
    source = PREFIX + block(prompt, "struct CommandPrompt {") + ";\n"
    for signature in ("CommandPrompt *prompt_create(", "void prompt_destroy(",
                      "int prompt_open(", "void prompt_close(", "void prompt_remember("):
        source += block(prompt, signature) + "\n"
    source += UI_TYPES + block(ui, "static void finish_command(") + "\n"
    source += ("static int run_submit(App *a, Ui *u, const char *text, int from_command)\n{\n"
               + prefix + "size_t source_view = from_command ? SIZE_MAX : a->current;\n"
               "int r = app_command(a, text, &u->pending);\n" + send + "\nreturn r;\n}\n")
    source += ("static void run_confirm(App *a, Ui *u, unsigned key, int special)\n{\n"
               "int enter = !special && (key == '\\r' || key == '\\n');\n"
               + confirm + "\n}\n" + TESTS)
    units = ["summary", "dcc", "idle_commands", "commands", "client", "app",
             "config", "autocommands", "protocol", "sim", "net", "credentials", "editor"]
    with tempfile.TemporaryDirectory(prefix="ui-history-boundary-") as temporary:
        directory = Path(temporary)
        generated = directory / "probe.c"
        binary = directory / "probe"
        generated.write_text(source, encoding="ascii")
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-D_POSIX_C_SOURCE=200809L", "-D_XOPEN_SOURCE=700", "-DONEKEY_ONLY", "-std=c11",
            "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I" + str(root / "src"),
            "-I" + str(root / "tests")]
        command += (["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
                    if args.sanitize else ["-O2"])
        command += [str(generated)] + [str(root / "src" / (unit + ".c")) for unit in units]
        command += ["-lssl", "-lcrypto", "-o", str(binary)]
        subprocess.run(command, check=True, timeout=60)
        environment = dict(os.environ)
        if args.sanitize:
            environment["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
            environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        subprocess.run([str(binary)], check=True, timeout=15, env=environment)


if __name__ == "__main__":
    main()
