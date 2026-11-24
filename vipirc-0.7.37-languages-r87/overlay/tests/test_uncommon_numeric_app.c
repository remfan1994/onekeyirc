/* Deterministic parser/application integration: no external IRC network. */
#define main legacy_app_test_main
#define force_terminal_signals use_signals /* legacy fixture field alias; not used in this test */
#include "test_app.c"
#undef main
#undef force_terminal_signals
#define IRC_L10N_IMPLEMENTATION
#include "../src/uncommon_numeric_l10n.h"

int main(void)
{
    App *a = fresh(0);
    register_plain(a);
    for (size_t i=0; i<sizeof irc_uncommon_numerics / sizeof irc_uncommon_numerics[0]; ++i) {
        const IrcUncommonNumeric *e = &irc_uncommon_numerics[i];
        char wire[512], expected[2048];
        char *params[] = {"tester", "Alpha%n", "#room{raw}", "server says 100% {literal}"};
        if (e->count == 1) params[1] = params[3];
        else if (e->count == 2 && !strcmp(e->names[1], "message")) params[2] = params[3];
        int size = snprintf(wire, sizeof wire, ":server %03d tester", e->code);
        assert(size > 0 && (size_t)size < sizeof wire);
        for (size_t j=1; j<=e->count; ++j) {
            int n = snprintf(wire+size, sizeof wire-(size_t)size, "%s%s", j == e->count ? " :" : " ", params[j]);
            assert(n > 0 && (size_t)n < sizeof wire-(size_t)size);
            size += n;
        }
        assert((size_t)size+2 < sizeof wire);
        memcpy(wire+size, "\r\n", 3);
        assert(irc_uncommon_numeric_render(expected,sizeof expected,e->code,params,e->count+1));
        feed(a,wire);
        if (!strstr(last_line(&a->buffers[0]),expected)) {
            fprintf(stderr,"numeric %d: expected %s, actual %s\n",e->code,expected,last_line(&a->buffers[0]));
            return 1;
        }
        assert(a->registered && a->net);
    }
    feed(a, ":server 441 tester Alpha #room EXTRA :extended reason\r\n");
    assert(strstr(last_line(&a->buffers[0]), "Alpha #room EXTRA extended reason"));
    feed(a, ":server 441 tester :short reason\r\n");
    assert(strstr(last_line(&a->buffers[0]), "short reason"));
    feed(a, ":server 999 tester Alpha :unknown reason\r\n");
    assert(strstr(last_line(&a->buffers[0]), "Alpha unknown reason"));
    destroy(a);
    puts("Uncommon numeric application dispatch: PASS");
    return 0;
}
