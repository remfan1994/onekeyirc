/* r44: literal named fields and localized full-screen help. */
#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    static const char *langs[]={"id","ms","tr","es","pt","fr","de","ru","it","ar"};
    const char *english="Review rows {first}-{last} of {total} | Up/Down | PgUp/PgDn | Home/End";
    char out[2048];
    for (size_t i=0;i<10;++i) {
        assert(setenv("ONEKEYIRC_LANG",langs[i],1)==0);
        assert(strcmp(irc_tr("Page Up and Page Down scroll long question details."),"Page Up and Page Down scroll long question details.")!=0);
        const IrcDisplayField f[]={ {"first","1%{x}"}, {"last","5"}, {"total","51"} };
        assert(irc_display_render(out,sizeof out,english,f,3));
        assert(strstr(out,"1%{x}") && strstr(out,"51"));
        assert(!irc_display_render(out,10,english,f,3));
        assert(out[0]==0);

    }
    assert(setenv("ONEKEYIRC_LANG","en",1)==0);
    assert(strcmp(irc_tr(english),english)==0);
    puts("r44 literal-field and fallback tests PASS");
    return 0;
}
