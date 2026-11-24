#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#define main legacy_idle_commands_main
#include "test_idle_commands.c"
#undef main
static void expect_text(int translated,const char *english) {
    const char *wanted=translated ? irc_tr(english):english;
    if (!strstr(output,wanted)) fprintf(stderr,"Expected: %s\nActual: %s\n",wanted,output);
    assert(strstr(output,wanted));
}
static void cases(int translated) {
    Fixture f; init(&f,1);
    f.commands.translate=translated?irc_tr:NULL;
    struct {const char *command,*message;} bad[]={
        {"/connect -nick #bad host","Nickname must be a valid IRC nickname; use -nick NICK before the hostname."},
        {"/connect -plain host +6697","A +PORT requests TLS and cannot be combined with -plain."},
        {"/connect host 65536","Port must be a number from 1 to 65535; use -nick NICK for a nickname."},
        {"/connect host 0","Port must be a number from 1 to 65535; use -nick NICK for a nickname."},
        {"/connect host 6697 secret-value","Extra connection arguments rejected. Use -nick NICK; passwords are configured with /autoauth or /panel."},
        {"not-a-command","Enter a supported /command; /help lists commands. Invalid controls or oversized input are rejected."},
        {"/set default_interface ncurses","onekeyirc always uses the idle interface; ncurses requires vipirc. The saved preference is unchanged."}
    };
    size_t count=session_count(&f);
    for(size_t j=0;j<sizeof bad/sizeof bad[0];++j) {
        assert(run(&f,bad[j].command)==IDLE_COMMAND_ERROR);
        expect_text(translated,bad[j].message);
        assert(!strstr(output,"secret-value"));
        assert(session_count(&f)==count);
        assert(!app(&f)->net && !app(&f)->sim);
    }
    assert(!idle_command_recordable("/connect host 6697 secret-value"));
    assert(run(&f,"/mentions clear")==IDLE_COMMAND_MENTIONS_CLEAR);
    expect_text(translated,"Cleared retained mentions across all servers.");
    for(int type=CONFIG_BOUNCER_ZNC;type<=CONFIG_BOUNCER_SOJU;++type) {
        app(&f)->cfg.bouncer_type=type;
        assert(run(&f,"/autoauth off")==IDLE_COMMAND_ERROR);
        expect_text(translated,"Use /panel to change this bouncer's login or password source; /autoauth status shows its settings.");
        assert(run(&f,"/autojoin add #blocked")==IDLE_COMMAND_ERROR);
        expect_text(translated,"This bouncer manages channel membership. Use /join or /part while connected, or /panel to change connection type.");
        assert(!strcmp(app(&f)->cfg.channels,"#test"));
        assert(run(&f,"/autoauth status")==IDLE_COMMAND_DONE);
        expect_text(translated,"(incomplete)");
    }
    cleanup(&f);
}
int main(void){cases(1);cases(0);puts("Connection argument and bouncer localization: PASS");return 0;}
