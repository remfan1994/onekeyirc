/* Use the real selected manual and actual in-app help buffer. */
#include "app.h"
#include "l10n.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    assert(argc==2);
    Config cfg; config_minimal_defaults(&cfg); strcpy(cfg.nick,"tester");
    App *app=calloc(1,sizeof *app); assert(app);
    assert(app_init(app,&cfg,""));
    app_help(app);
    const char *heading=app_manual_command_heading();
    const char *expected=!strcmp(argv[1],"fallback")?"COMMAND PROMPT":irc_tr("COMMAND PROMPT");
    if(strcmp(heading,expected)) fprintf(stderr,"heading: %s; expected: %s\n",heading,expected);
    assert(!strcmp(heading,expected));
    const Buffer *manual=&app->buffers[2]; size_t matches=0;
    for(size_t i=0;i<manual->line_count;++i)
        matches+=!strcmp(manual->lines[(manual->line_start+i)%manual->line_cap],heading);
    assert(matches==1); app_free(app); free(app); config_wipe(&cfg,sizeof cfg);
    puts("Selected manual command heading and actual help buffer: PASS"); return 0;
}
