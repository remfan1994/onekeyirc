/* r47: translated DCC/UI and auth/bouncer text, English fallback. */
#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    const char *langs[] = {"id","ms","tr","es","pt","fr","de","ru","it","ar"};
    const char *keys[] = {
    "Servers:",
    "Unread conversations:",
    "Authentication: bypassed in offline test mode.",
    "Required SASL: off",
    "File paths are shown only by this explicit inquiry. No credential file was read and no password content is displayed.",
    "Automatic retries keep current authentication. /disconnect then /connect applies pending settings; /save persists them.",
    "Bouncer profiles restore their own channel memberships; saved auto-join lists are not replayed.",
    "Use /join and /part while connected, or /channels to inspect restored channels.",
    };
    size_t count=sizeof keys/sizeof keys[0];
    for (size_t i=0;i<sizeof langs/sizeof langs[0];++i) {
        assert(setenv("VIPIRC_LANG", langs[i], 1)==0);
        for (size_t j=0;j<count;++j) {
            const char *translated=irc_tr(keys[j]);
            assert(translated && strcmp(translated,keys[j])!=0);
            assert(strlen(translated)<=900);
        }
        /* Untrusted names, paths, braces and formatting markers remain data. */
        const char *payload="user%p:{untrusted}/tmp/a";
        assert(strcmp(irc_tr(payload),payload)==0);
    }
    assert(setenv("VIPIRC_LANG","en",1)==0);
    for(size_t j=0;j<count;++j) assert(strcmp(irc_tr(keys[j]),keys[j])==0);
    puts("r47: ten-locale translated lookup, literal data and English fallback PASS");
    return 0;
}
