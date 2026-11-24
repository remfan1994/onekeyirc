/* In-memory numeric reply integration: the real App dispatcher, no network. */
#define force_terminal_signals use_signals
#define main historical_test_app_main
#include "test_app.c"
#undef main
#undef force_terminal_signals
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"

static void check_numeric(App *a, const char *wire, const char *template,
                          const IrcDisplayField *fields, size_t n)
{
    char expected[2048];
    if (!irc_display_render(expected,sizeof expected,template,fields,n)) abort();
    output_clear(a);
    feed(a,wire);
    const char *line=last_line(&a->buffers[0]);
    if (!strstr(line,expected)) {
        fprintf(stderr,"numeric display mismatch\nsource: %s\nexpected: %s\nactual: %s\n",
                template,expected,line);
        abort();
    }
    /* Replies inspected here are informational; no outgoing IRC frames. */
    if (a->net && a->net->output_len) {
        fprintf(stderr,"unexpected outgoing IRC data after %s\n",wire); abort();
    }
}

int main(void)
{
    App *a=fresh(0);
    register_plain(a);
    const IrcDisplayField user[]={{"nick","Alice"},{"user","u"},
         {"host","host.example"},{"realname","Real Name"}};
    check_numeric(a,":server 311 tester Alice u host.example * :Real Name\r\n",
            "{nick} is {user}@{host} ({realname})",user,4);
    check_numeric(a,":server 314 tester Alice u host.example * :Real Name\r\n",
            "{nick} was {user}@{host} ({realname})",user,4);
    const IrcDisplayField away[]={{"nick","Alice"},{"message","Away 100%n {raw}"}};
    check_numeric(a,":server 301 tester Alice :Away 100%n {raw}\r\n",
            "{nick} is away: {message}",away,2);
    const IrcDisplayField account[]={{"nick","Alice"},{"account","login%test"}};
    check_numeric(a,":server 330 tester Alice login%test :is logged in\r\n",
            "{nick} is signed in as {account}.",account,2);
    const IrcDisplayField chan[]={{"nick","Alice"},{"channels","#a #b"}};
    check_numeric(a,":server 319 tester Alice :#a #b\r\n",
            "{nick}'s channels: {channels}",chan,2);
    const IrcDisplayField topic[]={{"channel","#r"},{"topic","Topic 100%n {raw}"}};
    check_numeric(a,":server 332 tester #r :Topic 100%n {raw}\r\n",
            "Topic for {channel}: {topic}",topic,2);
    const IrcDisplayField modes[]={{"channel","#r"},{"modes","+nt"}};
    check_numeric(a,":server 324 tester #r +nt\r\n",
            "Modes for {channel}: {modes}",modes,2);
    const IrcDisplayField end[]={{"nick","Alice"},{"message","End of WHOIS"}};
    check_numeric(a,":server 318 tester Alice :End of WHOIS\r\n",
            "End of user information for {nick}: {message}",end,2);
    const IrcDisplayField list[]={{"channel","#r"},{"users","12"},{"topic","Welcome 100%"}};
    check_numeric(a,":server 322 tester #r 12 :Welcome 100%\r\n",
            "{channel} ({users} users): {topic}",list,3);
    const IrcDisplayField motd[]={{"message","No MOTD here"}};
    check_numeric(a,":server 422 tester :No MOTD here\r\n",
            "No message of the day: {message}",motd,1);
    const IrcDisplayField fallback[]={{"message","Unrecognized {raw} 100%n"}};
    check_numeric(a,":server 799 tester :Unrecognized {raw} 100%n\r\n",
            "Server: {message}",fallback,1);
    destroy(a);
    puts("real in-memory numeric dispatch: 11 replies, raw fields and wire bytes PASS");
    return 0;
}
