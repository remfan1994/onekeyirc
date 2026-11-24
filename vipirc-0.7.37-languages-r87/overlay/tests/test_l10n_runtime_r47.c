/* r47: translated DCC/UI and auth/bouncer text, English fallback. */
#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    const char *langs[] = {"id","ms","tr","es","pt","fr","de","ru","it","ar"};
    const char *keys[] = {
    "Directory",
    "Save as",
    "Source file",
    "Advertised IPv4",
    "Listen port",
    "Receive file",
    "Send file",
    "Choose file",
    "Reading directory...",
    "No sendable files here.",
    "Listing limit reached; Type path for another file.",
    "Unencrypted; IP sent",
    "Input is on the chat screen.",
    "(unavailable)",
    "Enter: stage | Esc: cancel",
    "File selection",
    "file browser",
    "Receive file screen",
    "Send file screen",
    "File transfers screen",
    "This entry is a directory.",
    "This entry is a file selection.",
    "Use Up/Down to select, Enter to open or choose, and Backspace to visit the parent directory. Choosing a file fills the transfer form for review; it does not send it.",
    "This field belongs to the transfer review form. Edit changes its value.",
    "Directory and Save as determine the local destination after you choose Accept.",
    "Nickname, source file, advertised IPv4 and listen port determine the offer after you choose Offer.",
    "Left/Right chooses an action; Enter activates it. Escape returns without approving the transfer.",
    "Open a new file-transfer form. There are currently no retained transfer entries. An offer requires an explicit reviewed action.",
    "Choose an absolute directory path without terminal controls.",
    "Use the parent-directory entry instead of '..' in a path.",
    "OFFLINE TEST MODE: file browsing is disabled; Type path can edit a sample.",
    "Directory path is too long or contains unsupported text.",
    "Choose an absolute directory path.",
    "OFFLINE TEST MODE: file selection is disabled.",
    "Select a file or directory after the listing finishes.",
    "That directory changed. Reopen it before selecting a file.",
    "That entry changed. Reopen its directory before selecting it.",
    "The selected path is too long. Use Type path.",
    "The selected path does not fit the source field.",
    "File browser unavailable.",
    "Servers:",
    "Unread conversations:",
    "Authentication: bypassed in offline test mode.",
    "Required SASL: off",
    "File paths are shown only by this explicit inquiry. No credential file was read and no password content is displayed.",
    "Automatic retries keep current authentication. /disconnect then /connect applies pending settings; /save persists them.",
    "Bouncer profiles restore their own channel memberships; saved auto-join lists are not replayed.",
    "Use /join and /part while connected, or /channels to inspect restored channels.",
    };
    size_t count=sizeof keys/sizeof keys[0];
    for (size_t i=0;i<sizeof langs/sizeof langs[0];++i) {
        assert(setenv("VIPIRC_LANG", langs[i], 1)==0);
        for (size_t j=0;j<count;++j) {
            const char *translated=irc_tr(keys[j]);
            assert(translated && strcmp(translated,keys[j])!=0);
            assert(strlen(translated)<=900);
        }
        /* Untrusted names, paths, braces and formatting markers remain data. */
        const char *payload="user%p:{untrusted}/tmp/a";
        assert(strcmp(irc_tr(payload),payload)==0);
    }
    assert(setenv("VIPIRC_LANG","en",1)==0);
    for(size_t j=0;j<count;++j) assert(strcmp(irc_tr(keys[j]),keys[j])==0);
    puts("r47: ten-locale translated lookup, literal data and English fallback PASS");
    return 0;
}
