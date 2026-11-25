/* Isolated test of the actual production network-display adapter. */
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/network_display_l10n.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    char output[512];
    if (argc != 2) return 2;
    irc_localize_network_error(output, sizeof output, strcmp(argv[1], "@null") == 0 ? NULL : argv[1]);
    fputs(output, stdout);
    return 0;
}
