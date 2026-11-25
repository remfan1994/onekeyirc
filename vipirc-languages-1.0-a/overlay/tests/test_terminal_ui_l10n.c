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
    {
        const char *key = "Session uptime: {hours}h {minutes}m {seconds}s";
        const IrcDisplayField fields[] = {{"hours", "98765%{hours}"}, {"minutes", "98765%{minutes}"}, {"seconds", "98765%{seconds}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 3), key);
        must(irc_display_render(out, sizeof out, key, fields, 3), key);
        must(strstr(out, "98765%{hours}") != NULL, key);
        must(strstr(out, "98765%{minutes}") != NULL, key);
        must(strstr(out, "98765%{seconds}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 3) && tiny[0] == 0, key);
    }
    {
        const char *key = "CPU time: {user} s user, {system} s system";
        const IrcDisplayField fields[] = {{"user", "98765%{user}"}, {"system", "98765%{system}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 2), key);
        must(irc_display_render(out, sizeof out, key, fields, 2), key);
        must(strstr(out, "98765%{user}") != NULL, key);
        must(strstr(out, "98765%{system}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 2) && tiny[0] == 0, key);
    }
    {
        const char *key = "Peak resident memory: {memory} MiB";
        const IrcDisplayField fields[] = {{"memory", "98765%{memory}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 1), key);
        must(irc_display_render(out, sizeof out, key, fields, 1), key);
        must(strstr(out, "98765%{memory}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 1) && tiny[0] == 0, key);
    }
    {
        const char *key = "Server entries: {servers}; registered connections: {registered}";
        const IrcDisplayField fields[] = {{"servers", "98765%{servers}"}, {"registered", "98765%{registered}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 2), key);
        must(irc_display_render(out, sizeof out, key, fields, 2), key);
        must(strstr(out, "98765%{servers}") != NULL, key);
        must(strstr(out, "98765%{registered}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 2) && tiny[0] == 0, key);
    }
    {
        const char *key = "Channel views: {channels}; query views: {queries}";
        const IrcDisplayField fields[] = {{"channels", "98765%{channels}"}, {"queries", "98765%{queries}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 2), key);
        must(irc_display_render(out, sizeof out, key, fields, 2), key);
        must(strstr(out, "98765%{channels}") != NULL, key);
        must(strstr(out, "98765%{queries}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 2) && tiny[0] == 0, key);
    }
    {
        const char *key = "Line {line}/{total}";
        const IrcDisplayField fields[] = {{"line", "98765%{line}"}, {"total", "98765%{total}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 2), key);
        must(irc_display_render(out, sizeof out, key, fields, 2), key);
        must(strstr(out, "98765%{line}") != NULL, key);
        must(strstr(out, "98765%{total}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 2) && tiny[0] == 0, key);
    }
    {
        const char *key = "Nickname: {nick}";
        const IrcDisplayField fields[] = {{"nick", "98765%{nick}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 1), key);
        must(irc_display_render(out, sizeof out, key, fields, 1), key);
        must(strstr(out, "98765%{nick}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 1) && tiny[0] == 0, key);
    }
    {
        const char *key = "About {screen}";
        const IrcDisplayField fields[] = {{"screen", "98765%{screen}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 1), key);
        must(irc_display_render(out, sizeof out, key, fields, 1), key);
        must(strstr(out, "98765%{screen}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 1) && tiny[0] == 0, key);
    }
    {
        const char *key = "Back to item";
        must(irc_tr(key) && *irc_tr(key), key);
    }
    {
        const char *key = "text budget <={limit}/message";
        const IrcDisplayField fields[] = {{"limit", "98765%{limit}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 1), key);
        must(irc_display_render(out, sizeof out, key, fields, 1), key);
        must(strstr(out, "98765%{limit}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 1) && tiny[0] == 0, key);
    }
    {
        const char *key = "text budget <={limit}";
        const IrcDisplayField fields[] = {{"limit", "98765%{limit}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 1), key);
        must(irc_display_render(out, sizeof out, key, fields, 1), key);
        must(strstr(out, "98765%{limit}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 1) && tiny[0] == 0, key);
    }
    {
        const char *key = "row {row} col {col}";
        const IrcDisplayField fields[] = {{"row", "98765%{row}"}, {"col", "98765%{col}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 2), key);
        must(irc_display_render(out, sizeof out, key, fields, 2), key);
        must(strstr(out, "98765%{row}") != NULL, key);
        must(strstr(out, "98765%{col}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 2) && tiny[0] == 0, key);
    }
    {
        const char *key = "commands or /msg";
        must(irc_tr(key) && *irc_tr(key), key);
    }
    {
        const char *key = "PASTING";
        must(irc_tr(key) && *irc_tr(key), key);
    }
    {
        const char *key = "literal paste";
        must(irc_tr(key) && *irc_tr(key), key);
    }
    {
        const char *key = "Message limit reached ({limit} bytes).";
        const IrcDisplayField fields[] = {{"limit", "98765%{limit}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 1), key);
        must(irc_display_render(out, sizeof out, key, fields, 1), key);
        must(strstr(out, "98765%{limit}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 1) && tiny[0] == 0, key);
    }
    {
        const char *key = "{limit}-byte limit";
        const IrcDisplayField fields[] = {{"limit", "98765%{limit}"}};
        must(irc_display_fields_valid(irc_tr(key), fields, 1), key);
        must(irc_display_render(out, sizeof out, key, fields, 1), key);
        must(strstr(out, "98765%{limit}") != NULL, key);
        must(!irc_display_render(tiny, sizeof tiny, key, fields, 1) && tiny[0] == 0, key);
    }
    {
        const char *key = "This draft needs multiple chat lines.";
        must(irc_tr(key) && *irc_tr(key), key);
    }
    {
        const char *key = "More text will need multiple chat lines.";
        must(irc_tr(key) && *irc_tr(key), key);
    }
    {
        const char *key = "Enter/Esc: keep editing. Nothing sent.";
        must(irc_tr(key) && *irc_tr(key), key);
    }
    {
        const char *key = "Multiple chat lines";
        must(irc_tr(key) && *irc_tr(key), key);
    }
    {
        const char *key = "More text: multiple lines";
        must(irc_tr(key) && *irc_tr(key), key);
    }
    {
        const char *key = "Nothing sent.";
        must(irc_tr(key) && *irc_tr(key), key);
    }
    {
        const char *key = "Really quit? [Y/N]";
        must(irc_tr(key) && *irc_tr(key), key);
    }
    {
        const char *key = "Y: quit; other key: stay";
        must(irc_tr(key) && *irc_tr(key), key);
    }
    puts("terminal UI localization: PASS");
    return 0;
}
