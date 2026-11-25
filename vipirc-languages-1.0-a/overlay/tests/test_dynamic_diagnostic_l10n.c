/* Regression for source-linked dynamic config and GPG error presentation.
 * No files are opened, no GPG commands run, and the producer is untouched.
 * Fail rather than truncating filenames or reinterpreting percent directives.
 */
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/security_diagnostic_l10n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Pattern { const char *prefix, *marker; } Pattern;
static const Pattern cases[] = {
    {"Configuration was created but directory synchronization failed: ", NULL},
    {"Configuration was replaced but directory synchronization failed: ", NULL},

    {"Cannot open file directory: ", NULL},
    {"Cannot open file directory (symlinks are refused): ", NULL},
    {"Cannot open private log file: ", NULL},
    {"Cannot inspect configuration path: ", NULL},
    {"Cannot open private file (symlinks are refused): ", NULL},
    {"Cannot read private file: ", NULL},
    {"Cannot inspect existing configuration: ", NULL},
    {"Cannot create private configuration: ", NULL},
    {"Cannot write configuration: ", NULL},
    {"Cannot flush configuration: ", NULL},
    {"Cannot close new configuration: ", NULL},
    {"Cannot create configuration without replacing an existing file: ", NULL},
    {"Configuration was created, but temporary-name cleanup failed: ", NULL},
    {"Cannot replace configuration: ", NULL},
    {"Cannot open server catalog lock: ", NULL},
    {"Cannot lock server catalog: ", NULL},
    {"Cannot inspect new profile: ", NULL},
    {"Cannot open GPG credential file (symlinks are refused): ", " GPG file: "},
    {"Cannot inspect the GPG credential file: ", " GPG file: "},
    {"Cannot determine the absolute credential location: ", NULL},
    {"Cannot open credential directory: ", " Path: "},
    {"Cannot create private credential directory: ", " Path: "},
    {"Cannot inspect credential directory: ", " Path: "},
    {"Cannot check the credential destination: ", " Path: "},
    {"Cannot check default credential names: ", " Path: "},
    {"Cannot create a private encrypted credential file: ", " Path: "},
    {"Cannot publish this credential: ", " No existing file was replaced. Path: "},
};
static void must(int okay, const char *why)
{
    if (!okay) { fprintf(stderr, "dynamic diagnostic test: %s\n", why); exit(1); }
}
int main(void)
{
    const char *detail = "Permission denied %n {untrusted} \303\251.";
    const char *path = "/tmp/100%{data}/\303\251.gpg";
    char raw[2048], out[4096];
    const int translated = getenv("TEST_EXPECT_TRANSLATION") != NULL &&
                           *getenv("TEST_EXPECT_TRANSLATION") != '\0';
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const Pattern *p = &cases[i];
        int needed = snprintf(raw, sizeof raw, "%s%s%s%s",
                              p->prefix, detail, p->marker ? p->marker : "",
                              p->marker ? path : "");
        must(needed > 0 && (size_t)needed < sizeof raw, "raw fixture overflow");
        const char *shown = irc_security_diagnostic(raw, out, sizeof out);
        must(strstr(shown, detail) != NULL, "dynamic OS message changed");
        must(strstr(shown, "%n {untrusted}") != NULL, "untrusted details were parsed");
        if (p->marker) {
            must(strstr(shown, path) != NULL, "UTF-8 path or percent signs changed");
            must(strstr(strstr(shown, path) + strlen(path), path) == NULL,
                 "path duplicated");
        }
        if (translated)
            must(strcmp(shown, raw) != 0, "translated prefix missing");
        else
            must(strcmp(shown, raw) == 0, "English fallback changed message");
        must(irc_security_diagnostic(raw, out, 6) == raw,
             "insufficient space must return original message");
    }
    const char *profiles[] = {"file is missing", "server is required", detail};
    for (size_t i = 0; i < 3; ++i) {
        snprintf(raw, sizeof raw, "Server profile abc-123.test: %s", profiles[i]);
        const char *shown = irc_security_diagnostic(raw, out, sizeof out);
        must(strstr(shown, "abc-123.test") != NULL, "profile ID changed");
        if (i == 2) must(strstr(shown, detail) != NULL, "loader detail changed");
        must((strcmp(shown, raw) != 0) == translated, "profile translation or fallback failed");
        must(irc_security_diagnostic(raw, out, 6) == raw, "profile overflow fallback failed");
    }
    const char *bad = "Server profile bad: id: detail";
    const char *invalid = "Server profile bad id: detail";
    (void)bad;
    must(irc_security_diagnostic(invalid, out, sizeof out) == invalid, "invalid ID altered");
    const char *unknown = "451 :raw server message %n {do-not-translate}";
    must(irc_security_diagnostic(unknown, out, sizeof out) == unknown,
         "unknown server message altered");
    const char *empty = "Cannot open file directory: ";
    must(irc_security_diagnostic(empty, out, sizeof out) == empty,
         "missing error detail should be left unchanged");
    const char *short_path = "Cannot open credential directory: Permission denied. Path: ";
    must(irc_security_diagnostic(short_path, out, sizeof out) == short_path,
         "missing path should be left unchanged");
    must(!*irc_security_diagnostic(NULL, out, sizeof out),
         "null input should remain empty");
    printf("dynamic diagnostics: %zu source patterns PASS\n",
           sizeof cases / sizeof cases[0]);
    return 0;
}
