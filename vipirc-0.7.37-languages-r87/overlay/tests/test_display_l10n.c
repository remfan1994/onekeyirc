/* Standalone r34 field-rendering regression: no curses or network needed. */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char *chosen;
static const char *irc_tr(const char *english) { return chosen ? chosen : english; }
#include "../src/display_l10n.h"

int main(void)
{
    char out[512];
    const IrcDisplayField one[] = {{"view", "#alpha%s%n{other}"}};
    const char *english = "Returned to {view}.";
    chosen = NULL;
    assert(irc_display_render(out, sizeof out, english, one, 1));
    assert(!strcmp(out, "Returned to #alpha%s%n{other}."));
    chosen = "Vista {view}: lista.";
    assert(irc_display_render(out, sizeof out, english, one, 1));
    assert(!strcmp(out, "Vista #alpha%s%n{other}: lista."));
    /* Never accept missing, repeated, unknown, or unbalanced fields. */
    const char *bad[] = {"Vista lista.", "{view} {view}", "{other}",
                         "{view", "{view}}", "{view}{", "{"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        chosen = bad[i];
        assert(irc_display_render(out, sizeof out, english, one, 1));
        assert(!strcmp(out, "Returned to #alpha%s%n{other}."));
    }
    chosen = "Correct {view}, 100% literal %n.";
    assert(irc_display_render(out, sizeof out, english, one, 1));
    assert(strstr(out, "100% literal %n.") != NULL);
    const IrcDisplayField two[] = {{"first", "100"}, {"second", "[user]%s"}};
    chosen = "{second} then {first}";
    assert(irc_display_render(out, sizeof out, "{first} / {second}", two, 2));
    assert(!strcmp(out, "[user]%s then 100"));
    chosen = "{second} then {first}";
    assert(!irc_display_render(out, 5, "{first} / {second}", two, 2));
    assert(out[0] == 0); /* no partial UTF-8 or message leakage */
    assert(!irc_display_render(out, 0, english, one, 1));
    const IrcDisplayField invalid[] = {{"view", NULL}};
    assert(!irc_display_render(out, sizeof out, english, invalid, 1));
    assert(!irc_display_render(out, sizeof out, english, one, 0));
    puts("r34 named-field formatter: bounds, reordering, literal data, fallback PASS");
    return 0;
}
