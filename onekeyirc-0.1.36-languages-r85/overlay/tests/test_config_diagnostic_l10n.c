/* Source-linked config validation display regression; no sockets/GPG/files. */
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/security_diagnostic_l10n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const config_errors[] = {
    "Authentication requires verified TLS",
    "Bouncer connections require verified TLS",
    "Enabled automatic commands require verified TLS",
    "SASL password: choose only one of password, password_file, or password_gpg_file",
    "Server password: choose only one of server_password, server_password_file, or server_password_gpg_file",
    "ZNC requires auth=none and exactly one server-password source; clear SASL credentials and auth_user",
    "ZNC login identity and password exceed the IRC PASS limit",
    "Configuration containing plaintext passwords must have mode 0600 or 0400",
    "Configuration containing automatic commands must have mode 0600 or 0400",
    "Configuration directory must belong to you and have mode 0700",
    "Refusing to replace an unsafe configuration file",
    "Configuration already exists; it was not overwritten",
    "Server catalog lock must be your regular file with mode 0600",
    "Parent traversal (..) is not allowed in file paths",
    "XDG_CONFIG_HOME must be absolute",
    "Invalid or overlong file path",
    "Invalid file name",
    "Private file exceeds its size limit",
    "NUL bytes are not allowed in private text files",
    "Cannot determine an absolute home directory",
    "Invalid primary configuration path",
    "Configuration path is too long",
    "Unknown key or action; use /bindings keys or /bindings actions",
    "A saved server profile needs a server",
    "auth must be none or sasl",
    "tls must be true (verified TLS) or false (plaintext)",
    "server must be a hostname or unbracketed numeric IP address",
    "nick must be a valid ASCII IRC nickname of at most 63 bytes",
    "realname must be valid UTF-8",
    "server_password allows at most 503 bytes",
    "autoconnect must be true or false",
    "bouncer.automatic_away must be true or false",
    "use_signals must be true or false",
    "queue_notices must be true or false",
    "program_messages must be true or false",
    "persistent_logging must be true or false",
    "single_session must be true or false",
    "background_away must be true or false",
    "autocommands.enabled must be true or false",
    "user_list must be true or false",
    "mouse must be true or false",
    "hints must be true or false",
    "shortcut_labels must be true or false",
    "autoaway_minutes must be between 0 and 1440",
    "autocommands.delay_seconds must be between 0 and 300",
    "autocommands.interval_seconds must be between 2 and 60",
    "bouncer.type must be none, znc or soju",
    "send_mode must be direct or review",
    "output format must be compact or full",
    "default_interface must be ncurses or idle",
    "command_suggestions must be alphabetical, common, history, frequent or contextual",
    "composer_mode must be line or fullscreen",
    "focus_style must be reverse or brackets",
    "Too many retained command usage counters",
    "Invalid command usage counter",
    "Duplicate command usage counter",
    "Internal configuration format flag is invalid",
    "auth_user must be valid UTF-8",
    "Presence messages must be valid UTF-8",
    "background_away_message cannot be empty while background_away is enabled",
    "autoaway_message cannot be empty while autoaway is enabled",
    "username must be an ASCII IRC token of at most 32 bytes",
    "Cannot allocate a unique server profile ID",
    "Cannot generate a server profile ID",
    "Cannot generate a temporary file name",
    "Cannot disable buffering for private configuration output",
    "Server profile path is too long",
    "Out of memory reading configuration",
    "Invalid server profile ID",
    "Default server profile is not in the saved catalog",
    "Invalid default server profile ID",
    "Invalid saved command usage; counters were not loaded",
    "Bouncer username and network are required; each component allows at most 63 ASCII letters, digits, '.', '_' or '-'",
    "Command usage path is too long",
    "Duplicate command usage entry",
    "Duplicate custom key binding",
    "Ignore masks must be ASCII nicknames or nick!user@host masks of at most 127 bytes",
    "Invalid command usage count",
    "Invalid command usage entry",
    "Invalid custom key binding",
    "Mention words must be nonempty and contain no controls",
    "Saved server limit reached (8 including primary); close a saved secondary first.",
    "This configuration format has no [interface] settings; remove that section or select a compatible configuration file",
    "channel must list at most 29 valid comma-separated channels, without empty entries",
    "soju requires auth=sasl and exactly one authentication-password source; clear server passwords and auth_user (the bouncer identity supplies it)",
};
static void must(int condition, const char *message)
{
    if (!condition) { fprintf(stderr, "%s\n",message); exit(1); }
}
int main(void)
{
    char buffer[2048];
    int translate = getenv("TEST_EXPECT_TRANSLATION") && *getenv("TEST_EXPECT_TRANSLATION");
    for (size_t i=0; i < sizeof config_errors / sizeof config_errors[0]; ++i) {
        const char *raw = config_errors[i];
        const char *display = irc_security_diagnostic(raw, buffer, sizeof buffer);
        if (translate) must(strcmp(display,raw) != 0,"missing config translation");
        else must(strcmp(display,raw) == 0,"English fallback changed config text");
        must(display && strlen(display)>2,"empty config message");
        /* Exact messages are not interpreted as C format strings. */
        must(strstr(display,"%n") == NULL,"unexpected format directive");
    }
    {
        const char *raw = "Unknown server reply: /tmp/100%/abc%n\303\251";
        must(irc_security_diagnostic(raw,buffer,sizeof buffer)==raw,
             "unknown diagnostic must be preserved as-is");
        must(irc_security_diagnostic(NULL,buffer,sizeof buffer)[0]==0,
             "NULL source should display as an empty string");
    }
    printf("85 fixed config validation messages: PASS\n");
    return 0;
}
