/* r42: safe named-field display templates in ten locales, with literal untrusted values. */
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
    const char *langs[] = {"id", "ms", "tr", "es", "pt", "fr", "de", "ru", "it", "ar"};
    const char *template = "Catch-up from your most recent sent message | S{number} {target}";
    const IrcDisplayField vars[] = {{"number", "7"}, {"target", "raw%{name}\\alpha"}};
    const char *fixed = "Watch log";
    const size_t field_count=2;
    char out[1024];
    for (size_t i=0; i<sizeof langs/sizeof *langs; ++i) {
        assert(setenv("ONEKEYIRC_LANG", langs[i], 1)==0);
        assert(strcmp(irc_tr(fixed), fixed)!=0);
        assert(irc_display_render(out, sizeof out, template, vars, field_count));
        assert(strstr(out, "raw%{name}\\alpha")!=NULL);
        assert(!irc_display_render(out, 9, template, vars, field_count));
        assert(out[0]=='\0');
    }
    assert(setenv("ONEKEYIRC_LANG", "en", 1)==0);
    assert(strcmp(irc_tr(fixed),fixed)==0);
    assert(irc_display_render(out,sizeof out,template,vars,field_count));
    assert(strstr(out, "raw%{name}\\alpha")!=NULL);
    puts("r42 ten-language literal-field formatter and English fallback PASS");
    return 0;
}
