/* r46 catalog lookup: user-visible strings only; arbitrary data stays literal. */
#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#define ONEKEY_ONLY
#include "l10n.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    const char *lang[] = {"id","ms","tr","es","pt","fr","de","ru","it","ar"};
    const char *keys[] = {
        "No such server number. Servers start at 1; /servers lists them. Panel 0 contains application preferences.",
        "No such server number. Use /servers to list available slots.",
        "No matching channels received yet.",
        "No cached channels match the current LIST filters.",
    };
    for (size_t i = 0; i < sizeof lang / sizeof lang[0]; ++i) {
        assert(setenv("ONEKEYIRC_LANG", lang[i], 1) == 0);
        for (size_t j = 0; j < sizeof keys / sizeof keys[0]; ++j) {
            const char *translated = irc_tr(keys[j]);
            assert(translated && strcmp(translated,keys[j]) != 0);
            assert(strlen(translated) <= 256);
        }
        /* User-controlled or protocol text is data, never translated by accident. */
        assert(strcmp(irc_tr("nickname%p: {literal}"),"nickname%p: {literal}") == 0);
    }
    assert(setenv("ONEKEYIRC_LANG", "en", 1) == 0);
    for (size_t j = 0; j < sizeof keys / sizeof keys[0]; ++j)
        assert(strcmp(irc_tr(keys[j]), keys[j]) == 0);
    puts("r46 ten-locale lookup and literal English fallback PASS");
    return 0;
}
