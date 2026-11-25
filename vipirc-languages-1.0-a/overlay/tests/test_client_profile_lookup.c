/* SOURCE-LINKED client.c localization test: compile with the real client
 * and generated catalogs, not a fixed English test stub. */
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include "client.h"
#include <stdio.h>
#include <string.h>
int main(void)
{
    static const char *const keys[] = {
        "Saved server profile is missing.",
        "Saved server profile needs a server.",
        "Invalid client or primary configuration path.",
        "Saved default server is unavailable:",
        "No such server session.",
        "Cannot allocate this server session.",
        "Cannot initialize this offline simulation.",
        "Enter a server before adding it.",
        "Server limit reached (8); close a secondary session to make room.",
        "Server identity counter exhausted; restart the application.",
        "The primary server stays available; disconnect it instead.",
        "OFFLINE TEST MODE: saving server profiles is disabled.",
        "Settings saved; command usage could not be saved:",
        "OFFLINE TEST MODE: saving configuration is disabled.",
        "Cannot save a server profile that could not be loaded.",
        "Choose a server with /connect HOST before /saveconf.",
        "Server settings and default selection were written, but durability confirmation failed:",
        "Server settings were saved, but the default selection was not changed:",
        "No application configuration is available.",
    };
    char error[512];
    for (size_t i=0; i<sizeof(keys)/sizeof(keys[0]); ++i)
        puts(irc_tr(keys[i]));
    /* Exercise the real client error path, not just catalog lookups. */
    if (client_init(NULL, NULL, NULL, NULL, 0, 0, error, sizeof error) != -1)
        return 3;
    puts(error);
    return 0;
}
