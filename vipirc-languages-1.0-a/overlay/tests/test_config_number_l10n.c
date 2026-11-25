/* Exercise actual config producers as well as malformed and oversized input. */
#define IRC_L10N_IMPLEMENTATION
#include "config_number_l10n.h"
#include "security_diagnostic_l10n.h"
#include "config.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static int translated;
static void check(const char *raw, const char *literal, const char *second)
{
    char out[4096], small[2];
    const char *shown = irc_security_diagnostic(raw, out, sizeof out);
    assert(shown && *shown);
    if (translated) assert(strcmp(raw, shown)); else assert(!strcmp(raw, shown));
    if (literal) assert(strstr(shown, literal));
    if (second) assert(strstr(shown, second));
    assert(irc_security_diagnostic(raw, small, sizeof small) == raw);
    assert(irc_config_number_diagnostic_lookup(raw, out, sizeof out, NULL) == raw);
}
static void write_file(const char *path, const char *text, mode_t mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    assert(fchmod(fd, mode) == 0); assert(close(fd) == 0);
}
int main(void)
{
    translated = getenv("TEST_EXPECT_TRANSLATION") && *getenv("TEST_EXPECT_TRANSLATION");
    char raw[1024], path[512], catalog[520], out[4096];
    char dir[] = "/tmp/irc-config-number-XXXXXX";
    assert(mkdtemp(dir));
    assert(snprintf(path, sizeof path, "%s/config", dir) > 0);
    assert(snprintf(catalog, sizeof catalog, "%s.servers", path) > 0);
    Config c;
    config_defaults(&c); c.port = -123;
    assert(config_validate(&c, raw, sizeof raw) < 0);
    check(raw, "-123", "65535");
    config_defaults(&c); c.history_lines = 6789;
    assert(config_validate(&c, raw, sizeof raw) < 0);
    check(raw, "6789", "5000");
    config_defaults(&c); strcpy(c.server, " server");
    assert(config_validate(&c, raw, sizeof raw) < 0);
    check(raw, "server", NULL);
    config_defaults(&c); strcpy(c.password, "secret\tvalue");
    assert(config_validate(&c, raw, sizeof raw) < 0);
    assert(!strstr(raw, "secret")); check(raw, "password", NULL);
    config_defaults(&c);
    write_file(path, "# comment\n[connection]\nnot_a_setting=abc\n", 0600);
    assert(config_load(&c, path, raw, sizeof raw) < 0);
    assert(!strcmp(raw,"Invalid or unknown configuration entry at line 3"));
    check(raw, "3", NULL);
    ConfigProfiles profiles;
    write_file(catalog, "[servers]\ninvalid=entry\n", 0600);
    assert(config_profiles_load(&profiles, path, raw, sizeof raw) < 0);
    assert(strstr(raw,"Invalid server catalog entry at line "));
    check(raw, NULL, NULL);
    write_file(path, "ciphertext", 0644);
    assert(config_open_gpg_file(path, raw, sizeof raw) < 0);
    assert(strstr(raw,"mode 0644")); check(raw,"0644",path);
    if (getuid() == 0 && chown(path, 12345, (gid_t)-1) == 0) {
        assert(config_open_gpg_file(path, raw, sizeof raw) < 0);
        assert(strstr(raw,"UID 12345")); check(raw,"12345",path);
    }
    assert(unlink(path) == 0); assert(unlink(catalog) == 0); assert(rmdir(dir) == 0);
    /* Reject malformed and non-producer variants. */
    const char *bad[] = {"Invalid or unknown configuration entry at line ",
        "Invalid or unknown configuration entry at line 42 trailing", "unknown.field exceeds its text limit",
        "mentions.word[] exceeds its text limit", "mentions.word[2]extra exceeds its text limit",
        "The GPG credential has mode 0999; use private mode 0600 or 0400. GPG file: /x",
        "The GPG credential is owned by UID 1; your UID is -2. GPG file: /x"};
    for (size_t i=0;i<sizeof bad/sizeof bad[0];++i)
        assert(irc_security_diagnostic(bad[i],out,sizeof out)==bad[i]);
        check("Invalid or unknown configuration entry at line 42", "42", NULL);
    check("Invalid server catalog entry at line 42", "42", NULL);
    check("Default server at catalog line 42 is not a listed profile", "42", NULL);
    check("port must be between 1 and 65535 (received -321)", "-321", NULL);
    check("history lines must be between 50 and 5000 (received -321)", "-321", NULL);
    check("At most 97 custom key bindings are allowed", "97", NULL);
    check("autocommands.command accepts at most 97 entries", "97", NULL);
    check("mentions.word accepts at most 97 entries", "97", NULL);
    check("ignore.mask accepts at most 97 entries", "97", NULL);
    check("Invalid automatic command at position 23; use /msg, /join, /nick, /away, /back or /whois", "23", NULL);
    check("mentions.word[2] exceeds its text limit", "mentions.word[2]", NULL);
    check("mentions.word[2] cannot contain control characters", "mentions.word[2]", NULL);
    check("mentions.word[2] cannot begin or end with spaces", "mentions.word[2]", NULL);
    check("mentions.word[2] exceeds its byte limit", "mentions.word[2]", NULL);
    check("mentions.word[2] cannot contain control bytes", "mentions.word[2]", NULL);
    check("The GPG credential is owned by UID 12345; your UID is 67890. GPG file: /tmp/space %n{literal}", "12345", "67890");
    assert(strstr(irc_security_diagnostic("The GPG credential is owned by UID 12345; your UID is 67890. GPG file: /tmp/space %n{literal}", out, sizeof out), "/tmp/space %n{literal}"));
    check("The GPG credential has mode 0644; use private mode 0600 or 0400. GPG file: /tmp/space %n{literal}", "0644", "/tmp/space %n{literal}");
    const char *nested = "Server profile test-123: Invalid or unknown configuration entry at line 42";
    const char *shown = irc_security_diagnostic(nested,out,sizeof out);
    assert(strstr(shown,"test-123") && strstr(shown,"42"));
    if (translated) assert(!strstr(shown,"Invalid or unknown configuration entry"));
    else assert(!strcmp(shown,nested));
    puts("Config number/ownership diagnostics: PASS");
    return 0;
}
