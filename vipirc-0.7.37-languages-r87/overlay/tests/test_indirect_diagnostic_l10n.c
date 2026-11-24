/* Actual producers retain English diagnostics; translate only display copies. */
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include "security_diagnostic_l10n.h"
#include "credentials.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "filepicker.h"
static int translated;
static void check(const char *raw, const char *expected, const char *literal) {
    char out[4096];
    const char *shown = irc_security_diagnostic(raw, out, sizeof out);
    assert(shown && *shown);
    if (expected) assert(!strcmp(shown, expected));
    assert(translated ? strcmp(shown, raw) != 0 : strcmp(shown, raw) == 0);
    if (literal) {
        assert(strstr(shown, literal));
        assert(irc_security_diagnostic(raw, out, 2) == raw);
    }
}
int main(void) {
    translated = getenv("TEST_EXPECT_TRANSLATION") && *getenv("TEST_EXPECT_TRANSLATION");
    char raw[1024], absolute[4096];
    const char *literal = "/tmp/a%n{literal}/../b";
    assert(credential_absolute_path(literal, absolute, sizeof absolute, raw, sizeof raw) < 0);
    assert(strstr(raw, "Parent traversal (..) is not allowed in credential paths."));
    check(raw, NULL, literal);
    assert(credential_absolute_path("/tmp/x", absolute, 1, raw, sizeof raw) < 0);
    check(raw, irc_tr("The absolute credential path does not fit this field."), NULL);
    assert(!credential_create_start("/tmp/x", "invalid", "secret", 0, raw, sizeof raw));
    check(raw, irc_tr("Use a full 40- or 64-digit hexadecimal fingerprint of a local GPG key."), NULL);
    assert(credential_default_path("/tmp/config.ini", 0, NULL, 0, raw, sizeof raw) < 0);
    check(raw, irc_tr("No space for the default credential path."), NULL);
    check("Cannot allocate GPG credential state.", irc_tr("Cannot allocate GPG credential state."), NULL);
    check("Cannot allocate GPG encryption state.", irc_tr("Cannot allocate GPG encryption state."), NULL);
    check("Cannot create isolated GPG encryption pipes.", irc_tr("Cannot create isolated GPG encryption pipes."), NULL);
    check("Cannot create isolated GPG pipes.", irc_tr("Cannot create isolated GPG pipes."), NULL);
    check("Cannot start GPG credential process.", irc_tr("Cannot start GPG credential process."), NULL);
    check("Cannot start GPG encryption.", irc_tr("Cannot start GPG encryption."), NULL);
    check("GPG cleanup is still pending; retry shortly.", irc_tr("GPG cleanup is still pending; retry shortly."), NULL);
    check("No space for the default credential path.", irc_tr("No space for the default credential path."), NULL);
    check("The absolute credential path does not fit this field.", irc_tr("The absolute credential path does not fit this field."), NULL);
    check("The credential must be one line without control bytes.", irc_tr("The credential must be one line without control bytes."), NULL);
    check("The credential must contain 1 to 1024 bytes.", irc_tr("The credential must contain 1 to 1024 bytes."), NULL);
    check("The default credential directory path is too long.", irc_tr("The default credential directory path is too long."), NULL);
    check("The default credential path is too long.", irc_tr("The default credential path is too long."), NULL);
    check("Use a full 40- or 64-digit hexadecimal fingerprint of a local GPG key.", irc_tr("Use a full 40- or 64-digit hexadecimal fingerprint of a local GPG key."), NULL);
    check("Enter a filename, not a directory (at most 255 bytes). Path: /tmp/a%n{literal}", NULL, "/tmp/a%n{literal}");
    check("Parent traversal (..) is not allowed in credential paths. Path: /tmp/a%n{literal}", NULL, "/tmp/a%n{literal}");
    check("Credential directory does not exist; choose the offered default or an existing private directory. Path: /tmp/a%n{literal}", NULL, "/tmp/a%n{literal}");
    check("This credential path component is not a directory. Path: /tmp/a%n{literal}", NULL, "/tmp/a%n{literal}");
    check("New credential directory must be owned by you with mode 0700. Path: /tmp/a%n{literal}", NULL, "/tmp/a%n{literal}");
    check("Credential parent is not a directory. Path: /tmp/a%n{literal}", NULL, "/tmp/a%n{literal}");
    check("All 64 default credential filenames exist; choose a new custom filename. Path: /tmp/a%n{literal}", NULL, "/tmp/a%n{literal}");
    check("Only the offered default credential directory can be created automatically. Path: /tmp/a%n{literal}", NULL, "/tmp/a%n{literal}");
    check("GPG encryption failed; check the local key and GPG installation. This credential file was not published. Path: /tmp/a%n{literal}", NULL, "/tmp/a%n{literal}");
    check("GPG did not confirm complete protected encryption; this credential file was not published. Path: /tmp/a%n{literal}", NULL, "/tmp/a%n{literal}");
    check("The newly created credential was changed or replaced; it was left untouched. Path: /tmp/a%n{literal}", NULL, "/tmp/a%n{literal}");
    check("Cannot write and synchronize this encrypted credential; this file was not published. Path: /tmp/a%n{literal}", NULL, "/tmp/a%n{literal}");
    check("Cannot open directory: OS error %n{literal}", NULL, "OS error %n{literal}");
    check("Cannot open directory (symlinks refused): OS error %n{literal}", NULL, "OS error %n{literal}");
    check("Cannot read directory: OS error %n{literal}", NULL, "OS error %n{literal}");
    check("Cannot read the launch directory: OS error %n{literal}. Use Type path.", NULL, "OS error %n{literal}");
    check("Directory listing stopped: OS error %n{literal}", NULL, "OS error %n{literal}");
    FilePicker *p = filepicker_create(0);
    assert(p);
    assert(!filepicker_open(p, "/dev/null"));
    const char *err = filepicker_error(p);
    assert(strstr(err, "Cannot open directory (symlinks refused): ") == err);
    check(err, NULL, err + strlen("Cannot open directory (symlinks refused): "));
    filepicker_free(p);
    puts("Indirect credential and file diagnostics: PASS");
    return 0;
}
