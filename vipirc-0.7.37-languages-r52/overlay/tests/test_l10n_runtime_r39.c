/* r39 regression: per-locale C lookup and literal dynamic rendering. */
#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    const char *langs[] = {"id","ms","tr","es","pt","fr","de","ru","it","ar"};
    const char *variable = "VIPIRC_LANG";
    const char *fixed = "About vipirc";
    for (size_t i = 0; i < sizeof(langs)/sizeof(*langs); ++i) {
        assert(setenv(variable, langs[i], 1) == 0);
        assert(strcmp(irc_tr(fixed), fixed) != 0);
        const char *templ = "Connected as {nick}. /help opens the manual.";
        const IrcDisplayField fields[] = {{"nick", "peer%{xyz}\n"}};
        char out[768];
        assert(strcmp(irc_tr(templ),templ) != 0);
        assert(irc_display_render(out,sizeof out,templ,fields,1));
        assert(strstr(out,"peer%{xyz}\n"));
        assert(!strstr(out,"{nick}"));
        const char *left = "Left {channel}; history retained. /close removes its view.";
        const IrcDisplayField chan[] = {{"channel", "#alpha%s{raw}"}};
        assert(irc_display_render(out,sizeof out,left,chan,1));
        assert(strstr(out,"#alpha%s{raw}"));
        assert(!irc_display_render(out,8,left,chan,1));
        assert(out[0] == 0);
    }
    assert(setenv(variable, "en", 1)==0);
    assert(strcmp(irc_tr(fixed),fixed)==0);
    puts("r39 10 locale runtime and fallback PASS");
    return 0;
}
