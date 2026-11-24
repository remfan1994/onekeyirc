/* r37: full generated locale catalogs and bounded literal field substitution. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
int main(void)
{
    const char *const langs[] = {"id","ms","tr","es","pt","fr","de","ru","it","ar"};
#ifdef ONEKEY_ONLY
    const char *const key = "File offer from {nick} rejected: {error}";
    const IrcDisplayField fields[] = {{"nick", "test%nick {code}"}, {"error", "literal %n {x} error"}};
    const char *const variable = "ONEKEYIRC_LANG";
#else
    const char *const key = "Automatic command {number} was not sent: {error}. Remaining automatic commands cancelled.";
    const char *const second = "Automatic command {number} was not sent: the message limit changed. Remaining automatic commands cancelled.";
    const IrcDisplayField fields[] = {{"number", "12"}, {"error", "literal %n {x} error"}};
    const IrcDisplayField just_number[] = {{"number", "4096"}};
    const char *const variable = "VIPIRC_LANG";
#endif
    char shown[1024];
    for (size_t i=0;i<sizeof(langs)/sizeof(*langs);++i) {
        assert(setenv("ONEKEYIRC_LANG", "", 1)==0);
        assert(setenv("VIPIRC_LANG", "", 1)==0);
        assert(setenv(variable, langs[i], 1)==0);
        assert(strcmp(irc_tr(key),key)!=0);
        assert(irc_display_render(shown,sizeof shown,key,fields,2));
#ifdef ONEKEY_ONLY
        assert(strstr(shown, "test%nick {code}"));
#else
        assert(strstr(shown, "12"));
        assert(strcmp(irc_tr(second),second)!=0);
        assert(irc_display_render(shown,sizeof shown,second,just_number,1));
        assert(strstr(shown, "4096"));
#endif
        assert(irc_display_render(shown,sizeof shown,key,fields,2));
        assert(strstr(shown, "literal %n {x} error"));
        assert(!strstr(shown, "{error}"));
        assert(!irc_display_render(shown,8,key,fields,2));
        assert(!shown[0]);
        const IrcDisplayField wrong[]={{"wrong", "raw"}, {"error", "message"}};
        assert(!irc_display_render(shown,sizeof shown,key,wrong,2));
        assert(!shown[0]);
    }
    assert(setenv(variable,"untranslated",1)==0);
    assert(strcmp(irc_tr(key),key)==0);
    assert(irc_display_render(shown,sizeof shown,key,fields,2));
    assert(strstr(shown,"literal %n {x} error"));
    puts("r37 10 locales, dynamic field order/literal values, bounds and fallback: PASS");
    return 0;
}
