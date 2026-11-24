/* Real onekeyirc numeric dispatcher and channel-directory completion.
 * All packets stay in memory. No external server or credentials are used. */
#define force_terminal_signals use_signals
#define main historical_test_app_main
#include "test_app.c"
#undef main
#undef force_terminal_signals
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"

static void check_error(const char *code, const char *source)
{
    App *a=fresh(0);
    register_plain(a);
    output_clear(a);
    const IrcDisplayField f[] = {
        {"target","#room%p"}, {"message","Error {raw} 100%n"}
    };
    char expected[1024], wire[256];
    assert(irc_display_render(expected,sizeof expected,source,f,2));
    snprintf(wire,sizeof wire,":server %s tester #room%%p :Error {raw} 100%%n\r\n",code);
    feed(a,wire);
    if (!strstr(last_line(&a->buffers[0]),expected)) {
        fprintf(stderr,"numeric %s mismatch\nexpected: %s\nactual: %s\n",code,
            expected,last_line(&a->buffers[0])); abort();
    }
    if (a->net->output_len) abort();
    destroy(a);
}
static void check_list(int truncated)
{
    App *a=fresh(0);
    SendPlan plan;
    register_plain(a);
    output_clear(a);
    assert(app_command(a,"/list",&plan)==6);
    assert(a->list.active);
    feed(a,":server 322 tester #sample 12 :Topic {raw} 100%n\r\n");
    a->list.truncated=truncated;
    const char *status_template = truncated ?
        "Channel list complete: {received} received, {matched} matched, {cached} cached (cache limit reached)." :
        "Channel list complete: {received} received, {matched} matched, {cached} cached.";
    const char *log_template = truncated ?
        "End of channel list: {received} received, {matched} matched; cache truncated." :
        "End of channel list: {received} received, {matched} matched.";
    char received[32],matched[32],cached[32],status[1024],logtext[1024];
    snprintf(received,sizeof received,"%zu",a->list.received);
    snprintf(matched,sizeof matched,"%zu",a->list.matched);
    snprintf(cached,sizeof cached,"%zu",a->list.count);
    const IrcDisplayField sf[]={ {"received",received},{"matched",matched},{"cached",cached}};
    const IrcDisplayField lf[]={ {"received",received},{"matched",matched}};
    assert(irc_display_render(status,sizeof status,status_template,sf,3));
    assert(irc_display_render(logtext,sizeof logtext,log_template,lf,2));
    output_clear(a);
    feed(a,":server 323 tester :End of LIST\r\n");
    if (!strstr(a->status,status) || !strstr(last_line(&a->buffers[0]),logtext)) {
        fprintf(stderr,"LIST status mismatch\nexpected: %s\nactual: %s\n"
                       "LIST log mismatch\nexpected: %s\nactual: %s\n",
                       status,a->status,logtext,last_line(&a->buffers[0])); abort();
    }
    if (!a->list.complete || a->list.active || a->net->output_len) abort();
    destroy(a);
}
int main(void)
{
    const struct { const char *code; const char *source; } errors[] = {
        {"401","Cannot find {target}: {message}"},
        {"404","Cannot send to {target}: {message}"},
        {"437","Cannot use {target}: {message}"},
        {"471","Cannot join {target}: {message}"},
        {"482","Cannot change {target}: {message}"},
        {"421","Server does not recognize {target}: {message}"},
        {"461","More information is required for {target}: {message}"}
    };
    for (size_t i=0;i<sizeof errors/sizeof errors[0];++i)
        check_error(errors[i].code,errors[i].source);
    check_list(0);
    check_list(1);
    puts("numeric error + list complete: 7 reply families, 2 completion states PASS");
    return 0;
}
