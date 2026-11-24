/* Display-only locale tests plus real filesystem error producers. */
#define IRC_L10N_IMPLEMENTATION
#include "background_display_l10n.h"
#include "security_diagnostic_l10n.h"
#include "config.h"
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

static int translated;
static void nested(const char *raw, const char *inner, const char *literal)
{
    char out[4096], small[2];
    const char *shown = irc_security_diagnostic(raw, out, sizeof out);
    assert(shown && strstr(shown, literal));
    if (translated) { assert(strcmp(shown, raw)); if (inner) assert(!strstr(shown, inner)); }
    else assert(!strcmp(shown, raw));
    assert(irc_security_diagnostic(raw, small, sizeof small) == raw);
}
int main(void)
{
    translated = getenv("TEST_EXPECT_TRANSLATION") && *getenv("TEST_EXPECT_TRANSLATION");
    char raw[1024], path[512], out[4096], small[2];
    char tmp[] = "/tmp/irc-nested-XXXXXX";
    assert(mkdtemp(tmp));
    assert(snprintf(path, sizeof path, "%s/missing/secret%%n{literal}", tmp) > 0);
    assert(config_open_gpg_file(path, raw, sizeof raw) < 0);
    assert(strstr(raw, "Cannot open file directory (symlinks are refused): "));
    nested(raw, "Cannot open file directory", path);
    assert(snprintf(path, sizeof path, "%s/../credential", tmp) > 0);
    assert(config_open_gpg_file(path, raw, sizeof raw) < 0);
    nested(raw, "Parent traversal", path);
    nested("Invalid file name. GPG file: /tmp/file name %n{path}", "Invalid file name", "/tmp/file name %n{path}");
    nested("Server profile example-1: Cannot open private file (symlinks are refused): literal %n{err}", "Cannot open private file", "literal %n{err}");
    nested("Server profile example-1: Configuration must be your regular file, not writable by other users", "Configuration must be", "example-1");
    assert(rmdir(tmp) == 0);
    const char *labels[] = {"nick @ example.net (1 server)", "nick @ example.net (0 servers)", "nick @ example.net (8 servers)"};
    for (size_t i = 0; i < sizeof labels/sizeof labels[0]; ++i) {
        const char *shown = irc_bg_job_label(labels[i], out, sizeof out);
        assert(!strncmp(shown, "nick @ example.net ", 19));
        const char *num = i==0 ? "1" : i==1 ? "0" : "8";
        assert(strstr(shown,num));
        /* Indonesian/Italian singular and German counts can legitimately match English. */
        if (!translated) assert(shown == labels[i]);
        assert(irc_bg_job_label(labels[i], small, sizeof small) == labels[i]);
    }
    const char *bad[] = {"nick @ host (01 server)", "nick @ host (1 servers)", "nick @ host (2 server)", "nick @ host (2 servers) trailing", "(2 servers)", "nick @ host (99 servers)"};
    for (size_t i=0;i<sizeof bad/sizeof bad[0];++i) assert(irc_bg_job_label(bad[i],out,sizeof out)==bad[i]);
    int len = irc_bg_detach_notice(out,sizeof out,"onekeyirc",42);
    assert(len>0 && (size_t)len==strlen(out));
    assert(strstr(out,"onekeyirc --resume 42") && strstr(out,"onekeyirc --sessions"));
    if (translated) assert(!strstr(out,"is running in the background"));
    else assert(!strcmp(out,"onekeyirc job 42 is running in the background.\nResume: onekeyirc --resume 42\nUse onekeyirc --resume for a sole detached job; onekeyirc --sessions lists jobs.\n"));
    assert(!irc_bg_detach_notice(small,sizeof small,"onekeyirc",42));
    FILE *f=tmpfile(); assert(f);
    irc_bg_startup_error(f,"vipirc","literal %n{error}");
    rewind(f); assert(fgets(out,sizeof out,f)); assert(!fclose(f));
    assert(!strncmp(out,"vipirc: ",8) && strstr(out,"literal %n{error}"));
    if (translated) assert(!strstr(out,"background startup failed"));
    else assert(!strcmp(out,"vipirc: background startup failed: literal %n{error}\n"));
    puts("Background display / nested filesystem diagnostics: PASS");
    return 0;
}
