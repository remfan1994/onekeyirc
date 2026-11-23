/* Compile and execute the generated lookup, separate from curses and network. */
#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    const char *keys[] = {
        "Inspect credential locations or prepare authentication",
        "Show or change automatic startup connection",
        "Inspect running joins or prepare the next explicit connection",
        "Set or clear away status",
        "Keep running and release this terminal; resume with vipirc --resume",
        "Assign a user-selected key to a local function",
        "Edit custom key bindings or list available names",
        "Stop unsent chat and startup commands on this server",
        "Switch channel; 0 is the server",
        "Open this server's channel list",
        "Open this server's channel list (alias of /channels)",
        "Clear the current view's stored lines",
        "Clear this conversation's draft",
        "Connect directly, or reuse the selected saved server",
        "Open file transfers, or manage a transfer directly",
        "Disconnect the selected or numbered server",
        "Open the manual; TEXT searches its contents",
        "Join one channel",
        "Explain ordinary navigation and legacy keymap names",
        "Open and filter this network channel directory",
        "Open a local list; channels by default",
        "Open the application manual",
        "Send an action to this conversation",
        "View this server's mentions, or clear retained mentions across servers",
        "Toggle or set mouse controls",
        "Switch to the next view",
        "Set or request a nickname",
        "Open the application configuration panel",
        "Switch to the previous view",
        "Open this server's query list",
        "Open this server's query list (alias of /ql)",
        "Open or select a private conversation without sending",
        "Open the selected server's connection settings",
        "Exit all sessions with an optional quit reason",
        "Save this server, primary preferences, or all profiles",
        "Connect or select a server; no argument opens its MOTD",
        "Open the server connection list",
        "Set application preferences",
        "Open the configuration panel",
        "Open the server connection list (alias of /servers)",
        "Send a private control command to this soju bouncer; help by default",
        "Review all servers, conversations, activity, queues and transfers",
        "Show or change the current channel topic",
        "Open file transfers",
        "Open grouped unread conversations and mentions across servers",
        "Show, hide or scroll the user list",
        "Switch to a named view or numbered channel",
        "Open this server's channel list",
        "Manage additional mention strings",
        "Request information about a nickname",
        "Send a private control command to this ZNC bouncer; Help by default"
    };
    const char *langs[] = {"id","ms","tr","es","pt","fr","de","ru","it","ar"};
    for(size_t l=0;l<sizeof langs/sizeof langs[0];++l) {
        assert(setenv("ONEKEYIRC_LANG",langs[l],1)==0);
        assert(setenv("VIPIRC_LANG",langs[l],1)==0);
        for(size_t k=0;k<sizeof keys/sizeof keys[0];++k) {
            const char *p=irc_tr(keys[k]);
            assert(p && strcmp(p,keys[k])!=0);
            assert(strlen(p)<256);
        }
        const char *payload="nick%p{from_server}:../file";
        assert(strcmp(irc_tr(payload),payload)==0);
    }
    assert(setenv("ONEKEYIRC_LANG","en",1)==0);
    assert(setenv("VIPIRC_LANG","en",1)==0);
    for(size_t k=0;k<sizeof keys/sizeof keys[0];++k)
        assert(strcmp(irc_tr(keys[k]),keys[k])==0);
    puts("r49: 51 command descriptions x 10 locales, literal data, English fallback PASS");
    return 0;
}
