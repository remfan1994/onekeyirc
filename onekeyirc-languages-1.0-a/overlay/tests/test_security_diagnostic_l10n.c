/* Security-display regression: no GPG calls, secrets or sockets involved. */
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/security_diagnostic_l10n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(int condition, const char *message)
{
    if (!condition) { fprintf(stderr, "%s\n", message); exit(1); }
}
int main(void)
{
    char out[512];
    const char *fixed = "GPG decryption failed; unlock the key outside the client, then reconnect.";
    const char *tr = irc_security_diagnostic(fixed, out, sizeof out);
    const char *lang = getenv("TEST_EXPECT_TRANSLATION");
    if (lang && *lang) require(strcmp(tr, fixed) != 0, "fixed local diagnostic not translated");
    else require(strcmp(tr, fixed) == 0, "English fallback changed");
    const char *suffix = "/tmp/100%/data-%n-\303\251.gpg";
    const char *reason = "Symlink credential directories are refused.";
    char raw[512];
    (void)snprintf(raw, sizeof raw, "%s Path: %s", reason, suffix);
    tr = irc_security_diagnostic(raw, out, sizeof out);
    if (lang && *lang) require(strcmp(raw, tr) != 0, "path diagnostic not translated");
    require(strstr(tr, suffix) != NULL, "path bytes changed or interpreted as printf format");
    const char *gpg_reason = "The GPG credential file does not exist.";
    (void)snprintf(raw, sizeof raw, "%s GPG file: %s", gpg_reason, suffix);
    tr = irc_security_diagnostic(raw, out, sizeof out);
    if (lang && *lang) require(strcmp(raw, tr) != 0, "GPG-file diagnostic not translated");
    require(strstr(tr, suffix) != NULL, "GPG file path bytes changed");
    const char *unknown = "Server reply 451: /tmp/%n is unrecognized";
    require(irc_security_diagnostic(unknown, out, sizeof out) == unknown,
            "unknown data was altered");
    tr = irc_security_diagnostic(raw, out, 12);
    require(tr == raw, "truncated translated path instead of falling back");
    printf("credential display localization: PASS\n");
    return 0;
}
