/* one key client must honor its own override before the shared vipirc alias. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
int main(void)
{
    assert(setenv("LC_ALL", "C", 1) == 0);
    assert(setenv("ONEKEYIRC_LANG", "tr_TR.UTF-8", 1) == 0);
    assert(setenv("VIPIRC_LANG", "id_ID.UTF-8", 1) == 0);
    assert(strcmp(irc_tr("Help"), "Yard\304\261m") == 0);
    assert(unsetenv("ONEKEYIRC_LANG") == 0);
    assert(strcmp(irc_tr("Help"), "Bantuan") == 0);
    assert(setenv("ONEKEYIRC_LANG", "", 1) == 0);
    assert(strcmp(irc_tr("Help"), "Bantuan") == 0);
    assert(setenv("ONEKEYIRC_LANG", "en", 1) == 0);
    assert(strcmp(irc_tr("Help"), "Help") == 0);
    assert(strcmp(irc_tr("/join"), "/join") == 0);
    return 0;
}
