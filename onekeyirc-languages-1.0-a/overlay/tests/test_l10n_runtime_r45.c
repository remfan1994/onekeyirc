/* r45: literal variable content, Unicode-safe bounded rendering and English fallback. */
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
    const char *english="Review S{number} {target}: {count} message ({bytes} UTF-8 bytes).";
    char out[2048];
    for (size_t i=0;i<10;++i) {
        assert(setenv("ONEKEYIRC_LANG",langs[i],1)==0);
        assert(strcmp(irc_tr("Draft cancelled; nothing sent."),"Draft cancelled; nothing sent.")!=0);
        const IrcDisplayField f[]={ {"number","2"}, {"target","#a%s{nick}"}, {"count","9"}, {"bytes","509"} };
        assert(irc_display_render(out,sizeof out,english,f,4));
        assert(strstr(out,"#a%s{nick}") && strstr(out,"509"));
        assert(!irc_display_render(out,10,english,f,4) && !out[0]);
        assert(!irc_display_fields_valid("{number} {target} {target} {count} {bytes}",f,4));

    }
    assert(setenv("ONEKEYIRC_LANG","en",1)==0);
    assert(strcmp(irc_tr(english),english)==0);
    puts("r45 literal-field and English fallback tests PASS");
    return 0;
}
