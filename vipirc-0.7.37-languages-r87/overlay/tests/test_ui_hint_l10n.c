#define _XOPEN_SOURCE 700
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/ui_hint_l10n.h"
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <wchar.h>
static int cells(const char *text, int max) {
    mbstate_t state = {0}; int count = 0;
    while (*text) {
        wchar_t wc; size_t n = mbrtowc(&wc, text, MB_CUR_MAX, &state);
        assert(n != (size_t)-1 && n != (size_t)-2 && n > 0);
        int w = wcwidth(wc); if (w < 0) w = 1;
        if (w > max - count) break;
        count += w; text += n;
    }
    return count;
}
int main(void) {
    assert(setlocale(LC_CTYPE, "C.UTF-8") || setlocale(LC_CTYPE, "en_US.UTF-8"));
    const char *keys[] = {
        "Up/Down: item | Left/Right: action | Enter: run | ! mentions",
        "Arrows: item/action",
        "Up/Down: item | Left/Right: action | Enter: run | Esc: chat",
        "Enter: run | Esc: chat",
        "Up/Down: channel | Left/Right: sort column | s: direction | Enter: join | r: refresh | /: command | Esc: back",
        "Enter: join | s: sort | Esc: back",
        "Up/Down: scroll | Left/Right: action | /: search | n/N: next/previous | Esc: back",
        "/: search | Esc: back",
        "Up/Down: item | Enter: open | /: command | Esc: quit",
        "Esc: quit",
        "Up/Down: scroll | Left/Right: action | r: refresh | Esc: vipirc",
        "r: refresh | Esc: back",
        "Up/Down: scroll | Esc: vipirc",
        "Settings opened. Chat draft preserved.",
        "Command cancelled; chat draft preserved.",
        "Draft preserved; keep editing. Nothing sent.",
        "View kept open; draft preserved.",
        "Not sent; edit your draft.",
        "Returned to the unfinished command at its original conversation; chat draft preserved.",
        "Cannot initialize the full interface.",
    };
    const char *localized = getenv("TEST_EXPECT_TRANSLATION");
    for (size_t i=0;i<sizeof keys/sizeof keys[0];++i) {
        const char *v=irc_tr(keys[i]);
        assert(v && *v);
        assert((strcmp(v, keys[i]) != 0) == (localized && *localized));
        assert(cells(v, INT_MAX)>0);
    }
    const char *full=keys[0], *compact=keys[1];
    int f=cells(irc_tr(full), INT_MAX), c=cells(irc_tr(compact), INT_MAX);
    assert(f>c && c>1);
    assert(irc_ui_hint(full,compact,f,cells)==irc_tr(full));
    assert(irc_ui_hint(full,compact,c,cells)==irc_tr(compact));
    assert(!irc_ui_hint(full,compact,c-1,cells));
    assert(!irc_ui_hint(full,compact,0,cells));
    int label=cells(irc_tr("Find"),INT_MAX);
    assert(irc_ui_search_start(label+12,cells)==label+3);
    assert(irc_ui_search_start(label+11,cells)==1);
    assert(irc_ui_search_start(80,cells)==label+3);
    assert(irc_ui_search_start(3,cells)==1);
    puts("ncurses hint and search geometry checks passed");
    return 0;
}
