/* Exercise real CLI usage routines without opening sockets or a terminal. */
#define main irc_cli_main_not_used
#define IRC_L10N_IMPLEMENTATION
#include "../src/main.c"
#undef main
#include <assert.h>
int main(void)
{
    FILE *f = tmpfile(); assert(f);
    cli_version(f);
    cli_localized_intro(f);
    usage(f, 1);
#ifndef ONEKEY_ONLY
    usage(f, 0);
#endif
    assert(!fflush(f)); assert(!fseek(f, 0, SEEK_SET));
    char buf[65536]; size_t n = fread(buf, 1, sizeof buf-1, f); assert(n && !ferror(f)); buf[n] = 0; fclose(f);
    const char *keys[] = {
        "Chat defaults to [HH:MM:SS] <nick> text. /set output_format full adds context; /set output_format compact restores concise output.",
        "/save output keeps this choice. Help opens a separate reader.",
        "Lists: /cl or /channels; /ql or /queries; /sl or /servers.",
        "Common commands:", "Interface language:",
        "An application of ThatIAm.vip (www.thatiam.vip), designed by remfan1994.",
        "License: IALA (Insha-Allah License Agreement); see the included license text.",
#ifndef ONEKEY_ONLY
        "Example:", "Terminal controls:", "Common in-app commands:", "Explicit -i or -N selects this launch without saving the override.",
        "Bare --resume keeps the worker's interface; -i or -N selects one.", "Changing interfaces discards drafts, send reviews, recall and unapplied forms.",
        "Use /set default_interface idle, then /save input, to remember -i.", "Read: man vipirc, vipirc --manual, or /help inside the app.",
#endif
    };
    for (size_t i=0; i<sizeof keys/sizeof *keys; ++i) assert(strstr(buf, irc_tr(keys[i])));
    assert(strstr(buf,PROGRAM_NAME " " PROGRAM_VERSION " [lang-rtl-1.0-a]"));
    assert(strstr(buf,irc_tr("Source available under the included license.")));
    assert(strstr(buf,"https://www.thatiam.vip"));
    assert(strstr(buf,"--server HOST")); assert(strstr(buf,"/set output_format full")); assert(strstr(buf,"/set output_format compact"));
    assert(strstr(buf,"/save output")); assert(strstr(buf,"[HH:MM:SS] <nick> text"));
#ifdef ONEKEY_ONLY
    assert(strstr(buf,"ONEKEYIRC_LANG=id_ID.UTF-8"));
#else
    assert(strstr(buf,"VIPIRC_LANG=id_ID.UTF-8")); assert(strstr(buf,"[session] default_interface=ncurses|idle."));
#endif
    if (strcmp(irc_tr("Interface language:"), "Interface language:"))
        assert(strstr(buf,irc_tr("Command and option spellings below are literal.")));
    puts("CLI help localization and literal syntax: PASS"); return 0;
}
