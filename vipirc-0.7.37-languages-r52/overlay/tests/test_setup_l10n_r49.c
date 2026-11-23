/* Compiled lookup test for real guided-setup descriptions. */
#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    const char *keys[] = {
        "yes = verified encrypted connection; no = unencrypted IRC.",
        "direct = ordinary IRC; znc or soju = one bounced network. Changing type clears this draft's previous credentials, startup commands and bouncer identity. Bouncers require verified TLS; the provider's port stays unchanged.",
        "no lets the bouncer manage automatic away. yes applies this client's existing idle/background-away rules to this profile; other connected devices may still be active. Manual /away and /back always work.",
        "file (private plaintext file), gpg (create or use an encrypted file), or plain (hidden entry saved in config). Supply the bouncer PASSWORD ONLY; the client adds the account and network. A source is required.",
        "create = encrypt a password into a new file; existing = use a file you already encrypted. Create offers a private file location beside this configuration and uses an existing local GPG key.",
        "GPG credential creation is unavailable in --test-mode. Go back to choose an existing file; no credential path is inspected here.",
        "Full 40- or 64-digit hexadecimal fingerprint of your local encryption key, without spaces. Verify it with gpg --fingerprint. No key is downloaded.",
        "Append chat to a private log in each saved configuration directory. Each log stops at 16 MiB; archive it and toggle off/on to resume. Off keeps history memory-only. Applies to all servers.",
    };
    const char *langs[] = {"id","ms","tr","es","pt","fr","de","ru","it","ar"};
    for(size_t l=0;l<sizeof langs/sizeof langs[0];++l) {
        assert(setenv("ONEKEYIRC_LANG",langs[l],1)==0);
        assert(setenv("VIPIRC_LANG",langs[l],1)==0);
        for(size_t k=0;k<sizeof keys/sizeof keys[0];++k) {
            const char *text=irc_tr(keys[k]);
            assert(text && strcmp(text,keys[k])!=0);
            assert(strlen(text)<=256);
        }
        const char *untrusted="server{error}%n\t\\literal";
        assert(strcmp(irc_tr(untrusted),untrusted)==0);
    }
    assert(setenv("ONEKEYIRC_LANG","en",1)==0);
    assert(setenv("VIPIRC_LANG","en",1)==0);
    for(size_t k=0;k<sizeof keys/sizeof keys[0];++k)
        assert(strcmp(irc_tr(keys[k]),keys[k])==0);
    puts("r49: 8 guided-setup descriptions x 10 locales; fallback PASS");
    return 0;
}
