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
    const char *variable = "ONEKEYIRC_LANG";
    const char *fixed = "Response reached its 512 KiB limit; request a narrower topic.";
    for (size_t i = 0; i < sizeof(langs)/sizeof(*langs); ++i) {
        assert(setenv(variable, langs[i], 1) == 0);
        assert(strcmp(irc_tr(fixed), fixed) != 0);
        char out[128];
        const IrcDisplayField field[] = {{"nick", "user%s{opaque}"}};
        assert(irc_display_render(out,sizeof out,"Seen {nick}",field,1));
        assert(strstr(out,"user%s{opaque}"));
    }
    assert(setenv(variable, "en", 1)==0);
    assert(strcmp(irc_tr(fixed),fixed)==0);
    puts("r39 10 locale runtime and fallback PASS");
    return 0;
}
