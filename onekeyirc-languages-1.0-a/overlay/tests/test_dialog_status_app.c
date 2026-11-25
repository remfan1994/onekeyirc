/* Exercise the real command dispatcher with the existing fake transport. */
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
    char expected[2048];
    register_plain(a);
    assert(app_command(a, "/watch add example%n{raw}", &plan)==1);
    assert(!strcmp(a->status,irc_tr("Watch string added; /save preserves it.")));
    assert(a->cfg.word_count==1 && !strcmp(a->cfg.words[0],"example%n{raw}"));
    const IrcDisplayField watch[]={{"nick",a->nick},{"count","1"}};
    assert(irc_display_render(expected,sizeof expected,"Nick mentions always match {nick}; {count} additional literal strings:",watch,2));
    assert(app_command(a,"/watch list",&plan)==1);
    assert(logs_contain(a,expected) && logs_contain(a,"example%n{raw}"));
    assert(app_command(a,"/watch remove example%n{raw}",&plan)==1);
    assert(!strcmp(a->status,irc_tr("Watch string removed; /save preserves it.")) && !a->cfg.word_count);
    assert(app_outgoing(a,1,"PRIVMSG n :ordinary chat"));
    const IrcDisplayField cancelled[]={{"messages","1"},{"commands","0"}};
    assert(irc_display_render(expected,sizeof expected,"Cancelled {messages} queued messages and {commands} automatic commands.",cancelled,2));
    assert(app_command(a,"/cancel",&plan)==1 && !a->chat_count);
    assert(!strcmp(a->status,expected));
    destroy(a);
    /* Force a message to exceed a subsequently negotiated line limit. */
    a=fresh_automatic("/msg n a-message-that-will-not-fit-a-much-smaller-relay-budget", "/whois cancelled",1);
    register_plain(a);output_clear(a);a->last_chat=clock_ms;
    clock_ms+=1000;app_network(a,0);assert(a->chat_count==1 && !a->inflight);
    feed(a,":server 005 tester LINELEN=80 :supported\r\n");
    clock_ms+=200;app_network(a,0);
    const IrcDisplayField failed[]={{"number","1"}};
    assert(irc_display_render(expected,sizeof expected,"Automatic command {number} was not sent: the message limit changed. Remaining automatic commands cancelled.",failed,1));
    assert(!a->chat_count && !a->autocommand_active && !a->net->output_len);
    assert(!strcmp(a->status,expected) && logs_contain(a,expected));
    assert(!logs_contain(a,"a-message-that-will-not-fit-a-much-smaller-relay-budget"));
    destroy(a);
    puts("Command status and log integration: PASS");
    return 0;
}
