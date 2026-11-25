/* Locale regression for newly wired terminal UI display templates. */
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void must(int ok, const char *key) { if (!ok) { fprintf(stderr, "UI localization failed: %s\n", key); exit(1); } }
int main(void) {
    char out[1024], tiny[2];
    {
        const char *key = "{count} bytes";
        const IrcDisplayField fields[] = {{"count", "98765%{count}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 1), key);
        must(irc_display_render(out, sizeof out, key, fields, 1), key);
        must(strstr(out, "98765%{count}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 1) && tiny[0] == 0, key);
    }
    {
        const char *key = "{used}/{limit} bytes";
        const IrcDisplayField fields[] = {{"used", "98765%{used}"}, {"limit", "98765%{limit}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 2), key);
        must(irc_display_render(out, sizeof out, key, fields, 2), key);
        must(strstr(out, "98765%{used}") != NULL, key);
        must(strstr(out, "98765%{limit}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 2) && tiny[0] == 0, key);
    }
    {
        const char *key = "{bytes} UTF-8 bytes | {budget}/message | row {row} col {col}";
        const IrcDisplayField fields[] = {{"bytes", "98765%{bytes}"}, {"budget", "98765%{budget}"}, {"row", "98765%{row}"}, {"col", "98765%{col}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 4), key);
        must(irc_display_render(out, sizeof out, key, fields, 4), key);
        must(strstr(out, "98765%{bytes}") != NULL, key);
        must(strstr(out, "98765%{budget}") != NULL, key);
        must(strstr(out, "98765%{row}") != NULL, key);
        must(strstr(out, "98765%{col}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 4) && tiny[0] == 0, key);
    }
    {
        const char *key = "[stream: {count} earlier records omitted at the bounded history limit]";
        const IrcDisplayField fields[] = {{"count", "98765%{count}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 1), key);
        must(irc_display_render(out, sizeof out, key, fields, 1), key);
        must(strstr(out, "98765%{count}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 1) && tiny[0] == 0, key);
    }
    puts("terminal UI localization: PASS");
    return 0;
}
