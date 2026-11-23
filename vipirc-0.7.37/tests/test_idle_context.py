#!/usr/bin/env python3
"""Native context help, suggestion safety, and summary reader regressions."""
import argparse

from test_idle_terminal import IdleSession, bare_start
from test_idle_setup import open_setup, setup_visible
from test_integration import IRCFixture

DOWN = b"\x1b[B"
DELETE = b"\x1b[3~"
ESCAPE = b"\x1b"


def selected(session, name):
    return any("".join(row).strip().startswith("> " + name) for row in session.screen.cells)


def help_visible(session, text):
    return (session.screen.alternate and not session.screen.cursor_visible and
            "Context help" in "".join(session.screen.cells[0]) and text in session.screen.text())


def exact_command_completion(full=False):
    for mode in ("common", "contextual"):
        with IdleSession(full=full, rows=24,
                         config_extra="[input]\ncommand_suggestions="+mode+"\n") as s:
            s.start()
            for exact, cycle in (("cl", ("cl", "clear", "close", "cl")),
                                 ("server", ("server", "servers", "server"))):
                s.type("/"+exact)
                s.wait(lambda: s.editor("/"+exact, len(exact)+1), "exact command draft missing")
                for name in cycle:
                    s.type(b"\t")
                    s.wait(lambda: s.editor(count=len(name)+2) and
                           any("".join(row).startswith("/"+name+" ") for row in s.screen.cells),
                           mode+" Tab completion lost the exact command or prefix cycle: /"+name)
                s.type("kept")
                s.wait(lambda: s.editor("/"+exact+" kept", len(exact)+6), "completed command lost its argument space")
                s.type(b"\t")
                s.quiet(.03)
                assert s.editor("/"+exact+" kept", len(exact)+6), "Tab completion replaced existing arguments"
                s.cancel()


def contextual_navigation(full=False):
    with IdleSession(full=full, rows=24,
                     config_extra="[input]\ncommand_suggestions=contextual\n") as s:
        s.start()
        s.type("/")
        s.wait(lambda: s.editor("/") and "Suggestions: Contextual" in s.screen.text(),
               "contextual suggestions were not drawn")
        s.type(DOWN)
        s.wait(lambda: selected(s, "/me"), "channel context did not prioritize /me")
        s.type("?")
        s.wait(lambda: help_visible(s, "/me is a suggestion on the command screen."),
               "selected command help omitted its containing screen")
        s.type(b"\r")
        s.wait(lambda: help_visible(s, "Commands are edited separately"),
               "parent-screen help link did not open")
        s.type(ESCAPE)
        s.wait(lambda: help_visible(s, "/me is a suggestion"), "help back lost the item page")
        s.type(ESCAPE)
        s.wait(lambda: selected(s, "/me") and "Context help" not in s.screen.text(),
               "closing help lost the selected suggestion")
        s.type(b"\r\r")
        s.wait(lambda: s.editor("/me ", 4), "selection did not insert /me for editing")
        s.type("literal?")
        s.wait(lambda: s.editor("/me literal?", 12), "question mark in command text was intercepted")
        s.type(DOWN + b"\r")
        s.wait(lambda: s.editor("/me literal?", 12), "selecting command usage discarded typed arguments")
        s.cancel()

        s.type("/quit")
        s.wait(lambda: s.editor("/quit", 5), "/quit prefix was not editable")
        s.type(DOWN + b"\r\r")
        # /quit [MESSAGE] completion adds its argument separator. Waiting for
        # the old five-byte draft can pass before any queued key is processed.
        # Screen.text() trims the trailing space; the byte count verifies it.
        s.wait(lambda: s.editor("/quit", 6), "queued Enter executed a selected /quit")
        s.quiet(.2)
        assert s.proc.poll() is None, "suggestion insertion executed /quit"
        s.type(DOWN)
        s.wait(lambda: selected(s, "/quit"), "quit suggestion could not be selected for resize")
        s.resize(9, 40)
        s.wait(lambda: s.editor("/quit", 6) and not selected(s, "/quit") and
               "Suggestions:" not in s.screen.text(),
               "tiny terminal retained an invisible selected suggestion")
        s.type(DOWN)
        s.wait(lambda: s.editor("/quit", 6) and not selected(s, "/quit") and
               "Resize the terminal" in s.screen.text(),
               "tiny terminal permitted hidden suggestion navigation")
        s.resize(24, 80)
        s.wait(lambda: s.editor("/quit", 6) and "Suggestions: Contextual" in s.screen.text(),
               "resize lost the command draft")
        s.cancel()

        s.command("/summary", "Session summary", keep_reader=True)
        marker = len(s.raw)
        s.type("r")
        s.wait(lambda: b"Session summary" in s.raw[marker:] and s.reader(),
               "summary refresh did not retain the reader")
        s.type("?")
        s.wait(lambda: help_visible(s, "/summary report is displayed"),
               "summary help did not identify its reader")
        s.type(ESCAPE)
        s.wait(lambda: s.reader() and "Session summary" in s.screen.text(),
               "summary help did not return to its snapshot")
        s.type("/")
        s.wait(lambda: s.editor("/") and "Suggestions: Contextual" in s.screen.text(),
               "command entry did not open from summary")
        s.type(DOWN)
        s.wait(lambda: selected(s, "/summary"), "summary origin reused hidden channel context")
        s.type(ESCAPE)
        s.wait(lambda: s.reader() and "Session summary" in s.screen.text(),
               "command cancellation did not return to summary")
        s.close_reader()

        s.command("/channel 0")
        s.type("/")
        s.wait(lambda: s.editor("/", 1), "server command editor missing")
        s.type(DOWN)
        s.wait(lambda: selected(s, "/join"), "server context did not prioritize /join")
        s.cancel()
        s.command("/query alice")
        s.type("/")
        s.wait(lambda: s.editor("/", 1), "query command editor missing")
        s.type(DOWN)
        s.wait(lambda: selected(s, "/me"), "query context did not prioritize actions")
        s.type(DOWN)
        s.wait(lambda: selected(s, "/whois"), "query context did not prioritize nickname information")
        s.cancel()
        s.type("?")
        s.wait(lambda: help_visible(s, "Query alice is selected in the watch log."),
               "watch help omitted the selected query")
        s.type(ESCAPE)
        s.wait(lambda: not s.screen.alternate, "watch help did not return to the log")
        s.type("literal?")
        s.wait(lambda: s.editor("literal?", 8), "question mark in chat text was intercepted")
        s.cancel()


def personal_suggestions(full=False):
    with IdleSession(full=full, rows=24,
                     config_extra="[input]\ncommand_suggestions=history\n") as s:
        s.start()
        s.command("/nick Alternate")
        s.type("/n")
        s.wait(lambda: s.editor("/n", 2), "history prefix was not displayed")
        s.type(b"\x1b[A")
        s.wait(lambda: s.editor("/nick Alternate"), "Up did not recall accepted history")
        s.type(DOWN)
        s.wait(lambda: s.editor("/n", 2), "Down while recalling history did not restore the original input")
        s.cancel()
        s.type("/nick")
        s.wait(lambda: s.editor("/nick", 5), "history prefix was not editable")
        s.type(DOWN)
        s.wait(lambda: selected(s, "/nick Alternate"), "recent command was not selectable")
        s.type(DELETE)
        s.wait(lambda: "No matching suggestions." in s.screen.text(), "Delete retained the selected history entry")
        s.cancel()
        s.command("/set command_suggestions frequent")
        for _ in range(3):
            s.command("/summary", "Session summary")
        s.type("/")
        s.wait(lambda: s.editor("/", 1), "frequent command editor missing")
        s.type(DOWN)
        s.wait(lambda: selected(s, "/summary"), "frequent mode did not count accepted commands")
        s.type(DELETE)
        s.wait(lambda: "Reset the selected command" in s.screen.text() and not selected(s, "/summary"),
               "Delete did not reset the selected command frequency")
        s.cancel()
        s.type("/summary")
        s.wait(lambda: s.editor("/summary", 8), "reset removed the command from the catalog")
        s.type(DOWN)
        s.wait(lambda: selected(s, "/summary"), "reset removed catalog completion")
        s.cancel()


def mentions_clear_keeps_source_history(full=False):
    other = IRCFixture()
    try:
        with IdleSession(full=full, live=True, rows=24, cols=120) as s:
            s.start()
            s.command("/server -plain -nick Other 127.0.0.1 %d" % other.port)
            s.wait(lambda: other.registered, "second local mention fixture did not register")
            s.command("/join #other")
            s.wait(lambda: b"joined #other" in s.raw, "second local mention channel did not join")
            records = ((1, "#test", b"CACHE_MENTION_PRIMARY"),
                       (2, "#other", b"CACHE_MENTION_SECONDARY"))
            s.peer.send(":alice!u@host PRIVMSG #test :tester CACHE_MENTION_PRIMARY")
            other.send(":alice!u@host PRIVMSG #other :Other CACHE_MENTION_SECONDARY")
            s.peer.send("PING :mentions-cache-primary-ready")
            other.send("PING :mentions-cache-secondary-ready")
            s.wait(lambda: any(line == b"PONG :mentions-cache-primary-ready" for _, line in s.peer.lines) and
                   any(line == b"PONG :mentions-cache-secondary-ready" for _, line in other.lines),
                   "mention fixtures did not pass their network barriers")
            for number, channel, record in records:
                s.command("/server %d" % number)
                marker = len(s.raw)
                s.command("/mentions")
                s.wait(lambda: record in s.raw[marker:], "server mention was not retained before clear")
            s.command("/mentions clear", "Cleared retained mentions across all servers.")
            for number, channel, record in records:
                marker = len(s.raw)
                s.command("/server %d" % number)
                # Opening a reader waits for the preceding watch output to
                # drain, so this absence check does not rely on a short sleep.
                s.command("/help mentions", keep_reader=True)
                assert record not in s.raw[marker:], "cleared mentions replayed from a server's stream cache"
                s.close_reader()
                marker = len(s.raw)
                s.command("/channel "+channel)
                s.wait(lambda: record in s.raw[marker:], "clearing mentions removed original conversation text")
    finally:
        other.close()
    print("native mentions clear: two local servers lose cached mention replays while original conversation history remains", flush=True)


def unsaved_configuration_has_no_usage_files(full=False):
    args = (["--idle"] if full else []) + ["--no-connect"]
    with IdleSession(full=full, args=args, rows=24) as s:
        def files():
            return {str(path.relative_to(s.directory)):
                    (path.stat().st_mode & 0o777, path.read_bytes() if path.is_file() else None)
                    for path in s.directory.rglob("*")}
        before = files()
        bare_start(s)
        s.command("/set command_suggestions frequent")
        s.command("/summary", "Session summary")
        s.command("/summary", "Session summary")
        s.type("/su")
        s.wait(lambda: s.editor("/su", 3), "unsaved profile command editor missing")
        s.type(DOWN)
        s.wait(lambda: selected(s, "/summary"), "unsaved profile did not retain in-memory usage")
        s.type(DELETE)
        s.wait(lambda: "Reset the selected command" in s.screen.text(),
               "unsaved profile usage could not be reset")
        s.cancel()
        s.type(b"\x03")
        s.wait(lambda: s.proc.poll() is not None, "unsaved native session did not exit")
        assert files() == before, "ordinary commands or count deletion created files before configuration Save"


def setup_help_preserves_answer(full=False):
    with IdleSession(full=full, rows=24) as s:
        s.start()
        open_setup(s, "quickstart", full)
        s.type("?")
        s.wait(lambda: setup_visible(s, "Server") and s.screen.cursor_visible and
               any("".join(row).strip() == "?" for row in s.screen.cells),
               "empty setup field intercepted literal question mark")
        s.type(b"\t")
        s.wait(lambda: "Quickstart" in "".join(s.screen.cells[0]) and
               "".join(s.screen.cells[2]).strip() == "Server" and not s.screen.cursor_visible,
               "Tab did not select the setup question")
        s.type("?")
        s.wait(lambda: help_visible(s, "Server is an option on the Quickstart"),
               "setup question help omitted field or screen")
        s.type(b"\r")
        s.wait(lambda: help_visible(s, "private configuration draft"), "setup screen help link failed")
        s.type(ESCAPE)
        s.wait(lambda: help_visible(s, "Server is an option"), "setup help back lost the item")
        s.type(ESCAPE)
        s.wait(lambda: "Quickstart" in "".join(s.screen.cells[0]) and
               "".join(s.screen.cells[2]).strip() == "Server" and not s.screen.cursor_visible,
               "setup help lost question focus")
        assert any("".join(row).strip() == "?" for row in s.screen.cells), "help discarded the setup answer"
        s.type(b"\t")
        s.wait(lambda: setup_visible(s, "Server") and s.screen.cursor_visible, "Tab did not restore setup editing")
        s.type(ESCAPE)
        s.wait(lambda: not s.screen.alternate, "setup cancellation did not return to log")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full", action="store_true")
    args = parser.parse_args()
    exact_command_completion(args.full)
    contextual_navigation(args.full)
    personal_suggestions(args.full)
    mentions_clear_keeps_source_history(args.full)
    unsaved_configuration_has_no_usage_files(args.full)
    setup_help_preserves_answer(args.full)
    print("native context: suggestion origins, insert-before-submit, help links, summary refresh, personal deletion, unsaved-profile no writes and literal question marks passed", flush=True)


if __name__ == "__main__":
    main()
