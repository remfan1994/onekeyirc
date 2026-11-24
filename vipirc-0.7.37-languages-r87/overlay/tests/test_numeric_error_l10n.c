/* Numeric error and channel-list translation: bounded, named-field lookups.
 * Source text is trusted; server names, explanations and counters are data. */
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const keys[] = {
    "Cannot find {target}: {message}",
    "Cannot send to {target}: {message}",
    "Cannot use {target}: {message}",
    "Cannot join {target}: {message}",
    "Cannot change {target}: {message}",
    "Server does not recognize {target}: {message}",
    "More information is required for {target}: {message}",
    "Channel list complete: {received} received, {matched} matched, {cached} cached.",
    "Channel list complete: {received} received, {matched} matched, {cached} cached (cache limit reached).",
    "End of channel list: {received} received, {matched} matched.",
    "End of channel list: {received} received, {matched} matched; cache truncated."
};
int main(void)
{
    const IrcDisplayField f[] = {
        {"target", "#room{raw}%p"},
        {"message", "server {why} 100%n"},
        {"received", "18446744073709551615"},
        {"matched", "5000"}, {"cached", "0"}
    };
    size_t checked = 0;
    for (size_t k = 0; k < sizeof keys / sizeof keys[0]; ++k) {
        IrcDisplayField needed[3];
        size_t n=0;
        for (const char *p=keys[k]; *p; ++p) {
            if (*p != '{') continue;
            const char *end=strchr(p+1,'}');
            if (!end) return 1;
            size_t i;
            for (i=0; i<sizeof f/sizeof f[0]; ++i)
                if (strlen(f[i].name)==(size_t)(end-p-1) &&
                    !memcmp(f[i].name,p+1,(size_t)(end-p-1))) break;
            if (i == sizeof f/sizeof f[0] || n >= 3) return 2;
            needed[n++]=f[i]; p=end;
        }
        char output[1024];
        if (!irc_display_render(output,sizeof output,keys[k],needed,n)) {
            fprintf(stderr,"bad template: %s\n",keys[k]); return 3;
        }
        for (size_t i=0; i<n; ++i)
            if (!strstr(output,needed[i].value)) {
                fprintf(stderr,"lost untrusted field: %s\n",output); return 4;
            }
        char tiny[4]={'x','x','x','x'};
        if (irc_display_render(tiny,sizeof tiny,keys[k],needed,n) || tiny[0]) return 5;
        checked++;
    }
    printf("numeric error / channel-list localization: %zu safe templates PASS\n",checked);
    return 0;
}
