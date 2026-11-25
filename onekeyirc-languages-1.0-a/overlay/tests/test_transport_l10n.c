/* Exercise the real receive/disconnect path with the deterministic App fixture. */
#define main english_app_regression_main
#include "test_app.c"
#undef main
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include "display_l10n.h"

int main(void)
{
    static const unsigned char tls_record[] = {21,3,3,0,2,2,10};
    static const unsigned char bad_text[] = {'x',0,'y'};
    const char *key = "Server sent a NUL byte on the unencrypted IRC connection to {server}:{port} (TLS disabled). {detail} Check Transport and Port, then /connect";
    for (int looks_tls = 0; looks_tls <= 1; ++looks_tls) {
        App *a = fresh(0);
        const IrcDisplayField fields[] = {{"server","localhost"},{"port","6667"},
            {"detail",irc_tr(looks_tls ? "The response looks like a TLS record; select verified TLS for a TLS endpoint." :
            "This is not valid IRC text; the endpoint may require TLS.")}};
        char reason[512];
        assert(irc_display_render(reason, sizeof reason, key, fields, 3));
        feed_bytes(a, looks_tls ? tls_record : bad_text, looks_tls ? sizeof tls_record : sizeof bad_text);
        assert(!a->net && strstr(a->status,reason));
        destroy(a);
    }
    App *a = fresh(1);
    feed_bytes(a, bad_text, sizeof bad_text);
    assert(!a->net && strstr(a->status,irc_tr("Server sent a NUL byte inside verified TLS; malformed IRC text rejected. Check that this endpoint is an IRC service")));
    destroy(a);
    /* A self PART must retain history, localize the status and preserve the
     * exact channel name. This catches a catalog omission invisible to an
     * irc_tr-only audit. */
    a = fresh(0);
    register_plain(a);
    feed(a, ":tester!user@host JOIN #test\r\n");
    size_t view = app_buffer(a, "#test", B_CHANNEL, 0);
    assert(view != SIZE_MAX && a->buffers[view].joined);
    feed(a, ":alice!user@host PRIVMSG #test :retained fixture text\r\n");
    feed(a, ":tester!user@host PART #test :fixture reason\r\n");
    const char *part_key = "Left {channel}; history retained. /close removes its view.";
    const IrcDisplayField part_fields[] = {{"channel", "#test"}};
    char part[512];
    const char *lang = getenv("ONEKEYIRC_LANG");
    if (!lang || !*lang) lang = getenv("VIPIRC_LANG");
    if (lang && strcmp(lang, "en")) assert(strcmp(irc_tr(part_key), part_key));
    assert(irc_display_render(part, sizeof part, part_key, part_fields, 1));
    assert(!a->buffers[view].joined && !strcmp(a->status, part));
    assert(buffer_contains(&a->buffers[view], "retained fixture text"));
    destroy(a);
    puts("transport receive/disconnect localization: PASS");
    return 0;
}
