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
    const char *english="{channel} has {users} listed users. Topic: {topic}\n\nEnter joins the selected channel. Left/Right changes the sort column; S reverses its direction. R repeats the current LIST request with the same filters.";
    char out[2048];
    for (size_t i=0;i<10;++i) {
        assert(setenv("VIPIRC_LANG",langs[i],1)==0);
        assert(strcmp(irc_tr("These lists show views already open in the selected server session. Up/Down selects a view; Left/Right selects Open, New, Part or Close as appropriate; Enter performs the selected action. Channel 0 is the server. /cl and /channels show local channel views; /list [MASK] [OPTIONS] opens the sortable network channel directory. /ql shows private conversations."),"These lists show views already open in the selected server session. Up/Down selects a view; Left/Right selects Open, New, Part or Close as appropriate; Enter performs the selected action. Channel 0 is the server. /cl and /channels show local channel views; /list [MASK] [OPTIONS] opens the sortable network channel directory. /ql shows private conversations.")!=0);
        const IrcDisplayField f[]={ {"channel","#unsafe%{nick}"}, {"users","12"}, {"topic","topic %s {literal}"} };
        assert(irc_display_render(out,sizeof out,english,f,3));
        assert(strstr(out,"#unsafe%{nick}") && strstr(out,"topic %s {literal}"));
        assert(!irc_display_render(out,10,english,f,3));
        assert(out[0]==0);

    }
    assert(setenv("VIPIRC_LANG","en",1)==0);
    assert(strcmp(irc_tr(english),english)==0);
    puts("r44 literal-field and fallback tests PASS");
    return 0;
}
