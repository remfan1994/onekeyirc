/* Offline manual/lookup tracer. It exercises the same formatter as /manual
 * and CLI --manual without initializing a client, terminal or connection. */
#define _POSIX_C_SOURCE 200809L
#include "idle_commands.h"
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"

static const char *record_key(const char *english)
{
    if (*english) puts(english);
    return english;
}

static const char *mark_key(const char *english)
{
    return *english ? "@TRANSLATED@" : english;
}

static const char *percent_key(const char *english)
{
    return *english ? "@translated %s %n 100%@" : english;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    assert(!strcmp(argv[2], "onekeyirc") || !strcmp(argv[2], "vipirc"));
    (void)setlocale(LC_ALL, "C");
    if (!strcmp(argv[1], "--english")) idle_manual(stdout, argv[2]);
    else if (!strcmp(argv[1], "--localized"))
        idle_manual_translated(stdout, argv[2], irc_tr);
    else if (!strcmp(argv[1], "--mark"))
        idle_manual_translated(stdout, argv[2], mark_key);
    else if (!strcmp(argv[1], "--percent"))
        idle_manual_translated(stdout, argv[2], percent_key);
    else if (!strcmp(argv[1], "--keys")) {
        FILE *sink = tmpfile();
        assert(sink);
        idle_manual_translated(sink, argv[2], record_key);
        assert(!ferror(sink));
        assert(fclose(sink) == 0);
    } else assert(!"unknown fixture mode");
    return ferror(stdout) ? 1 : 0;
}
