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
    const char *english="Nickname {nick} is already in use.{detail} Use /nick another-nick.";
    char out[2048];
    for (size_t i=0;i<10;++i) {
        assert(setenv("VIPIRC_LANG",langs[i],1)==0);
        assert(strcmp(irc_tr(" One in-flight message will finish.")," One in-flight message will finish.")!=0);
        const IrcDisplayField f[]={ {"nick","#a%s{nick}"}, {"detail"," error%{literal}"} };
        assert(irc_display_render(out,sizeof out,english,f,2));
        assert(strstr(out,"#a%s{nick}") && strstr(out,"error%{literal}"));
        assert(!irc_display_render(out,10,english,f,2) && !out[0]);
        assert(!irc_display_fields_valid("{nick} {nick} {detail}",f,2));

    }
    assert(setenv("VIPIRC_LANG","en",1)==0);
    assert(strcmp(irc_tr(english),english)==0);
    puts("r45 literal-field and English fallback tests PASS");
    return 0;
}
