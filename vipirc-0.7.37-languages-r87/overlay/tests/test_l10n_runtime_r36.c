/* r36: real per-locale generated catalogs and safe literal numeric fields. */
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
    const char *keys[] = {"Input exceeds {limit} UTF-8 bytes.",
                          "The outgoing queue cannot hold all {count} messages; nothing queued."};
    const IrcDisplayField fields[][1] = {{{"limit", "16384"}}, {{"count", "63"}}};
    char out[512];
    for (size_t i=0;i<sizeof langs/sizeof *langs;i++) {
        assert(setenv("VIPIRC_LANG",langs[i],1)==0);
        for (size_t j=0;j<2;j++) {
            assert(strcmp(keys[j],irc_tr(keys[j]))!=0);
            assert(irc_display_render(out,sizeof out,keys[j],fields[j],1));
            assert(strstr(out,j==0?"16384":"63"));
            assert(!strstr(out,"{limit}") && !strstr(out,"{count}"));
            assert(!irc_display_render(out,4,keys[j],fields[j],1));
            assert(!out[0]);
        }
        const IrcDisplayField badnum[] = {{"count", "%n {name} \r\n file/user"}};
        assert(irc_display_render(out,sizeof out,keys[1],badnum,1));
        assert(strstr(out,"%n {name} \r\n file/user"));
        const IrcDisplayField wrong[] = {{"unknown", "999"}};
        assert(!irc_display_render(out,sizeof out,keys[1],wrong,1));
        assert(!out[0]);
    }
    assert(setenv("VIPIRC_LANG","untranslated",1)==0);
    assert(strcmp(irc_tr(keys[0]),keys[0])==0);
    assert(strcmp(irc_tr(keys[1]),keys[1])==0);
    puts("r36 10 locales x 2 numeric templates, literal data and English fallback: PASS");
    return 0;
}
