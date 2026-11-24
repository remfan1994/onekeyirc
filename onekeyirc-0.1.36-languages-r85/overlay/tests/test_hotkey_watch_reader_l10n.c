#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#define main legacy_idle_commands_main
#include "test_idle_commands.c"
#undef main
static void expect(int translated, const char *key, const IrcDisplayField *f, size_t n) {
    char rendered[2048];
    assert(irc_display_render_lookup(rendered, sizeof rendered, key, f, n, translated ? irc_tr : NULL));
    if (!strstr(output, rendered)) fprintf(stderr, "Expected: %s\nActual: %s\n", rendered, output);
    assert(strstr(output, rendered));
}
static void readers(int translated) {
    Fixture f; init(&f, 1);
    f.commands.translate = translated ? irc_tr : NULL;
    assert(run(&f, "/watch add literal %n {term}") == IDLE_COMMAND_DONE);
    assert(run(&f, "/watch list") == IDLE_COMMAND_DONE);
    expect(translated,"Watch strings: nickname {nick} plus {count} literal terms",(IrcDisplayField[]){{"nick","tester"},{"count","1"}},2);
    assert(strstr(output,"literal %n {term}"));
    assert(run(&f, "/ignore add noisy!*@*") == IDLE_COMMAND_DONE);
    assert(run(&f, "/ignore list") == IDLE_COMMAND_DONE);
    expect(translated,"Ignore masks: {count}",(IrcDisplayField[]){{"count","1"}},1);
    assert(strstr(output,"noisy!*@*"));
    assert(run(&f, "/bindings actions") == IDLE_COMMAND_DONE);
    for(size_t i=0;i<config_hotkey_action_count();++i) {
        const ConfigHotkeyAction *a=config_hotkey_action_at(i);
        assert(strstr(output,a->name));
        assert(strstr(output,translated ? irc_tr(a->description) : a->description));
    }
    assert(run(&f, "/bind F12 compose") == IDLE_COMMAND_DONE);
    expect(translated,"Bound {key} to {action} for all servers. /save input saves this preference.",(IrcDisplayField[]){{"key","F12"},{"action","compose"}},2);
    assert(run(&f, "/bindings") == IDLE_COMMAND_DONE);
    expect(translated,"Custom key bindings: {count} (application-wide; none assigned by default)",(IrcDisplayField[]){{"count","1"}},1);
    assert(strstr(output,"F12") && strstr(output,"compose"));
    assert(run(&f,"/unbind F12") == IDLE_COMMAND_DONE);
    expect(translated,"Removed {key} custom binding. /save input saves this preference.",(IrcDisplayField[]){{"key","F12"}},1);
    assert(run(&f,"/server add second.invalid 6697 othernick") == IDLE_COMMAND_DONE);
    expect(translated,"Added server {number} with verified TLS and autoconnect off. /server {select} selects it; /connect then connects; /save persists.",(IrcDisplayField[]){{"number","1"},{"select","1"}},2);
    assert(run(&f,"/server disconnect 1") == IDLE_COMMAND_DONE);
    expect(translated,"Server {number}: {action} requested.",(IrcDisplayField[]){{"number","1"},{"action","disconnect"}},2);
    cleanup(&f);
}
int main(void) { readers(1); readers(0); puts("Hotkey, watch, ignore and server reader tests: PASS"); return 0; }
