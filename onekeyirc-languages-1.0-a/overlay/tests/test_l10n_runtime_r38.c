/* r38 runtime probe: translated display templates never become format strings. */
#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    static const char *const languages[] = {"id","ms","tr","es","pt","fr","de","ru","it","ar"};
    const char *const variable = "ONEKEYIRC_LANG";
    const char *const fixed = "Your nickname on IRC.";
    for (size_t i = 0; i < sizeof languages / sizeof languages[0]; ++i) {
        if (setenv(variable, languages[i], 1)) return 1;
        const char *translated = irc_tr(fixed);
        if (!translated || !strcmp(translated, fixed)) { fputs("untranslated fixed key\n",stderr);return 2; }
        char output[1024];
        const IrcDisplayField fields[] = { {"nick", "alice%n{wire}"}, {"error", "reply %s {literal}"} };
        if (!irc_display_render(output, sizeof output,
                "File offer from {nick} rejected: {error}", fields, 2)) return 3;
        if (!strstr(output, "alice%n{wire}") || !strstr(output, "reply %s {literal}")) return 4;
        char small[8];
        if (irc_display_render(small, sizeof small,
                "File offer from {nick} rejected: {error}", fields, 2)) return 5;
        if (small[0] != 0) return 6;
        const IrcDisplayField wrong[] = { {"bogus", "value"} };
        if (irc_display_render(output, sizeof output, "Name {nick}", wrong, 1)) return 7;
    }
    if (setenv(variable, "en", 1)) return 8;
    if (strcmp(irc_tr(fixed), fixed)) return 9;
    puts("r38 ten-language literal rendering and English fallback: PASS");
    return 0;
}
