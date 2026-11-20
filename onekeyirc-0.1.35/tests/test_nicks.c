/* Pure local completion tests: no transport, configuration I/O or terminal. */
#include "nicks.h"
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static App app;
static Editor editor;
static NickCompletion cycle;
static User roster[8], other_roster[8];

static void text_is(const char *expected)
{
    char actual[ED_MAX_BYTES + 1];
    assert(ed_to_utf8(&editor, actual, sizeof actual) == strlen(expected));
    assert(!strcmp(actual, expected));
    assert(editor.bytes == strlen(expected));
    assert(editor.pos <= editor.len);
}

static void input(const char *text, size_t pos)
{
    assert(ed_from_utf8(&editor, text));
    assert(pos <= editor.len);
    editor.pos = pos;
    nick_completion_reset(&cycle);
}

static void fixture(void)
{
    memset(&app, 0, sizeof app);
    memset(roster, 0, sizeof roster);
    memset(other_roster, 0, sizeof other_roster);
    app.buffer_count = 6;
    app.current = 3;
    app.casemap = IRC_CASE_RFC1459;
    strcpy(app.nick, "tester");
    app.buffers[0].kind = B_SERVER;
    app.buffers[1].kind = B_MENTIONS;
    app.buffers[2].kind = B_HELP;
    app.buffers[3].kind = app.buffers[4].kind = B_CHANNEL;
    app.buffers[3].joined = app.buffers[4].joined = 1;
    app.buffers[3].users = roster;
    app.buffers[4].users = other_roster;
    app.buffers[3].user_count = 3;
    strcpy(roster[0].nick, "Aster");
    strcpy(roster[1].nick, "Ally");
    strcpy(roster[2].nick, "Aspen");
    app.buffers[4].user_count = 2;
    strcpy(other_roster[0].nick, "Another");
    strcpy(other_roster[1].nick, "ALLY");
    app.buffers[5].kind = B_QUERY;
    strcpy(app.buffers[5].name, "QueryPeer");
    ed_init(&editor);
    nick_completion_reset(&cycle);
}

static void cycle_and_boundaries(void)
{
    fixture();
    input("a", 1);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("Ally: ");
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("Aspen: ");
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("Aster: ");
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("Ally: ");
    assert(nick_complete(&app, &editor, &cycle, -1) == 1); text_is("Aster: ");
    assert(nick_complete(&app, &editor, &cycle, -1) == 1); text_is("Aspen: ");
    input("A", 1);
    assert(nick_complete(&app, &editor, &cycle, -1) == 1); text_is("Aster: ");
    input("a: hello", 1);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("Ally: hello");
    assert(editor.pos == 4);
    input("hello Asxyz world", 8);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("hello Aspen world");
    assert(editor.pos == 11);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("hello Aster world");
    input("hello As", 8);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("hello Aspen ");
    input("(Al)", 3);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("(Ally)");
    input("", 0);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("Ally: ");
    const char *unchanged[] = {"Nobody", "#As", "https://As", "/As", "x.As", "caf\xc3\xa9" "As"};
    for (size_t i = 0; i < sizeof unchanged / sizeof unchanged[0]; ++i) {
        input(unchanged[i], 0); editor.pos = editor.len;
        Editor before = editor;
        assert(nick_complete(&app, &editor, &cycle, 1) == 0);
        assert(!memcmp(&editor, &before, sizeof editor));
        text_is(unchanged[i]);
    }
    const char *unicode_suffix[] = {"Al\xcc\x81", "Al\xe4\xb8\xad"};
    for (size_t i = 0; i < sizeof unicode_suffix / sizeof unicode_suffix[0]; ++i) {
        for (size_t position = 0; position <= 2; ++position) {
            input(unicode_suffix[i], position);
            Editor before = editor;
            assert(!nick_complete(&app, &editor, &cycle, 1));
            assert(!memcmp(&editor, &before, sizeof editor));
            assert(!nick_complete(&app, &editor, &cycle, -1));
            assert(!memcmp(&editor, &before, sizeof editor));
            text_is(unicode_suffix[i]);
        }
    }
}

static void contexts_and_live_membership(void)
{
    fixture();
    input("An", 2);
    assert(!nick_complete(&app, &editor, &cycle, 1)); text_is("An");
    app.current = 0;
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("Another: ");
    app.current = 3;
    input("A", 1);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("Ally: ");
    strcpy(roster[1].nick, "NewPeer");
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("Aspen: ");
    strcpy(roster[1].nick, "Assam");
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("Assam: ");
    app.current = 5;
    input("Q", 1);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("QueryPeer: ");
    input("t", 1);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("tester: ");
    input("A", 1);
    assert(!nick_complete(&app, &editor, &cycle, 1)); text_is("A");
    app.current = 1;
    app.buffers[4].joined = 0;
    input("An", 2);
    assert(!nick_complete(&app, &editor, &cycle, 1)); text_is("An");
    input("Q", 1);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("QueryPeer: ");
    app.buffers[4].joined = 1;
    strcpy(roster[1].nick, "Ally");
    input("Al", 2);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("Ally: ");
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("Ally: ");
}

static void casemapping(void)
{
    fixture();
    app.buffers[3].user_count = 1;
    strcpy(roster[0].nick, "[Nick]^");
    input("{ni", 3);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("[Nick]^: ");
    input("{Nick}~", 7);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("[Nick]^: ");
    app.casemap = IRC_CASE_STRICT;
    input("{Nick}~", 7);
    assert(!nick_complete(&app, &editor, &cycle, 1)); text_is("{Nick}~");
    input("{ni", 3);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("[Nick]^: ");
    app.casemap = IRC_CASE_ASCII;
    input("{ni", 3);
    assert(!nick_complete(&app, &editor, &cycle, 1)); text_is("{ni");
    input("[NI", 3);
    assert(nick_complete(&app, &editor, &cycle, 1) == 1); text_is("[Nick]^: ");
}

static void atomic_limits(void)
{
    fixture();
    ed_init(&editor);
    for (size_t i = 0; i < ED_MAX_CHARS - 2; ++i) assert(ed_insert(&editor, L'x'));
    assert(ed_insert(&editor, L' ')); assert(ed_insert(&editor, L'A'));
    Editor before = editor;
    assert(nick_complete(&app, &editor, &cycle, 1) == -1);
    assert(!memcmp(&editor, &before, sizeof editor));
    ed_init(&editor); nick_completion_reset(&cycle);
    for (size_t i = 0; i < (ED_MAX_BYTES - 2) / 3; ++i) assert(ed_insert(&editor, (wchar_t)0x4e00));
    while (editor.bytes < ED_MAX_BYTES - 2) assert(ed_insert(&editor, L'x'));
    assert(ed_insert(&editor, L' ')); assert(ed_insert(&editor, L'A'));
    before = editor;
    assert(nick_complete(&app, &editor, &cycle, 1) == -1);
    assert(!memcmp(&editor, &before, sizeof editor));
    assert(editor.bytes == ED_MAX_BYTES);
    assert(!nick_complete(NULL, &editor, &cycle, 1));
    assert(!nick_complete(&app, NULL, &cycle, 1));
    assert(!nick_complete(&app, &editor, NULL, 1));
}

int main(void)
{
    (void)setlocale(LC_ALL, "C");
    cycle_and_boundaries();
    contexts_and_live_membership();
    casemapping();
    atomic_limits();
    puts("nickname completion: bidirectional stable cycling, token boundaries, suffix/caret preservation, contexts, live roster mutation, IRC casemaps, duplicate suppression, and atomic byte/character limits passed");
    return 0;
}
