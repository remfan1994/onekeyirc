/* Compiled separately to verify single-source English-only builds. */
#define IRC_L10N_IMPLEMENTATION
#define IRC_DISABLE_LOCALIZATION
#include <assert.h>
#include <string.h>
#include "l10n.h"
int main(void)
{
    assert(irc_tr(NULL) == NULL);
    assert(strcmp(irc_tr("Settings"), "Settings") == 0);
    assert(strcmp(irc_tr("Help"), "Help") == 0);
    assert(strcmp(irc_tr("/join"), "/join") == 0);
    return 0;
}
