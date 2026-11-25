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
        "Custom hotkeys are optional; none are assigned by default.",
        "Chat starts ready to type; arrows edit and hjkl are literal letters.",
        "Each function key opens the indicated panel.",
        "Esc in a private conversation opens the private conversation list; in other chat views it opens vipirc.",
        "Esc returns from settings and application information to vipirc; in an ordinary panel it returns to chat. Arrow keys navigate within the panel.",
        "Empty categories are omitted; vipirc is always available. Counts appear in parentheses after category names.",
        "Tab completes nicknames in the chat field.",
        "The vipirc (F1) menu contains the following entries:",
        "The manual returns to the screen that opened it; / searches, n/N finds next/previous.",
        "Press / in vipirc (F1), or at the beginning of the chat field, to open the command prompt.",
        "In the command prompt, Right completes names; Up/Down recalls matching history. PgUp/PgDn browses all suggestions.",
        "Enter executes the command; Esc cancels without changing the chat draft. The corresponding on-screen buttons do the same.",
        "In lists, actions appear beneath the heading. Up/Down selects an item; Left/Right selects its action. Both remain highlighted; Enter executes the selected action.",
        "Opening a server from its list opens its MOTD view. Delete closes the selected server, channel or private conversation view.",
        "In vipirc (F1), b returns to chat; Esc asks for confirmation before quitting. Only Y/y confirms; other keys cancel. Job controls remain native.",
        "Enter submits chat; /clearinput clears the draft. In mouse mode, two buttons below the text field clear or send it.",
        "/users toggles the channel roster. PgUp/PgDn scrolls chat history.",
        "Settings let you edit automatic connections, channel joins, authentication and preferences.",
        "Disable optional guidance using this setting:",
        "Authentication sources: private file, GPG file, or explicit value in a private configuration file.",
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
    assert(strstr(buf,"F1 vipirc; F2 "));
    assert(strstr(buf,"/cl, /channels; /ql, /queries; /sl, /servers"));
    assert(strstr(buf,"/clearinput")); assert(strstr(buf,"/dcc, /transfers"));
    assert(strstr(buf,"/mentions")); assert(strstr(buf,"/unread"));
    assert(strstr(buf,"vipirc --resume")); assert(strstr(buf,"Y/y"));
    const char *menu_labels[] = {"Key bindings", "Servers", "Channels", "Queries", "Mentions/unread", "File transfers", "Settings", "Interface", "Hints", "Server", "Nickname", "Manual", "Command", "Background", "About", "Insha-Allah license agreement"};
    for (size_t i=0; i<sizeof menu_labels/sizeof *menu_labels; ++i) assert(strstr(buf,irc_tr(menu_labels[i])));
    assert(strstr(buf,"VIPIRC_LANG=id_ID.UTF-8")); assert(strstr(buf,"[session] default_interface=ncurses|idle."));
#endif
    if (strcmp(irc_tr("Interface language:"), "Interface language:"))
        assert(strstr(buf,irc_tr("Command and option spellings below are literal.")));
    puts("CLI help localization and literal syntax: PASS"); return 0;
}
