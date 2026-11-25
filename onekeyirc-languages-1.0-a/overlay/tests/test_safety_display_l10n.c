/* Link with the generated tables, NOT a fake translation lookup. */
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const char *translated = irc_tr(argv[1]);
    if (!translated) return 3;
    fputs(translated, stdout);
    return 0;
}
