/* Real DCC command handler with deterministic IRC transport and loopback sockets.
 * No public network or credentials are used. */
#define main legacy_app_test_main
#define force_terminal_signals use_signals
#include "test_app.c"
#undef main
#undef force_terminal_signals
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include "../src/dcc_commands.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static char captured[8192];
static void capture(void *unused, const char *line)
{
    (void)unused;
    size_t n = strlen(captured);
    assert(n + strlen(line) + 2 < sizeof captured);
    snprintf(captured + n, sizeof captured - n, "%s\n", line);
}
static void expect_template(const char *key, const IrcDisplayField *fields, size_t count)
{
    char expected[2048];
    assert(irc_display_render(expected, sizeof expected, key, fields, count));
    assert(strstr(captured, expected));
}
static void expect_id(const char *key, uint64_t id)
{
    char number[32];
    snprintf(number, sizeof number, "%llu", (unsigned long long)id);
    const IrcDisplayField f[] = {{"id", number}};
    expect_template(key, f, 1);
}
int main(void)
{
    App *app = fresh(0);
    Client *client = calloc(1, sizeof *client);
    DccManager *dcc = dcc_create(0);
    assert(client && dcc);
    client->slots[0].used = 1;
    client->slots[0].token = 5;
    client->slots[0].app = app;
    const char *usage = "Usage: /dcc NICK PATH | /dcc send [-address IPV4] [-port PORT] NICK PATH";
    const char *paths = "Paths are literal, including spaces, relative to the directory where this process was launched.";
    assert(!dcc_command(&dcc,client,app,"accept invalid",capture,NULL));
    assert(strstr(captured,irc_tr(usage)) && strstr(captured,irc_tr(paths)));
    assert(strstr(captured,"/dcc list | /dcc accept NUMBER [DIRECTORY] | /dcc reject NUMBER | /dcc cancel NUMBER"));
    captured[0] = 0;
    assert(!dcc_command(&dcc,client,app,"cancel 18446744073709551615",capture,NULL));
    expect_id("No file transfer has number {id}. Use /dcc list.", UINT64_MAX);
    captured[0] = 0;
    client->test_mode = 1;
    assert(!dcc_command(&dcc,client,app,"send Aster nonexisting",capture,NULL));
    assert(strstr(captured,irc_tr("File transfers are disabled in offline test mode; no file was opened.")));
    client->test_mode = 0;
    captured[0] = 0;
    assert(!dcc_command(&dcc,client,app,"send Aster nonexisting",capture,NULL));
    assert(strstr(captured,irc_tr("Connect to a server before offering a file.")));
    register_plain(app);
    captured[0] = 0;
    client->slots[0].used = 0;
    assert(!dcc_command(&dcc,client,app,"send Aster nonexisting",capture,NULL));
    assert(strstr(captured,irc_tr("This server is no longer available.")));
    client->slots[0].used = 1;
    captured[0] = 0;
    assert(!dcc_command(&dcc,client,app,"send Aster nonexisting",capture,NULL));
    assert(strstr(captured,irc_tr("This connection has no local IPv4 address. Use /dcc send -address IPV4 NICK PATH.")));
    char error[512], args[1024], dir[] = "/tmp/irc-dcc-confirm-XXXXXX", source[512];
    uint64_t id;
    assert(mkdtemp(dir));
    snprintf(source,sizeof source,"%s/file%%n{raw}.txt",dir);
    int fd = open(source,O_CREAT|O_EXCL|O_WRONLY,0600);
    assert(fd >= 0 && write(fd,"abc",3) == 3 && close(fd) == 0);
    snprintf(args,sizeof args,"send -address 127.0.0.1 Aster %s",source);
    captured[0] = 0;
    app->wire_limit = 32;
    assert(!dcc_command(&dcc,client,app,args,capture,NULL));
    const IrcDisplayField failure[] = {{"error",app->status}};
    expect_template("The file offer could not be queued: {error}",failure,1);
    assert(!app->chat_count);
    app->wire_limit = 512;
    captured[0] = 0;
    assert(dcc_command(&dcc,client,app,args,capture,NULL));
    const DccInfo *outgoing = dcc_at(dcc,dcc_count(dcc)-1);
    assert(outgoing && outgoing->outgoing && outgoing->size == 3 && outgoing->port);
    id = outgoing->id;
    char number[32], port[32];
    snprintf(number,sizeof number,"%llu",(unsigned long long)id);
    snprintf(port,sizeof port,"%u",(unsigned)outgoing->port);
    const IrcDisplayField offer[] = {{"id",number},{"nick","Aster"},{"filename","file%n{raw}.txt"},{"size","3"}};
    const IrcDisplayField warning[] = {{"address","127.0.0.1"},{"port",port}};
    expect_template("File offer #{id} to {nick}: {filename} ({size} bytes).",offer,4);
    expect_template("Unencrypted peer transfer; advertised address {address}:{port}. The peer must be able to reach it.",warning,2);
    assert(app->chat_count == 1);
    snprintf(args,sizeof args,"cancel %llu",(unsigned long long)id);
    captured[0] = 0;
    assert(dcc_command(&dcc,client,app,args,capture,NULL));
    expect_id("File transfer #{id} cancelled.",id);
    assert(dcc_get(dcc,id)->state == DCC_CANCELLED && !app->chat_count);
    assert(dcc_offer(dcc,5,"Aster","DCC SEND rejected.txt 2130706433 9000 3",&id,error,sizeof error));
    snprintf(args,sizeof args,"reject %llu",(unsigned long long)id);
    captured[0] = 0;
    assert(dcc_command(&dcc,client,app,args,capture,NULL));
    expect_id("File transfer #{id} rejected.",id);
    assert(dcc_get(dcc,id)->state == DCC_REJECTED);
    int listener = socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in sa;
    memset(&sa,0,sizeof sa); sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(listener >= 0 && bind(listener,(struct sockaddr *)&sa,sizeof sa) == 0 && listen(listener,1) == 0);
    socklen_t slen = sizeof sa;
    assert(getsockname(listener,(struct sockaddr *)&sa,&slen) == 0);
    snprintf(args,sizeof args,"DCC SEND accepted.txt 2130706433 %u 3",(unsigned)ntohs(sa.sin_port));
    assert(dcc_offer(dcc,5,"Aster",args,&id,error,sizeof error));
    snprintf(args,sizeof args,"accept %llu %s",(unsigned long long)id,dir);
    captured[0] = 0;
    assert(dcc_command(&dcc,client,app,args,capture,NULL));
    expect_id("File transfer #{id} accepted.",id);
    assert(dcc_get(dcc,id)->state == DCC_CONNECTING || dcc_get(dcc,id)->state == DCC_RECEIVING);
    assert(dcc_cancel(dcc,id,error,sizeof error));
    close(listener);
    dcc_free(dcc); destroy(app); free(client);
    assert(!unlink(source) && !rmdir(dir));
    puts("DCC command confirmations and outgoing offers: PASS");
    return 0;
}
