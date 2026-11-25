#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(irc_tr("Use /panel to change this bouncer's login or password source; /autoauth status shows its settings.") && *irc_tr("Use /panel to change this bouncer's login or password source; /autoauth status shows its settings."));
    assert(irc_tr("This bouncer manages channel membership. Use /join or /part while connected, or /panel to change connection type.") && *irc_tr("This bouncer manages channel membership. Use /join or /part while connected, or /panel to change connection type."));
    assert(irc_tr("Nickname must be a valid IRC nickname; use -nick NICK before the hostname.") && *irc_tr("Nickname must be a valid IRC nickname; use -nick NICK before the hostname."));
    assert(irc_tr("A +PORT requests TLS and cannot be combined with -plain.") && *irc_tr("A +PORT requests TLS and cannot be combined with -plain."));
    assert(irc_tr("Port must be a number from 1 to 65535; use -nick NICK for a nickname.") && *irc_tr("Port must be a number from 1 to 65535; use -nick NICK for a nickname."));
    assert(irc_tr("Extra connection arguments rejected. Use -nick NICK; passwords are configured with /autoauth or /panel.") && *irc_tr("Extra connection arguments rejected. Use -nick NICK; passwords are configured with /autoauth or /panel."));
    assert(irc_tr("Enter a supported /command; /help lists commands. Invalid controls or oversized input are rejected.") && *irc_tr("Enter a supported /command; /help lists commands. Invalid controls or oversized input are rejected."));
    assert(irc_tr("Cleared retained mentions across all servers.") && *irc_tr("Cleared retained mentions across all servers."));
    assert(irc_tr("onekeyirc always uses the idle interface; ncurses requires vipirc. The saved preference is unchanged.") && *irc_tr("onekeyirc always uses the idle interface; ncurses requires vipirc. The saved preference is unchanged."));
    puts("Connection and bouncer strings: PASS");
    return 0;
}
