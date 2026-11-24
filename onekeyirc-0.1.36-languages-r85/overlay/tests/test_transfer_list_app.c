/* Test real command and reply handling, using the existing fake transport. */
#define main legacy_app_test_main
#define force_terminal_signals use_signals
#include "test_app.c"
#undef main
#undef force_terminal_signals
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
int main(void) {
    App *a = fresh(0);
    SendPlan plan;
    char expected[1024];
    register_plain(a);
    output_clear(a);
    const char *masked = "Network channel list requested with a server mask; directory updates as replies arrive.";
    const char *plain = "Network channel list requested; directory updates as replies arrive.";
    assert(app_command(a, "/list *example", &plan)==6 && a->list.active);
    assert(!strcmp(a->status,irc_tr(masked)));
    app_network(a,0); assert(!strcmp(a->net->output,"LIST *example\r\n"));
    output_clear(a);
    feed(a,":server 263 tester LIST :busy %n{raw}\r\n");
    const IrcDisplayField error[]={{"error","busy %n{raw}"}};
    assert(irc_display_render(expected,sizeof expected,"Channel list request failed: {error}. Use /list to retry.",error,1));
    assert(!a->list.active && !strcmp(a->status,expected));
    assert(app_list_repeat(a)); assert(a->list.active && !strcmp(a->status,irc_tr(masked)));
    app_network(a,0); assert(!strcmp(a->net->output,"LIST *example\r\n"));
    output_clear(a);
    feed(a,":server 323 tester :End of LIST\r\n");
    assert(!a->list.active && a->list.complete);
    assert(app_command(a,"/list",&plan)==6 && a->list.active);
    assert(!strcmp(a->status,irc_tr(plain)));
    app_network(a,0); assert(!strcmp(a->net->output,"LIST\r\n"));
    destroy(a);
    puts("LIST request/retry/failure integration: PASS");
    return 0;
}
