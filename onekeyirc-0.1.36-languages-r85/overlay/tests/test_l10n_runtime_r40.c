/* r40: compiled 10-language literal data, fallback and bounds. */
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
    const char *variable="ONEKEYIRC_LANG";
    const char *fixed="command response reader";
    const char *template="{item} is displayed in the command response reader.";
    const IrcDisplayField fields[]={{"item", "raw%{item}/ok"}};
    const size_t count=sizeof fields/sizeof fields[0];
    char out[1024];
    for (size_t i=0;i<sizeof langs/sizeof *langs;++i) {
        assert(!setenv(variable,langs[i],1));
        assert(strcmp(irc_tr(fixed),fixed));
        assert(strcmp(irc_tr(template),template));
        assert(irc_display_render(out,sizeof out,template,fields,count));
        assert(strstr(out,"raw%{"));
        assert(strstr(out,"/ok"));
        assert(!irc_display_render(out,6,template,fields,count));
        assert(!out[0]);
    }
    assert(!setenv(variable,"en",1));
    assert(!strcmp(irc_tr(fixed),fixed));
    assert(irc_display_render(out,sizeof out,template,fields,count));
    assert(strstr(out,"raw%{"));
    puts("r40 10-locale display formatter and fallback PASS");
    return 0;
}
