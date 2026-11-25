/* r41: Test named-field localization without format interpretation. */
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
    const char *langs[]={"id","ms","tr","es","pt","fr","de","ru","it","ar"};
    const char *fixed="Configuration saved. Reconnect to use connection changes.";
    const char *templ="Query with {nick}; type a message to send.";
    const IrcDisplayField fields[]={{"nick", "raw%{name}\\value"}};
    char out[1024];
    for (size_t i=0;i<sizeof langs/sizeof *langs;++i) {
        assert(!setenv("VIPIRC_LANG",langs[i],1));
        assert(strcmp(irc_tr(fixed),fixed));
        assert(irc_display_render(out,sizeof out,templ,fields,1));
        assert(strstr(out,"raw%{name}\\value"));
        assert(!irc_display_render(out,7,templ,fields,1));
        assert(out[0]=='\0');
    }
    assert(!setenv("VIPIRC_LANG","en",1));
    assert(!strcmp(irc_tr(fixed),fixed));
    assert(irc_display_render(out,sizeof out,templ,fields,1));
    puts("r41 ten-language formatter and English fallback PASS");
    return 0;
}
