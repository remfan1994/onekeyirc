/* r46 catalog lookup: user-visible strings only; arbitrary data stays literal. */
#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    const char *lang[] = {"id","ms","tr","es","pt","fr","de","ru","it","ar"};
    const char *keys[] = {
        "Offline sample offer. Sending and receiving are disabled before file or network access.",
        "Select a pending offer to accept.",
        "Accept connects to this peer and creates a new private file; existing files are never overwritten.",
        "Enter the IPv4 address the peer can reach. Port 0 chooses a local port; NAT may require forwarding.",
        "This field is not valid UTF-8.",
        "File browser unavailable; enter the source path here.",
        "Cannot read the launch directory. Use Type path.",
        "The field allows at most 4095 UTF-8 bytes.",
        "Use a single line without control characters.",
        "Offer rejected. No connection or file was opened.",
        "Transfer cancelled. An offer already sent or being sent cannot be recalled.",
        "Transfer cancelled. Its incomplete receive file was removed if still unchanged.",
        "Resize to at least 12 rows by 20 columns to review this transfer.",
        "Receiving file. You can return to chat while it transfers.",
        "OFFLINE TEST MODE: DCC sending is disabled before file or network access.",
        "Connect this IRC server before offering a file.",
        "Enter a valid recipient nickname.",
        "Listen port must be an integer from 0 to 65535.",
        "The file offer could not be queued within the IRC message budget. Try a shorter filename.",
        "File offered. Waiting for the peer to connect.",
        "Paste contained rejected controls or exceeded the field limit.",
        "Field limit reached or unsupported control character.",
        "Unencrypted peer connection. Accepting reveals your IP to the peer.",
        "Unencrypted peer connection. Your advertised IP is included in the offer.",
        "No file transfers.",
        "File stays unopened until Accept.",
        "No such server number. Servers start at 1; /servers lists them. Panel 0 contains application preferences.",
        "No such server number. Use /servers to list available slots.",
        "No matching channels received yet.",
        "No cached channels match the current LIST filters.",
    };
    for (size_t i = 0; i < sizeof lang / sizeof lang[0]; ++i) {
        assert(setenv("VIPIRC_LANG", lang[i], 1) == 0);
        for (size_t j = 0; j < sizeof keys / sizeof keys[0]; ++j) {
            const char *translated = irc_tr(keys[j]);
            assert(translated && strcmp(translated,keys[j]) != 0);
            assert(strlen(translated) <= 256);
        }
        /* User-controlled or protocol text is data, never translated by accident. */
        assert(strcmp(irc_tr("nickname%p: {literal}"),"nickname%p: {literal}") == 0);
    }
    assert(setenv("VIPIRC_LANG", "en", 1) == 0);
    for (size_t j = 0; j < sizeof keys / sizeof keys[0]; ++j)
        assert(strcmp(irc_tr(keys[j]), keys[j]) == 0);
    puts("r46 ten-locale lookup and literal English fallback PASS");
    return 0;
}
