#define _POSIX_C_SOURCE 200809L
#include "../src/dcc_commands.h"
#include "../src/l10n.h"
#include "../src/security_diagnostic_l10n.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
static char output[8192];
static void capture(void *unused, const char *line) {
    (void)unused;
    size_t n = strlen(output);
    assert(n + strlen(line) + 2 < sizeof output);
    snprintf(output + n, sizeof output - n, "%s\n", line);
}
int main(void) {
    Client *client = calloc(1, sizeof *client);
    App *app = calloc(1, sizeof *app);
    DccManager *dcc = dcc_create(0);
    assert(client && app && dcc);
    client->slots[0].used=1; client->slots[0].token=5;
    client->slots[0].app=app; app->registered=1;
    char error[512], localized[1024], args[1024]; uint64_t id;
    const struct { const char *body, *reason; } bad[] = {
        {"DCC CHAT chat 2130706433 9000", "Only active DCC SEND with filename, IPv4, port and size is supported."},
        {"DCC SEND \"file.txt 2130706433 9000 1024", "DCC filename has an unmatched quote."},
        {"DCC SEND \"file.txt\"2130706433 9000 1024", "DCC filename must be followed by an address."},
        {"DCC SEND \"\" 2130706433 9000 1024", "DCC filename is empty or too long."},
        {"DCC SEND ../file.txt 2130706433 9000 1024", "DCC filename must be a visible safe basename without paths or controls."},
        {"DCC SEND file.txt 2130706433 0 1024", "DCC requires a decimal IPv4 address, port 1..65535 and size 0..4294967295; extensions are unsupported."}
    };
    for (size_t i=0; i<sizeof bad/sizeof bad[0]; ++i) {
        assert(!dcc_offer(dcc,5,"Aster",bad[i].body,&id,error,sizeof error));
        assert(!strcmp(error,bad[i].reason));
        assert(!strcmp(irc_security_diagnostic(error,localized,sizeof localized),irc_tr(bad[i].reason)));
        assert(dcc_count(dcc)==0);
    }
    assert(dcc_command(&dcc,client,app,"list",capture,NULL));
    assert(strstr(output,irc_tr("No file transfers.")));
    output[0]=0;
    assert(dcc_offer(dcc,5,"Aster","DCC SEND file%n{raw}.txt 2130706433 9000 1024",&id,error,sizeof error));
    assert(dcc_command(&dcc,client,app,"list",capture,NULL));
    assert(strstr(output,irc_tr("File transfers")));
    assert(strstr(output,irc_tr("offered")) && strstr(output,irc_tr("from")));
    assert(strstr(output,"file%n{raw}.txt") && strstr(output,"127.0.0.1:9000"));
    assert(strstr(output,"0/1024") && strstr(output,"Aster"));
    output[0]=0;
    struct timespec now; assert(clock_gettime(CLOCK_MONOTONIC,&now)==0);
    dcc_process(dcc,NULL,NULL,0,(int64_t)now.tv_sec*1000 + now.tv_nsec/1000000 + 300001);
    const DccInfo *info=dcc_get(dcc,id);
    assert(info && info->state==DCC_FAILED);
    const char *reason="DCC offer expired without acceptance.";
    assert(!strcmp(info->error,reason));
    assert(dcc_command(&dcc,client,app,"list",capture,NULL));
    assert(strstr(output,irc_tr(reason)) && strstr(output,"file%n{raw}.txt"));
    if (strcmp(irc_tr(reason),reason)) assert(!strstr(output,reason));
    char root[]="/tmp/irc-dcc-localize-XXXXXX", link[512], source[512], body[512];
    assert(mkdtemp(root));
    snprintf(link,sizeof link,"%s/link",root);
    assert(!symlink(root,link));
    snprintf(source,sizeof source,"%s/link/file.txt",root);
    assert(!dcc_send(dcc,5,"Aster",source,"127.0.0.1",0,&id,body,sizeof body,error,sizeof error));
    assert(strstr(error,"Cannot open DCC directory (symlinks refused): ")==error);
    const char *expected=irc_security_diagnostic(error,localized,sizeof localized);
    output[0]=0;
    snprintf(args,sizeof args,"send -address 127.0.0.1 Aster %s",source);
    assert(!dcc_command(&dcc,client,app,args,capture,NULL));
    assert(strstr(output,expected));
    assert(!unlink(link) && !rmdir(root));
    dcc_free(dcc); free(app); free(client);
    puts("Actual DCC expiry and rejected source command: PASS");
    return 0;
}
