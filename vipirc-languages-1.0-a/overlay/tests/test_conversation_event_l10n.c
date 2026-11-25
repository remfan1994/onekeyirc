/* Source-linked conversation-event localization fixture.
 * This uses the real generated lookup and named-field renderer. */
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const templates[] = {
    "CTCP from {nick}: {text}",
    "Leaving late or unrequested JOIN to {channel}.",
    "{nick} has joined {channel}",
    "{nick} has left {channel}",
    "{nick} has left {channel}: {reason}",
    "{nick} has disconnected",
    "{nick} has disconnected: {reason}",
    "{nick} is now known as {newnick}",
    "{nick} removed {target} from {channel}",
    "{nick} removed {target} from {channel}: {reason}",
    "{nick} changed modes: {modes}",
    "{nick} changed modes on {target}: {modes}",
    "{nick} invited you to {channel}",
    "{nick} invited {target} to {channel}",
    "Topic: {topic}",
};

int main(void)
{
    const IrcDisplayField inputs[] = {
        {"nick", "Alice%p"}, {"channel", "#test{evil}"},
        {"reason", "bye%n_UTF8"}, {"newnick", "N%<ick>"},
        {"target", "Bob%s"}, {"modes", "+o-v"},
        {"topic", "Topic {raw} 100%"}, {"text", "CTCP{raw}%n"}
    };
    const size_t templates_count = sizeof templates / sizeof templates[0];
    for (size_t t=0; t<templates_count; ++t) {
        const char *source=templates[t];
        IrcDisplayField needed[4];
        size_t count=0;
        for (const char *p=source; *p; ++p) {
            if (*p!='{') continue;
            const char *end=strchr(p+1, '}');
            if (!end) return 10;
            size_t j=0;
            for (;j<sizeof inputs/sizeof inputs[0];++j)
                if (strlen(inputs[j].name)==(size_t)(end-p-1) &&
                    !memcmp(p+1,inputs[j].name,(size_t)(end-p-1))) break;
            if (j==sizeof inputs/sizeof inputs[0] || count==4) return 11;
            needed[count++]=inputs[j];
            p=end;
        }
        char out[1024];
        if (!irc_display_render(out,sizeof out,source,needed,count)) {
            fprintf(stderr,"render failed at %zu: %s\n",t,source); return 12;
        }
        for(size_t i=0;i<count;++i)
            if (!strstr(out,needed[i].value)) {
                fprintf(stderr,"unsafe field substitution at %zu: %s\n",t,out); return 13;
            }
        if (!strcmp(out,source) || !*out) return 14;
        if (t == 2 || t == 9 || t == 14) printf("%s\n",out);
        char too_short[4]={'x','x','x','x'};
        if (irc_display_render(too_short,sizeof too_short,source,needed,count)
            || too_short[0]!='\0') return 15;
    }
    fprintf(stderr,"conversation-event localization: %zu keys passed\n",templates_count);
    return 0;
}
