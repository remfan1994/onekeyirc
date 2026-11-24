#define main legacy_app_test_main
#define force_terminal_signals use_signals
#include "test_app.c"
#undef main
#undef force_terminal_signals
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
static void expect_status(App *a, const char *key, const char *field, const char *value) {
    char expected[1024];
    const IrcDisplayField f[]={{field,value}};
    assert(irc_display_render(expected,sizeof expected,key,f,1));
    assert(!strcmp(a->status,expected));
}
int main(void) {
    App *a=fresh(0);
    SendPlan plan;
    expect_status(a,"Registering as {nick}","nick",a->nick);
    register_plain(a);
    assert(app_command(a,"/pm Visitor",&plan)==1);
    expect_status(a,"Query with {nick}; type a message to send.","nick","Visitor");
    assert(app_command(a,"/pm invalid!nick",&plan)==0);
    expect_status(a,"Usage: /{command} nickname|query-number","command","pm");
    assert(app_command(a,"/channels unexpected",&plan)==0);
    expect_status(a,"Usage: /{command}","command","channels");
    assert(app_command(a,"/zzzznope",&plan)==0);
    expect_status(a,"Unknown command /{command}; /help lists supported commands.","command","zzzznope");
    app_disconnect(a,"test");
    assert(app_command(a,"/nick NewNick",&plan)==1);
    expect_status(a,"Nickname set to {nick}; /connect to use it.","nick","NewNick");
    assert(!strcmp(a->cfg.nick,"NewNick"));
    destroy(a);
    puts("Registration and command status integration: PASS");
    return 0;
}
