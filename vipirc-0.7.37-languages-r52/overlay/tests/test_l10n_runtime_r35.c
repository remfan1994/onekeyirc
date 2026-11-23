/* Offline r35 locale/formatter regression: actual generated catalog data. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
int main(void) {
    const char *langs[] = {"id","ms","tr","es","pt","fr","de","ru","it","ar"};
    char shown[1024];
    const char *bad = "server.example/%s%n {name} ERROR";
    const IrcDisplayField error[] = {{"error", bad}};
    const IrcDisplayField conn[] = {{"server", "irc.example/%n{unsafe}"}, {"port", "6697"}};
    const IrcDisplayField retry[] = {{"seconds", "300"}};
    const char *english[] = {
       "Cannot connect: {error}",
       "Connecting to {server}:{port}",
       "Automatic reconnect in {seconds} seconds; /disconnect cancels it."
    };
    for (size_t i=0; i<sizeof langs/sizeof *langs; i++) {
        assert(setenv("VIPIRC_LANG", langs[i], 1) == 0);
        for (size_t j=0; j<3; j++) assert(strcmp(english[j],irc_tr(english[j])));
        assert(irc_display_render(shown,sizeof shown,english[0],error,1));
        assert(strstr(shown,bad));
        assert(irc_display_render(shown,sizeof shown,english[1],conn,2));
        assert(strstr(shown,"irc.example/%n{unsafe}") && strstr(shown,"6697"));
        assert(irc_display_render(shown,sizeof shown,english[2],retry,1));
        assert(strstr(shown,"300") && strstr(shown,"/disconnect"));
        /* Reject overlong output rather than silently truncating UTF-8. */
        assert(!irc_display_render(shown,7,english[1],conn,2));
        assert(!shown[0]);
    }
    assert(setenv("VIPIRC_LANG", "untranslated",1)==0);
    for(size_t j=0;j<3;j++)assert(irc_tr(english[j])==english[j]);
    puts("r35 10 locales x 3 runtime dynamic templates + English fallback: PASS");
    return 0;
}
