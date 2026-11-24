/* Standalone compiled localization test: no curses, network, or credentials. */
#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    const char *keys[] = {
        "The new credential file must differ from configuration files and their sidecars; choose the offered credentials directory.",
        "The new credential file matches a server configuration file; choose a different destination.",
        "Choose different new files for the authentication and server passwords.",
        "GPG file creation is disabled in --test-mode; no helper, keyring, or credential path was accessed.",
        "Enter the password, or go back to change its source.",
        "Enter the credential file path, or go back and select Create new to offer a private default location.",
        "Cannot determine the current directory; enter an absolute credential path.",
        "The absolute credential path is too long; use a shorter path.",
        "Bouncer fields allow at most 63 ASCII letters, digits, '.', '_' or '-'; enter each component separately.",
        "GPG encryption is still running; wait or go back to cancel it.",
        "Invalid or oversized answer; existing settings are unchanged.",
        "Enter one answer without control characters or line breaks.",
        "Choose create for a new encrypted file or existing for a file you already have.",
        "Use the full local GPG key fingerprint: 40 or 64 hexadecimal digits without spaces.",
        "Enter a password of 1 to 1024 UTF-8 bytes.",
        "The passwords do not match; type the same password again or go back to change it.",
        "Complete and confirm each GPG password before Finish.",
        "Enter yes for verified TLS or no for unencrypted IRC.",
        "Bouncer connections require verified TLS. Change Connection type to direct before disabling TLS.",
        "Port must be a number from 1 to 65535.",
        "Choose direct, znc, or soju.",
        "Enter the bouncer account name.",
        "Enter the network name configured on your bouncer.",
        "Enter yes or no for this profile's automatic-away rules.",
        "Enter the account name used for SASL authentication.",
        "Choose none, file, gpg, or plain.",
        "Choose a final action before applying settings.",
        "Already connecting or connected. Choose 2 or 4 to stage settings; /disconnect before a new /connect."
    };
    const char *langs[] = {"id","ms","tr","es","pt","fr","de","ru","it","ar"};
    for(size_t l=0;l<10;++l) {
        assert(setenv("VIPIRC_LANG", langs[l],1)==0);
        assert(setenv("ONEKEYIRC_LANG", langs[l],1)==0);
        for(size_t k=0;k<sizeof keys/sizeof keys[0];++k) {
            const char *p=irc_tr(keys[k]);
            assert(p && strcmp(p,keys[k])!=0);
            assert(strlen(p)<=256);
        }
        const char *raw="nick%p{server}:../../../credentials";
        assert(strcmp(irc_tr(raw),raw)==0);
    }
    assert(setenv("ONEKEYIRC_LANG","en",1)==0);
    assert(setenv("VIPIRC_LANG","en",1)==0);
    for(size_t k=0;k<sizeof keys/sizeof keys[0];++k)
        assert(strcmp(irc_tr(keys[k]),keys[k])==0);
    puts("r49: 28 setup notices x ten locales / English fallback PASS");
    return 0;
}
